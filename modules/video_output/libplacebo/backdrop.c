/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <vlc_common.h>
#include <math.h>
#include <libplacebo/dispatch.h>
#include <libplacebo/shaders/custom.h>
#include "backdrop.h"

enum { LEVELS = 12, HALF_KERNEL = 24 };
struct vlc_placebo_backdrop
{
    pl_gpu gpu;
    pl_dispatch dispatch;
    pl_tex source, composite, pyramid[LEVELS], horizontal, vertical;
};

struct vlc_placebo_backdrop *vlc_placebo_backdrop_New(pl_log log, pl_gpu gpu)
{
    struct vlc_placebo_backdrop *b = calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->gpu = gpu;
    b->dispatch = pl_dispatch_create(log, gpu);
    if (!b->dispatch) { free(b); return NULL; }
    return b;
}

void vlc_placebo_backdrop_Delete(struct vlc_placebo_backdrop *b)
{
    if (!b) return;
    pl_tex_destroy(b->gpu, &b->source);
    pl_tex_destroy(b->gpu, &b->composite);
    for (size_t i = 0; i < LEVELS; ++i)
        pl_tex_destroy(b->gpu, &b->pyramid[i]);
    pl_tex_destroy(b->gpu, &b->horizontal);
    pl_tex_destroy(b->gpu, &b->vertical);
    pl_dispatch_destroy(&b->dispatch);
    free(b);
}

static bool Texture(struct vlc_placebo_backdrop *b, pl_tex *tex,
                    int width, int height, pl_fmt format)
{
    return pl_tex_recreate(b->gpu, tex, &(struct pl_tex_params) {
        .w = width, .h = height, .format = format,
        .sampleable = true, .renderable = true, .blit_dst = true,
    });
}

pl_tex vlc_placebo_backdrop_Target(struct vlc_placebo_backdrop *b, pl_tex target)
{
    pl_fmt format = target->params.format;
    const enum pl_fmt_caps caps = PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_LINEAR
                           | PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_BLENDABLE
                           | PL_FMT_CAP_BLITTABLE;
    /* An opaque OpenGL default framebuffer need not expose its storage format.
     * Keep a floating-point working surface in that case; do not read it back. */
    if (!format || (format->caps & caps) != caps)
        format = pl_find_fmt(b->gpu, PL_FMT_FLOAT, 4, 16, 0, caps);
    if (!format
        || !Texture(b, &b->source, target->params.w, target->params.h, format))
        return NULL;
    return b->source;
}

static const char copy_shader[] = "color = texture(image, uv);\n";
static const char blur_shader[] =
    /* Difference accumulation preserves uniform colors exactly, including
     * values above SDR white in floating-point intermediate textures. */
    "vec3 center = texture(image, uv).rgb; vec3 rgb = center;\n"
    "for (int i = 1; i <= 24; ++i) {\n"
    " vec2 offset = float(i) * step_size;\n"
    " rgb += ((texture(image, uv + offset).rgb - center)"
    "       + (texture(image, uv - offset).rgb - center))"
    " * weights[i / 4][i - 4 * (i / 4)];\n"
    "}\ncolor = vec4(rgb, 1.0);\n";
static const char mask_shader[] =
    "vec2 p = abs(pixel - region.xy) - region.zw + radius;\n"
    "float d = length(max(p, vec2(0.0))) + min(max(p.x, p.y), 0.0) - radius;\n"
    "float a = 1.0 - smoothstep(-0.5, 0.5, d);\n"
    "color = vec4(texture(image, uv).rgb, a);\n";

