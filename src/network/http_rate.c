// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc_common.h>
#include <vlc_network.h>
#include <vlc_interrupt.h>
#include <vlc_strings.h>
#include <errno.h>

static bool MusicBrainz(const char *authority)
{
    if (authority == NULL) return false;
    const char *port = strchr(authority, ':');
    size_t length = port ? (size_t)(port - authority) : strlen(authority);
    if (port) {
        if (!port[1]) return false;
        for (const char *p = port + 1; *p; ++p)
            if (*p < '0' || *p > '9') return false;
    }
    if (length && authority[length - 1] == '.') --length;
    const char mirror[] = "mb.videolan.org", domain[] = "musicbrainz.org";
    const size_t suffix = sizeof(domain) - 1;
    return (length == sizeof(mirror) - 1 && !vlc_ascii_strncasecmp(authority, mirror, length))
        || (length >= suffix && !vlc_ascii_strncasecmp(authority + length - suffix, domain, suffix)
            && (length == suffix || authority[length - suffix - 1] == '.'));
}

int vlc_http_request_wait(const char *authority)
{
    if (!MusicBrainz(authority)) return 0;
    static vlc_mutex_t lock = VLC_STATIC_MUTEX;
    static vlc_tick_t next_request;
    for (;;) {
        vlc_mutex_lock(&lock);
        if (vlc_killed()) {
            vlc_mutex_unlock(&lock);
            return EINTR;
        }
        const vlc_tick_t now = vlc_tick_now();
        const vlc_tick_t deadline = next_request;
        if (now >= deadline) {
            next_request = now + VLC_TICK_FROM_SEC(1);
            vlc_mutex_unlock(&lock);
            return 0;
        }
        vlc_mutex_unlock(&lock);
        // Reserve only the current slot, so cancelled requests do not delay later ones.
        if (vlc_mwait_i11e(deadline)) return EINTR;
    }
}
