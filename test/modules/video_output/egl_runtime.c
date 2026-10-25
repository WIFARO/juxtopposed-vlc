// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#undef NDEBUG
#include <assert.h>
#include <stdarg.h>
#include <EGL/egl.h>
#include <vlc_common.h>

static unsigned errors;
static void test_error(const char *format, ...)
{
    assert(strstr(format, "failed (EGL error") != NULL);
    va_list args;
    va_start(args, format);
    assert(strncmp(va_arg(args, const char *), "egl", 3) == 0);
    EGLint code = va_arg(args, EGLint);
    assert(code == EGL_BAD_ACCESS || code == EGL_BAD_SURFACE);
    va_end(args);
    ++errors;
}
static void test_debug(void *object, const char *format, ...)
{
    (void)object;
    (void)format;
}
#undef msg_Err
#define msg_Err(object, ...) test_error(__VA_ARGS__)
#undef msg_Dbg
#define msg_Dbg(object, ...) test_debug(object, __VA_ARGS__)
#define USE_PLATFORM_WAYLAND
#define MODULE_NAME egl_wl
#define MODULE_STRING "egl_wl"
#include "../../../modules/video_output/opengl/egl.c"

static EGLDisplay current_display;
static EGLSurface current_draw, current_read;
static EGLContext current_context;
static EGLint thread_error = EGL_SUCCESS;
static EGLint make_error = EGL_SUCCESS, swap_error = EGL_SUCCESS;
static EGLContext reject_context;
static unsigned make_calls, swap_calls;

EGLDisplay EGLAPIENTRY eglGetCurrentDisplay(void) { return current_display; }
EGLContext EGLAPIENTRY eglGetCurrentContext(void) { return current_context; }
EGLSurface EGLAPIENTRY eglGetCurrentSurface(EGLint target)
{
    return target == EGL_DRAW ? current_draw : current_read;
}
EGLint EGLAPIENTRY eglGetError(void)
{
    EGLint code = thread_error;
    thread_error = EGL_SUCCESS;
    return code;
}
EGLBoolean EGLAPIENTRY eglMakeCurrent(EGLDisplay display, EGLSurface draw,
                                      EGLSurface read, EGLContext context)
{
    ++make_calls;
    if (make_error != EGL_SUCCESS ||
        (reject_context != EGL_NO_CONTEXT && context == reject_context))
    {
        thread_error = make_error != EGL_SUCCESS ? make_error : EGL_BAD_ACCESS;
        return EGL_FALSE;
    }
    current_display = context != EGL_NO_CONTEXT ? display : EGL_NO_DISPLAY;
    current_draw = draw;
    current_read = read;
    current_context = context;
    return EGL_TRUE;
}
EGLBoolean EGLAPIENTRY eglSwapBuffers(EGLDisplay display, EGLSurface surface)
{
    assert(display == current_display && surface == current_draw);
    ++swap_calls;
    if (swap_error != EGL_SUCCESS)
    {
        thread_error = swap_error;
        return EGL_FALSE;
    }
    return EGL_TRUE;
}

int main(void)
{
    vlc_gl_sys_t sys = {
        .display = (EGLDisplay)1,
        .surface = (EGLSurface)2,
        .context = (EGLContext)3,
    };
    vlc_gl_t gl = {.sys = &sys};
    assert(MakeCurrent(&gl) == VLC_SUCCESS && current_context == sys.context);
    make_error = EGL_BAD_ACCESS;
    ReleaseCurrent(&gl);
    ReleaseCurrent(&gl);
    assert(current_context == sys.context && errors == 1);
    make_error = EGL_SUCCESS;
    ReleaseCurrent(&gl);
    assert(current_context == EGL_NO_CONTEXT);
    make_error = EGL_BAD_ACCESS;
    ReleaseCurrent(&gl);
    assert(errors == 2); /* Successful recovery re-enables the diagnostic. */
    make_error = EGL_SUCCESS;

    const EGLDisplay other_display = (EGLDisplay)4;
    const EGLSurface other_draw = (EGLSurface)5, other_read = (EGLSurface)6;
    const EGLContext other_context = (EGLContext)7;
    assert(eglMakeCurrent(other_display, other_draw, other_read, other_context));
    make_error = EGL_BAD_ACCESS;
    unsigned swaps = swap_calls;
    SwapBuffers(&gl);
    SwapBuffers(&gl);
    assert(swap_calls == swaps && current_context == other_context);
    assert(errors == 3);
    make_error = EGL_SUCCESS;
    SwapBuffers(&gl);
    assert(swap_calls == swaps + 1);
    assert(current_context == other_context && current_display == other_display);
    assert(current_draw == other_draw && current_read == other_read);

    swap_error = EGL_BAD_SURFACE;
    SwapBuffers(&gl);
    SwapBuffers(&gl);
    assert(errors == 4 && current_context == other_context);
    swap_error = EGL_SUCCESS;
    SwapBuffers(&gl);
    swap_error = EGL_BAD_SURFACE;
    SwapBuffers(&gl);
    assert(errors == 5);
    swap_error = EGL_SUCCESS;

    reject_context = other_context;
    SwapBuffers(&gl);
    assert(errors == 6 && current_context == sys.context);
    reject_context = EGL_NO_CONTEXT;
    assert(eglMakeCurrent(other_display, other_draw, other_read, other_context));
    unsigned makes = make_calls;
    SwapBuffers(&gl);
    assert(make_calls == makes + 2 && current_context == other_context);
    assert(MakeCurrent(&gl) == VLC_SUCCESS);
    makes = make_calls;
    SwapBuffers(&gl);
    assert(make_calls == makes && current_context == sys.context);
    ReleaseCurrent(&gl);
    SwapBuffers(&gl);
    assert(current_context == EGL_NO_CONTEXT);
    assert(thread_error == EGL_SUCCESS);
    puts("PASS: EGL failures, bounded diagnostics, recovery and context restoration");
}
