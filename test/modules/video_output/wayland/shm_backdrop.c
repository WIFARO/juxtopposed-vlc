/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include "../../../../modules/video_output/wayland/shm_backdrop.h"
static unsigned checks;
static uint8_t *Pixel(picture_t *p,unsigned x,unsigned y)
{ return p->p[0].p_pixels+(size_t)y*p->p[0].i_pitch+(size_t)x*4; }
static picture_t *Picture(unsigned width,unsigned height)
{
    picture_t *p=picture_New(VLC_CODEC_BGRX,width,height,1,1);assert(p);
    memset(p->p[0].p_pixels,0,(size_t)p->p[0].i_pitch*p->p[0].i_lines);return p;
}
static void Checker(picture_t *p,unsigned alpha)
{
    for(unsigned y=0;y<p->format.i_visible_height;++y)for(unsigned x=0;x<p->format.i_visible_width;++x)
    { uint8_t *v=Pixel(p,x,y);v[0]=v[1]=v[2]=((x/8+y/8)&1)?255:0;v[3]=alpha; }
}
static void EqualRGB(picture_t *a,picture_t *b)
{
    assert(a->format.i_visible_width==b->format.i_visible_width&&a->format.i_visible_height==b->format.i_visible_height);
    for(unsigned y=0;y<a->format.i_visible_height;++y)for(unsigned x=0;x<a->format.i_visible_width;++x)
        assert(memcmp(Pixel(a,x,y),Pixel(b,x,y),3)==0);
}
static picture_t *Prepare(struct vlc_shm_backdrop *b,picture_t *src,
    const struct vlc_window_backdrop_region *regions,size_t count)
{
    vout_display_place_t place={.x=0,.y=0,.width=src->format.i_visible_width,.height=src->format.i_visible_height};
    picture_t *out=vlc_shm_backdrop_Prepare(b,src,&src->format,&place,place.width,place.height,regions,count);assert(out);return out;
}
static void Orientations(void)
{
    const uint8_t expected[8][6]={{1,2,3,4,5,6},{3,2,1,6,5,4},{4,5,6,1,2,3},{6,5,4,3,2,1},
        {1,4,2,5,3,6},{3,6,2,5,1,4},{4,1,5,2,6,3},{6,3,5,2,4,1}};
    struct vlc_shm_backdrop *b=vlc_shm_backdrop_New();assert(b);
    picture_t *src=Picture(7,5);
    src->format.i_x_offset=2;src->format.i_y_offset=1;src->format.i_visible_width=3;src->format.i_visible_height=2;
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<3;++x)
    { uint8_t *v=Pixel(src,x+2,y+1);unsigned id=y*3+x+1;v[0]=id*23;v[1]=id*17;v[2]=id*13; }
    size_t bytes=(size_t)src->p[0].i_pitch*src->p[0].i_lines;
    uint8_t *original=malloc(bytes);assert(original);memcpy(original,src->p[0].p_pixels,bytes);
    for(unsigned reset=0;reset<2;++reset)for(unsigned orientation=0;orientation<8;++orientation)
    {
        src->format.orientation=reset?ORIENT_NORMAL:orientation;
        video_format_t crop=src->format;crop.orientation=orientation;
        vout_display_place_t place={.x=1,.y=2,.width=orientation&4?2:3,.height=orientation&4?3:2};
        picture_t *out=vlc_shm_backdrop_Prepare(b,src,&crop,&place,6,7,NULL,0);assert(out);
        picture_t *cached=vlc_shm_backdrop_Prepare(b,src,&crop,&place,6,7,NULL,0);assert(cached==out);picture_Release(cached);
        for(unsigned y=0;y<7;++y)for(unsigned x=0;x<6;++x)
        {
            uint8_t id=x>=1&&x<place.width+1&&y>=2&&y<place.height+2?expected[orientation][(y-2)*place.width+x-1]:0;
            uint8_t *p=Pixel(out,x,y);assert(p[0]==id*23&&p[1]==id*17&&p[2]==id*13);
        }
        assert(!memcmp(original,src->p[0].p_pixels,bytes));picture_Release(out);++checks;
    }
    free(original);picture_Release(src);vlc_shm_backdrop_Delete(b);
}
static void ScaledCropAndZoom(void)
{
    struct vlc_shm_backdrop *b=vlc_shm_backdrop_New();picture_t *src=Picture(10,8);assert(b);
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<10;++x)
    { uint8_t *v=Pixel(src,x,y);v[0]=20*x;v[1]=25*y;v[2]=7*x+11*y; }
    video_format_t crop=src->format;crop.i_x_offset=2;crop.i_y_offset=1;crop.i_visible_width=4;crop.i_visible_height=3;
    const vout_display_place_t places[]={{.x=1,.y=2,.width=4,.height=3},{.x=-3,.y=-2,.width=8,.height=6},
        {.x=-100000,.y=-75000,.width=200000,.height=150000}};
    for(unsigned i=0;i<ARRAY_SIZE(places);++i)
    {
        const vout_display_place_t *place=&places[i];picture_t *out=vlc_shm_backdrop_Prepare(b,src,&crop,place,7,6,NULL,0);assert(out);
        for(unsigned y=0;y<6;++y)for(unsigned x=0;x<7;++x)
        {
            uint8_t *p=Pixel(out,x,y);
            if((int)x<place->x||(int)y<place->y||(int64_t)x>=place->x+(int64_t)place->width||(int64_t)y>=place->y+(int64_t)place->height)
            { assert(p[0]==0&&p[1]==0&&p[2]==0);continue; }
            double u=2+((x+.5-place->x)/place->width)*4-.5,v=1+((y+.5-place->y)/place->height)*3-.5;
            u=fmax(2,fmin(5,u));v=fmax(1,fmin(3,v));
            assert(fabs(p[0]-20*u)<=1&&fabs(p[1]-25*v)<=1&&fabs(p[2]-(7*u+11*v))<=1);
        }
        picture_Release(out);++checks;
    }
    picture_Release(src);vlc_shm_backdrop_Delete(b);
}
static void Gaussian(double radius)
{
    struct vlc_shm_backdrop *b=vlc_shm_backdrop_New();picture_t *src=Picture(512,384);assert(b);
    for(unsigned y=0;y<384;++y)for(unsigned x=256;x<512;++x)memset(Pixel(src,x,y),255,3);
    struct vlc_window_backdrop_region r={16,16,480,352,radius,14};picture_t *out=Prepare(b,src,&r,1);
    double mass=0,mean=0,second=0;
    for(unsigned x=64;x<448;++x)
    { int weight=Pixel(out,x,192)[0]-Pixel(out,x-1,192)[0];assert(weight>=-1);mass+=weight;mean+=(double)x*weight;second+=(double)x*x*weight; }
    assert(mass>250);mean/=mass;double sigma=sqrt(second/mass-mean*mean);
    fprintf(stderr,"Gaussian %.1f: sigma %.3f, center %.3f\n",radius,sigma,mean);
    assert(fabs(sigma-radius)<radius*.12&&fabs(mean-256)<2);
    assert(Pixel(src,255,192)[0]==0&&Pixel(src,256,192)[0]==255);
    picture_Release(out);picture_Release(src);vlc_shm_backdrop_Delete(b);++checks;
}
static void Regions(unsigned alpha,struct vlc_window_backdrop_region r,unsigned width,unsigned height)
{
    struct vlc_shm_backdrop *b=vlc_shm_backdrop_New();picture_t *src=Picture(width,height);assert(b);Checker(src,alpha);
    picture_t *plain=Prepare(b,src,NULL,0),*out=Prepare(b,src,&r,1);unsigned changed=0,outside=0;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
    {
        uint8_t *a=Pixel(out,x,y),*z=Pixel(plain,x,y);
        if(x+.5<r.x-.5||x+.5>r.x+r.width+.5||y+.5<r.y-.5||y+.5>r.y+r.height+.5)
        { assert(!memcmp(a,z,4));++outside; }
        else if(abs(a[0]-z[0])>10)++changed;
    }
    assert(changed>1000&&outside>1000);
    assert(Pixel(out,0,0)[3]==255&&Pixel(out,width-1,height-1)[3]==255);
    if(r.x==64&&r.y==32)
    {
        assert(memcmp(Pixel(out,64,32),Pixel(plain,64,32),4)==0);
        assert(abs(Pixel(out,256,192)[0]-128)<4);
    }
    picture_t *cleared=Prepare(b,src,NULL,0);EqualRGB(cleared,plain);EqualRGB(src,plain);
    picture_Release(cleared);picture_Release(plain);picture_Release(out);picture_Release(src);vlc_shm_backdrop_Delete(b);++checks;
}
static void CacheAndLifetime(void)
{
    struct vlc_shm_backdrop *b=vlc_shm_backdrop_New();picture_t *src=Picture(512,384);Checker(src,0);
    struct vlc_window_backdrop_region r={64,32,384,320,25,14};
    picture_t *first=Prepare(b,src,&r,1),*same=Prepare(b,src,&r,1);assert(same==first);picture_Release(same);
    src->date++;same=Prepare(b,src,&r,1);assert(same==first);picture_Release(same);
    Pixel(src,0,0)[3]=17;same=Prepare(b,src,&r,1);assert(same==first);picture_Release(same);
    uint8_t old[4];memcpy(old,Pixel(first,0,0),4);Pixel(src,0,0)[0]=77;
    same=Prepare(b,src,&r,1);assert(same!=first&&Pixel(same,0,0)[0]==77&&!memcmp(old,Pixel(first,0,0),4));
    picture_Release(same);r.x=120;same=Prepare(b,src,&r,1);assert(same!=first);picture_Release(same);
    picture_t *cleared=Prepare(b,src,NULL,0);EqualRGB(cleared,src);assert(abs(Pixel(first,256,192)[0]-128)<4);
    picture_Release(src);vlc_shm_backdrop_Delete(b);
    assert(abs(Pixel(first,256,192)[0]-128)<4&&Pixel(cleared,0,0)[0]==77);
    picture_Release(first);picture_Release(cleared);++checks;
}
static void Overlap(void)
{
    struct vlc_shm_backdrop *b=vlc_shm_backdrop_New();picture_t *src=Picture(512,384);Checker(src,17);
    struct vlc_window_backdrop_region r[]={{100,64,300,250,25,40},{160,100,300,250,50,14}};
    picture_t *alone=Prepare(b,src,r+1,1),*both=Prepare(b,src,r,2);
    for(unsigned y=130;y<300;++y)for(unsigned x=200;x<400;++x)assert(!memcmp(Pixel(alone,x,y),Pixel(both,x,y),3));
    picture_Release(alone);picture_Release(both);picture_Release(src);vlc_shm_backdrop_Delete(b);++checks;
}
static void Invalid(void)
{
    struct vlc_shm_backdrop *b=vlc_shm_backdrop_New();picture_t *src=Picture(32,24);vout_display_place_t place={.width=32,.height=24};
    assert(!vlc_shm_backdrop_Prepare(b,src,&src->format,&place,0,24,NULL,0));
    assert(!vlc_shm_backdrop_Prepare(b,src,&src->format,&place,UINT_MAX,24,NULL,0));
    assert(!vlc_shm_backdrop_Prepare(b,src,&src->format,&place,32,24,NULL,1));
    struct vlc_window_backdrop_region r={0,0,32,24,25,0};
    assert(!vlc_shm_backdrop_Prepare(b,src,&src->format,&place,32,24,&r,SIZE_MAX));
    r.x=NAN;assert(!vlc_shm_backdrop_Prepare(b,src,&src->format,&place,32,24,&r,1));
    r.x=0;r.blur_radius=INFINITY;assert(!vlc_shm_backdrop_Prepare(b,src,&src->format,&place,32,24,&r,1));
    video_format_t crop=src->format;crop.i_x_offset=1;assert(!vlc_shm_backdrop_Prepare(b,src,&crop,&place,32,24,NULL,0));
    picture_t *out=Prepare(b,src,NULL,0);picture_Release(out);picture_Release(src);vlc_shm_backdrop_Delete(b);++checks;
}
static void DecoderOwnedMemory(void)
{
    uint8_t pixels[24];
    for (unsigned i=0;i<sizeof(pixels);++i) pixels[i]=i;
    video_format_t format;video_format_Init(&format,VLC_CODEC_BGRX);video_format_Setup(&format,VLC_CODEC_BGRX,3,2,3,2,1,1);
    picture_resource_t resource={0};
    resource.p[0].p_pixels=pixels;resource.p[0].i_pitch=12;resource.p[0].i_lines=2;
    picture_t *src=picture_NewFromResource(&format,&resource);assert(src&&src->p_sys==NULL);
    struct vlc_shm_backdrop *b=vlc_shm_backdrop_New();assert(b);
    picture_t *out=Prepare(b,src,NULL,0);EqualRGB(out,src);
    assert(out->p_sys&&((picture_buffer_t *)out->p_sys)->fd>=0);
    int fd=((picture_buffer_t *)out->p_sys)->fd;
    picture_Release(src);vlc_shm_backdrop_Delete(b);
    assert(Pixel(out,2,1)[0]==20&&Pixel(out,2,1)[3]==255&&fcntl(fd,F_GETFD)>=0);
    for (unsigned i=0;i<sizeof(pixels);++i) assert(pixels[i]==i);
    picture_Release(out);assert(fcntl(fd,F_GETFD)==-1&&errno==EBADF);video_format_Clean(&format);++checks;
}
int main(void)
{
    Orientations();ScaledCropAndZoom();Gaussian(25);Gaussian(37.5);Gaussian(50);
    for(unsigned i=0;i<3;++i)Regions(i==0?0:i==1?17:255,(struct vlc_window_backdrop_region){64,32,384,320,25,14},512,384);
    Regions(0,(struct vlc_window_backdrop_region){64.25,32.5,383.5,319.25,25,14},513,383);
    Regions(0,(struct vlc_window_backdrop_region){-50,-20,200,200,25,14},512,384);
    CacheAndLifetime();Overlap();Invalid();DecoderOwnedMemory();
    fprintf(stderr,"%u software video backdrop scenarios passed\n",checks);return 0;
}
