// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#undef NDEBUG
#include <assert.h>
#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_network.h>
#include <vlc_url.h>
#include <vlc_charset.h>
#include <vlc_interrupt.h>
#include <winhttp.h>

static const char *explicit_proxy, *explicit_password;
static const wchar_t *system_proxy, *system_bypass, *system_pac;
static unsigned system_queries;
static const char *inherited_proxy, *inherited_password;

static char *test_inherit(vlc_object_t *obj, const char *name)
{
    (void)obj;
    const char *value = !strcmp(name, "http-proxy") ? inherited_proxy
                                                  : inherited_password;
    return value && *value ? strdup(value) : NULL;
}

static char *test_config(const char *name)
{
    const char *value = !strcmp(name, "http-proxy") ? explicit_proxy
                                                  : explicit_password;
    return value ? strdup(value) : NULL;
}

static wchar_t *system_string(const wchar_t *value)
{
    if (!value)
        return NULL;
    size_t size = (wcslen(value) + 1) * sizeof(wchar_t);
    wchar_t *copy = GlobalAlloc(GMEM_FIXED, size);
    assert(copy);
    memcpy(copy, value, size);
    return copy;
}

static BOOL test_system_config(WINHTTP_CURRENT_USER_IE_PROXY_CONFIG *config)
{
    system_queries++;
    *config = (WINHTTP_CURRENT_USER_IE_PROXY_CONFIG) {
        .lpszProxy = system_string(system_proxy),
        .lpszProxyBypass = system_string(system_bypass),
        .lpszAutoConfigUrl = system_string(system_pac),
    };
    return TRUE;
}

/* Read settings from fixtures, without changing the user's registry. URL
 * parsing, WinHTTP PAC execution, callbacks and cancellation are real. */
char *test_get_proxy(const char *);
char *test_get_proxy_for_object(vlc_object_t *, const char *);
#undef var_InheritString
#define var_InheritString test_inherit
#define config_GetPsz test_config
#define WinHttpGetIEProxyConfigForCurrentUser test_system_config
#define vlc_getProxyUrl test_get_proxy
#define vlc_getProxyUrlForObject test_get_proxy_for_object
#include "../../../src/win32/netconf.c"
#undef vlc_getProxyUrl
#undef vlc_getProxyUrlForObject
#undef WinHttpGetIEProxyConfigForCurrentUser
#undef config_GetPsz
#undef var_InheritString

static unsigned cases;
static void check(const char *target, const char *expected)
{
    char *actual = test_get_proxy(target);
    if ((expected && (!actual || strcmp(actual, expected))) || (!expected && actual))
        fprintf(stderr, "Proxy case %u: target=%s expected=%s actual=%s\n",
                cases, target ? target : "(null)", expected ? expected : "DIRECT",
                actual ? actual : "DIRECT");
    if (expected)
        assert(actual && !strcmp(actual, expected));
    else
        assert(!actual);
    free(actual);
    cases++;
}

struct pac_server
{
    SOCKET listener;
    HANDLE received, release;
    vlc_thread_t thread;
    unsigned port;
    bool slow;
};

static void *serve_pac(void *opaque)
{
    struct pac_server *server = opaque;
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(server->listener, &read_set);
    struct timeval timeout = {.tv_sec = 10};
    if (select(0, &read_set, NULL, NULL, &timeout) <= 0)
        return NULL;
    SOCKET client = accept(server->listener, NULL, NULL);
    assert(client != INVALID_SOCKET);
    DWORD receive_timeout = 5000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (void *)&receive_timeout,
               sizeof(receive_timeout));
    char request[4096];
    assert(recv(client, request, sizeof(request), 0) > 0);
    SetEvent(server->received);
    if (server->slow)
        WaitForSingleObject(server->release, 10000);
    else
    {
        const char *script = "function FindProxyForURL(url, host) {"
            "if (host == 'direct.invalid') return 'DIRECT';"
            "if (url.indexOf('/direct/') >= 0) return 'DIRECT';"
            "return 'PROXY pac-proxy.invalid:8123; DIRECT'; }";
        char response[1024];
        int size = snprintf(response, sizeof(response),
            "HTTP/1.1 200 OK\r\nContent-Type: application/x-ns-proxy-autoconfig\r\n"
            "Content-Length: %zu\r\nConnection: close\r\n\r\n%s",
            strlen(script), script);
        assert(size > 0 && (size_t)size < sizeof(response));
        for (int offset = 0; offset < size;)
        {
            int sent = send(client, response + offset, size - offset, 0);
            assert(sent > 0);
            offset += sent;
        }
    }
    closesocket(client);
    return NULL;
}

