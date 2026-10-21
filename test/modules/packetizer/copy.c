// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_codec.h>
#include <vlc_modules.h>

static block_t *Frame(unsigned value, vlc_tick_t pts, vlc_tick_t length)
{
    block_t *frame = block_Alloc(4);
    assert(frame);
    memset(frame->p_buffer, value, frame->i_buffer);
    frame->i_pts = frame->i_dts = pts;
    frame->i_length = length;
    return frame;
}

static void Check(libvlc_instance_t *vlc, int category, vlc_fourcc_t codec)
{
    decoder_t *decoder = vlc_object_create(vlc->p_libvlc_int, sizeof(*decoder));
    assert(decoder);
    es_format_t input;
    es_format_Init(&input, category, codec);
    input.audio.i_bitspersample = 16;
    decoder->fmt_in = &input;
    es_format_Init(&decoder->fmt_out, category, 0);
    module_t *module = module_need(decoder,
        category == AUDIO_ES ? "audio packetizer" : "video packetizer",
        "packetizer_copy,copy", true);
    assert(module);
    assert(decoder->pf_packetize(decoder, NULL) == NULL);
    const vlc_tick_t start = VLC_TICK_0;
    block_t *first = Frame(17, start, 10000);
    assert(decoder->pf_packetize(decoder, &first) == NULL);
    assert(first == NULL);
    /* Asking for another output from this input is not EOF. */
    assert(decoder->pf_packetize(decoder, &first) == NULL);
    block_t *second = Frame(34, start + 40000, 7000);
    block_t *out = decoder->pf_packetize(decoder, &second);
    assert(second == NULL && out);
    assert(out->p_buffer[0] == 17 && out->i_pts == start);
    assert(out->i_length == (codec == VLC_CODEC_OPUS ? 10000 : 40000));
    block_Release(out);
    assert(decoder->pf_packetize(decoder, &second) == NULL);
    out = decoder->pf_packetize(decoder, NULL);
    assert(out);
    assert(out->i_buffer == 4 && out->p_buffer[0] == 34);
    assert(out->i_pts == start + 40000 && out->i_dts == start + 40000);
    assert(out->i_length == 7000);
    block_Release(out);
    assert(decoder->pf_packetize(decoder, NULL) == NULL);
    first = Frame(51, start + 80000, 9000);
    assert(decoder->pf_packetize(decoder, &first) == NULL);
    decoder->pf_flush(decoder);
    assert(decoder->pf_packetize(decoder, NULL) == NULL);
    first = Frame(68, start + 120000, 11000);
    assert(decoder->pf_packetize(decoder, &first) == NULL);
    out = decoder->pf_packetize(decoder, NULL);
    assert(out && out->p_buffer[0] == 68 && out->i_length == 11000);
    block_Release(out);
    assert(decoder->pf_packetize(decoder, NULL) == NULL);
    module_unneed(decoder, module);
    es_format_Clean(&decoder->fmt_out);
    es_format_Clean(&input);
    vlc_object_delete(decoder);
    fprintf(stderr, "copy packetizer drain/flush passed for %4.4s\n", (char *)&codec);
}

int main(void)
{
    test_init();
    const char *args[] = { "--ignore-config", "--no-plugins-cache" };
    libvlc_instance_t *vlc = libvlc_new(ARRAY_SIZE(args), args);
    assert(vlc);
    if (!module_exists("packetizer_copy") && !module_exists("copy"))
    {
        libvlc_release(vlc);
        return 77;
    }
    Check(vlc, AUDIO_ES, VLC_CODEC_S16L);
    Check(vlc, AUDIO_ES, VLC_CODEC_OPUS);
    Check(vlc, VIDEO_ES, VLC_CODEC_I420);
    libvlc_release(vlc);
    return 0;
}
