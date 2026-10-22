/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <vlc/vlc.h>
#include <vlc_common.h>
#include <vlc_opengl.h>
#include "../../../../lib/libvlc_internal.h"
#include "../../../../modules/video_output/opengl/gl_api.h"
#include "../../../../modules/video_output/opengl/backdrop.h"

#ifndef VLC_TEST_OPENGL_API
# define VLC_TEST_OPENGL_API VLC_OPENGL
#endif
#ifndef GL_READ_BUFFER
# define GL_READ_BUFFER 0x0C02
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
# define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER_BINDING
# define GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#endif
#ifndef GL_RGBA16F
# define GL_RGBA16F 0x881A
#endif
#ifndef GL_RGBA32F
# define GL_RGBA32F 0x8814
#endif
const char vlc_module_name[] = "test_gl_backdrop";
static void *GetSymbol(vlc_gl_t *gl, const char *name)
{ (void)gl; return eglGetProcAddress(name); }
static const struct vlc_gl_operations ops = {.get_proc_address = GetSymbol};
static unsigned checks;
static unsigned char before[640*480*4], after[sizeof(before)], isolated[sizeof(before)];
static void (APIENTRY *Scissor)(GLint, GLint, GLsizei, GLsizei);
static GLboolean (APIENTRY *IsEnabled)(GLenum);
static void (APIENTRY *ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
static void (APIENTRY *ReadBuffer)(GLenum);
static struct vlc_gl_api api;
static const opengl_vtable_t *v = &api.vt;

static void Integer(GLenum key, GLint expected)
{ GLint actual; v->GetIntegerv(key, &actual); assert(actual == expected); }
static void Read(unsigned char *pixels, int width, int height)
{
    v->ReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(v->GetError() == GL_NO_ERROR);
}
static void Pattern(int width, int height, int phase, bool step)
{
    v->BindFramebuffer(GL_FRAMEBUFFER, 0);
    ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    v->Disable(GL_DITHER); v->Enable(GL_SCISSOR_TEST);
    for (int y = 0; y < height; y += 8)
        for (int x = 0; x < width; x += 8)
        {
            float value = step ? x >= width/2 : ((x/8+y/8+phase)&1);
            Scissor(x, y, __MIN(8,width-x), __MIN(8,height-y));
            v->ClearColor(value, value, value, 0.f);
            v->Clear(GL_COLOR_BUFFER_BIT);
        }
    v->Disable(GL_SCISSOR_TEST);
}
static void Gaussian(struct vlc_gl_backdrop *b, double radius)
{
    const int width = 512, height = 384;
    Pattern(width, height, 0, true);
    struct vlc_window_backdrop_region r = {16,16,480,352,radius,14};
    vlc_gl_backdrop_Draw(b, width, height, &r, 1);
    Read(after, width, height);
    double mass=0, mean=0, second=0;
    for (int x=64; x<448; ++x)
    {
        int weight = after[(192*width+x)*4] - after[(192*width+x-1)*4];
        assert(weight >= -1);
        mass += weight; mean += x*weight; second += x*x*weight;
    }
    assert(mass > 250);
    mean /= mass;
    double measured = sqrt(second/mass-mean*mean);
    fprintf(stderr, "Gaussian %.1f: sigma %.3f, center %.3f\n", radius, measured, mean);
    assert(fabs(measured-radius) < radius*.12 && fabs(mean-256)<2);
    ++checks;
}
static void Clipped(struct vlc_gl_backdrop *b, int width, int height,
                    struct vlc_window_backdrop_region r)
{
    Pattern(width, height, 0, false); Read(before,width,height);
    vlc_gl_backdrop_Draw(b,width,height,&r,1); Read(after,width,height);
    size_t changed=0, outside=0;
    for (int y=0; y<height; ++y)
        for (int x=0; x<width; ++x)
        {
            const unsigned char *a=after+(y*width+x)*4, *z=before+(y*width+x)*4;
            double px=x+.5, py=height-y-.5;
            bool out=px<r.x-.5 || px>r.x+r.width+.5 || py<r.y-.5 || py>r.y+r.height+.5;
            if (out) { assert(memcmp(a,z,4)==0); ++outside; }
            else if (abs(a[0]-z[0])>10) ++changed;
        }
    assert(changed>1000 && outside>1000);
    ++checks;
}

static void EdgeGaussian(struct vlc_gl_backdrop *b, double sigma, bool clipped)
{
    const int width = 513, height = 385, radius = ceil(3 * sigma);
    Pattern(width, height, 0, false); Read(before, width, height);
    struct vlc_window_backdrop_region r = {
        clipped ? -20.5 : 12.25, clipped ? -10.25 : 20.75,
        width - 35.5, height - 50.25, sigma, 14,
    };
    vlc_gl_backdrop_Draw(b, width, height, &r, 1); Read(after, width, height);
    double weights[2 * radius + 1], total = 0;
    for (int i = -radius; i <= radius; ++i)
    { weights[i + radius] = exp(-i * i / (2 * sigma * sigma)); total += weights[i + radius]; }
    for (int i = 0; i <= 2 * radius; ++i) weights[i] /= total;
    double *horizontal = malloc(width * height * sizeof(*horizontal)); assert(horizontal);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            double value = 0;
            for (int i = -radius; i <= radius; ++i)
                value += before[(y * width + VLC_CLIP(x + i, 0, width - 1)) * 4] * weights[i + radius];
            horizontal[y * width + x] = value;
        }
    double error = 0, sum = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            double value = 0;
            for (int i = -radius; i <= radius; ++i)
                value += horizontal[VLC_CLIP(y + i, 0, height - 1) * width + x] * weights[i + radius];
            const double px = fabs(x + .5 - r.x - r.width / 2.) - r.width / 2. + r.corner_radius;
            const double py = fabs(height - y - .5 - r.y - r.height / 2.) - r.height / 2. + r.corner_radius;
            const double distance = hypot(fmax(px, 0), fmax(py, 0)) + fmin(fmax(px, py), 0) - r.corner_radius;
            const double t = VLC_CLIP(distance + .5, 0, 1), alpha = 1 - t * t * (3 - 2 * t);
            const unsigned char *original = before + (y * width + x) * 4;
            const unsigned char *actual = after + (y * width + x) * 4;
            if (alpha == 0) assert(!memcmp(original, actual, 4));
            const double expected = alpha * value + (1 - alpha) * original[0];
            for (int c = 0; c < 3; ++c)
            { double delta = fabs(actual[c] - expected); error = fmax(error, delta); sum += delta; }
        }
    free(horizontal);
    fprintf(stderr, "Edge Gaussian %.1f clipped=%d: max %.3f mean %.3f\n",
            sigma, clipped, error, sum / (width * height * 3));
    assert(error <= 5 && sum / (width * height * 3) <= 1);
    ++checks;
}
static void State(struct vlc_gl_backdrop *b)
{
    Pattern(512,384,0,false); Read(before,512,384);
    GLuint buffer, texture, read_fbo, array;
    v->GenBuffers(1,&buffer); v->GenTextures(1,&texture); v->GenFramebuffers(1,&read_fbo);
    v->GenVertexArrays(1,&array); v->BindVertexArray(array);
    v->BindBuffer(GL_ARRAY_BUFFER,buffer);
    v->VertexAttribPointer(0,3,GL_FLOAT,GL_TRUE,24,(void *)(uintptr_t)4);
    void (APIENTRY *DisableAttribute)(GLuint)=(void *)eglGetProcAddress("glDisableVertexAttribArray");
    DisableAttribute(0);
    v->BlendFunc(GL_ONE_MINUS_DST_COLOR,GL_SRC_COLOR);
    v->ActiveTexture(GL_TEXTURE3); v->BindTexture(GL_TEXTURE_2D,texture);
    v->Viewport(7,11,123,234); Scissor(13,17,19,23);
    v->Enable(GL_SCISSOR_TEST); v->Enable(GL_BLEND); v->Enable(GL_DEPTH_TEST);
    ColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_TRUE);
    bool separate=api.supports_multisample;
    if (separate)
    {
        v->BindBuffer(GL_PIXEL_UNPACK_BUFFER,buffer);
        v->BufferData(GL_PIXEL_UNPACK_BUFFER,16,NULL,GL_STATIC_DRAW);
        if (ReadBuffer) ReadBuffer(GL_NONE);
        v->BindFramebuffer(GL_READ_FRAMEBUFFER,read_fbo);
    }
    assert(v->GetError()==GL_NO_ERROR);
    struct vlc_window_backdrop_region r={64,32,384,320,25,14};
    vlc_gl_backdrop_Draw(b,512,384,&r,1);
    assert(v->GetError()==GL_NO_ERROR);
    Integer(GL_ARRAY_BUFFER_BINDING,buffer); Integer(GL_ACTIVE_TEXTURE,GL_TEXTURE3);
    Integer(GL_BLEND_SRC_RGB,GL_ONE_MINUS_DST_COLOR); Integer(GL_BLEND_DST_RGB,GL_SRC_COLOR);
    void (APIENTRY *GetAttribute)(GLuint,GLenum,GLint*)=(void *)eglGetProcAddress("glGetVertexAttribiv");
    GLint attr; GetAttribute(0,GL_VERTEX_ATTRIB_ARRAY_SIZE,&attr); assert(attr==3);
    GetAttribute(0,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&attr); assert(attr==24);
    GetAttribute(0,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&attr); assert(!attr);
    GetAttribute(0,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&attr); assert(attr==(GLint)buffer);
    if (array) Integer(GL_VERTEX_ARRAY_BINDING,array);
    Integer(GL_TEXTURE_BINDING_2D,texture); Integer(GL_FRAMEBUFFER_BINDING,0);
    GLint box[4]; v->GetIntegerv(GL_VIEWPORT,box);
    assert(box[0]==7&&box[1]==11&&box[2]==123&&box[3]==234);
    v->GetIntegerv(GL_SCISSOR_BOX,box); assert(box[0]==13&&box[1]==17&&box[2]==19&&box[3]==23);
    assert(IsEnabled(GL_SCISSOR_TEST)&&IsEnabled(GL_BLEND)&&IsEnabled(GL_DEPTH_TEST));
    GLboolean mask[4];
    void (APIENTRY *GetBooleanv)(GLenum,GLboolean*)=(void *)eglGetProcAddress("glGetBooleanv");
    GetBooleanv(GL_COLOR_WRITEMASK,mask); assert(!mask[0]&&mask[1]&&!mask[2]&&mask[3]);
    if (separate)
    {
        Integer(GL_READ_FRAMEBUFFER_BINDING,read_fbo);
        Integer(GL_PIXEL_UNPACK_BUFFER_BINDING,buffer);
        v->BindFramebuffer(GL_READ_FRAMEBUFFER,0);
        if (ReadBuffer) { Integer(GL_READ_BUFFER,GL_NONE); ReadBuffer(GL_BACK); }
        v->BindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    }
    v->Disable(GL_BLEND); v->Disable(GL_DEPTH_TEST); v->Disable(GL_SCISSOR_TEST);
    ColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    v->ActiveTexture(GL_TEXTURE0); v->BindBuffer(GL_ARRAY_BUFFER,0);
    v->DeleteTextures(1,&texture); v->DeleteBuffers(1,&buffer); v->DeleteFramebuffers(1,&read_fbo);
    v->BindVertexArray(0); v->DeleteVertexArrays(1,&array);
    Read(after,512,384);
    assert(abs(after[(192*512+256)*4]-128)<4);
    assert(memcmp(before,after,4)==0);
    ++checks;
}

