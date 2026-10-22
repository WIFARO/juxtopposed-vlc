/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <libplacebo/config.h>
#ifndef PL_HAVE_VULKAN
int main(void) { return 77; }
#else
#undef NDEBUG
#include <vlc_common.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <libplacebo/vulkan.h>
#include <libplacebo/dispatch.h>
#include <libplacebo/shaders/sampling.h>
#include "../../../../modules/video_output/libplacebo/backdrop.h"

static void Log(void *opaque, enum pl_log_level level, const char *text)
{
    (void)opaque; (void)level;
    fprintf(stderr, "%s\n", text);
}

static void Copy(pl_dispatch dispatch, pl_tex input, pl_tex output)
{
    pl_dispatch_reset_frame(dispatch);
    pl_shader shader = pl_dispatch_begin(dispatch);
    assert(pl_shader_sample_direct(shader, &(struct pl_sample_src) {.tex = input}));
    assert(pl_dispatch_finish(dispatch, &(struct pl_dispatch_params) {
        .shader = &shader, .target = output,
    }));
}

static void Download(pl_gpu gpu, pl_tex texture, void *data)
{
    assert(pl_tex_download(gpu, &(struct pl_tex_transfer_params) {
        .tex = texture, .ptr = data,
    }));
}

static double Coverage(const struct vlc_window_backdrop_region *r,
                       double x, double y, int height, bool flipped)
{
    if (flipped) y = height - y;
    const double radius = fmin(r->corner_radius, fmin(r->width, r->height) / 2.);
    const double px = fabs(x - r->x - r->width / 2.) - r->width / 2. + radius;
    const double py = fabs(y - r->y - r->height / 2.) - r->height / 2. + radius;
    const double d = hypot(fmax(px, 0), fmax(py, 0)) + fmin(fmax(px, py), 0) - radius;
    const double t = fmax(0, fmin(1, d + .5));
    return 1 - t * t * (3 - 2 * t);
}

static void Gaussian(const uint8_t *source, double *output,
                     int width, int height, double sigma)
{
    const int radius = ceil(3 * sigma);
    double weights[2 * radius + 1], total = 0;
    for (int i = -radius; i <= radius; ++i)
    { weights[i + radius] = exp(-i * i / (2 * sigma * sigma)); total += weights[i + radius]; }
    for (int i = 0; i <= 2 * radius; ++i) weights[i] /= total;
    double *temp = malloc(width * height * 3 * sizeof(*temp)); assert(temp);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            for (int c = 0; c < 3; ++c)
            {
                double value = 0;
                for (int i = -radius; i <= radius; ++i)
                    value += source[(y * width + VLC_CLIP(x + i, 0, width - 1)) * 4 + c] * weights[i + radius];
                temp[(y * width + x) * 3 + c] = value;
            }
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            for (int c = 0; c < 3; ++c)
            {
                double value = 0;
                for (int i = -radius; i <= radius; ++i)
                    value += temp[(VLC_CLIP(y + i, 0, height - 1) * width + x) * 3 + c] * weights[i + radius];
                output[(y * width + x) * 3 + c] = value;
            }
    free(temp);
}

static void CheckRegion(pl_gpu gpu, struct vlc_placebo_backdrop *b, pl_tex target,
                        const uint8_t *source, uint8_t *actual,
                        struct vlc_window_backdrop_region region, bool flipped)
{
    const int width = target->params.w, height = target->params.h;
    double *blur = malloc(width * height * 3 * sizeof(*blur)); assert(blur);
    Gaussian(source, blur, width, height, region.blur_radius);
    assert(vlc_placebo_backdrop_Render(b, target, &region, 1, flipped));
    Download(gpu, target, actual);
    double error = 0, total = 0; size_t samples = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const double alpha = Coverage(&region, x + .5, y + .5, height, flipped);
            const int pixel = y * width + x;
            assert(actual[4 * pixel + 3] == source[4 * pixel + 3]);
            for (int c = 0; c < 3; ++c)
            {
                if (alpha == 0) assert(actual[4 * pixel + c] == source[4 * pixel + c]);
                const double expected = alpha * blur[3 * pixel + c] + (1 - alpha) * source[4 * pixel + c];
                const double delta = fabs(actual[4 * pixel + c] - expected);
                error = fmax(error, delta); total += delta; ++samples;
            }
        }
    fprintf(stderr, "Gaussian %.2f flipped=%d: max %.3f mean %.3f\n",
            region.blur_radius, flipped, error, total / samples);
    assert(error <= 5 && total / samples <= 1);
    free(blur);
    assert(vlc_placebo_backdrop_Render(b, target, NULL, 0, false));
    Download(gpu, target, actual);
    assert(!memcmp(source, actual, width * height * 4));
}

