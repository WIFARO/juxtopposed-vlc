/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#undef NDEBUG
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <vlc_common.h>

static unsigned errors;
static const char *failed_stage;
static void test_error(const char *format, ...)
{
    assert(strcmp(format, "OpenGL frame %s failed (%d)") == 0);
    va_list args;
    va_start(args, format);
    failed_stage = va_arg(args, const char *);
    assert(va_arg(args, int) == VLC_EGENERIC);
    va_end(args);
    ++errors;
}
#undef msg_Err
#define msg_Err(object, ...) test_error(__VA_ARGS__)
#define MODULE_NAME test_gl_display
#define MODULE_STRING "test_gl_display"
#include "../../../../modules/video_output/opengl/display.c"

static bool current;
static int make_result, prepare_result, draw_result;
static unsigned makes, releases, prepares, draws, flushes, swaps, backdrops;
static vout_display_opengl_t *const helper = (vout_display_opengl_t *)1;

static int TestMakeCurrent(vlc_gl_t *gl)
{
    (void)gl;
    assert(!current);
    ++makes;
    current = make_result == VLC_SUCCESS;
    return make_result;
}
static void TestReleaseCurrent(vlc_gl_t *gl)
{
    (void)gl;
    assert(current);
    current = false;
    ++releases;
}
static void TestSwap(vlc_gl_t *gl)
{
    (void)gl;
    assert(!current);
    ++swaps;
}
static void APIENTRY TestFlush(void)
{
    assert(current);
    ++flushes;
}
int vout_display_opengl_Prepare(vout_display_opengl_t *vgl, picture_t *picture,
                                const struct vlc_render_subpicture *subpicture)
{
    assert(vgl == helper && picture && !subpicture && current);
    ++prepares;
    return prepare_result;
}
int vout_display_opengl_Display(vout_display_opengl_t *vgl)
{
    assert(vgl == helper && current);
    ++draws;
    return draw_result;
}
void vlc_gl_backdrop_window_Draw(struct vlc_gl_backdrop_window *state, vlc_gl_t *gl,
                                vlc_window_t *window, unsigned width, unsigned height)
{
    (void)state; (void)gl;
    assert(window->type == VLC_WINDOW_TYPE_WAYLAND && width == 16 && height == 16);
    assert(current);
    ++backdrops;
}
/* The fixture keeps placement, projection and stereo unchanged. These helpers
 * must not be invoked while exercising frame preparation and presentation. */
int vout_display_opengl_ChangeStereoMode(vout_display_opengl_t *vgl,
                                         vlc_stereoscopic_mode_t mode)
{
    (void)vgl; (void)mode;
    assert(!"unexpected stereo change");
    return VLC_EGENERIC;
}
int vout_display_opengl_ChangeProjection(vout_display_opengl_t *vgl,
                                         video_projection_mode_t projection)
{
    (void)vgl; (void)projection;
    assert(!"unexpected projection change");
    return VLC_EGENERIC;
}
void vout_display_opengl_SetOutputSize(vout_display_opengl_t *vgl,
                                       unsigned width, unsigned height)
{
    (void)vgl; (void)width; (void)height;
    assert(!"unexpected output resize");
}
void vout_display_opengl_Viewport(vout_display_opengl_t *vgl, int x, int y,
                                  unsigned width, unsigned height)
{
    (void)vgl; (void)x; (void)y; (void)width; (void)height;
    assert(!"unexpected viewport change");
}

int main(void)
{
    static const struct vlc_gl_operations gl_ops = {
        .make_current = TestMakeCurrent,
        .release_current = TestReleaseCurrent,
        .swap = TestSwap,
    };
    vlc_gl_t gl = {.ops = &gl_ops};
    vout_display_sys_t sys = {.vgl = helper, .gl = &gl, .vt.Flush = TestFlush};
    vlc_window_t window = {.type = VLC_WINDOW_TYPE_WAYLAND};
    vout_display_cfg_t cfg = {.window = &window, .display = {.width = 16, .height = 16}};
    picture_t picture = {.format = {.i_visible_width = 16, .i_visible_height = 16}};
    vout_display_t vd = {.sys = &sys, .source = &picture.format, .cfg = &cfg};

    /* Failed imports never draw, flush, blur or present an incomplete frame.
     * Repeated failures produce one diagnostic until a frame succeeds. */
    prepare_result = VLC_EGENERIC;
    PictureRender(&vd, &picture, NULL, 0);
    PictureDisplay(&vd, &picture);
    PictureRender(&vd, &picture, NULL, 0);
    PictureDisplay(&vd, &picture);
    assert(prepares == 2 && draws == 0 && flushes == 0 && swaps == 0 && backdrops == 0);
    assert(makes == 2 && releases == 2 && !current && !sys.is_dirty);
    assert(errors == 1 && strcmp(failed_stage, "prepare") == 0);

    prepare_result = VLC_SUCCESS;
    PictureRender(&vd, &picture, NULL, 0);
    PictureDisplay(&vd, &picture);
    assert(prepares == 3 && draws == 1 && flushes == 1 && swaps == 1 && backdrops == 1);
    assert(makes == 3 && releases == 3 && !current);

    /* A failed draw does not reuse the successful frame's presentation flag. */
    draw_result = VLC_EGENERIC;
    PictureRender(&vd, &picture, NULL, 0);
    PictureDisplay(&vd, &picture);
    assert(prepares == 4 && draws == 2 && flushes == 1 && swaps == 1 && backdrops == 1);
    assert(makes == 4 && releases == 4 && !current && !sys.is_dirty);
    assert(errors == 2 && strcmp(failed_stage, "draw") == 0);

    draw_result = VLC_SUCCESS;
    PictureRender(&vd, &picture, NULL, 0);
    PictureDisplay(&vd, &picture);
    assert(prepares == 5 && draws == 3 && flushes == 2 && swaps == 2 && backdrops == 2);
    assert(makes == 5 && releases == 5 && !current);

    /* A context failure after success also cannot present stale contents. */
    make_result = VLC_EGENERIC;
    PictureRender(&vd, &picture, NULL, 0);
    PictureDisplay(&vd, &picture);
    assert(prepares == 5 && draws == 3 && flushes == 2 && swaps == 2 && backdrops == 2);
    assert(makes == 6 && releases == 5 && !current && !sys.is_dirty);

    make_result = VLC_SUCCESS;
    PictureRender(&vd, &picture, NULL, 0);
    PictureDisplay(&vd, &picture);
    assert(prepares == 6 && draws == 4 && flushes == 3 && swaps == 3 && backdrops == 3);
    assert(makes == 7 && releases == 6 && !current);
    puts("PASS: OpenGL frame failures, bounded diagnostics, context release and recovery");
}
