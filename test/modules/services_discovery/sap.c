// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"

#include <net/if.h>

struct capture
{
    vlc_mutex_t lock;
    vlc_cond_t condition;
    bool finished;
    unsigned scoped[2];
    unsigned invalid;
};

static void capture_log(void *opaque, int level, const libvlc_log_t *context,
                        const char *format, va_list args)
{
    (void)level;
    (void)context;
    char text[2048];
    vsnprintf(text, sizeof(text), format, args);
    const char prefix[] = "net: opening ";
    if (strncmp(text, prefix, strlen(prefix)))
        return;
    char *address = text + strlen(prefix);
    char *suffix = strstr(address, " datagram port ");
    if (suffix == NULL)
        return;
    *suffix = '\0';

    struct capture *capture = opaque;
    vlc_mutex_lock(&capture->lock);
    if (!strncmp(address, "ff01::", 6) || !strncmp(address, "ff02::", 6))
    {
        /* Both local multicast scopes need an actual interface zone. */
        char *zone = strchr(address, '%');
        if (zone == NULL || if_nametoindex(zone + 1) == 0)
            ++capture->invalid;
        else
            ++capture->scoped[address[3] - '1'];
    }
    /* The configured address is opened after all default SAP groups. */
    if (!strcmp(address, "127.0.0.1"))
    {
        capture->finished = true;
        vlc_cond_signal(&capture->condition);
    }
    vlc_mutex_unlock(&capture->lock);
}

int main(void)
{
    test_init();
    const char *args[] = {
        "--ignore-config", "--no-media-library", "--no-plugins-cache",
        "--sap-addr=127.0.0.1",
    };
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance != NULL);
    struct capture capture = {0};
    vlc_mutex_init(&capture.lock);
    vlc_cond_init(&capture.condition);
    libvlc_log_set(instance, capture_log, &capture);

    libvlc_media_discoverer_t *discovery =
        libvlc_media_discoverer_new(instance, "sap", NULL, NULL);
    assert(discovery != NULL);
    assert(libvlc_media_discoverer_start(discovery) == 0);

    vlc_mutex_lock(&capture.lock);
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
    while (!capture.finished)
        assert(vlc_cond_timedwait(&capture.condition, &capture.lock, deadline) == 0);
    vlc_mutex_unlock(&capture.lock);
    libvlc_media_discoverer_destroy(discovery);
    libvlc_log_unset(instance);
    libvlc_release(instance);

    test_log("SAP multicast addresses: %u invalid, %u interface-local, %u link-local\n",
             capture.invalid, capture.scoped[0], capture.scoped[1]);
    assert(capture.invalid == 0);
    assert(capture.scoped[0] > 0);
    assert(capture.scoped[0] == capture.scoped[1]);
    return 0;
}
