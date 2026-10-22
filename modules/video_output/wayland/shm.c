/**
 * @file shm.c
 * @brief Wayland shared memory video output module for VLC media player
 */
/*****************************************************************************
 * Copyright © 2014, 2017 Rémi Denis-Courmont
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>
#include "shm_backdrop.h"
#include "registry.h"

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_vout_display.h>

#define MAX_PICTURES 4

typedef struct vout_display_sys_t
{
    vlc_window_t *embed; /* VLC window */
    struct wl_event_queue *eventq;
    struct wl_shm *shm;
    struct vlc_shm_backdrop *backdrop;
    picture_t *attached_picture;
    bool dirty;
    bool frame_failed;

    size_t active_buffers;
} vout_display_sys_t;

struct buffer_data
{
    picture_t *picture;
    size_t *counter;
};

static void buffer_release_cb(void *data, struct wl_buffer *buffer)
{
    struct buffer_data *d = data;

    picture_Release(d->picture);
    (*(d->counter))--;
    free(d);
    wl_buffer_destroy(buffer);
}

static const struct wl_buffer_listener buffer_cbs =
{
    buffer_release_cb,
};

static void Prepare(vout_display_t *vd, picture_t *pic,
                    const struct vlc_render_subpicture *subpic,
                    vlc_tick_t date)
{
    VLC_UNUSED(date);
    vout_display_sys_t *sys = vd->sys;
    struct wl_display *display = sys->embed->display.wl;
    struct wl_surface *surface = sys->embed->handle.wl;
    sys->dirty = false;
    if (sys->active_buffers >= MAX_PICTURES)
        return;

    vlc_window_backdrops_t *snapshot = vlc_window_HoldBackdrops(sys->embed);
    size_t count;
    const struct vlc_window_backdrop_region *regions =
        vlc_window_GetBackdropRegions(snapshot, &count);
    picture_t *rendered = vlc_shm_backdrop_Prepare(sys->backdrop, pic, vd->source,
        vd->place, vd->cfg->display.width, vd->cfg->display.height, regions, count);
    vlc_window_ReleaseBackdrops(snapshot);
    if (rendered == NULL)
    {
        if (!sys->frame_failed)
        {
            msg_Warn(vd, "Cannot prepare a software video frame");
            video_format_Print(VLC_OBJECT(vd), "picture storage", &pic->format);
            video_format_Print(VLC_OBJECT(vd), "video source", vd->source);
        }
        sys->frame_failed = true;
        return;
    }
    sys->frame_failed = false;
    if (rendered == sys->attached_picture)
    {
        picture_Release(rendered);
        return;
    }

    const picture_buffer_t *picbuf = rendered->p_sys;
    if (picbuf->fd == -1 || picbuf->offset < 0
        || (uint64_t)picbuf->offset + picbuf->size > INT32_MAX)
    {
        picture_Release(rendered);
        return;
    }
    struct buffer_data *d = malloc(sizeof(*d));
    if (d == NULL)
    {
        picture_Release(rendered);
        return;
    }
    d->picture = rendered;
    d->counter = &sys->active_buffers;
    struct wl_shm_pool *pool = wl_shm_create_pool(sys->shm, picbuf->fd,
                                                picbuf->offset + picbuf->size);
    if (pool == NULL)
    {
        picture_Release(rendered);
        free(d);
        return;
    }
    struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, picbuf->offset,
        rendered->format.i_visible_width, rendered->format.i_visible_height,
        rendered->p[0].i_pitch, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    if (buf == NULL)
    {
        picture_Release(rendered);
        free(d);
        return;
    }

    wl_buffer_add_listener(buf, &buffer_cbs, d);
    wl_surface_attach(surface, buf, 0, 0);
    if (sys->attached_picture)
        picture_Release(sys->attached_picture);
    sys->attached_picture = picture_Hold(rendered);
    sys->dirty = true;
    wl_surface_damage(surface, 0, 0, vd->cfg->display.width, vd->cfg->display.height);
    wl_display_flush(display);

    sys->active_buffers++;

    (void) subpic;
}

static void Display(vout_display_t *vd, picture_t *pic)
{
    vout_display_sys_t *sys = vd->sys;
    struct wl_display *display = sys->embed->display.wl;
    struct wl_surface *surface = sys->embed->handle.wl;

    if (sys->dirty)
        wl_surface_commit(surface);
    wl_display_roundtrip_queue(display, sys->eventq);

    (void) pic;
}

static int SetDisplaySize(vout_display_t *vd, unsigned width, unsigned height)
{
    VLC_UNUSED(width); VLC_UNUSED(height);
    VLC_UNUSED(vd);
    return VLC_SUCCESS;
}

