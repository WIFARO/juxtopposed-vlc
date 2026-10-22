/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <math.h>
#include <vlc_common.h>
#include <vlc_opengl.h>
#include "gl_api.h"
#include "gl_util.h"
#include "backdrop.h"

/* Keep the Gaussian at most 49 taps wide, including at high display scales.
 * Repeated 2:1 reductions avoid aliasing from a single large reduction. */
#define LEVELS 16
#define TAPS 49
#ifndef GL_FRAMEBUFFER_SRGB
# define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif
#ifndef GL_READ_BUFFER
# define GL_READ_BUFFER 0x0C02
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
# define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER_BINDING
# define GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#endif
#ifndef GL_RGBA16F
# define GL_RGBA16F 0x881A
#endif
#ifndef GL_RGBA32F
# define GL_RGBA32F 0x8814
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE
# define GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE 0x8212
#endif
#ifndef GL_BACK_LEFT
# define GL_BACK_LEFT 0x0402
#endif
struct target { GLuint texture, framebuffer; GLsizei width, height; GLint format; };
struct region_targets { struct target horizontal, vertical; };
struct program { GLuint id; GLint uv, step, weights, rect, radius; };
struct vlc_gl_backdrop
{
    struct vlc_gl_api api;
    struct program copy, blur, mask;
    struct target source, pyramid[LEVELS];
    struct region_targets *regions;
    size_t count;
    GLuint buffer, array;
    bool separate_framebuffers, srgb;
    GLint max_texture;
    GLint target_format;
    void (APIENTRY *CopyTexSubImage2D)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
    void (APIENTRY *GetBooleanv)(GLenum, GLboolean *);
    GLboolean (APIENTRY *IsEnabled)(GLenum);
    void (APIENTRY *Scissor)(GLint, GLint, GLsizei, GLsizei);
    void (APIENTRY *ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
    void (APIENTRY *BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
    void (APIENTRY *BlendEquationSeparate)(GLenum, GLenum);
    void (APIENTRY *GetVertexAttribiv)(GLuint, GLenum, GLint *);
    void (APIENTRY *GetVertexAttribPointerv)(GLuint, GLenum, void **);
    void (APIENTRY *DisableVertexAttribArray)(GLuint);
    void (APIENTRY *BindAttribLocation)(GLuint, GLuint, const GLchar *);
    void (APIENTRY *ReadBuffer)(GLenum);
};

static void DeleteTarget(struct vlc_gl_backdrop *b, struct target *t)
{
    const opengl_vtable_t *v = &b->api.vt;
    v->DeleteTextures(1, &t->texture);
    v->DeleteFramebuffers(1, &t->framebuffer);
    *t = (struct target){0};
}

static bool Target(struct vlc_gl_backdrop *b, struct target *t, GLsizei w, GLsizei h)
{
    const opengl_vtable_t *v = &b->api.vt;
    if (t->width == w && t->height == h && t->format == b->target_format)
        return true;
    DeleteTarget(b, t);
    if (w < 1 || h < 1 || w > b->max_texture || h > b->max_texture)
        return false;
    v->GenTextures(1, &t->texture);
    v->BindTexture(GL_TEXTURE_2D, t->texture);
    v->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    v->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    v->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    v->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    v->TexImage2D(GL_TEXTURE_2D, 0, b->target_format, w, h, 0, GL_RGBA,
                 b->target_format == GL_RGBA ? GL_UNSIGNED_BYTE : GL_FLOAT, NULL);
    v->GenFramebuffers(1, &t->framebuffer);
    v->BindFramebuffer(GL_FRAMEBUFFER, t->framebuffer);
    v->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->texture, 0);
    if (v->CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE
        || v->GetError() != GL_NO_ERROR)
    {
        DeleteTarget(b, t);
        return false;
    }
    t->width = w; t->height = h; t->format = b->target_format;
    return true;
}

static struct program Program(struct vlc_gl_backdrop *b, vlc_gl_t *gl,
                              const char *body)
{
    const opengl_vtable_t *v = &b->api.vt;
    bool modern = !b->api.is_gles && b->api.glsl_version >= 130;
    const char *version = b->api.is_gles ? "#version 100\nprecision highp float;\n"
                        : modern ? "#version 130\n" : "#version 120\n";
    const char *vertex[] = {version,
        modern ? "in vec2 position; out vec2 uv;\n" : "attribute vec2 position; varying vec2 uv;\n",
        "uniform vec4 sourceRect; void main(){uv=sourceRect.xy+(position+1.0)*0.5*sourceRect.zw;"
        "gl_Position=vec4(position,0.0,1.0);}\n"};
    const char *fragment[] = {version,
        modern ? "in vec2 uv; out vec4 outputColor;\n#define SAMPLE texture\n#define COLOR outputColor\n"
               : "varying vec2 uv;\n#define SAMPLE texture2D\n#define COLOR gl_FragColor\n",
        b->api.is_gles ? "uniform highp sampler2D image;\n" : "uniform sampler2D image;\n", body};
    struct program p = {0};
    p.id = vlc_gl_BuildProgram(VLC_OBJECT(gl), v, ARRAY_SIZE(vertex), vertex,
                               ARRAY_SIZE(fragment), fragment);
    if (!p.id)
        return p;
    /* All three programs use the same private vertex array. */
    b->BindAttribLocation(p.id, 0, "position");
    v->LinkProgram(p.id);
    GLint linked;
    v->GetProgramiv(p.id, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        v->DeleteProgram(p.id); p.id = 0;
        return p;
    }
    p.uv = v->GetUniformLocation(p.id, "sourceRect");
    p.step = v->GetUniformLocation(p.id, "stepSize");
    p.weights = v->GetUniformLocation(p.id, "weights");
    p.rect = v->GetUniformLocation(p.id, "rect");
    p.radius = v->GetUniformLocation(p.id, "radius");
    return p;
}

struct vlc_gl_backdrop *vlc_gl_backdrop_New(vlc_gl_t *gl)
{
    struct vlc_gl_backdrop *b = calloc(1, sizeof(*b));
    if (!b)
        return NULL;
    if (vlc_gl_api_Init(&b->api, gl) != VLC_SUCCESS)
    { free(b); return NULL; }
    if (!b->api.vt.GenFramebuffers || !b->api.vt.DeleteFramebuffers
        || !b->api.vt.BindFramebuffer || !b->api.vt.FramebufferTexture2D
        || !b->api.vt.CheckFramebufferStatus)
    { free(b); return NULL; }
#define LOAD(name) do { b->name = vlc_gl_GetProcAddress(gl, "gl" #name); if (!b->name) goto error; } while (0)
    LOAD(CopyTexSubImage2D); LOAD(GetBooleanv); LOAD(IsEnabled); LOAD(Scissor);
    LOAD(ColorMask); LOAD(BlendFuncSeparate); LOAD(BlendEquationSeparate);
    LOAD(GetVertexAttribiv); LOAD(GetVertexAttribPointerv);
    LOAD(DisableVertexAttribArray); LOAD(BindAttribLocation);
#undef LOAD
    struct vlc_gl_extension_vt ext;
    vlc_gl_LoadExtensionFunctions(gl, &ext);
    b->separate_framebuffers = vlc_gl_GetVersionMajor(&ext) >= 3;
    if (!b->api.is_gles || b->separate_framebuffers)
    {
        b->ReadBuffer = vlc_gl_GetProcAddress(gl, "glReadBuffer");
        if (!b->ReadBuffer) goto error;
    }
    b->srgb = !b->api.is_gles && (b->separate_framebuffers
              || vlc_gl_HasExtension(&ext, "GL_EXT_framebuffer_sRGB"));
    const opengl_vtable_t *v = &b->api.vt;
    v->GetIntegerv(GL_MAX_TEXTURE_SIZE, &b->max_texture);
    b->copy = Program(b, gl, "void main(){COLOR=vec4(SAMPLE(image,uv).rgb,1.0);}\n");
    b->blur = Program(b, gl,
        /* Pack the symmetric kernel into seven vectors. ES 2 guarantees only
         * 16 fragment uniform vectors; scalar arrays can use one per element. */
        "uniform vec2 stepSize; uniform vec4 weights[7];\n"
        "void main(){vec3 color=SAMPLE(image,uv).rgb*weights[0].x;for(int i=1;i<=24;i++){"
        "vec2 d=float(i)*stepSize; color+=(SAMPLE(image,uv+d).rgb+SAMPLE(image,uv-d).rgb)"
        "*weights[i/4][i-4*(i/4)];}COLOR=vec4(color,1.0);}\n");
    b->mask = Program(b, gl,
        "uniform vec4 rect; uniform float radius;\n"
        "void main(){vec2 p=abs(gl_FragCoord.xy-rect.xy)-rect.zw+radius;"
        "float d=length(max(p,0.0))+min(max(p.x,p.y),0.0)-radius;"
        "float a=1.0-smoothstep(-0.5,0.5,d);COLOR=vec4(SAMPLE(image,uv).rgb,a);}\n");
    if (!b->copy.id || !b->blur.id || !b->mask.id)
        goto error;
    /* Do not change the caller's vertex state during construction. */
    v->GenBuffers(1, &b->buffer);
    v->GenVertexArrays(1, &b->array);
    if (!b->buffer || (b->separate_framebuffers && !b->array))
        goto error;
    return b;
error:
    vlc_gl_backdrop_Delete(b);
    return NULL;
}

void vlc_gl_backdrop_Delete(struct vlc_gl_backdrop *b)
{
    if (!b)
        return;
    const opengl_vtable_t *v = &b->api.vt;
    DeleteTarget(b, &b->source);
    for (size_t i = 0; i < LEVELS; ++i)
        DeleteTarget(b, &b->pyramid[i]);
    for (size_t i = 0; i < b->count; ++i)
    {
        DeleteTarget(b, &b->regions[i].horizontal);
        DeleteTarget(b, &b->regions[i].vertical);
    }
    free(b->regions);
    v->DeleteBuffers(1, &b->buffer);
    v->DeleteVertexArrays(1, &b->array);
    v->DeleteProgram(b->copy.id); v->DeleteProgram(b->blur.id); v->DeleteProgram(b->mask.id);
    free(b);
}

/* The video renderer retains state between pictures. Restore every state this
 * pass changes, including ES 2 vertex attributes where VAOs are unavailable. */
struct state
{
    GLint program, buffer, unpack_buffer, array, active, texture, draw, read, viewport[4], scissor[4];
    GLint blend_src_rgb, blend_dst_rgb, blend_src_alpha, blend_dst_alpha;
    GLint blend_eq_rgb, blend_eq_alpha;
    GLint attr_enabled, attr_size, attr_type, attr_normalized, attr_stride, attr_buffer;
    void *attr_pointer;
    GLboolean color[4], enabled[8];
};
static const GLenum capabilities[] = {GL_BLEND, GL_DEPTH_TEST, GL_STENCIL_TEST,
    GL_SCISSOR_TEST, GL_CULL_FACE, GL_DITHER, GL_SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_COVERAGE};

static void Save(struct vlc_gl_backdrop *b, struct state *s)
{
    const opengl_vtable_t *v = &b->api.vt;
#define GET(name, field) v->GetIntegerv(name, &s->field)
    GET(GL_CURRENT_PROGRAM, program); GET(GL_ARRAY_BUFFER_BINDING, buffer);
    GET(GL_ACTIVE_TEXTURE, active);
    v->ActiveTexture(GL_TEXTURE0); GET(GL_TEXTURE_BINDING_2D, texture);
    GET(GL_FRAMEBUFFER_BINDING, draw);
    s->read = s->draw;
    if (b->separate_framebuffers)
    {
        GET(GL_READ_FRAMEBUFFER_BINDING, read);
        GET(GL_PIXEL_UNPACK_BUFFER_BINDING, unpack_buffer);
        v->BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }
    v->GetIntegerv(GL_VIEWPORT, s->viewport); v->GetIntegerv(GL_SCISSOR_BOX, s->scissor);
    GET(GL_BLEND_SRC_RGB, blend_src_rgb); GET(GL_BLEND_DST_RGB, blend_dst_rgb);
    GET(GL_BLEND_SRC_ALPHA, blend_src_alpha); GET(GL_BLEND_DST_ALPHA, blend_dst_alpha);
    GET(GL_BLEND_EQUATION_RGB, blend_eq_rgb); GET(GL_BLEND_EQUATION_ALPHA, blend_eq_alpha);
    b->GetBooleanv(GL_COLOR_WRITEMASK, s->color);
    for (size_t i = 0; i < ARRAY_SIZE(capabilities); ++i)
    {
        s->enabled[i] = b->IsEnabled(capabilities[i]);
        v->Disable(capabilities[i]);
    }
    if (b->array)
    { GET(GL_VERTEX_ARRAY_BINDING, array); v->BindVertexArray(b->array); }
    else
    {
#define ATTR(name, field) b->GetVertexAttribiv(0, name, &s->field)
        ATTR(GL_VERTEX_ATTRIB_ARRAY_ENABLED, attr_enabled);
        ATTR(GL_VERTEX_ATTRIB_ARRAY_SIZE, attr_size); ATTR(GL_VERTEX_ATTRIB_ARRAY_TYPE, attr_type);
        ATTR(GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, attr_normalized);
        ATTR(GL_VERTEX_ATTRIB_ARRAY_STRIDE, attr_stride);
        ATTR(GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, attr_buffer);
#undef ATTR
        b->GetVertexAttribPointerv(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, &s->attr_pointer);
    }
#undef GET
    b->ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    v->BindBuffer(GL_ARRAY_BUFFER, b->buffer);
    static const GLfloat quad[] = {-1,-1, 1,-1, -1,1, 1,1};
    v->BufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    v->VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    v->EnableVertexAttribArray(0);
}

static void Restore(struct vlc_gl_backdrop *b, const struct state *s)
{
    const opengl_vtable_t *v = &b->api.vt;
    if (b->array)
        v->BindVertexArray(s->array);
    else
    {
        v->BindBuffer(GL_ARRAY_BUFFER, s->attr_buffer);
        v->VertexAttribPointer(0, s->attr_size, s->attr_type, s->attr_normalized,
                               s->attr_stride, s->attr_pointer);
        if (!s->attr_enabled) b->DisableVertexAttribArray(0);
    }
    v->BindBuffer(GL_ARRAY_BUFFER, s->buffer);
    v->UseProgram(s->program);
    v->BindTexture(GL_TEXTURE_2D, s->texture); v->ActiveTexture(s->active);
    v->BindFramebuffer(GL_FRAMEBUFFER, s->draw);
    if (b->separate_framebuffers)
    {
        v->BindFramebuffer(GL_READ_FRAMEBUFFER, s->read);
        v->BindBuffer(GL_PIXEL_UNPACK_BUFFER, s->unpack_buffer);
    }
    v->Viewport(s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3]);
    b->Scissor(s->scissor[0], s->scissor[1], s->scissor[2], s->scissor[3]);
    b->ColorMask(s->color[0], s->color[1], s->color[2], s->color[3]);
    b->BlendFuncSeparate(s->blend_src_rgb, s->blend_dst_rgb, s->blend_src_alpha, s->blend_dst_alpha);
    b->BlendEquationSeparate(s->blend_eq_rgb, s->blend_eq_alpha);
    for (size_t i = 0; i < ARRAY_SIZE(capabilities); ++i)
        if (s->enabled[i]) v->Enable(capabilities[i]); else v->Disable(capabilities[i]);
}

