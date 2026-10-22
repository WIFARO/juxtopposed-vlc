/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#define MODULE_NAME test_window_backdrop
#undef VLC_DYNAMIC_PLUGIN
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_plugin.h>
#include <vlc_window_backdrop.h>
#include <vlc_threads.h>
#include <math.h>

static const struct vlc_window_operations ops = {0};
static int Open(vlc_window_t *window)
{
    window->type = VLC_WINDOW_TYPE_DUMMY;
    window->ops = &ops;
    return VLC_SUCCESS;
}
vlc_module_begin()
    set_callback(Open)
    set_capability("vout window", 1)
vlc_module_end()
VLC_EXPORT const vlc_plugin_cb vlc_static_modules[] = {VLC_SYMBOL(vlc_entry), NULL};

static void *Read(void *opaque)
{
    for (unsigned i = 0; i < 10000; ++i)
    {
        vlc_window_backdrops_t *snapshot = vlc_window_HoldBackdrops(opaque);
        size_t count;
        const struct vlc_window_backdrop_region *r = vlc_window_GetBackdropRegions(snapshot, &count);
        assert(count == 1);
        assert(r->x > 0 && r->y == 2*r->x && r->width == 3*r->x);
        vlc_window_ReleaseBackdrops(snapshot);
    }
    return NULL;
}

int main(void)
{
    test_init();
    const char *args[] = {"--ignore-config", "--no-plugins-cache", "--disable-screensaver=0"};
    libvlc_instance_t *vlc = libvlc_new(ARRAY_SIZE(args), args);
    assert(vlc);
    vlc_window_owner_t owner = {0};
    vlc_window_t *window = vlc_window_New(&vlc->p_libvlc_int->obj, MODULE_STRING, &owner, NULL);
    assert(window);
    size_t count = SIZE_MAX;
    assert(!vlc_window_GetBackdropRegions(NULL, &count) && count == 0);
    assert(!vlc_window_HoldBackdrops(window));
    assert(vlc_window_SetBackdrops(window, NULL, 0) == VLC_SUCCESS);
    vlc_window_ReleaseBackdrops(NULL);
    struct vlc_window_backdrop_region region = {1, 2, 3, 4, 25, 14};
    assert(vlc_window_SetBackdrops(window, &region, 1) == VLC_SUCCESS);
    vlc_window_backdrops_t *first = vlc_window_HoldBackdrops(window);
    assert(first);
    assert(vlc_window_SetBackdrops(window, &region, 1) == VLC_SUCCESS);
    vlc_window_backdrops_t *duplicate = vlc_window_HoldBackdrops(window);
    assert(duplicate == first); // Identical geometry must not wake a paused renderer.
    vlc_window_ReleaseBackdrops(duplicate);
    assert(vlc_window_SetBackdrops(window, NULL, 1) == VLC_EINVAL);
    assert(vlc_window_SetBackdrops(window, &region, SIZE_MAX) == VLC_EINVAL);
    const struct vlc_window_backdrop_region invalid[] = {
        {NAN,2,3,4,25,14}, {1,INFINITY,3,4,25,14}, {1,2,0,4,25,14},
        {1,2,3,-1,25,14}, {1,2,3,4,0,14}, {1,2,3,4,INFINITY,14}, {1,2,3,4,25,-1},
    };
    for (size_t i = 0; i < ARRAY_SIZE(invalid); ++i)
        assert(vlc_window_SetBackdrops(window, &invalid[i], 1) == VLC_EINVAL);
    duplicate = vlc_window_HoldBackdrops(window);
    assert(duplicate == first);
    vlc_window_ReleaseBackdrops(duplicate);
    vlc_thread_t reader;
    assert(vlc_clone(&reader, Read, window) == 0);
    for (unsigned i = 1; i < 3000; ++i)
    {
        region.x = i; region.y = 2*i; region.width = 3*i;
        assert(vlc_window_SetBackdrops(window, &region, 1) == VLC_SUCCESS);
    }
    vlc_join(reader, NULL);
    const struct vlc_window_backdrop_region *old = vlc_window_GetBackdropRegions(first, &count);
    assert(count == 1 && old->x == 1 && old->width == 3); // A held array never changes.
    struct vlc_window_backdrop_region pair[] = {region, {-50,-70,100,200,10,0}};
    assert(vlc_window_SetBackdrops(window, pair, 2) == VLC_SUCCESS);
    vlc_window_backdrops_t *last = vlc_window_HoldBackdrops(window);
    assert(vlc_window_GetBackdropRegions(last, &count)[1].x == -50 && count == 2);
    assert(vlc_window_SetBackdrops(window, NULL, 0) == VLC_SUCCESS);
    assert(!vlc_window_HoldBackdrops(window));
    vlc_window_Delete(window);
    assert(vlc_window_GetBackdropRegions(last, &count)[1].y == -70 && count == 2);
    assert(vlc_window_GetBackdropRegions(first, &count)[0].x == 1 && count == 1);
    vlc_window_ReleaseBackdrops(last);
    vlc_window_ReleaseBackdrops(first);
    libvlc_release(vlc);
    return 0;
}
