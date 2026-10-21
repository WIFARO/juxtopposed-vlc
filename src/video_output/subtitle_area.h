/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef VLC_SUBTITLE_AREA_H
#define VLC_SUBTITLE_AREA_H

#include <limits.h>
#include <vlc_common.h>
#include <vlc_es.h>
#include <vlc_subpicture.h>
#include <vlc_vout_display.h>

/* Normalized window coordinates. Other window providers use the full area. */
struct vlc_subtitle_area { float left, top, right, bottom; };
#define VLC_SUBTITLE_AREA_FULL ((struct vlc_subtitle_area){ 0, 0, 1, 1 })

static inline bool vlc_subtitle_area_IsValid(struct vlc_subtitle_area a)
{
    return a.left >= 0 && a.top >= 0 && a.right <= 1 && a.bottom <= 1
        && a.left < a.right && a.top < a.bottom;
}

static inline bool vlc_subtitle_area_GetBounds(struct vlc_subtitle_area a,
        unsigned window_width, unsigned window_height,
        const vout_display_place_t *video, const video_format_t *frame,
        bool window_coordinates, vout_display_place_t *bounds)
{
    if (!vlc_subtitle_area_IsValid(a) || !window_width || !window_height
        || window_width > INT_MAX || window_height > INT_MAX
        || (a.left == 0 && a.top == 0 && a.right == 1 && a.bottom == 1))
        return false;
    double left = a.left * (double)window_width;
    double top = a.top * (double)window_height;
    double right = a.right * (double)window_width;
    double bottom = a.bottom * (double)window_height;
    double x = 0, y = 0, width = window_width, height = window_height;
    if (!window_coordinates)
    {
        if (!video || !video->width || !video->height || !frame
            || (uint64_t)frame->i_x_offset + frame->i_visible_width > INT_MAX
            || (uint64_t)frame->i_y_offset + frame->i_visible_height > INT_MAX)
            return false;
        x = frame->i_x_offset; y = frame->i_y_offset;
        width = frame->i_visible_width; height = frame->i_visible_height;
        left = x + (left - video->x) * width / video->width;
        right = x + (right - video->x) * width / video->width;
        top = y + (top - video->y) * height / video->height;
        bottom = y + (bottom - video->y) * height / video->height;
    }
    left = __MAX(x, left); right = __MIN(x + width, right);
    top = __MAX(y, top); bottom = __MIN(y + height, bottom);
    if (right <= left || bottom <= top) return false;
    bounds->x = (int)(left + .5); bounds->y = (int)(top + .5);
    bounds->width = (int)(right + .5) - bounds->x;
    bounds->height = (int)(bottom + .5) - bounds->y;
    return bounds->width > 0 && bounds->height > 0;
}

static inline void vlc_subtitle_area_PlaceDefault(vout_display_place_t *region,
        const vout_display_place_t *target, const vout_display_place_t *safe,
        int alignment)
{
    const int64_t x_min = __MAX((int64_t)target->x, safe->x);
    const int64_t y_min = __MAX((int64_t)target->y, safe->y);
    const int64_t x_max = __MIN((int64_t)target->x + target->width,
                               (int64_t)safe->x + safe->width);
    const int64_t y_max = __MIN((int64_t)target->y + target->height,
                               (int64_t)safe->y + safe->height);
    if (x_max <= x_min || y_max <= y_min)
        return;

    const int64_t left = __MAX((int64_t)safe->x - target->x, 0);
    const int64_t top = __MAX((int64_t)safe->y - target->y, 0);
    const int64_t right = __MAX((int64_t)target->x + target->width
                               - ((int64_t)safe->x + safe->width), 0);
    const int64_t bottom = __MAX((int64_t)target->y + target->height
                                - ((int64_t)safe->y + safe->height), 0);
    /* Add the interface inset independently of the saved subtitle offset. */
    int64_t x = (int64_t)region->x + (alignment & SUBPICTURE_ALIGN_LEFT ? left
              : alignment & SUBPICTURE_ALIGN_RIGHT ? -right : (left - right) / 2);
    int64_t y = (int64_t)region->y + (alignment & SUBPICTURE_ALIGN_TOP ? top
              : alignment & SUBPICTURE_ALIGN_BOTTOM ? -bottom : (top - bottom) / 2);
    /* Keep excessive offsets within the part of the picture that is visible. */
    if (region->width <= (uint64_t)(x_max - x_min))
        x = __MAX(x_min, __MIN(x, x_max - region->width));
    if (region->height <= (uint64_t)(y_max - y_min))
        y = __MAX(y_min, __MIN(y, y_max - region->height));
    region->x = __MAX(INT_MIN, __MIN(x, INT_MAX));
    region->y = __MAX(INT_MIN, __MIN(y, INT_MAX));
}
#endif
