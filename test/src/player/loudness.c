// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * loudness.c: audio loudness meter player test
 *****************************************************************************
 * Copyright (C) 2018-2025 VLC authors and VideoLAN
 *****************************************************************************/

#include "common.h"
#include <stdatomic.h>

static void
test_audio_loudness_meter_cb(vlc_tick_t date, double momentary_loudness,
                             void *data)
{
    (void) date;
    assert(momentary_loudness>= -23 -0.1 && momentary_loudness <= -23 +0.1);

    bool *loudness_meter_received = data;
    *loudness_meter_received = true;
}

static void
test_audio_loudness_meter(struct ctx *ctx)
{
    test_log("test_audio_loudness_meter\n");

    vlc_player_t *player = ctx->player;

    static const union vlc_player_metadata_cbs cbs = {
        test_audio_loudness_meter_cb,
    };

    if (!module_exists("ebur128"))
    {
        test_log("audio loudness meter test skipped\n");
        return;
    }

    struct media_params params = DEFAULT_MEDIA_PARAMS(VLC_TICK_FROM_SEC(1));
    params.track_count[AUDIO_ES] = 1;
    params.track_count[VIDEO_ES] = 0;
    params.track_count[SPU_ES] = 0;

    /* cf. EBU TECH 3341, Table 1. A 1000Hz stereo sine wave, with an amplitude
     * of -23dbFS (0.0707) should have a momentary loudness of -23LUFS */
    params.config = "audio[0]{sinewave=true,sinewave_frequency=1000"
                    ",sinewave_amplitude=0.0707}";
    player_set_next_mock_media(ctx, "media1", &params);

    bool loudness_meter_received = false;
    vlc_player_metadata_listener_id *listener_id =
        vlc_player_AddMetadataListener(player, VLC_PLAYER_METADATA_LOUDNESS_MOMENTARY,
                                       &cbs, &loudness_meter_received);
    assert(listener_id);

    player_start(ctx);
    wait_state(ctx, VLC_PLAYER_STATE_STARTED);
    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);

    assert(loudness_meter_received);

    vlc_player_RemoveMetadataListener(player, listener_id);
    test_end(ctx);
}

struct sample_observer { atomic_uint calls; atomic_bool audible; vlc_sem_t first_sample; };
static void audio_samples(vlc_tick_t time, double rate,
                          const audio_sample_format_t *format,
                          const block_t *block, void *opaque)
{
    struct sample_observer *observer = opaque;
    assert(time >= 0 && rate > 0 && format->i_rate > 0 && block->i_nb_samples > 0);
    if (atomic_fetch_add(&observer->calls, 1) == 0) vlc_sem_post(&observer->first_sample);
    // The observer runs before software volume, even while playback is muted.
    for (size_t i = 0; i < block->i_buffer; ++i)
        if (block->p_buffer[i]) atomic_store(&observer->audible, true);
}
static void test_audio_sample_observers(struct ctx *ctx)
{
    vlc_player_t *player = ctx->player;
    struct media_params params = DEFAULT_MEDIA_PARAMS(VLC_TICK_FROM_SEC(1));
    params.track_count[AUDIO_ES] = 1; params.track_count[VIDEO_ES] = 0; params.track_count[SPU_ES] = 0;
    params.config = "audio[0]{sinewave=true,sinewave_frequency=1000,sinewave_amplitude=0.5}";
    player_set_next_mock_media(ctx, "samples", &params);
    struct sample_observer first = {0}, second = {0};
    vlc_sem_init(&first.first_sample, 0); vlc_sem_init(&second.first_sample, 0);
    static const union vlc_player_metadata_cbs callbacks = {.on_audio_samples = audio_samples};
    vlc_player_metadata_listener_id *a = vlc_player_AddMetadataListener(player, VLC_PLAYER_METADATA_AUDIO_SAMPLES, &callbacks, &first);
    vlc_player_metadata_listener_id *b = vlc_player_AddMetadataListener(player, VLC_PLAYER_METADATA_AUDIO_SAMPLES, &callbacks, &second);
    assert(a && b);
    vlc_player_aout_Mute(player, true);
    player_start(ctx); wait_state(ctx, VLC_PLAYER_STATE_STARTED);
    vlc_player_Unlock(player);
    vlc_sem_wait(&first.first_sample);
    vlc_player_Lock(player);
    vlc_player_RemoveMetadataListener(player, a);
    const unsigned first_calls = atomic_load(&first.calls);
    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);
    assert(atomic_load(&first.calls) > 0 && atomic_load(&first.audible));
    assert(atomic_load(&first.calls) == first_calls);
    assert(atomic_load(&second.calls) >= first_calls && atomic_load(&second.audible));
    vlc_player_RemoveMetadataListener(player, b);
    vlc_player_aout_Mute(player, false);
    test_end(ctx);
}

int
main(void)
{
    struct ctx ctx;
    /* Test with --no-video */
    ctx_init(&ctx, DISABLE_VIDEO);
    test_audio_loudness_meter(&ctx);
    test_audio_sample_observers(&ctx);
    ctx_destroy(&ctx);
    return 0;
}
