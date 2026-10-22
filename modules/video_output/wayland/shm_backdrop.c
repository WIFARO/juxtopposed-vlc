/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include "shm_backdrop.h"

#define LEVELS 27
#define TAPS 49
struct allocation { void *data; size_t capacity; };
struct image { struct allocation storage; unsigned width, height; };
static uint8_t *Pixels(const struct image *image) { return image->storage.data; }
struct sample { size_t lo, hi; unsigned fraction; };
struct vlc_shm_backdrop
{
    picture_t *input, *rendered;
    video_format_t crop;
    vout_display_place_t place;
    struct allocation regions;
    size_t count;
    struct allocation maps[2];
    struct image levels[LEVELS];
    struct allocation horizontal, blurred;
};

static bool Grow(struct allocation *allocation, size_t needed)
{
    if (allocation->capacity >= needed) return true;
    void *next = realloc(allocation->data, needed);
    if (next == NULL) return false;
    allocation->data = next; allocation->capacity = needed;
    return true;
}

struct vlc_shm_backdrop *vlc_shm_backdrop_New(void)
{ return calloc(1, sizeof(struct vlc_shm_backdrop)); }

void vlc_shm_backdrop_Delete(struct vlc_shm_backdrop *b)
{
    if (!b) return;
    if (b->input) picture_Release(b->input);
    if (b->rendered) picture_Release(b->rendered);
    free(b->regions.data); free(b->maps[0].data); free(b->maps[1].data);
    for (size_t i = 0; i < LEVELS; ++i) free(b->levels[i].storage.data);
    free(b->horizontal.data); free(b->blurred.data); free(b);
}

static bool ValidSource(const picture_t *pic, const video_format_t *crop)
{
    const video_format_t *fmt = &pic->format;
    return fmt->i_chroma == VLC_CODEC_BGRX && pic->i_planes == 1 && pic->p[0].p_pixels
        && pic->p[0].i_pitch > 0 && pic->p[0].i_lines > 0
        && (uint64_t)fmt->i_width * 4 <= (unsigned)pic->p[0].i_pitch
        && fmt->i_height <= (unsigned)pic->p[0].i_lines
        && crop->orientation <= ORIENT_MAX
        && crop->i_visible_width && crop->i_visible_height
        && crop->i_x_offset >= fmt->i_x_offset && crop->i_y_offset >= fmt->i_y_offset
        && (uint64_t)crop->i_x_offset + crop->i_visible_width <= (uint64_t)fmt->i_x_offset + fmt->i_visible_width
        && (uint64_t)crop->i_y_offset + crop->i_visible_height <= (uint64_t)fmt->i_y_offset + fmt->i_visible_height
        && (uint64_t)fmt->i_x_offset + fmt->i_visible_width <= fmt->i_width
        && (uint64_t)fmt->i_y_offset + fmt->i_visible_height <= fmt->i_height;
}

static bool SameStorageFormat(const video_format_t *a, const video_format_t *b)
{
    // A picture allocator can reset orientation. The requested source format,
    // not this storage header, determines the display orientation.
    return a->i_chroma == b->i_chroma && a->i_width == b->i_width && a->i_height == b->i_height
        && a->i_x_offset == b->i_x_offset && a->i_y_offset == b->i_y_offset
        && a->i_visible_width == b->i_visible_width && a->i_visible_height == b->i_visible_height;
}

static bool SamePixels(const picture_t *a, const picture_t *b)
{
    if (!a || !SameStorageFormat(&a->format, &b->format)) return false;
    const video_format_t *f = &a->format;
    for (unsigned y = 0; y < f->i_visible_height; ++y)
    {
        const uint8_t *left = a->p[0].p_pixels + (size_t)(f->i_y_offset+y)*a->p[0].i_pitch + (size_t)f->i_x_offset*4;
        const uint8_t *right = b->p[0].p_pixels + (size_t)(f->i_y_offset+y)*b->p[0].i_pitch + (size_t)f->i_x_offset*4;
        for (unsigned x = 0; x < f->i_visible_width; ++x)
        {
            uint32_t aa, bb;
            memcpy(&aa,left+x*4,4); memcpy(&bb,right+x*4,4);
            // X is unused and can be undefined in a decoder's BGRX picture.
#ifdef WORDS_BIGENDIAN
            const uint32_t rgb = UINT32_C(0xffffff00);
#else
            const uint32_t rgb = UINT32_C(0x00ffffff);
#endif
            if ((aa^bb)&rgb) return false;
        }
    }
    return true;
}

static bool SameCrop(const video_format_t *a, const video_format_t *b)
{
    return a->i_x_offset == b->i_x_offset && a->i_y_offset == b->i_y_offset
        && a->i_visible_width == b->i_visible_width && a->i_visible_height == b->i_visible_height
        && a->orientation == b->orientation;
}

