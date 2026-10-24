// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc/vlc.h>
#include <vlc_common.h>
#include <vlc_filter.h>
#include <vlc_picture.h>
#include "../../../lib/libvlc_internal.h"
#undef NDEBUG
#include <assert.h>

static filter_t *NewFilter(libvlc_instance_t *instance, vlc_fourcc_t input,
                           vlc_fourcc_t output)
{
    filter_t *filter = vlc_object_create(instance->p_libvlc_int, sizeof(*filter));
    assert(filter);
    es_format_Init(&filter->fmt_in, VIDEO_ES, input);
    es_format_Init(&filter->fmt_out, VIDEO_ES, output);
    video_format_Setup(&filter->fmt_in.video, input, 16, 2, 16, 2, 1, 1);
    video_format_Setup(&filter->fmt_out.video, output, 16, 2, 16, 2, 1, 1);
    return filter;
}

static void DeleteFilter(filter_t *filter)
{
    vlc_filter_UnloadModule(filter);
    es_format_Clean(&filter->fmt_in);
    es_format_Clean(&filter->fmt_out);
    vlc_object_delete(filter);
}

static void KeepBuffer(picture_t *picture) { (void)picture; }

static picture_t *GuardedPicture(const video_format_t *format, uint8_t *memory)
{
    picture_resource_t resource = {
        .pf_destroy = KeepBuffer,
        .p = {{.p_pixels = memory + 16, .i_lines = 2, .i_pitch = 64}},
    };
    picture_t *picture = picture_NewFromResource(format, &resource);
    assert(picture);
    return picture;
}

static picture_t *OutputBuffer(filter_t *filter)
{
    return picture_Hold(filter->owner.sys);
}

static void CheckRGB(libvlc_instance_t *instance, vlc_fourcc_t chroma,
                     int alpha)
{
    filter_t *filter = NewFilter(instance, chroma, chroma);
    filter->p_module = vlc_filter_LoadModule(filter, "video filter", "sepia", true);
    assert(filter->p_module);
    uint8_t input_memory[160], output_memory[160];
    memset(input_memory, 0x23, sizeof(input_memory));
    memset(output_memory, 0xa5, sizeof(output_memory));
    picture_t *input = GuardedPicture(&filter->fmt_in.video, input_memory);
    picture_t *buffer = GuardedPicture(&filter->fmt_out.video, output_memory);
    static const struct filter_video_callbacks callbacks = {.buffer_new = OutputBuffer};
    filter->owner.video = &callbacks;
    filter->owner.sys = buffer;
    picture_t *output = filter->ops->filter_video(filter, input);
    assert(output == buffer);
    for (size_t i = 0; i < 16; ++i)
    {
        /* RGBX has no alpha index. It must not access byte -1. */
        assert(output_memory[i] == 0xa5);
        assert(output_memory[144 + i] == 0xa5);
    }
    assert(output_memory[20] != 0xa5 || output_memory[21] != 0xa5);
    if (alpha >= 0)
        for (size_t pixel = 0; pixel < 32; ++pixel)
            assert(output_memory[16 + pixel * 4 + alpha] == 0x23);
    picture_Release(output);
    picture_Release(buffer);
    DeleteFilter(filter);
}

int main(void)
{
    const char *args[] = {"--ignore-config", "--no-plugins-cache", "--no-media-library", "--no-auto-preparse"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance);
    const vlc_fourcc_t mismatches[][2] = {
        {VLC_CODEC_RGBA, VLC_CODEC_I420},
        {VLC_CODEC_I420, VLC_CODEC_RGBA},
        {VLC_CODEC_RGBX, VLC_CODEC_BGRX},
    };
    for (size_t i = 0; i < ARRAY_SIZE(mismatches); ++i)
    {
        filter_t *filter = NewFilter(instance, mismatches[i][0], mismatches[i][1]);
        assert(!vlc_filter_LoadModule(filter, "video filter", "sepia", true));
        es_format_Clean(&filter->fmt_in);
        es_format_Clean(&filter->fmt_out);
        vlc_object_delete(filter);
    }
    CheckRGB(instance, VLC_CODEC_RGBX, -1);
    CheckRGB(instance, VLC_CODEC_XRGB, -1);
    CheckRGB(instance, VLC_CODEC_BGRX, -1);
    CheckRGB(instance, VLC_CODEC_XBGR, -1);
    CheckRGB(instance, VLC_CODEC_RGBA, 3);
    CheckRGB(instance, VLC_CODEC_ARGB, 0);
    CheckRGB(instance, VLC_CODEC_BGRA, 3);
    CheckRGB(instance, VLC_CODEC_ABGR, 0);
    libvlc_release(instance);
    return 0;
}