static void start_pac(struct pac_server *server, bool slow)
{
    *server = (struct pac_server) {
        .listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP),
        .received = CreateEventW(NULL, TRUE, FALSE, NULL),
        .release = CreateEventW(NULL, TRUE, FALSE, NULL),
        .slow = slow,
    };
    assert(server->listener != INVALID_SOCKET && server->received && server->release);
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    assert(!bind(server->listener, (void *)&address, sizeof(address)));
    int size = sizeof(address);
    assert(!getsockname(server->listener, (void *)&address, &size));
    server->port = ntohs(address.sin_port);
    assert(!listen(server->listener, 1));
    assert(!vlc_clone(&server->thread, serve_pac, server));
}

static void stop_pac(struct pac_server *server)
{
    SetEvent(server->release);
    vlc_join(server->thread, NULL);
    closesocket(server->listener);
    CloseHandle(server->received);
    CloseHandle(server->release);
}

struct interrupted_lookup
{
    vlc_interrupt_t *interrupt;
    HANDLE finished;
};

static void *lookup_until_cancelled(void *opaque)
{
    struct interrupted_lookup *lookup = opaque;
    vlc_interrupt_t *previous = vlc_interrupt_set(lookup->interrupt);
    check("http://cancel.invalid", NULL);
    vlc_interrupt_set(previous);
    SetEvent(lookup->finished);
    return NULL;
}