static void Map(struct sample *map, unsigned count, int first, int origin,
                unsigned extent, unsigned offset, unsigned length, bool flip, size_t stride)
{
    for (unsigned i = 0; i < count; ++i)
    {
        double t = ((double)first+i+.5-origin)/extent;
        double p = offset + (flip ? 1-t : t)*length - .5;
        double lo = floor(p);
        map[i].fraction = lround((p-lo)*256);
        const double a = fmax(offset, fmin((double)offset+length-1, lo));
        const double z = fmax(offset, fmin((double)offset+length-1, lo+1));
        map[i].lo = (size_t)a*stride; map[i].hi = (size_t)z*stride;
    }
}

static bool Compose(struct vlc_shm_backdrop *b, picture_t *dst, const picture_t *src,
                    const video_format_t *crop, const vout_display_place_t *place)
{
    const unsigned width = dst->format.i_visible_width, height = dst->format.i_visible_height;
    memset(dst->p[0].p_pixels, 0, (size_t)dst->p[0].i_pitch*dst->p[0].i_lines);
    int left = __MAX(0, place->x), top = __MAX(0, place->y);
    int right = __MIN((int64_t)width, (int64_t)place->x+place->width);
    int bottom = __MIN((int64_t)height, (int64_t)place->y+place->height);
    if (right <= left || bottom <= top) return true;
    if (crop->orientation == ORIENT_NORMAL && crop->i_visible_width == place->width
        && crop->i_visible_height == place->height)
    {
        for (int y = top; y < bottom; ++y)
            memcpy(dst->p[0].p_pixels+(size_t)y*dst->p[0].i_pitch+(size_t)left*4,
                src->p[0].p_pixels+((size_t)crop->i_y_offset+y-place->y)*src->p[0].i_pitch
                +((size_t)crop->i_x_offset+left-place->x)*4, (size_t)(right-left)*4);
        return true;
    }
    if (!Grow(&b->maps[0], (size_t)(right-left)*sizeof(struct sample))
        || !Grow(&b->maps[1], (size_t)(bottom-top)*sizeof(struct sample))) return false;
    struct sample *horizontal_map = b->maps[0].data, *vertical_map = b->maps[1].data;
    const bool swap = ORIENT_IS_SWAP(crop->orientation);
    const bool flip_x = crop->orientation & 1, flip_y = crop->orientation & 2;
    Map(horizontal_map, right-left, left, place->x, place->width,
        swap ? crop->i_y_offset : crop->i_x_offset, swap ? crop->i_visible_height : crop->i_visible_width,
        swap ? flip_y : flip_x, swap ? src->p[0].i_pitch : 4);
    Map(vertical_map, bottom-top, top, place->y, place->height,
        swap ? crop->i_x_offset : crop->i_y_offset, swap ? crop->i_visible_width : crop->i_visible_height,
        swap ? flip_x : flip_y, swap ? 4 : src->p[0].i_pitch);
    for (int y = top; y < bottom; ++y)
    {
        const struct sample yy = vertical_map[y-top];
        uint8_t *out = dst->p[0].p_pixels+(size_t)y*dst->p[0].i_pitch+(size_t)left*4;
        for (int x = left; x < right; ++x, out += 4)
        {
            const struct sample xx = horizontal_map[x-left];
            const uint8_t *a = src->p[0].p_pixels+xx.lo+yy.lo, *c = src->p[0].p_pixels+xx.lo+yy.hi;
            const uint8_t *d = src->p[0].p_pixels+xx.hi+yy.lo, *e = src->p[0].p_pixels+xx.hi+yy.hi;
            for (unsigned channel = 0; channel < 3; ++channel)
                out[channel] = ((a[channel]*(256-xx.fraction)+d[channel]*xx.fraction)*(256-yy.fraction)
                    +(c[channel]*(256-xx.fraction)+e[channel]*xx.fraction)*yy.fraction+32768) >> 16;
            out[3] = 255;
        }
    }
    return true;
}

static bool Image(struct image *image, unsigned width, unsigned height)
{
    if ((uint64_t)width*height > SIZE_MAX/4) return false;
    if (!Grow(&image->storage, (size_t)width*height*4)) return false;
    image->width = width; image->height = height;
    return true;
}

static unsigned Level(double radius)
{
    unsigned level = 0;
    while (radius/(1u<<level) > 8 && level+1 < LEVELS) ++level;
    return level;
}

