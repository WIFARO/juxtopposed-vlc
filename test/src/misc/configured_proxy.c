// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_url.h>

static const char *proxy_setting, *password_setting;

static char *test_config(const char *name)
{
    const char *value = !strcmp(name, "http-proxy") ? proxy_setting
                                                   : password_setting;
    /* Config string setters store an empty value as NULL. */
    return value && *value ? strdup(value) : NULL;
}

#define config_GetPsz test_config
#include "../../../src/network/proxy.h"
#undef config_GetPsz

static unsigned cases;

static void check(const char *proxy, const char *password,
                  bool configured, const char *expected)
{
    proxy_setting = proxy;
    password_setting = password;
    bool actual_configured;
    char *actual = vlc_proxy_GetConfigured(&actual_configured);
    assert(actual_configured == configured);
    if (expected)
        assert(actual && !strcmp(actual, expected));
    else
        assert(actual == NULL);
    free(actual);
    cases++;
}

int main(void)
{
    check(NULL, NULL, false, NULL);
    check("", NULL, false, NULL);
    check(NULL, "unused", false, NULL);
    check("http://proxy.invalid:8080/", NULL, true, "http://proxy.invalid:8080/");
    check("http://proxy.invalid:8080/", "", true, "http://proxy.invalid:8080/");
    check("http://user@proxy.invalid:8123/", "p@ss:word", true,
          "http://user:p%40ss%3Aword@proxy.invalid:8123/");
    check("http://user@proxy.invalid:8123/", "space /@:%", true,
          "http://user:space%20%2F%40%3A%25@proxy.invalid:8123/");
    check("http://user:existing@proxy.invalid:8123/", "other", true,
          "http://user:existing@proxy.invalid:8123/");
    check("http://user@proxy.invalid:8123/", NULL, true,
          "http://user@proxy.invalid:8123/");
    check("http://user@[::1]:8123/", "p@ss", true,
          "http://user:p%40ss@[::1]:8123/");
    check("http://user%40name@proxy.invalid:8123/", "secret", true,
          "http://user%40name:secret@proxy.invalid:8123/");
    /* A malformed explicit URL must not enable automatic discovery. */
    check("http://[invalid", "password", true, NULL);

    proxy_setting = "http://proxy.invalid:8080/";
    password_setting = NULL;
    bool configured;
    char *first = vlc_proxy_GetConfigured(&configured);
    char *second = vlc_proxy_GetConfigured(&configured);
    assert(first && second && first != second);
    first[0] = 'X';
    assert(!strcmp(second, proxy_setting));
    free(first);
    free(second);
    cases++;
    printf("Configured proxy: %u cases passed\n", cases);
    return 0;
}