static bool Pass(struct vlc_placebo_backdrop *b, pl_tex source, pl_tex target,
                 struct pl_rect2df sample, struct pl_rect2d output,
                 const char *body, const struct pl_shader_var *vars, int count,
                 bool blend)
{
    pl_shader shader = pl_dispatch_begin(b->dispatch);
    if (!shader) return false;
    const struct pl_shader_desc descriptor = {
        .desc = { .name = "image", .type = PL_DESC_SAMPLED_TEX },
        .binding = { .object = source, .address_mode = PL_TEX_ADDRESS_CLAMP,
                     .sample_mode = PL_TEX_SAMPLE_LINEAR },
    };
    const float x0 = sample.x0 / source->params.w;
    const float y0 = sample.y0 / source->params.h;
    const float x1 = sample.x1 / source->params.w;
    const float y1 = sample.y1 / source->params.h;
    const float uv[4][2] = { {x0,y0}, {x1,y0}, {x0,y1}, {x1,y1} };
    const float pixel[4][2] = {
        {output.x0,output.y0}, {output.x1,output.y0},
        {output.x0,output.y1}, {output.x1,output.y1},
    };
    const pl_fmt vertex_format = pl_find_vertex_fmt(b->gpu, PL_FMT_FLOAT, 2);
    const struct pl_shader_va attribs[] = {
        { .attr = {.name = "uv", .fmt = vertex_format},
          .data = {uv[0], uv[1], uv[2], uv[3]} },
        { .attr = {.name = "pixel", .fmt = vertex_format},
          .data = {pixel[0], pixel[1], pixel[2], pixel[3]} },
    };
    if (!vertex_format || !pl_shader_custom(shader, &(struct pl_custom_shader) {
        .prelude = "#if __VERSION__ < 130\n#ifndef texture\n"
                   "#define texture texture2D\n#endif\n#endif\n",
        .description = "VLC display backdrop", .body = body,
        .input = PL_SHADER_SIG_NONE, .output = PL_SHADER_SIG_COLOR,
        .descriptors = &descriptor, .num_descriptors = 1,
        .variables = vars, .num_variables = count,
        .vertex_attribs = attribs, .num_vertex_attribs = ARRAY_SIZE(attribs),
    }))
    {
        pl_dispatch_abort(b->dispatch, &shader);
        return false;
    }
    /* Preserve the original display alpha, including transparent letterboxes.
     * Each region samples the same unmodified source, even where they overlap. */
    const struct pl_blend_params blending = {
        .src_rgb = PL_BLEND_SRC_ALPHA, .dst_rgb = PL_BLEND_ONE_MINUS_SRC_ALPHA,
        .src_alpha = PL_BLEND_ZERO, .dst_alpha = PL_BLEND_ONE,
    };
    return pl_dispatch_finish(b->dispatch, &(struct pl_dispatch_params) {
        .shader = &shader, .target = target, .rect = output,
        .blend_params = blend ? &blending : NULL,
    });
}

static bool Copy(struct vlc_placebo_backdrop *b, pl_tex source, pl_tex target)
{
    return Pass(b, source, target,
        (struct pl_rect2df) {0, 0, source->params.w, source->params.h},
        (struct pl_rect2d) {0, 0, target->params.w, target->params.h},
        copy_shader, NULL, 0, false);
}

static bool Blur(struct vlc_placebo_backdrop *b, pl_tex source, pl_tex target,
                 struct pl_rect2df sample, double sigma, float x, float y)
{
    float weights[28] = {0};
    double total = 0;
    for (int i = 0; i <= HALF_KERNEL; ++i)
    {
        weights[i] = exp(-i * i / (2 * sigma * sigma));
        total += weights[i] * (i ? 2 : 1);
    }
    for (int i = 0; i <= HALF_KERNEL; ++i) weights[i] /= total;
    const float step[] = {x, y};
    struct pl_var kernel = pl_var_vec4("weights");
    kernel.dim_a = ARRAY_SIZE(weights) / 4;
    const struct pl_shader_var vars[] = {
        {.var = kernel, .data = weights, .dynamic = true},
        {.var = pl_var_vec2("step_size"), .data = step, .dynamic = true},
    };
    return Pass(b, source, target, sample,
        (struct pl_rect2d) {0, 0, target->params.w, target->params.h},
        blur_shader, vars, ARRAY_SIZE(vars), false);
}