struct bounds { int left, top, right, bottom, x0, y0, x1, y1; unsigned level; double radius; };
static struct bounds Bounds(const struct vlc_window_backdrop_region *r, unsigned width, unsigned height)
{
    struct bounds b = {0};
    double left = fmax(0,r->x), top = fmax(0,r->y);
    double right = fmin(width,(double)r->x+r->width), bottom = fmin(height,(double)r->y+r->height);
    if (right <= left || bottom <= top) return b;
    b.left = floor(left); b.top = floor(top); b.right = ceil(right); b.bottom = ceil(bottom);
    b.radius = fmin(r->blur_radius, __MAX(width,height)); b.level = Level(b.radius);
    const unsigned factor = 1u<<b.level;
    b.x0 = fmax(0,floor((left-3*b.radius-2*factor)/factor));
    b.y0 = fmax(0,floor((top-3*b.radius-2*factor)/factor));
    b.x1 = fmin((width+factor-1)/factor,ceil((right+3*b.radius+2*factor)/factor));
    b.y1 = fmin((height+factor-1)/factor,ceil((bottom+3*b.radius+2*factor)/factor));
    return b;
}

static bool Blur(struct vlc_shm_backdrop *b, picture_t *dst,
                 const struct vlc_window_backdrop_region *regions, size_t count)
{
    if (!count) return true;
    const unsigned width = dst->format.i_visible_width, height = dst->format.i_visible_height;
    unsigned max_level = 0; size_t max_pixels = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const struct bounds box = Bounds(&regions[i],width,height);
        max_level = __MAX(max_level,box.level);
        max_pixels = __MAX(max_pixels,(size_t)(box.x1-box.x0)*(box.y1-box.y0));
    }
    if (!max_pixels) return true;
    if (max_pixels > SIZE_MAX/(3*sizeof(float))) return false;
    if (!Grow(&b->horizontal,max_pixels*3*sizeof(float))
        || !Grow(&b->blurred,max_pixels*3*sizeof(float))
        || !Image(&b->levels[0],width,height)) return false;
    float *horizontal = b->horizontal.data, *blurred = b->blurred.data;
    for (unsigned y = 0; y < height; ++y)
        memcpy(Pixels(&b->levels[0])+(size_t)y*width*4,dst->p[0].p_pixels+(size_t)y*dst->p[0].i_pitch,(size_t)width*4);
    for (unsigned n = 1; n <= max_level; ++n)
    {
        struct image *in = &b->levels[n-1], *out = &b->levels[n];
        if (!Image(out,(in->width+1)/2,(in->height+1)/2)) return false;
        for (unsigned y = 0; y < out->height; ++y) for (unsigned x = 0; x < out->width; ++x)
        {
            unsigned x1 = __MIN(2*x+1,in->width-1), y1 = __MIN(2*y+1,in->height-1);
            for (unsigned c = 0; c < 3; ++c)
                Pixels(out)[((size_t)y*out->width+x)*4+c] =
                    (Pixels(in)[((size_t)(2*y)*in->width+2*x)*4+c]
                    +Pixels(in)[((size_t)(2*y)*in->width+x1)*4+c]
                    +Pixels(in)[((size_t)y1*in->width+2*x)*4+c]
                    +Pixels(in)[((size_t)y1*in->width+x1)*4+c]+2)/4;
        }
    }
    for (size_t index = 0; index < count; ++index)
    {
        const struct vlc_window_backdrop_region *r = &regions[index];
        const struct bounds box = Bounds(r,width,height);
        if (box.right <= box.left || box.bottom <= box.top) continue;
        const int w = box.x1-box.x0, h = box.y1-box.y0, factor = 1u<<box.level;
        const struct image *in = &b->levels[box.level];
        const double sigma = fmax(.001,box.radius/factor);
        int taps = ceil(3*sigma); taps = __MIN(TAPS/2,taps);
        float weights[TAPS]; double total = 0;
        for (int k = -taps; k <= taps; ++k) total += weights[k+taps] = exp(-k*k/(2*sigma*sigma));
        for (int k = 0; k <= 2*taps; ++k) weights[k] /= total;
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) for (unsigned c = 0; c < 3; ++c)
        {
            float value = 0;
            for (int k = -taps; k <= taps; ++k)
            {
                int xx = __MAX(0,__MIN((int)in->width-1,box.x0+x+k));
                value += weights[k+taps]*Pixels(in)[((size_t)(box.y0+y)*in->width+xx)*4+c];
            }
            horizontal[((size_t)y*w+x)*3+c] = value;
        }
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) for (unsigned c = 0; c < 3; ++c)
        {
            float value = 0;
            for (int k = -taps; k <= taps; ++k)
            {
                int yy = __MAX(0,__MIN(h-1,y+k));
                value += weights[k+taps]*horizontal[((size_t)yy*w+x)*3+c];
            }
            blurred[((size_t)y*w+x)*3+c] = value;
        }
        const double corner = fmin(r->corner_radius,fmin(r->width,r->height)/2);
        for (int y = box.top; y < box.bottom; ++y) for (int x = box.left; x < box.right; ++x)
        {
            const double px = fabs(x+.5-(r->x+r->width/2))-r->width/2+corner;
            const double py = fabs(y+.5-(r->y+r->height/2))-r->height/2+corner;
            const double distance = hypot(fmax(px,0),fmax(py,0))+fmin(fmax(px,py),0)-corner;
            const double t = fmax(0,fmin(1,distance+.5)), alpha = 1-t*t*(3-2*t);
            if (alpha <= 0) continue;
            const double u = (x+.5)/factor-.5-box.x0, v = (y+.5)/factor-.5-box.y0;
            const double fx = u-floor(u), fy = v-floor(v);
            const int ix = floor(u), iy = floor(v);
            int x0 = __MAX(0,__MIN(w-1,ix)), x1 = __MAX(0,__MIN(w-1,ix+1));
            int y0 = __MAX(0,__MIN(h-1,iy)), y1 = __MAX(0,__MIN(h-1,iy+1));
            uint8_t *out = dst->p[0].p_pixels+(size_t)y*dst->p[0].i_pitch+(size_t)x*4;
            for (unsigned c = 0; c < 3; ++c)
            {
                double a = blurred[((size_t)y0*w+x0)*3+c]*(1-fx)+blurred[((size_t)y0*w+x1)*3+c]*fx;
                double d = blurred[((size_t)y1*w+x0)*3+c]*(1-fx)+blurred[((size_t)y1*w+x1)*3+c]*fx;
                out[c] = lround(out[c]*(1-alpha)+(a*(1-fy)+d*fy)*alpha);
            }
        }
    }
    return true;
}

