// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define MODULE_NAME test_filter_aspect
#undef VLC_DYNAMIC_PLUGIN
#include <vlc/vlc.h>
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_player.h>
#include <vlc_vout.h>
#include <vlc_vout_display.h>
#include <vlc_window.h>
#include <stdatomic.h>
#include <limits.h>
#include <stdio.h>
#undef NDEBUG
#include <assert.h>
#include "../../../lib/libvlc_internal.h"

static vlc_sem_t event;
static atomic_uint frames, width, height;
static atomic_int ratio;
enum { UNKNOWN, LANDSCAPE, PORTRAIT };

static int EnableWindow(vlc_window_t *window, const vlc_window_cfg_t *cfg)
{
    (void) cfg;
    vlc_window_ReportSize(window, 640, 480);
    return VLC_SUCCESS;
}

static int OpenWindow(vlc_window_t *window)
{
    static const struct vlc_window_operations ops = { .enable = EnableWindow };
    window->ops = &ops;
    return VLC_SUCCESS;
}

static void Display(vout_display_t *display, picture_t *picture)
{
    (void) picture;
    unsigned w = display->place->width, h = display->place->height;
    atomic_store(&width, w);
    atomic_store(&height, h);
    atomic_store(&ratio, w * 3 == h * 4 ? LANDSCAPE :
                         w * 4 == h * 3 ? PORTRAIT : UNKNOWN);
    atomic_fetch_add(&frames, 1);
    vlc_sem_post(&event);
}

static int OpenDisplay(vout_display_t *display, video_format_t *format,
                       vlc_video_context *context)
{
    (void) format; (void) context;
    static const struct vlc_display_operations ops = { .display = Display };
    display->ops = &ops;
    return VLC_SUCCESS;
}

vlc_module_begin()
    set_capability("vout window", INT_MAX)
    set_callback(OpenWindow)
    add_shortcut("aspect-test")
    add_submodule()
        set_callback_display(OpenDisplay, INT_MAX)
        add_shortcut("aspect-test")
vlc_module_end()

VLC_EXPORT const vlc_plugin_cb vlc_static_modules[] = { VLC_SYMBOL(vlc_entry), NULL };

static void WaitRatio(int expected, unsigned previous)
{
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
    while (atomic_load(&ratio) != expected || atomic_load(&frames) <= previous)
    {
        if (vlc_sem_timedwait(&event, deadline) != 0)
        {
            fprintf(stderr, "Expected ratio %d, got %ux%u\n", expected,
                    atomic_load(&width), atomic_load(&height));
            assert(!"Video retained the previous filter's aspect ratio");
        }
    }
}

static void Run(bool paused)
{
    fprintf(stderr, "filter aspect: paused=%d\n", paused);
    const char *args[] = {
        "--ignore-config", "--no-plugins-cache", "--no-media-library", "--no-auto-preparse",
        "--no-spu", "--no-osd", "--aout=dummy", "--clock-master=monotonic",
        "--vout=aspect-test,none", "--video-filter=",
    };
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance);
    assert(var_Create(instance->p_libvlc_int, "window", VLC_VAR_STRING) == VLC_SUCCESS);
    assert(var_SetString(instance->p_libvlc_int, "window", "aspect-test,none") == VLC_SUCCESS);
    vlc_player_t *player = vlc_player_New(VLC_OBJECT(instance->p_libvlc_int), VLC_PLAYER_LOCK_NORMAL);
    assert(player);
    input_item_t *media = input_item_New("mock://video_track_count=1;video_width=64;video_height=48;length=60000000", "aspect test");
    assert(media);
    const unsigned initial = atomic_load(&frames);
    vlc_player_Lock(player);
    assert(vlc_player_SetCurrentMedia(player, media) == VLC_SUCCESS);
    assert(vlc_player_Start(player) == VLC_SUCCESS);
    vlc_player_Unlock(player);
    input_item_Release(media);
    WaitRatio(LANDSCAPE, initial);
    vout_thread_t *vout = vlc_player_vout_Hold(player);
    assert(vout);
    assert(var_Create(vout, "transform-type", VLC_VAR_STRING) == VLC_SUCCESS);
    if (paused)
    {
        vlc_player_Lock(player);
        vlc_player_Pause(player);
        vlc_player_Unlock(player);
        const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
        for (;;)
        {
            vlc_player_Lock(player);
            const bool is_paused = vlc_player_IsPaused(player);
            vlc_player_Unlock(player);
            if (is_paused) break;
            assert(vlc_tick_now() < deadline);
            vlc_tick_sleep(VLC_TICK_FROM_MS(10));
        }
    }
    static const struct { const char *type; int ratio; } cases[] = {
        {"90", PORTRAIT}, {"180", LANDSCAPE}, {"270", PORTRAIT},
        {"hflip", LANDSCAPE}, {"transpose", PORTRAIT}, {"vflip", LANDSCAPE},
        {"antitranspose", PORTRAIT}, {"180", LANDSCAPE},
    };
    for (unsigned round = 0; round < 2; ++round)
    {
        for (size_t i = 0; i < ARRAY_SIZE(cases); ++i)
        {
            fprintf(stderr, "transform=%s\n", cases[i].type);
            const unsigned before = atomic_load(&frames);
            assert(var_SetString(vout, "transform-type", cases[i].type) == VLC_SUCCESS);
            assert(var_SetString(vout, "video-filter", "transform") == VLC_SUCCESS);
            WaitRatio(cases[i].ratio, before);
        }
        const unsigned before = atomic_load(&frames);
        assert(var_SetString(vout, "video-filter", "") == VLC_SUCCESS);
        WaitRatio(LANDSCAPE, before);
    }
    vlc_player_Lock(player);
    assert(vlc_player_IsPaused(player) == paused);
    vlc_player_Unlock(player);
    vout_Release(vout);
    vlc_player_Delete(player);
    libvlc_release(instance);
}

int main(void)
{
    vlc_sem_init(&event, 0);
    Run(false);
    Run(true);
    return 0;
}
