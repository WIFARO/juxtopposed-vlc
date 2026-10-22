// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc/vlc.h>
#include <vlc_common.h>
#include <vlc_filter.h>
#include <vlc_picture.h>
#include <vlc_mouse.h>
#include "../../../lib/libvlc_internal.h"
#include <stdio.h>
#undef NDEBUG
#include <assert.h>

static uint8_t Pixel(unsigned x, unsigned y, unsigned plane)
{
    return 20 + (x * 3 + y * 5 + plane * 17) % 200;
}

static void CheckMouse(filter_chain_t *chain, unsigned size, unsigned angle)
{
    for (unsigned y = 0; y < size; y += size - 1)
        for (unsigned x = 0; x < size; x += size - 1)
        {
            vlc_mouse_t input = {.i_x = x, .i_y = y}, output;
            assert(filter_chain_MouseFilter(chain, &output, &input) == VLC_SUCCESS);
            unsigned sx = x, sy = y;
            if (angle == 90) { sx = size - 1 - y; sy = x; }
            else if (angle == 180) { sx = size - 1 - x; sy = size - 1 - y; }
            else if (angle == 270) { sx = y; sy = size - 1 - x; }
            assert(output.i_x == (int)sx && output.i_y == (int)sy);
        }
}

static void Check(libvlc_instance_t *instance, vlc_fourcc_t chroma,
                  unsigned size, unsigned angle)
{
    fprintf(stderr, "rotate %.4s size=%u angle=%u\n", (char *)&chroma, size, angle);
    es_format_t format;
    es_format_Init(&format, VIDEO_ES, chroma);
    video_format_Setup(&format.video, chroma, size, size, size, size, 1, 1);
    filter_chain_t *chain = filter_chain_NewVideo(VLC_OBJECT(instance->p_libvlc_int), true, NULL);
    assert(chain);
    filter_chain_Reset(chain, &format, NULL, &format);
    char options[64];
    snprintf(options, sizeof(options), "rotate{angle=%u}", angle);
    assert(filter_chain_AppendFromString(chain, options) > 0);
    picture_t *input = picture_NewFromFormat(&format.video);
    assert(input);
    for (int p = 0; p < input->i_planes; ++p)
        for (int y = 0; y < input->p[p].i_visible_lines; ++y)
            for (int x = 0; x < input->p[p].i_visible_pitch; ++x)
                input->p[p].p_pixels[y * input->p[p].i_pitch + x] = Pixel(x, y, p);
    picture_t *output = filter_chain_VideoFilter(chain, input);
    assert(output);
    for (int p = 0; p < output->i_planes; ++p)
    {
        const plane_t *plane = &output->p[p];
        unsigned width = plane->i_visible_pitch, height = plane->i_visible_lines;
        /* A quarter turn of subsampled chroma needs resampling between unequal
         * axes. Identity and half turns still preserve every sample. */
        if (width != height && angle % 180 != 0) continue;
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x)
            {
                unsigned sx = x, sy = y;
                /* The native filter's positive angle turns counterclockwise. */
                switch (angle) {
                    case 90: sx = width - 1 - y; sy = x; break;
                    case 180: sx = width - 1 - x; sy = height - 1 - y; break;
                    case 270: sx = y; sy = height - 1 - x; break;
                }
                uint8_t actual = plane->p_pixels[y * plane->i_pitch + x];
                uint8_t expected = Pixel(sx, sy, p);
                if (actual != expected)
                    fprintf(stderr, "plane=%d x=%u y=%u: got %u, expected %u\n",
                            p, x, y, actual, expected);
                assert(actual == expected);
            }
    }
    CheckMouse(chain, size, angle);
    picture_Release(output);
    filter_chain_Delete(chain);
    es_format_Clean(&format);
}

static void CheckPacked(libvlc_instance_t *instance, vlc_fourcc_t chroma, unsigned angle)
{
    const unsigned size = 32;
    unsigned yo = chroma == VLC_CODEC_UYVY ? 1 : 0;
    unsigned uo = chroma == VLC_CODEC_UYVY ? 0 : chroma == VLC_CODEC_YUYV ? 1 : 3;
    unsigned vo = chroma == VLC_CODEC_UYVY ? 2 : chroma == VLC_CODEC_YUYV ? 3 : 1;
    fprintf(stderr, "rotate %.4s size=%u angle=%u\n", (char *)&chroma, size, angle);
    es_format_t format;
    es_format_Init(&format, VIDEO_ES, chroma);
    video_format_Setup(&format.video, chroma, size, size, size, size, 1, 1);
    filter_chain_t *chain = filter_chain_NewVideo(VLC_OBJECT(instance->p_libvlc_int), true, NULL);
    assert(chain);
    filter_chain_Reset(chain, &format, NULL, &format);
    char options[64];
    snprintf(options, sizeof(options), "rotate{angle=%u}", angle);
    assert(filter_chain_AppendFromString(chain, options) > 0);
    picture_t *input = picture_NewFromFormat(&format.video);
    assert(input);
    for (unsigned y = 0; y < size; ++y)
        for (unsigned x = 0; x < size; x += 2)
        {
            uint8_t *pair = &input->p[0].p_pixels[y * input->p[0].i_pitch + x * 2];
            pair[yo] = Pixel(x, y, 0); pair[yo + 2] = Pixel(x + 1, y, 0);
            pair[uo] = Pixel(x / 2, y, 1); pair[vo] = Pixel(x / 2, y, 2);
        }
    picture_t *output = filter_chain_VideoFilter(chain, input);
    assert(output);
    for (unsigned y = 0; y < size; ++y)
        for (unsigned x = 0; x < size; ++x)
        {
            unsigned sx = x, sy = y;
            if (angle == 90) { sx = size - 1 - y; sy = x; }
            else if (angle == 180) { sx = size - 1 - x; sy = size - 1 - y; }
            else if (angle == 270) { sx = y; sy = size - 1 - x; }
            const uint8_t *pixel = &output->p[0].p_pixels[y * output->p[0].i_pitch + x * 2];
            assert(pixel[yo] == Pixel(sx, sy, 0));
            if (angle % 180 == 0 && x % 2 == 0)
            {
                assert(pixel[uo] == Pixel(sx / 2, sy, 1));
                assert(pixel[vo] == Pixel(sx / 2, sy, 2));
            }
        }
    CheckMouse(chain, size, angle);
    picture_Release(output);
    filter_chain_Delete(chain);
    es_format_Clean(&format);
}

int main(void)
{
    const char *args[] = {"--ignore-config", "--no-plugins-cache", "--no-media-library", "--no-auto-preparse"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance);
    const vlc_fourcc_t chromas[] = {VLC_CODEC_I444, VLC_CODEC_I420, VLC_CODEC_I422, VLC_CODEC_I411, VLC_CODEC_I410, VLC_CODEC_YV12, VLC_CODEC_YUVA};
    const unsigned sizes[] = {32, 33};
    for (size_t i = 0; i < ARRAY_SIZE(chromas); ++i)
        for (size_t j = 0; j < ARRAY_SIZE(sizes); ++j)
            for (unsigned angle = 0; angle < 360; angle += 90)
                Check(instance, chromas[i], sizes[j], angle);
    const vlc_fourcc_t packed[] = {VLC_CODEC_YUYV, VLC_CODEC_UYVY, VLC_CODEC_YVYU};
    for (size_t i = 0; i < ARRAY_SIZE(packed); ++i)
        for (unsigned angle = 0; angle < 360; angle += 90)
            CheckPacked(instance, packed[i], angle);
    libvlc_release(instance);
    return 0;
}
