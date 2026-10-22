/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_picture.h>
#include <assert.h>
#include <stdio.h>
#include "../../../../modules/video_output/libplacebo/utils.h"

const char vlc_module_name[] = "libplacebo_plane_test";

int main(void)
{
    const struct {
        vlc_fourcc_t chroma;
        unsigned width_den, height_den;
    } formats[] = {
        {VLC_CODEC_BGRX, 1, 1}, {VLC_CODEC_I420, 2, 2},
        {VLC_CODEC_I422, 2, 1}, {VLC_CODEC_I444, 1, 1},
        {VLC_CODEC_NV12, 2, 2}, {VLC_CODEC_NV16, 2, 1},
    };
    const struct { unsigned x, y, half_x, half_y; } crops[] = {
        {0, 0, 0, 0}, {4, 6, 2, 3}, {5, 7, 3, 4},
    };
    unsigned scenarios = 0;
    for (size_t f = 0; f < ARRAY_SIZE(formats); ++f)
    {
        video_format_t fmt; video_format_Init(&fmt, formats[f].chroma);
        video_format_Setup(&fmt, formats[f].chroma, 64, 48, 64, 48, 1, 1);
        picture_t *pic = picture_NewFromFormat(&fmt); assert(pic);
        for (unsigned offset = 0; offset < 3; ++offset)
        {
            const unsigned x = crops[offset].x;
            const unsigned y = crops[offset].y;
            pic->format.i_x_offset = x; pic->format.i_y_offset = y;
            pic->format.i_visible_width = 40; pic->format.i_visible_height = 30;
            struct pl_plane_data data[4];
            assert(vlc_placebo_PlaneData(pic, data) == pic->i_planes);
            for (int p = 0; p < pic->i_planes; ++p)
            {
                const unsigned wd = p ? formats[f].width_den : 1;
                const unsigned hd = p ? formats[f].height_den : 1;
                const unsigned plane_x = wd == 2 ? crops[offset].half_x : x;
                const unsigned plane_y = hd == 2 ? crops[offset].half_y : y;
                const uint8_t *expected = pic->p[p].p_pixels
                    + plane_y * pic->p[p].i_pitch + plane_x * pic->p[p].i_pixel_pitch;
                if (data[p].pixels != expected)
                    fprintf(stderr, "Plane offset mismatch: %.4s plane%d ROI%u,%u\n",
                            (const char *)&formats[f].chroma, p, x, y);
                assert(data[p].pixels == expected);
                assert(data[p].width == (wd == 2 ? 20 : 40));
                assert(data[p].height == (hd == 2 ? 15 : 30));
                assert(data[p].row_stride == (size_t)pic->p[p].i_pitch);
            }
            video_format_t source = pic->format;
            struct pl_rect2df crop = vlc_placebo_PictureCrop(pic, &source);
            assert(crop.x0 == 0 && crop.y0 == 0 && crop.x1 == 40 && crop.y1 == 30);
            source.i_x_offset += 8; source.i_y_offset += 4;
            source.i_visible_width = 24; source.i_visible_height = 22;
            crop = vlc_placebo_PictureCrop(pic, &source);
            assert(crop.x0 == 8 && crop.y0 == 4 && crop.x1 == 32 && crop.y1 == 26);
            ++scenarios;
        }
        picture_Release(pic); video_format_Clean(&fmt);
    }
    fprintf(stderr, "%u libplacebo plane/crop scenarios passed\n", scenarios);
    return 0;
}
