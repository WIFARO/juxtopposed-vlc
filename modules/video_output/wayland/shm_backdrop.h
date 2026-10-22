/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef VLC_WAYLAND_SHM_BACKDROP_H
#define VLC_WAYLAND_SHM_BACKDROP_H

#include <vlc_picture.h>
#include <vlc_vout_display.h>
#include <vlc_window_backdrop.h>

struct vlc_shm_backdrop;
struct vlc_shm_backdrop *vlc_shm_backdrop_New(void);
void vlc_shm_backdrop_Delete(struct vlc_shm_backdrop *);

/* Produce a full video-window BGRX buffer, including placement and letterbox
 * pixels. crop supplies the source's orientation as well as its visible area;
 * an allocator may have reset the picture storage header's orientation.
 * Coordinates and radii are physical pixels. The returned picture is
 * held for the caller and remains immutable across later calls. Source pictures
 * are never modified. Identical pixels and geometry reuse the cached result. */
picture_t *vlc_shm_backdrop_Prepare(struct vlc_shm_backdrop *, const picture_t *,
    const video_format_t *crop, const vout_display_place_t *, unsigned width,
    unsigned height, const struct vlc_window_backdrop_region *, size_t count);
#endif
