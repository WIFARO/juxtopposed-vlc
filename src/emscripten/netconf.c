/*****************************************************************************
 * vlc_getProxyUrl for emscripten
 *****************************************************************************
 * Copyright (C) 2021 - VideoLabs, VideoLAN and VLC Authors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
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
# include "config.h"
#endif
#include <vlc_common.h>
#include <vlc_network.h>
#include "../network/proxy.h"

static char *proxy_find(vlc_object_t *obj, const char *url)
{
    bool configured;
    char *proxy = vlc_proxy_GetConfigured(obj, &configured);
    if (configured)
        return proxy;

    VLC_UNUSED(url);
    return NULL;
}

char *vlc_getProxyUrl(const char *url)
{
    return proxy_find(NULL, url);
}

char *vlc_getProxyUrlForObject(vlc_object_t *obj, const char *url)
{
    assert(obj != NULL);
    return proxy_find(obj, url);
}
