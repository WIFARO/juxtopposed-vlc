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

/* Per-output state. Initialize to zero; destroy with the GL context current. */
struct vlc_gl_backdrop_window
{
    struct vlc_gl_backdrop *renderer;
    bool failed;
};
void vlc_gl_backdrop_window_Draw(struct vlc_gl_backdrop_window *, vlc_gl_t *,
                                vlc_window_t *, unsigned width, unsigned height);
void vlc_gl_backdrop_window_Close(struct vlc_gl_backdrop_window *);
#endif
