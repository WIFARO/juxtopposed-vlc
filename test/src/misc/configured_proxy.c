// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include "../lib/libvlc_internal.h"
#include <vlc_network.h>
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
    char *actual = vlc_proxy_GetConfigured(NULL, &actual_configured);
    assert(actual_configured == configured);
    if (expected)
        assert(actual && !strcmp(actual, expected));
    else
        assert(actual == NULL);
    free(actual);
    cases++;
}

static void check_object(vlc_object_t *obj, const char *expected)
{
    char *actual = vlc_getProxyUrlForObject(obj, "http://media.invalid/test.wav");
    assert(actual && !strcmp(actual, expected));
    free(actual);
    cases++;
}

static void test_inherited_proxy(void)
{
    const char *one_args[] = {"--ignore-config", "--no-media-library",
        "--http-proxy=http://user@one.invalid:8123/", "--http-proxy-pwd=p@ss:word"};
    const char *two_args[] = {"--ignore-config", "--no-media-library",
        "--http-proxy=http://two.invalid:8124/"};
    const char *saved_args[] = {"--ignore-config", "--no-media-library"};
    libvlc_instance_t *one = libvlc_new(ARRAY_SIZE(one_args), one_args);
    libvlc_instance_t *two = libvlc_new(ARRAY_SIZE(two_args), two_args);
    libvlc_instance_t *saved = libvlc_new(ARRAY_SIZE(saved_args), saved_args);
    assert(one && two && saved);
    vlc_object_t *first = VLC_OBJECT(one->p_libvlc_int);
    vlc_object_t *second = VLC_OBJECT(two->p_libvlc_int);
    vlc_object_t *third = VLC_OBJECT(saved->p_libvlc_int);
    char *old_proxy = config_GetPsz("http-proxy");
    char *old_password = config_GetPsz("http-proxy-pwd");
    config_PutPsz("http-proxy", "http://saved.invalid:8125/");
    config_PutPsz("http-proxy-pwd", NULL);
    check_object(first, "http://user:p%40ss%3Aword@one.invalid:8123/");
    check_object(second, "http://two.invalid:8124/");
    check_object(third, "http://saved.invalid:8125/");
    /* Reading one instance must not overwrite another or the saved value. */
    check_object(first, "http://user:p%40ss%3Aword@one.invalid:8123/");
    char *actual = vlc_getProxyUrl("http://media.invalid/test.wav");
    assert(actual && !strcmp(actual, "http://saved.invalid:8125/"));
    free(actual); cases++;
    actual = config_GetPsz("http-proxy");
    assert(actual && !strcmp(actual, "http://saved.invalid:8125/"));
    free(actual); cases++;

    vlc_object_t *child = vlc_object_create(first, sizeof(*child));
    assert(child);
    check_object(child, "http://user:p%40ss%3Aword@one.invalid:8123/");
    var_Create(child, "http-proxy", VLC_VAR_STRING);
    var_SetString(child, "http-proxy", "http://user@item.invalid:8126/");
    var_Create(child, "http-proxy-pwd", VLC_VAR_STRING);
    var_SetString(child, "http-proxy-pwd", "item /password");
    check_object(child, "http://user:item%20%2Fpassword@item.invalid:8126/");
    var_SetString(child, "http-proxy", "http://user:inline@item.invalid:8126/");
    check_object(child, "http://user:inline@item.invalid:8126/");
    /* An explicit empty variable selects discovery instead of the saved proxy. */
    var_SetString(child, "http-proxy", "");
    bool configured;
    actual = vlc_proxy_GetConfigured(child, &configured);
    assert(!configured && !actual); cases++;
    var_SetString(child, "http-proxy", "http://[invalid");
    actual = vlc_proxy_GetConfigured(child, &configured);
    assert(configured && !actual); cases++;
    var_Destroy(child, "http-proxy-pwd");
    var_Destroy(child, "http-proxy");
    check_object(child, "http://user:p%40ss%3Aword@one.invalid:8123/");
    vlc_object_delete(child);

    config_PutPsz("http-proxy", old_proxy);
    config_PutPsz("http-proxy-pwd", old_password);
    free(old_proxy); free(old_password);
    libvlc_release(saved); libvlc_release(two); libvlc_release(one);
}

int main(void)
{
    test_init();
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
    char *first = vlc_proxy_GetConfigured(NULL, &configured);
    char *second = vlc_proxy_GetConfigured(NULL, &configured);
    assert(first && second && first != second);
    first[0] = 'X';
    assert(!strcmp(second, proxy_setting));
    free(first);
    free(second);
    cases++;
    test_inherited_proxy();
    printf("Configured proxy: %u cases passed\n", cases);
    return 0;
}
