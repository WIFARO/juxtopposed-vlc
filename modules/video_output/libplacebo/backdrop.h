/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef VLC_PLACEBO_BACKDROP_H
#define VLC_PLACEBO_BACKDROP_H

#include <libplacebo/gpu.h>
#include <vlc_window_backdrop.h>

struct vlc_placebo_backdrop;
struct vlc_placebo_backdrop *vlc_placebo_backdrop_New(pl_log, pl_gpu);
void vlc_placebo_backdrop_Delete(struct vlc_placebo_backdrop *);

/* Render the complete display, including subpictures, to this texture before
 * applying backdrops. The original swapchain texture need not be sampleable. */
pl_tex vlc_placebo_backdrop_Target(struct vlc_placebo_backdrop *, pl_tex);
bool vlc_placebo_backdrop_Render(struct vlc_placebo_backdrop *, pl_tex,
    const struct vlc_window_backdrop_region *, size_t, bool flipped);

#endif
