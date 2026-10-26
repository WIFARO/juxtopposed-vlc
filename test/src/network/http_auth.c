// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlc_common.h>
#include <vlc_http.h>
#include <vlc_strings.h>
#include <vlc_hash.h>
#include <vlc/vlc.h>
#include "../../../lib/libvlc_internal.h"

static void Digest(const char *text, char output[VLC_HASH_MD5_DIGEST_HEX_SIZE])
{
    vlc_hash_md5_t hash;
    vlc_hash_md5_Init(&hash);
    vlc_hash_md5_Update(&hash, text, strlen(text));
    vlc_hash_FinishHex(&hash, output);
}

/* These controlled fixtures have quoted parameters without escaped quotes. */
static void Parameter(const char *header, const char *name, char *out, size_t size)
{
    char key[32];
    assert(snprintf(key, sizeof(key), "%s=\"", name) < (int)sizeof(key));
    const char *start = strstr(header, key);
    assert(start != NULL);
    start += strlen(key);
    const char *end = strchr(start, '"');
    assert(end != NULL && (size_t)(end - start) < size);
    memcpy(out, start, end - start);
    out[end - start] = '\0';
}

static void Check(vlc_object_t *object, bool session, bool qop)
{
    vlc_http_auth_t auth;
    vlc_http_auth_Init(&auth);
    char challenge[256];
    snprintf(challenge, sizeof(challenge),
             "Digest realm=\"Owned realm\", nonce=\"owned-nonce\", algorithm=%s%s",
             session ? "MD5-sess" : "MD5", qop ? ", qop=\"auth\"" : "");
    vlc_http_auth_ParseWwwAuthenticateHeader(object, &auth, challenge);
    for (unsigned request = 1; request <= 3; ++request)
    {
        char *header = vlc_http_auth_FormatAuthorizationHeader(
            object, &auth, "GET", "/media.wav", "owned", "fixture");
        assert(header != NULL && strncmp(header, "Digest ", 7) == 0);
        char cnonce[65], count[16], response[33];
        Parameter(header, "cnonce", cnonce, sizeof(cnonce));
        Parameter(header, "nc", count, sizeof(count));
        Parameter(header, "response", response, sizeof(response));
        assert(strtoul(count, NULL, 16) == request);
        char ha1[VLC_HASH_MD5_DIGEST_HEX_SIZE];
        char ha2[VLC_HASH_MD5_DIGEST_HEX_SIZE];
        char expected[VLC_HASH_MD5_DIGEST_HEX_SIZE];
        char text[512];
        Digest("owned:Owned realm:fixture", ha1);
        if (session)
        {
            snprintf(text, sizeof(text), "%s:owned-nonce:%s", ha1, cnonce);
            Digest(text, ha1);
        }
        Digest("GET:/media.wav", ha2);
        if (qop)
            snprintf(text, sizeof(text), "%s:owned-nonce:%s:%s:auth:%s",
                     ha1, count, cnonce, ha2);
        else
            snprintf(text, sizeof(text), "%s:owned-nonce:%s", ha1, ha2);
        Digest(text, expected);
        assert(strcmp(response, expected) == 0);
        free(header);
    }
    vlc_http_auth_Deinit(&auth);
}

int main(void)
{
    const char *args[] = {"--quiet", "--no-plugins-cache"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance != NULL);
    for (unsigned session = 0; session < 2; ++session)
        for (unsigned qop = 0; qop < 2; ++qop)
            Check(VLC_OBJECT(instance->p_libvlc_int), session, qop);
    libvlc_release(instance);
    return 0;
}
