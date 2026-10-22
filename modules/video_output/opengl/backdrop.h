/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef VLC_GL_BACKDROP_H
#define VLC_GL_BACKDROP_H

#include <vlc_opengl.h>
#include <vlc_window_backdrop.h>

struct vlc_gl_backdrop;
/* All calls require the video output's current GL context. */
struct vlc_gl_backdrop *vlc_gl_backdrop_New(vlc_gl_t *);
void vlc_gl_backdrop_Delete(struct vlc_gl_backdrop *);
void vlc_gl_backdrop_Draw(struct vlc_gl_backdrop *, unsigned width, unsigned height,
                         const struct vlc_window_backdrop_region *, size_t count);
#endif
