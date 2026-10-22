/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef VLC_VIDEO_WINDOW_INTERNAL_H
#define VLC_VIDEO_WINDOW_INTERNAL_H

/* Called under the window lock. The callback must only wake the renderer;
 * it must not access the window or synchronously render. */
void vlc_window_SetBackdropInvalidator(vlc_window_t *, void (*)(void *), void *);

#endif
