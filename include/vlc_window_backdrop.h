/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef VLC_WINDOW_BACKDROP_H
#define VLC_WINDOW_BACKDROP_H

#include <vlc_common.h>
#include <vlc_window.h>

/** A display-only blur region, in physical pixels relative to the video window.
 * Coordinates start at its top left. Radii are also in physical pixels.
 * This must not alter decoder pictures, filters or video snapshots. */
struct vlc_window_backdrop_region
{
    float x, y, width, height;
    float blur_radius, corner_radius;
};

typedef struct vlc_window_backdrops vlc_window_backdrops_t;

/** Replace the regions atomically. A zero count removes all regions.
 * The caller must keep the window alive for the duration of this call.
 * Invalid regions leave the previous snapshot unchanged. */
VLC_API int vlc_window_SetBackdrops(vlc_window_t *,
    const struct vlc_window_backdrop_region *, size_t count);

/** Acquire an immutable snapshot. NULL means no active regions.
 * A held snapshot remains valid after the window is destroyed. */
VLC_API vlc_window_backdrops_t *vlc_window_HoldBackdrops(vlc_window_t *);
VLC_API void vlc_window_ReleaseBackdrops(vlc_window_backdrops_t *);

/** Borrow the array for the lifetime of a held snapshot. Accepts NULL. */
VLC_API const struct vlc_window_backdrop_region *
vlc_window_GetBackdropRegions(const vlc_window_backdrops_t *, size_t *count);

#endif