static int PlacementChanged(vout_display_t *vd, const vout_display_place_t *place)
{
    VLC_UNUSED(place);
    VLC_UNUSED(vd);
    return VLC_SUCCESS;
}

static int AspectChanged(vout_display_t *vd, const video_format_t *source)
{
    VLC_UNUSED(source);
    return PlacementChanged(vd, NULL);
}

static void shm_format_cb(void *data, struct wl_shm *shm, uint32_t format)
{
    vout_display_t *vd = data;
    char str[4];

    memcpy(str, &format, sizeof (str));

    if (format >= 0x20202020)
        msg_Dbg(vd, "format %.4s (0x%08"PRIx32")", str, format);
    else
        msg_Dbg(vd, "format %4"PRIu32" (0x%08"PRIx32")", format, format);
    (void) shm;
}

static const struct wl_shm_listener shm_cbs =
{
    shm_format_cb,
};

static void Close(vout_display_t *vd)
{
    vout_display_sys_t *sys = vd->sys;
    struct wl_display *display = sys->embed->display.wl;
    struct wl_surface *surface = sys->embed->handle.wl;

    wl_surface_attach(surface, NULL, 0, 0);
    wl_surface_commit(surface);

    /* Wait until all picture buffers are released by the server */
    while (sys->active_buffers > 0) {
        msg_Dbg(vd, "%zu buffer(s) still active", sys->active_buffers);
        wl_display_roundtrip_queue(display, sys->eventq);
    }
    msg_Dbg(vd, "no active buffers left");

    vlc_shm_backdrop_Delete(sys->backdrop);
    if (sys->attached_picture)
        picture_Release(sys->attached_picture);
    wl_shm_destroy(sys->shm);
    wl_display_flush(display);
    wl_event_queue_destroy(sys->eventq);
    free(sys);
}

static const struct vlc_display_operations ops = {
    .close = Close,
    .prepare = Prepare,
    .display = Display,
    .set_display_size = SetDisplaySize,
    .video_place_changed = PlacementChanged,
    .set_source_aspect = AspectChanged,
    .set_source_crop = AspectChanged,
};

static int Open(vout_display_t *vd,
                video_format_t *fmtp, vlc_video_context *context)
{
    if (vd->cfg->window->type != VLC_WINDOW_TYPE_WAYLAND)
        return VLC_EGENERIC;

    vout_display_sys_t *sys = malloc(sizeof (*sys));
    if (unlikely(sys == NULL))
        return VLC_ENOMEM;
    struct vlc_wl_registry *registry = NULL;

    vd->sys = sys;
    sys->embed = NULL;
    sys->eventq = NULL;
    sys->shm = NULL;
    sys->active_buffers = 0;
    sys->attached_picture = NULL;
    sys->dirty = false;
    sys->frame_failed = false;
    sys->backdrop = vlc_shm_backdrop_New();
    if (sys->backdrop == NULL)
        goto error;

    /* Get window */
    sys->embed = vd->cfg->window;
    assert(sys->embed != NULL);

    struct wl_display *display = sys->embed->display.wl;

    sys->eventq = wl_display_create_queue(display);
    if (sys->eventq == NULL)
        goto error;

    registry = vlc_wl_registry_get(display, sys->eventq);
    if (registry == NULL)
        goto error;

    sys->shm = vlc_wl_shm_get(registry);
    if (sys->shm == NULL)
        goto error;

    wl_shm_add_listener(sys->shm, &shm_cbs, vd);
    wl_display_roundtrip_queue(display, sys->eventq);

    /* The window provider owns buffer scale and any fractional viewport.
     * Attach a full physical-pixel window buffer; a second viewport on the
     * same surface would be a protocol error. Compose orientation and crop
     * into display-owned memory instead of changing decoder pictures. */
    struct wl_surface *surface = sys->embed->handle.wl;
    if (wl_surface_get_version(surface) >= 2)
        wl_surface_set_buffer_transform(surface, WL_OUTPUT_TRANSFORM_NORMAL);
    fmtp->i_chroma = VLC_CODEC_BGRX;

    vd->ops = &ops;

    vlc_wl_registry_destroy(registry);
    (void) context;
    return VLC_SUCCESS;

error:
    vlc_shm_backdrop_Delete(sys->backdrop);
    if (sys->shm != NULL)
        wl_shm_destroy(sys->shm);

    if (registry != NULL)
        vlc_wl_registry_destroy(registry);

    if (sys->eventq != NULL)
        wl_event_queue_destroy(sys->eventq);
    free(sys);
    return VLC_EGENERIC;
}

vlc_module_begin()
    set_shortname(N_("WL SHM"))
    set_description(N_("Wayland shared memory video output"))
    set_subcategory(SUBCAT_VIDEO_VOUT)
    set_callback_display(Open, 170)
    add_shortcut("wl")
vlc_module_end()