int main(void)
{
    pl_log log = pl_log_create(PL_API_VER, &(struct pl_log_params) {
        .log_cb = Log, .log_level = PL_LOG_WARN,
    });
    pl_vulkan vulkan = pl_vulkan_create(log, &(struct pl_vulkan_params) {
        .allow_software = true,
    });
    if (!vulkan) { pl_log_destroy(&log); return 77; }
    const pl_gpu gpu = vulkan->gpu;
    pl_dispatch dispatch = pl_dispatch_create(log, gpu); assert(dispatch);
    struct vlc_placebo_backdrop *b = vlc_placebo_backdrop_New(log, gpu); assert(b);
    pl_fmt format = pl_find_named_fmt(gpu, "rgba8"); assert(format);
    unsigned scenarios = 0;
    for (unsigned resized = 0; resized < 2; ++resized)
    {
        const int width = resized ? 193 : 513, height = resized ? 129 : 385;
        uint8_t *source = malloc(width * height * 4), *actual = malloc(width * height * 4);
        uint8_t *single = malloc(width * height * 4); assert(source && actual && single);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                uint8_t *p = source + (y * width + x) * 4;
                p[0] = ((x / 17 + y / 23) & 1) ? 220 : 35;
                p[1] = x * 255 / width; p[2] = y * 255 / height;
                p[3] = 32 + (x + y) % 224;
            }
        const struct pl_tex_params params = {
            .w = width, .h = height, .format = format,
            .sampleable = true, .renderable = true,
            .host_writable = true, .host_readable = true,
        };
        pl_tex input = pl_tex_create(gpu, &params), target = pl_tex_create(gpu, &params);
        assert(input && target);
        assert(pl_tex_upload(gpu, &(struct pl_tex_transfer_params) {.tex = input, .ptr = source}));
        if (!resized)
        {
            /* Default framebuffers can hide their storage format. GPU-only
             * working textures must not require a particular host transfer
             * type: rgba16f commonly has a 32-bit host representation. */
            PL_STRUCT(pl_tex) opaque = *target;
            opaque.params.format = NULL;
            pl_tex floating_work = vlc_placebo_backdrop_Target(b, &opaque);
            assert(floating_work && floating_work->params.format->type == PL_FMT_FLOAT);
            Copy(dispatch, input, floating_work);
            assert(vlc_placebo_backdrop_Render(b, target, NULL, 0, false));
            Download(gpu, target, actual);
            assert(!memcmp(source, actual, width * height * 4)); ++scenarios;
        }
        pl_tex work = vlc_placebo_backdrop_Target(b, target); assert(work);
        Copy(dispatch, input, work);
        assert(vlc_placebo_backdrop_Render(b, target, NULL, 0, false));
        Download(gpu, target, actual); assert(!memcmp(source, actual, width * height * 4)); ++scenarios;
        struct vlc_window_backdrop_region r = {
            .x = 12.25, .y = 20.75, .width = width - 35.5, .height = height - 50.25,
            .blur_radius = 25, .corner_radius = 14,
        };
        CheckRegion(gpu, b, target, source, actual, r, false); ++scenarios;
        CheckRegion(gpu, b, target, source, actual, r, true); ++scenarios;
        r.blur_radius = 37.5; CheckRegion(gpu, b, target, source, actual, r, false); ++scenarios;
        r.blur_radius = 50; r.x = -20.5; r.y = -10.25;
        CheckRegion(gpu, b, target, source, actual, r, false); ++scenarios;
        struct vlc_window_backdrop_region regions[] = {r, {
            .x = 30, .y = 30, .width = 100, .height = 60, .blur_radius = 10,
        }};
        assert(vlc_placebo_backdrop_Render(b, target, &regions[1], 1, false));
        Download(gpu, target, single);
        assert(vlc_placebo_backdrop_Render(b, target, regions, 2, false));
        Download(gpu, target, actual);
        for (int y = 31; y < 89; ++y)
            assert(!memcmp(actual + (y * width + 31) * 4,
                           single + (y * width + 31) * 4, 98 * 4));
        ++scenarios;
        pl_tex_destroy(gpu, &input); pl_tex_destroy(gpu, &target);
        free(source); free(actual); free(single);
    }
    /* Preserve display values above SDR white and the original alpha. This is
     * a floating-point storage check, not proof of an HDR display pipeline. */
    pl_fmt floating = pl_find_named_fmt(gpu, "rgba16f"); assert(floating);
    pl_tex hdr = pl_tex_create(gpu, &(struct pl_tex_params) {
        .w = 64, .h = 96, .format = floating,
        .sampleable = true, .renderable = true, .host_readable = true,
    }); assert(hdr);
    pl_tex work = vlc_placebo_backdrop_Target(b, hdr); assert(work);
    pl_tex_clear(gpu, work, (float[4]) {2, 1.5, .5, .75});
    const struct vlc_window_backdrop_region region = {
        .x = 8, .y = 8, .width = 40, .height = 50,
        .blur_radius = 25, .corner_radius = 14,
    };
    assert(vlc_placebo_backdrop_Render(b, hdr, &region, 1, false));
    /* GPU storage depth and the host transfer type can differ: libplacebo
     * commonly transfers rgba16f through 32-bit floats. */
    void *pixels = malloc(64 * 96 * floating->texel_size); assert(pixels);
    Download(gpu, hdr, pixels);
    for (size_t i = 0; i < 64 * 96 * 4; i += 4)
    {
        if (floating->host_bits[0] == 16)
        {
            const uint16_t *p = pixels;
            assert(p[i] == 0x4000 && p[i + 1] == 0x3e00);
            assert(p[i + 2] == 0x3800 && p[i + 3] == 0x3a00);
        }
        else
        {
            assert(floating->host_bits[0] == 32);
            const float *p = pixels;
            assert(p[i] == 2 && p[i + 1] == 1.5);
            assert(p[i + 2] == .5 && p[i + 3] == .75);
        }
    }
    free(pixels); ++scenarios; pl_tex_destroy(gpu, &hdr);
    assert(!pl_gpu_is_failed(gpu));
    pl_gpu_finish(gpu);
    vlc_placebo_backdrop_Delete(b); pl_dispatch_destroy(&dispatch);
    pl_vulkan_destroy(&vulkan); pl_log_destroy(&log);
    fprintf(stderr, "%u Vulkan backdrop scenarios passed\n", scenarios);
    return 0;
}
#endif