static unsigned missing_function_requests;
static void *MissingSymbol(vlc_gl_t *gl, const char *name)
{
    if (!strcmp(name, "glCopyTexSubImage2D"))
    { ++missing_function_requests; return NULL; }
    return GetSymbol(gl, name);
}

static void WindowBackdrops(vlc_gl_t *gl, struct vlc_gl_backdrop *reference)
{
    const vlc_window_owner_t owner = {0};
    vlc_window_t *window = vlc_window_New(VLC_OBJECT(gl), "dummy", &owner, NULL);
    assert(window);
    /* The core snapshot API must also work for a Cocoa video window. No OS
     * drawable is needed for this EGL test of the shared final render pass. */
    window->type = VLC_WINDOW_TYPE_NSOBJECT;
    struct vlc_gl_backdrop_window state = {0};
    const unsigned width = 513, height = 385;
    Pattern(width, height, 0, false); Read(before, width, height);
    vlc_gl_backdrop_window_Draw(&state, gl, window, width, height);
    Read(after, width, height);
    assert(!state.renderer && !state.failed && !memcmp(before, after, width * height * 4));
    ++checks;

    struct vlc_window_backdrop_region regions[] = {
        {12.25, 305, 140, 100, 25, 14},
        {-20.5, -10.25, 277.5, 234.75, 37.5, 14},
    };
    for (size_t count = 1; count <= ARRAY_SIZE(regions); ++count)
    {
        assert(vlc_window_SetBackdrops(window, regions, count) == VLC_SUCCESS);
        Pattern(width, height, 0, false);
        vlc_gl_backdrop_Draw(reference, width, height, regions, count);
        Read(isolated, width, height);
        Pattern(width, height, 0, false);
        vlc_gl_backdrop_window_Draw(&state, gl, window, width, height);
        Read(after, width, height);
        assert(state.renderer && !state.failed && !memcmp(isolated, after, width * height * 4));
        ++checks;
    }
    assert(vlc_window_SetBackdrops(window, NULL, 0) == VLC_SUCCESS);
    Pattern(width, height, 0, false);
    vlc_gl_backdrop_window_Draw(&state, gl, window, width, height);
    Read(after, width, height);
    assert(!memcmp(before, after, width * height * 4));
    vlc_gl_backdrop_window_Close(&state);
    assert(!state.renderer && !state.failed);
    ++checks;

    assert(vlc_window_SetBackdrops(window, regions, 1) == VLC_SUCCESS);
    const struct vlc_gl_operations missing_ops = {.get_proc_address = MissingSymbol};
    gl->ops = &missing_ops;
    vlc_gl_backdrop_window_Draw(&state, gl, window, width, height);
    assert(state.failed && !state.renderer && missing_function_requests == 1);
    vlc_gl_backdrop_window_Draw(&state, gl, window, width, height);
    Read(after, width, height);
    assert(missing_function_requests == 1 && !memcmp(before, after, width * height * 4));
    gl->ops = &ops;
    vlc_gl_backdrop_window_Close(&state);
    vlc_window_Delete(window);
    ++checks;
}