static void Draw(struct vlc_gl_backdrop *b, const struct program *p, GLuint texture,
                 const GLfloat uv[4])
{
    const opengl_vtable_t *v = &b->api.vt;
    v->UseProgram(p->id);
    v->Uniform1i(v->GetUniformLocation(p->id, "image"), 0);
    v->Uniform4fv(p->uv, 1, uv);
    v->BindTexture(GL_TEXTURE_2D, texture);
    v->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void Weights(struct vlc_gl_backdrop *b, double sigma, float x, float y)
{
    GLfloat weights[28] = {0}; double total = 0;
    sigma = fmax(sigma, 0.001);
    for (int i = 0; i <= TAPS/2; ++i)
    { weights[i] = exp(-i*i/(2*sigma*sigma)); total += weights[i] * (i ? 2 : 1); }
    for (int i = 0; i <= TAPS/2; ++i) weights[i] /= total;
    const opengl_vtable_t *v = &b->api.vt;
    v->UseProgram(b->blur.id);
    v->Uniform4fv(b->blur.weights, ARRAY_SIZE(weights)/4, weights);
    v->Uniform2f(b->blur.step, x, y);
}

void vlc_gl_backdrop_Draw(struct vlc_gl_backdrop *b, unsigned width, unsigned height,
                         const struct vlc_window_backdrop_region *regions, size_t count)
{
    if (!count || !width || !height || width > (unsigned)b->max_texture
        || height > (unsigned)b->max_texture)
        return;
    const opengl_vtable_t *v = &b->api.vt;
    struct state s;
    Save(b, &s);
    GLboolean srgb = b->srgb && b->IsEnabled(GL_FRAMEBUFFER_SRGB);
    if (srgb) v->Disable(GL_FRAMEBUFFER_SRGB);
    /* Cocoa can present extended-range floating-point pixels. Keep those
     * values through every intermediate, rather than clipping them to SDR. */
    GLint red_bits = 8;
    if (b->separate_framebuffers && v->GetFramebufferAttachmentParameteriv)
        v->GetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,
            s.draw ? GL_COLOR_ATTACHMENT0 : b->api.is_gles ? GL_BACK : GL_BACK_LEFT,
            GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE, &red_bits);
    else
        v->GetIntegerv(GL_RED_BITS, &red_bits);
    b->target_format = red_bits > 16 ? GL_RGBA32F : red_bits > 8 ? GL_RGBA16F : GL_RGBA;
    if (b->count != count)
    {
        for (size_t i = 0; i < b->count; ++i)
        { DeleteTarget(b, &b->regions[i].horizontal); DeleteTarget(b, &b->regions[i].vertical); }
        free(b->regions); b->count = 0;
        b->regions = vlc_alloc(count, sizeof(*b->regions));
        if (!b->regions) goto done;
        memset(b->regions, 0, count * sizeof(*b->regions)); b->count = count;
    }
    if (!Target(b, &b->source, width, height)) goto done;
    /* Read the unmodified display once. Overlapping panels must never sample
     * an image that another panel has already blurred. No CPU readback. */
    v->BindFramebuffer(GL_FRAMEBUFFER, s.draw);
    v->BindTexture(GL_TEXTURE_2D, b->source.texture);
    GLint read_buffer = 0;
    if (b->ReadBuffer)
    {
        v->GetIntegerv(GL_READ_BUFFER, &read_buffer);
        b->ReadBuffer(s.draw ? GL_COLOR_ATTACHMENT0 : GL_BACK);
    }
    b->CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
    if (b->ReadBuffer) b->ReadBuffer(read_buffer);
    const GLfloat full[] = {0,0,1,1};
    unsigned ready = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const struct vlc_window_backdrop_region *r = &regions[i];
        double left = fmax(0, r->x), right = fmin(width, (double)r->x + r->width);
        double bottom = fmax(0, height - ((double)r->y + r->height));
        double top = fmin(height, height - (double)r->y);
        if (right <= left || top <= bottom) continue;
        double radius = fmin(r->blur_radius, fmax(width, height));
        unsigned level = 0;
        while (radius / (1u << level) > 8 && level < LEVELS) ++level;
        while (ready < level)
        {
            const struct target *input = ready ? &b->pyramid[ready-1] : &b->source;
            struct target *output = &b->pyramid[ready];
            unsigned scale = 1u << (ready + 1);
            /* Preserve an extruded edge texel at every level and keep the
             * grid aligned to source pixels, including odd display sizes. */
            if (!Target(b, output, (width+scale-1)/scale+2,
                        (height+scale-1)/scale+2)) goto done;
            v->BindFramebuffer(GL_FRAMEBUFFER, output->framebuffer);
            v->Viewport(0, 0, output->width, output->height);
            float start = ready ? -1.f : -2.f;
            const GLfloat padded[] = {start/input->width, start/input->height,
                2.f*output->width/input->width, 2.f*output->height/input->height};
            Draw(b, &b->copy, input->texture, padded);
            ++ready;
        }
        const struct target *input = level ? &b->pyramid[level-1] : &b->source;
        double sx = 1u << level, sy = sx, origin = level ? -sx : 0;
        int x0 = fmax(0, floor((left - 3*radius - 2*sx - origin)/sx));
        int y0 = fmax(0, floor((bottom - 3*radius - 2*sy - origin)/sy));
        int x1 = fmin(input->width, ceil((right + 3*radius + 2*sx - origin)/sx));
        int y1 = fmin(input->height, ceil((top + 3*radius + 2*sy - origin)/sy));
        struct region_targets *targets = &b->regions[i];
        if (!Target(b, &targets->horizontal, x1-x0, y1-y0)
            || !Target(b, &targets->vertical, x1-x0, y1-y0)) goto done;
        const GLfloat source_uv[] = {(float)x0/input->width, (float)y0/input->height,
            (float)(x1-x0)/input->width, (float)(y1-y0)/input->height};
        v->BindFramebuffer(GL_FRAMEBUFFER, targets->horizontal.framebuffer);
        v->Viewport(0, 0, x1-x0, y1-y0);
        Weights(b, radius/sx, 1.f/input->width, 0);
        Draw(b, &b->blur, input->texture, source_uv);
        v->BindFramebuffer(GL_FRAMEBUFFER, targets->vertical.framebuffer);
        Weights(b, radius/sy, 0, 1.f/(y1-y0));
        Draw(b, &b->blur, targets->horizontal.texture, full);
        int l = floor(left), btm = floor(bottom), rr = ceil(right), tt = ceil(top);
        const GLfloat mask_uv[] = {(float)(((l-origin)/sx-x0)/(x1-x0)), (float)(((btm-origin)/sy-y0)/(y1-y0)),
            (float)((rr-l)/sx/(x1-x0)), (float)((tt-btm)/sy/(y1-y0))};
        v->BindFramebuffer(GL_FRAMEBUFFER, s.draw);
        v->Viewport(l, btm, rr-l, tt-btm);
        v->UseProgram(b->mask.id);
        v->Uniform4f(b->mask.rect, r->x+r->width/2, height-(r->y+r->height/2), r->width/2, r->height/2);
        v->Uniform1f(b->mask.radius, fmin(r->corner_radius, fmin(r->width,r->height)/2));
        v->Enable(GL_BLEND); b->BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        b->BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        Draw(b, &b->mask, targets->vertical.texture, mask_uv);
        v->Disable(GL_BLEND);
    }
done:
    if (srgb) v->Enable(GL_FRAMEBUFFER_SRGB);
    Restore(b, &s);
}

void vlc_gl_backdrop_window_Draw(struct vlc_gl_backdrop_window *state, vlc_gl_t *gl,
                                vlc_window_t *window, unsigned width, unsigned height)
{
    vlc_window_backdrops_t *snapshot = vlc_window_HoldBackdrops(window);
    size_t count;
    const struct vlc_window_backdrop_region *regions =
        vlc_window_GetBackdropRegions(snapshot, &count);
    if (count && !state->renderer && !state->failed)
    {
        state->renderer = vlc_gl_backdrop_New(gl);
        state->failed = !state->renderer;
        if (state->failed)
            msg_Warn(gl, "Could not create native video backdrop renderer");
    }
    if (count && state->renderer)
        vlc_gl_backdrop_Draw(state->renderer, width, height, regions, count);
    vlc_window_ReleaseBackdrops(snapshot);
}

void vlc_gl_backdrop_window_Close(struct vlc_gl_backdrop_window *state)
{
    vlc_gl_backdrop_Delete(state->renderer);
    *state = (struct vlc_gl_backdrop_window) {0};
}
