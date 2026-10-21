// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_NETWORK_CONFIGURED_PROXY_H
#define VLC_NETWORK_CONFIGURED_PROXY_H

#include <stdlib.h>
#include <assert.h>
#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_url.h>

/* A configured proxy takes precedence over platform discovery. Keep that
 * distinction even if its URL cannot be composed; do not try another proxy. */
static inline char *vlc_proxy_GetConfigured(vlc_object_t *obj, bool *configured)
{
    char *proxy = obj ? var_InheritString(obj, "http-proxy")
                      : config_GetPsz("http-proxy");
    *configured = proxy != NULL;
    if (proxy == NULL)
        return NULL;

    char *password = obj ? var_InheritString(obj, "http-proxy-pwd")
                         : config_GetPsz("http-proxy-pwd");
    if (password == NULL)
        return proxy;

    vlc_url_t url;
    if (vlc_UrlParse(&url, proxy) < 0) {
        vlc_UrlClean(&url);
        free(proxy);
        free(password);
        return NULL;
    }
    if (url.psz_password == NULL)
        url.psz_password = password;
    char *result = vlc_uri_compose(&url);
    vlc_UrlClean(&url);
    free(password);
    free(proxy);
    return result;
}

#endif