static void FloatingPoint(struct vlc_gl_backdrop *b, GLint format)
{
    const unsigned width = 513, height = 385;
    GLuint texture, framebuffer;
    v->GenTextures(1, &texture);
    v->BindTexture(GL_TEXTURE_2D, texture);
    v->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    v->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    float *pixels = malloc(width * height * 4 * sizeof(*pixels)); assert(pixels);
    float *result = malloc(width * height * 4 * sizeof(*result)); assert(result);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
        {
            float *p = pixels + (y * width + x) * 4;
            p[0] = ((x / 8 + y / 8) % 2) ? 2 : 4;
            p[1] = .1234130859375f; p[2] = -.25f; p[3] = 1;
        }
    v->TexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, GL_FLOAT, pixels);
    if (v->GetError() != GL_NO_ERROR)
    {
        assert(api.is_gles);
        fprintf(stderr, "Floating-point target unavailable in this GLES context\n");
        goto done;
    }
    v->GenFramebuffers(1, &framebuffer);
    v->BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    v->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (v->CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        assert(api.is_gles);
        v->BindFramebuffer(GL_FRAMEBUFFER, 0);
        v->DeleteFramebuffers(1, &framebuffer);
        fprintf(stderr, "Floating-point target not renderable in this GLES context\n");
        goto done;
    }
    const struct vlc_window_backdrop_region region = {32, 32, 400, 300, 25, 14};
    vlc_gl_backdrop_Draw(b, width, height, &region, 1);
    v->ReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, result);
    assert(v->GetError() == GL_NO_ERROR);
    const float *center = result + ((height / 2) * width + width / 2) * 4;
    fprintf(stderr, "Floating-point 0x%x backdrop center: %.6f %.6f %.6f\n",
            (unsigned)format, center[0], center[1], center[2]);
    const float tolerance = format == GL_RGBA32F ? .0001f : .01f;
    assert(fabsf(center[0] - 3) < tolerance && fabsf(center[1] - pixels[1]) < tolerance
           && fabsf(center[2] - pixels[2]) < tolerance);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
        {
            const double py = height - y - .5;
            if (x < 30 || x > 434 || py < 30 || py > 334)
                assert(!memcmp(pixels + (y * width + x) * 4,
                               result + (y * width + x) * 4, 4 * sizeof(*pixels)));
        }
    v->BindFramebuffer(GL_FRAMEBUFFER, 0);
    v->DeleteFramebuffers(1, &framebuffer);
    ++checks;