bool vlc_placebo_backdrop_Render(struct vlc_placebo_backdrop *b, pl_tex target,
    const struct vlc_window_backdrop_region *regions, size_t count, bool flipped)
{
    const int width = target->params.w, height = target->params.h;
    pl_dispatch_reset_frame(b->dispatch);
    if (!b->source || b->source->params.w != width
        || b->source->params.h != height
        || !Texture(b, &b->composite, width, height, b->source->params.format)
        || !Copy(b, b->source, b->composite))
        return false;
    unsigned ready = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const struct vlc_window_backdrop_region *r = &regions[i];
        const double center_y = flipped ? height - (r->y + r->height / 2.)
                                        : r->y + r->height / 2.;
        const double left = fmax(0, r->x), right = fmin(width, (double)r->x + r->width);
        const double top = fmax(0, center_y - r->height / 2.);
        const double bottom = fmin(height, center_y + r->height / 2.);
        if (right <= left || bottom <= top || r->blur_radius <= 0) continue;
        const double radius = fmin(r->blur_radius, fmax(width, height));
        unsigned level = 0;
        while (radius / (1u << level) > 8 && level < LEVELS) ++level;
        while (ready < level)
        {
            const pl_tex input = ready ? b->pyramid[ready - 1] : b->source;
            pl_tex *output = &b->pyramid[ready];
            const unsigned scale = 1u << (ready + 1);
            /* Keep an extruded edge texel at every level. Otherwise clamping
             * after downsampling repeats an average of the first source block
             * rather than its edge pixel, changing the Gaussian near borders.
             * The fixed 2:1 grid also preserves phase for odd display sizes. */
            if (!Texture(b, output, (width + scale - 1) / scale + 2,
                         (height + scale - 1) / scale + 2, b->source->params.format))
                goto error;
            const float start = ready ? -1.f : -2.f;
            if (!Pass(b, input, *output,
                (struct pl_rect2df) {start, start,
                    start + 2 * (*output)->params.w, start + 2 * (*output)->params.h},
                (struct pl_rect2d) {0, 0, (*output)->params.w, (*output)->params.h},
                copy_shader, NULL, 0, false)) goto error;
            ++ready;
        }
        const pl_tex input = level ? b->pyramid[level - 1] : b->source;
        const double sx = 1u << level, sy = sx;
        const double origin = level ? -sx : 0;
        const int x0 = fmax(0, floor((left - 3 * radius - 2 * sx - origin) / sx));
        const int y0 = fmax(0, floor((top - 3 * radius - 2 * sy - origin) / sy));
        const int x1 = fmin(input->params.w, ceil((right + 3 * radius + 2 * sx - origin) / sx));
        const int y1 = fmin(input->params.h, ceil((bottom + 3 * radius + 2 * sy - origin) / sy));
        if (!Texture(b, &b->horizontal, x1 - x0, y1 - y0, b->source->params.format)
            || !Texture(b, &b->vertical, x1 - x0, y1 - y0, b->source->params.format)
            || !Blur(b, input, b->horizontal,
                     (struct pl_rect2df) {x0, y0, x1, y1}, radius / sx,
                     1.f / input->params.w, 0)
            || !Blur(b, b->horizontal, b->vertical,
                     (struct pl_rect2df) {0, 0, x1 - x0, y1 - y0}, radius / sy,
                     0, 1.f / (y1 - y0))) goto error;
        const struct pl_rect2d output = {floor(left), floor(top), ceil(right), ceil(bottom)};
        const struct pl_rect2df sample = {
            (output.x0 - origin) / sx - x0, (output.y0 - origin) / sy - y0,
            (output.x1 - origin) / sx - x0, (output.y1 - origin) / sy - y0,
        };
        const float rect[] = {r->x + r->width / 2., center_y, r->width / 2., r->height / 2.};
        const float corner = fmin(r->corner_radius, fmin(r->width, r->height) / 2.);
        const struct pl_shader_var vars[] = {
            {.var = pl_var_vec4("region"), .data = rect, .dynamic = true},
            {.var = pl_var_float("radius"), .data = &corner, .dynamic = true},
        };
        if (!Pass(b, b->vertical, b->composite, sample, output,
                     mask_shader, vars, ARRAY_SIZE(vars), true)) goto error;
    }
    return Copy(b, b->composite, target);
error:
    /* Do not leave a partly updated effect if a GPU allocation or pass fails. */
    Copy(b, b->source, target);
    return false;
}
