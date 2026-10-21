// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_sout.h>
#include <vlc_frame.h>
#include <vlc_modules.h>
#include <vlc_aout.h>

struct output {
    unsigned rate;
    uint64_t samples;
};

static void *Add(sout_stream_t *stream, const es_format_t *format,
                 const char *es_id)
{
    (void) es_id;
    struct output *output = stream->p_sys;
    assert(format->i_cat == AUDIO_ES);
    assert(format->i_codec == VLC_CODEC_S16L);
    assert(format->audio.i_rate == output->rate);
    assert(format->audio.i_channels == 2);
    return output;
}

static void Del(sout_stream_t *stream, void *id)
{
    assert(id == stream->p_sys);
}

static int Send(sout_stream_t *stream, void *id, vlc_frame_t *frame)
{
    assert(id == stream->p_sys);
    struct output *output = id;
    assert(frame != NULL);
    assert(frame->i_buffer % 4 == 0);
    output->samples += frame->i_buffer / 4;
    vlc_frame_Release(frame);
    return VLC_SUCCESS;
}

static void Check(libvlc_instance_t *vlc, unsigned input_rate,
                  unsigned output_rate, unsigned samples, unsigned chunk)
{
    vlc_object_t *parent = VLC_OBJECT(vlc->p_libvlc_int);
    struct output output = { .rate = output_rate };
    sout_stream_t *sink = sout_StreamNew(parent, "audio-tail-sink");
    assert(sink);
    static const struct sout_stream_operations ops = {
        .add = Add, .del = Del, .send = Send,
    };
    sink->ops = &ops;
    sink->p_sys = &output;
    char chain[128];
    snprintf(chain, sizeof(chain),
             "transcode{acodec=s16l,samplerate=%u,channels=2}", output_rate);
    sout_stream_t *transcode = sout_StreamChainNew(parent, chain, sink);
    assert(transcode);
    es_format_t format;
    es_format_Init(&format, AUDIO_ES, VLC_CODEC_S16L);
    format.audio.i_format = VLC_CODEC_S16L;
    format.audio.i_rate = input_rate;
    format.audio.i_physical_channels = AOUT_CHAN_CENTER;
    aout_FormatPrepare(&format.audio);
    void *id = sout_StreamIdAdd(transcode, &format, "audio-tail");
    assert(id);
    for (unsigned offset = 0; offset < samples; )
    {
        unsigned count = samples - offset;
        if (count > chunk) count = chunk;
        vlc_frame_t *frame = vlc_frame_Alloc(count * 2);
        assert(frame);
        memset(frame->p_buffer, 0, frame->i_buffer);
        frame->i_nb_samples = count;
        frame->i_pts = frame->i_dts = VLC_TICK_0 + vlc_tick_from_samples(offset, input_rate);
        frame->i_length = vlc_tick_from_samples(count, input_rate);
        assert(sout_StreamIdSend(transcode, id, frame) == VLC_SUCCESS);
        offset += count;
    }
    /* The input signals EOF through the transcode send callback. The public
     * frame-send wrapper requires a non-NULL frame. */
    assert(transcode->ops->send(transcode, id, NULL) == VLC_SUCCESS);
    uint64_t expected = (uint64_t) samples * output_rate / input_rate;
    fprintf(stderr, "%u -> %u Hz, %u input samples: got %" PRIu64 ", expected %" PRIu64 "\n",
            input_rate, output_rate, samples, output.samples, expected);
    assert(output.samples == expected);
    /* A second drain must not duplicate the buffered tail. */
    assert(transcode->ops->send(transcode, id, NULL) == VLC_SUCCESS);
    assert(output.samples == expected);
    sout_StreamIdDel(transcode, id);
    assert(output.samples == expected);
    sout_StreamChainDelete(transcode, sink);
    sout_StreamChainDelete(sink, NULL);
    es_format_Clean(&format);
}

int main(void)
{
#ifndef ENABLE_SOUT
    (void) Check;
    return 77;
#endif
    test_init();
    const char *args[] = { "--ignore-config", "--no-plugins-cache", "--audio-resampler=soxr" };
    libvlc_instance_t *vlc = libvlc_new(ARRAY_SIZE(args), args);
    assert(vlc);
    if (!module_exists("soxr") || !module_exists("araw") || !module_exists("stream_out_transcode"))
    {
        libvlc_release(vlc);
        return 77;
    }
    Check(vlc, 48000, 44100, 96000, 2400);
    Check(vlc, 44100, 48000, 88200, 2205);
    Check(vlc, 48000, 48000, 96000, 2400);
    Check(vlc, 48000, 44100, 480, 480);
    Check(vlc, 48000, 44100, 0, 480);
    libvlc_release(vlc);
    return 0;
}
