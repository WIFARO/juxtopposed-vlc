/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <vlc_window.h>
#include "../../../src/video_output/subtitle_area.h"

static unsigned reports;
static void Report(vlc_window_t *window, float left, float top, float right, float bottom)
{
    (void)window;
    assert(left == .1f && top == .2f && right == .8f && bottom == .9f);
    reports++;
}

static void CheckBounds(vout_display_place_t place, int x, int y, unsigned w, unsigned h)
{
    assert(place.x == x && place.y == y && place.width == w && place.height == h);
}

int main(void)
{
    struct vlc_window_callbacks callbacks = { 0 };
    vlc_window_t window = { .owner.cbs = &callbacks };
    vlc_window_ReportSubtitleArea(&window, .1f, .2f, .8f, .9f);
    assert(reports == 0);
    callbacks.subtitle_area = Report;
    vlc_window_ReportSubtitleArea(&window, .1f, .2f, .8f, .9f);
    assert(reports == 1);

    const struct vlc_subtitle_area full = VLC_SUBTITLE_AREA_FULL;
    const struct vlc_subtitle_area visible = { 10.f/1920, 40.f/1080, 1910.f/1920, 1026.f/1080 };
    assert(vlc_subtitle_area_IsValid(full));
    const struct vlc_subtitle_area invalid[] = {
        { NAN, 0, 1, 1 }, { 0, 0, INFINITY, 1 }, { 0, -1, 1, 1 },
        { 0, 0, 1, 2 }, { 1, 0, 0, 1 }, { 0, 0, 0, 1 },
    };
    vout_display_place_t bounds;
    for (size_t i = 0; i < ARRAY_SIZE(invalid); ++i)
    {
        assert(!vlc_subtitle_area_IsValid(invalid[i]));
        assert(!vlc_subtitle_area_GetBounds(invalid[i], 1920, 1080, NULL, NULL, true, &bounds));
    }
    assert(!vlc_subtitle_area_GetBounds(full, 1920, 1080, NULL, NULL, true, &bounds));
    assert(!vlc_subtitle_area_GetBounds(visible, 0, 1080, NULL, NULL, true, &bounds));
    assert(!vlc_subtitle_area_GetBounds(visible, UINT_MAX, 1080, NULL, NULL, true, &bounds));
    assert(!vlc_subtitle_area_GetBounds(visible, 1920, 1080, NULL, NULL, false, &bounds));
    assert(vlc_subtitle_area_GetBounds(visible, 1920, 1080, NULL, NULL, true, &bounds));
    CheckBounds(bounds, 10, 40, 1900, 986);
    assert(vlc_subtitle_area_GetBounds(visible, 3840, 2160, NULL, NULL, true, &bounds));
    CheckBounds(bounds, 20, 80, 3800, 1972);

    video_format_t frame = { .i_visible_width = 1200, .i_visible_height = 400 };
    vout_display_place_t video = { .x = 0, .y = 220, .width = 1920, .height = 640 };
    assert(vlc_subtitle_area_GetBounds(visible, 1920, 1080, &video, &frame, false, &bounds));
    CheckBounds(bounds, 6, 0, 1188, 400);
    frame.i_visible_width = 360; frame.i_visible_height = 640;
    video = (vout_display_place_t){ .x = 656, .y = 0, .width = 608, .height = 1080 };
    assert(vlc_subtitle_area_GetBounds(visible, 1920, 1080, &video, &frame, false, &bounds));
    CheckBounds(bounds, 0, 24, 360, 584);
    frame.i_x_offset = UINT_MAX;
    assert(!vlc_subtitle_area_GetBounds(visible, 1920, 1080, &video, &frame, false, &bounds));

    const vout_display_place_t target = { .width = 1920, .height = 1080 };
    const vout_display_place_t safe = { .x = 10, .y = 40, .width = 1900, .height = 986 };
    vout_display_place_t region = { .x = 660, .y = 990, .width = 600, .height = 90 };
    vlc_subtitle_area_PlaceDefault(&region, &target, &safe, SUBPICTURE_ALIGN_BOTTOM);
    CheckBounds(region, 660, 936, 600, 90);
    region.y = 960; /* Saved user margin of 30 remains independent of the UI inset. */
    vlc_subtitle_area_PlaceDefault(&region, &target, &safe, SUBPICTURE_ALIGN_BOTTOM);
    assert(region.y == 906);
    region.y = 0;
    vlc_subtitle_area_PlaceDefault(&region, &target, &safe, SUBPICTURE_ALIGN_TOP);
    assert(region.y == 40);
    region.y = 495;
    vlc_subtitle_area_PlaceDefault(&region, &target, &safe, 0);
    assert(region.y == 488);
    region.x = 0;
    vlc_subtitle_area_PlaceDefault(&region, &target, &safe, SUBPICTURE_ALIGN_LEFT);
    assert(region.x == 10);
    region.x = 1320;
    vlc_subtitle_area_PlaceDefault(&region, &target, &safe, SUBPICTURE_ALIGN_RIGHT);
    assert(region.x == 1310);
    region.y = INT_MAX;
    vlc_subtitle_area_PlaceDefault(&region, &target, &safe, SUBPICTURE_ALIGN_TOP);
    assert(region.y == 936);
    const vout_display_place_t disjoint = { .x = 2000, .width = 20, .height = 20 };
    vlc_subtitle_area_PlaceDefault(&region, &target, &disjoint, SUBPICTURE_ALIGN_LEFT);
    assert(region.x == 1310 && region.y == 936);
    const vout_display_place_t zoom = { .x = -300, .y = -200, .width = 2520, .height = 1480 };
    region.x = 660; region.y = 1190;
    vlc_subtitle_area_PlaceDefault(&region, &zoom, &safe, SUBPICTURE_ALIGN_BOTTOM);
    CheckBounds(region, 660, 936, 600, 90);
    return 0;
}