done:
    v->DeleteTextures(1, &texture);
    free(pixels); free(result);
}

int main(void)
{
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display==EGL_NO_DISPLAY || !eglInitialize(display,NULL,NULL))
    { assert(!getenv("VLC_TEST_REQUIRE_GL")); return 77; }
    bool es=VLC_TEST_OPENGL_API==VLC_OPENGL_ES2;
    assert(eglBindAPI(es?EGL_OPENGL_ES_API:EGL_OPENGL_API));
    const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,
        es?EGL_OPENGL_ES2_BIT:EGL_OPENGL_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config; EGLint count;
    assert(eglChooseConfig(display,attrs,&config,1,&count)&&count);
    const EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,es?2:3,EGL_NONE};
    const EGLint core[]={EGL_CONTEXT_MAJOR_VERSION_KHR,3,EGL_CONTEXT_MINOR_VERSION_KHR,3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,EGL_NONE};
    const EGLint legacy[]={EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,
        !es&&getenv("VLC_TEST_GL_LEGACY")?legacy:!es&&getenv("VLC_TEST_GL_CORE")?core:ca);
    assert(context!=EGL_NO_CONTEXT);
    const EGLint sa[]={EGL_WIDTH,640,EGL_HEIGHT,480,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,sa); assert(surface!=EGL_NO_SURFACE);
    assert(eglMakeCurrent(display,surface,surface,context));
    const char *args[]={"--ignore-config","--no-plugins-cache","--disable-screensaver=0"};
    libvlc_instance_t *vlc=libvlc_new(ARRAY_SIZE(args),args); assert(vlc);
    vlc_gl_t *gl=vlc_object_create(&vlc->p_libvlc_int->obj,sizeof(*gl)); assert(gl);
    gl->api_type=VLC_TEST_OPENGL_API; gl->ops=&ops;
    assert(vlc_gl_api_Init(&api,gl)==VLC_SUCCESS);
    Scissor=(void *)eglGetProcAddress("glScissor"); IsEnabled=(void *)eglGetProcAddress("glIsEnabled");
    ColorMask=(void *)eglGetProcAddress("glColorMask"); ReadBuffer=(void *)eglGetProcAddress("glReadBuffer");
    assert(Scissor&&IsEnabled&&ColorMask);
    fprintf(stderr,"OpenGL version: %s\n",v->GetString(GL_VERSION));
    struct vlc_gl_backdrop *b=vlc_gl_backdrop_New(gl); assert(b);
    Gaussian(b,25); Gaussian(b,37.5); Gaussian(b,50);
    EdgeGaussian(b,25,false); EdgeGaussian(b,37.5,false); EdgeGaussian(b,50,true);
    WindowBackdrops(gl,b);
    FloatingPoint(b,GL_RGBA16F); FloatingPoint(b,GL_RGBA32F);
    Clipped(b,512,384,(struct vlc_window_backdrop_region){64,32,384,320,25,14});
    Clipped(b,513,383,(struct vlc_window_backdrop_region){64.25,32.5,383.5,319.25,25,14});
    Clipped(b,512,384,(struct vlc_window_backdrop_region){-50,-20,200,200,25,14});
    State(b);
    Pattern(512,384,0,false); Read(before,512,384);
    struct vlc_window_backdrop_region pair[]={{100,64,300,250,25,40},{160,100,300,250,50,14}};
    vlc_gl_backdrop_Draw(b,512,384,pair+1,1); Read(isolated,512,384);
    Pattern(512,384,0,false); vlc_gl_backdrop_Draw(b,512,384,pair,2); Read(after,512,384);
    for (int y=60;y<250;++y) for(int x=200;x<400;++x)
        assert(memcmp(after+(y*512+x)*4,isolated+(y*512+x)*4,4)==0);
    ++checks;
    Pattern(512,384,1,false); Read(before,512,384);
    vlc_gl_backdrop_Draw(b,512,384,NULL,0); Read(after,512,384);
    assert(memcmp(before,after,512*384*4)==0); ++checks;
    struct vlc_window_backdrop_region outside={-100,-100,50,50,25,0};
    vlc_gl_backdrop_Draw(b,512,384,&outside,1); Read(after,512,384);
    assert(memcmp(before,after,512*384*4)==0); ++checks;
    vlc_gl_backdrop_Delete(b); v->Finish(); assert(v->GetError()==GL_NO_ERROR);
    vlc_object_delete(gl); libvlc_release(vlc);
    assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
    assert(eglDestroySurface(display,surface)); assert(eglDestroyContext(display,context));
    assert(eglTerminate(display));
    assert(eglReleaseThread());
    fprintf(stderr,"%u native video backdrop pixel/state checks passed\n",checks);
    return 0;
}