picture_t *vlc_shm_backdrop_Prepare(struct vlc_shm_backdrop *b, const picture_t *src,
    const video_format_t *crop, const vout_display_place_t *place, unsigned width,
    unsigned height, const struct vlc_window_backdrop_region *regions, size_t count)
{
    if (!width || !height || width > INT_MAX || height > INT_MAX
        || (uint64_t)width*height >= (UINT64_C(1)<<26)
        || !place->width || !place->height || !ValidSource(src,crop)
        || count > SIZE_MAX/sizeof(*regions) || (count && !regions)) return NULL;
    for (size_t i = 0; i < count; ++i)
    {
        const struct vlc_window_backdrop_region *r = &regions[i];
        if (!isfinite(r->x) || !isfinite(r->y) || !isfinite(r->width) || !isfinite(r->height)
            || !isfinite(r->blur_radius) || !isfinite(r->corner_radius)
            || r->width <= 0 || r->height <= 0 || r->blur_radius <= 0 || r->corner_radius < 0
            || !isfinite(r->x+r->width) || !isfinite(r->y+r->height)) return NULL;
    }
    if (b->rendered && b->rendered->format.i_visible_width == width
        && b->rendered->format.i_visible_height == height && SameCrop(&b->crop,crop)
        && vout_display_PlaceEquals(&b->place,place) && b->count == count
        && (!count || !memcmp(b->regions.data,regions,count*sizeof(*regions))) && SamePixels(b->input,src))
        return picture_Hold(b->rendered);
    if (b->rendered) { picture_Release(b->rendered); b->rendered = NULL; }
    if (!b->input || !SameStorageFormat(&b->input->format,&src->format))
    {
        if (b->input) picture_Release(b->input);
        b->input = picture_NewFromFormat(&src->format);
        if (!b->input) return NULL;
    }
    // plane_CopyPixels starts at the coded origin. Copy the valid crop at its
    // actual offset, including when coded borders or row padding are present.
    const video_format_t *f = &src->format;
    for (unsigned y = 0; y < f->i_visible_height; ++y)
        memcpy(b->input->p[0].p_pixels+(size_t)(f->i_y_offset+y)*b->input->p[0].i_pitch+(size_t)f->i_x_offset*4,
               src->p[0].p_pixels+(size_t)(f->i_y_offset+y)*src->p[0].i_pitch+(size_t)f->i_x_offset*4,
               (size_t)f->i_visible_width*4);
    picture_CopyProperties(b->input,src);
    if (!Grow(&b->regions,count*sizeof(*regions))) return NULL;
    picture_t *out = picture_New(VLC_CODEC_BGRX,width,height,1,1);
    if (!out) return NULL;
    if (!Compose(b,out,b->input,crop,place) || !Blur(b,out,regions,count))
    { picture_Release(out); return NULL; }
    // Do not export a decoder's undefined X bytes to the compositor.
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            out->p[0].p_pixels[(size_t)y*out->p[0].i_pitch+(size_t)x*4+3] = 255;
    picture_CopyProperties(out,src);
    if (count) memcpy(b->regions.data,regions,count*sizeof(*regions));
    b->count = count; b->place = *place; b->crop = *crop; b->rendered = out;
    return picture_Hold(out);
}
