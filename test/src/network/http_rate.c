// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include <vlc_network.h>
#include <vlc_interrupt.h>
#include <errno.h>

struct worker {
    vlc_interrupt_t *interrupt;
    vlc_sem_t started;
    int result;
    vlc_tick_t finished;
};
static void *Wait(void *data)
{
    struct worker *worker = data;
    vlc_interrupt_t *previous = vlc_interrupt_set(worker->interrupt);
    vlc_sem_post(&worker->started);
    worker->result = vlc_http_request_wait("mb.videolan.org");
    worker->finished = vlc_tick_now();
    vlc_interrupt_set(previous);
    return NULL;
}
static vlc_thread_t Start(struct worker *worker)
{
    worker->interrupt = vlc_interrupt_create();
    assert(worker->interrupt);
    vlc_sem_init(&worker->started, 0);
    vlc_thread_t thread;
    assert(vlc_clone(&thread, Wait, worker) == 0);
    vlc_sem_wait(&worker->started);
    return thread;
}
static int Compare(const void *a, const void *b)
{
    const vlc_tick_t first = *(const vlc_tick_t *)a, second = *(const vlc_tick_t *)b;
    return (first > second) - (first < second);
}
int main(void)
{
    const char *accepted[] = {
        "mb.videolan.org", "musicbrainz.org", "MB.VIDEOLAN.ORG", "www.musicbrainz.org",
        "beta.musicbrainz.org", "mb.videolan.org.:80", "musicbrainz.org:443",
    };
    const char *ignored[] = {
        "coverartarchive.org", "127.0.0.1:1234", "mb.videolan.org.example", "notmusicbrainz.org",
        "musicbrainz.org.evil", "musicbrainz.org@127.0.0.1", "[::1]:80", "example.org",
        "mb.videolan.org:invalid", NULL,
    };
    assert(vlc_http_request_wait("mb.videolan.org") == 0);
    for (size_t i = 0; i < ARRAY_SIZE(accepted); ++i) {
        const vlc_tick_t before = vlc_tick_now();
        assert(vlc_http_request_wait(accepted[i]) == 0);
        assert(vlc_tick_now() - before >= VLC_TICK_FROM_MS(980));
    }
    const vlc_tick_t before_ignored = vlc_tick_now();
    for (size_t i = 0; i < ARRAY_SIZE(ignored); ++i) assert(vlc_http_request_wait(ignored[i]) == 0);
    assert(vlc_tick_now() - before_ignored < VLC_TICK_FROM_MS(300));
    struct worker workers[3] = {0};
    vlc_thread_t threads[3];vlc_tick_t times[3];
    for (size_t i = 0; i < ARRAY_SIZE(workers); ++i) threads[i] = Start(&workers[i]);
    for (size_t i = 0; i < ARRAY_SIZE(workers); ++i) {
        vlc_join(threads[i], NULL);assert(workers[i].result == 0);
        times[i] = workers[i].finished;vlc_interrupt_destroy(workers[i].interrupt);
    }
    qsort(times, ARRAY_SIZE(times), sizeof(*times), Compare);
    for (size_t i = 1; i < ARRAY_SIZE(times); ++i) assert(times[i] - times[i - 1] >= VLC_TICK_FROM_MS(980));
    for (unsigned killed = 0; killed < 2; ++killed) {
        assert(vlc_http_request_wait("mb.videolan.org") == 0);
        const vlc_tick_t granted = vlc_tick_now();
        struct worker cancelled = {0};vlc_thread_t thread = Start(&cancelled);
        vlc_tick_wait(vlc_tick_now() + VLC_TICK_FROM_MS(50));
        const vlc_tick_t interrupted = vlc_tick_now();
        if (killed) vlc_interrupt_kill(cancelled.interrupt);
        else vlc_interrupt_raise(cancelled.interrupt);
        vlc_join(thread, NULL);
        assert(cancelled.result == EINTR);
        assert(cancelled.finished - interrupted < VLC_TICK_FROM_MS(500));
        vlc_interrupt_destroy(cancelled.interrupt);
        // A cancelled waiter does not push the next request into a later slot.
        assert(vlc_http_request_wait("mb.videolan.org") == 0);
        assert(vlc_tick_now() - granted < VLC_TICK_FROM_MS(1800));
    }
    struct worker killed = {0};
    killed.interrupt = vlc_interrupt_create();assert(killed.interrupt);
    vlc_sem_init(&killed.started, 0);vlc_interrupt_kill(killed.interrupt);Wait(&killed);
    assert(killed.result == EINTR);vlc_interrupt_destroy(killed.interrupt);
    puts("17 host cases, concurrent spacing, interruption and cancelled-slot checks passed.");
    return 0;
}