int main(void)
{
    WSADATA data;
    assert(!WSAStartup(MAKEWORD(2, 2), &data));
    system_proxy = L"http=system-http.invalid:8080;https=system-tls.invalid:8443";
    check("http://example.invalid", "http://system-http.invalid:8080");
    check("https://example.invalid", "http://system-tls.invalid:8443");
    check("ftp://example.invalid", NULL);
    check(NULL, NULL);
    check("not a URL", NULL);

    explicit_proxy = "http://user@manual.invalid:9000/";
    explicit_password = "p@ss:word";
    unsigned before = system_queries;
    check("https://example.invalid", "http://user:p%40ss%3Aword@manual.invalid:9000/");
    assert(system_queries == before);
    explicit_proxy = "http://user:existing@manual.invalid:9000/";
    check("http://example.invalid", explicit_proxy);
    explicit_proxy = NULL;
    /* A stale custom password must never go to a system-selected proxy. */
    check("http://example.invalid", "http://system-http.invalid:8080");
    explicit_password = NULL;

    system_proxy = L"default.invalid:8888";
    check("https://example.invalid", "http://default.invalid:8888");
    system_bypass = L"<local>;*.internal.invalid;exact.invalid;port.invalid:8443;[::1]";
    check("http://printer", NULL);
    check("http://media.INTERNAL.invalid", NULL);
    check("https://exact.invalid", NULL);
    check("https://port.invalid:8443", NULL);
    check("https://port.invalid", "http://default.invalid:8888");
    check("http://notinternal.invalid", "http://default.invalid:8888");
    check("http://[::1]", NULL);
    check("http://127.0.0.1", "http://default.invalid:8888");
    system_proxy = L"http=plain.invalid:80; HTTPS=tls.invalid:443";
    check("https://outside.invalid", "http://tls.invalid:443");
    system_proxy = L"https=only.invalid:8443";
    check("http://outside.invalid", NULL);
    system_proxy = L"[::1]:8181";
    system_bypass = L"secure.invalid:443";
    check("https://outside.invalid", "http://[::1]:8181");
    check("https://secure.invalid", NULL);
    check("http://secure.invalid", "http://[::1]:8181");
    system_proxy = NULL;
    check("https://outside.invalid", NULL);
    system_bypass = NULL;

    /* Optional Windows 8 API: old systems retain all static/explicit cases. */
    if (GetProcAddress(GetModuleHandleW(L"winhttp.dll"), "WinHttpCreateProxyResolver"))
    {
        struct pac_server server;
        wchar_t pac_url[256];
        start_pac(&server, false);
        swprintf(pac_url, ARRAY_SIZE(pac_url), L"http://127.0.0.1:%u/proxy.pac", server.port);
        system_pac = pac_url;
        check("http://outside.invalid", "http://pac-proxy.invalid:8123");
        assert(WaitForSingleObject(server.received, 0) == WAIT_OBJECT_0);
        stop_pac(&server);

        start_pac(&server, false);
        swprintf(pac_url, ARRAY_SIZE(pac_url), L"http://127.0.0.1:%u/direct.pac", server.port);
        system_proxy = L"fallback.invalid:8888";
        check("https://direct.invalid", NULL);
        stop_pac(&server);

        start_pac(&server, false);
        swprintf(pac_url, ARRAY_SIZE(pac_url), L"http://127.0.0.1:%u/path.pac", server.port);
        check("http://outside.invalid/direct/movie.mkv?track=2", NULL);
        stop_pac(&server);
        system_proxy = NULL;

        start_pac(&server, true);
        swprintf(pac_url, ARRAY_SIZE(pac_url), L"http://127.0.0.1:%u/timeout.pac", server.port);
        system_proxy = L"fallback.invalid:8888";
        vlc_tick_t start = vlc_tick_now();
        check("http://timeout.invalid", "http://fallback.invalid:8888");
        vlc_tick_t elapsed = vlc_tick_now() - start;
        printf("PAC timeout returned after %"PRId64" ms\n", MS_FROM_VLC_TICK(elapsed));
        assert(elapsed >= VLC_TICK_FROM_MS(4900) && elapsed < VLC_TICK_FROM_MS(6500));
        assert(WaitForSingleObject(server.received, 0) == WAIT_OBJECT_0);
        stop_pac(&server);

        start_pac(&server, true);
        swprintf(pac_url, ARRAY_SIZE(pac_url), L"http://127.0.0.1:%u/slow.pac", server.port);
        struct interrupted_lookup lookup = {
            .interrupt = vlc_interrupt_create(),
            .finished = CreateEventW(NULL, TRUE, FALSE, NULL),
        };
        assert(lookup.interrupt && lookup.finished);
        vlc_thread_t worker;
        assert(!vlc_clone(&worker, lookup_until_cancelled, &lookup));
        assert(WaitForSingleObject(server.received, 5000) == WAIT_OBJECT_0);
        vlc_interrupt_kill(lookup.interrupt);
        assert(WaitForSingleObject(lookup.finished, 1500) == WAIT_OBJECT_0);
        puts("PAC cancellation completed within 1500 ms");
        vlc_join(worker, NULL);
        stop_pac(&server);
        CloseHandle(lookup.finished);
        vlc_interrupt_destroy(lookup.interrupt);
        system_pac = NULL;
        system_proxy = NULL;
    }
    else
        puts("PAC tests unavailable on this Windows version");

    /* An item/instance override must not use the saved proxy. An empty one
     * selects the real system path, with only its registry inputs substituted. */
    explicit_proxy = "http://saved.invalid:8080/";
    explicit_password = NULL;
    system_proxy = L"system.invalid:8127";
    inherited_proxy = "http://user@item.invalid:8128/";
    inherited_password = "p@ss:word";
    unsigned before_queries = system_queries;
    vlc_object_t object = {0}; /* Only test_inherit sees this fixture object. */
    char *actual = test_get_proxy_for_object(&object, "http://outside.invalid");
    assert(actual && !strcmp(actual, "http://user:p%40ss%3Aword@item.invalid:8128/"));
    assert(system_queries == before_queries);
    free(actual); cases++;
    inherited_proxy = "";
    actual = test_get_proxy_for_object(&object, "http://outside.invalid");
    assert(actual && !strcmp(actual, "http://system.invalid:8127"));
    assert(system_queries > before_queries);
    free(actual); cases++;
    before_queries = system_queries;
    inherited_proxy = "http://[invalid";
    actual = test_get_proxy_for_object(&object, "http://outside.invalid");
    assert(!actual && system_queries == before_queries); cases++;
    check("http://outside.invalid", "http://saved.invalid:8080/");

    WSACleanup();
    printf("Windows proxy: %u explicit, system, bypass, PAC and cancellation cases passed\n", cases);
    return 0;
}
