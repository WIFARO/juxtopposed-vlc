// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc/vlc.h>
#include <vlc_common.h>
#include <vlc_aout.h>
#include <vlc_block.h>
#include "../../../lib/libvlc_internal.h"

#undef NDEBUG
#include <assert.h>

static void Check(vlc_object_t *obj, vlc_fourcc_t codec, const char *chain,
                  float inherited_gain, float expected_gain)
{
    var_SetString(obj, "audio-filter", chain);
    var_SetFloat(obj, "gain-value", inherited_gain);
    audio_sample_format_t format = {
        .i_format = codec,
        .i_rate = 48000,
        .i_physical_channels = AOUT_CHANS_STEREO,
        .channel_type = AUDIO_CHANNEL_TYPE_BITMAP,
    };
    aout_FormatPrepare(&format);
    aout_filters_t *filters = aout_FiltersNew(obj, &format, &format, NULL);
    assert(filters != NULL);

    const unsigned frames = 64;
    block_t *input = block_Alloc(frames * format.i_bytes_per_frame);
    assert(input != NULL);
    input->i_nb_samples = frames;
    input->i_pts = VLC_TICK_0;
    input->i_length = vlc_tick_from_samples(frames, format.i_rate);
    for (unsigned i = 0; i < frames * 2; ++i)
    {
        const int sample = ((int)(i % 31) - 15) * 64;
        if (codec == VLC_CODEC_FL32)
            ((float *)input->p_buffer)[i] = sample / 4096.f;
        else
            ((int16_t *)input->p_buffer)[i] = sample;
    }
    block_t *output = aout_FiltersPlay(filters, input, 1.f);
    assert(output != NULL && output->i_nb_samples == frames);
    for (unsigned i = 0; i < frames * 2; ++i)
    {
        const int sample = ((int)(i % 31) - 15) * 64;
        float expected = sample * expected_gain;
        float actual;
        if (codec == VLC_CODEC_FL32)
        {
            expected /= 4096.f;
            actual = ((float *)output->p_buffer)[i];
        }
        else
            actual = ((int16_t *)output->p_buffer)[i];
        assert(actual - expected < 0.00001f && expected - actual < 0.00001f);
    }
    block_Release(output);
    aout_FiltersDelete(obj, filters);
}

int main(void)
{
    const char *args[] = {"--ignore-config", "--no-plugins-cache", "--no-media-library"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance != NULL);
    vlc_object_t *obj = VLC_OBJECT(instance->p_libvlc_int);
    var_Create(obj, "audio-filter", VLC_VAR_STRING);
    var_Create(obj, "gain-value", VLC_VAR_FLOAT);
    var_Create(obj, "audio-time-stretch", VLC_VAR_BOOL);
    var_SetBool(obj, "audio-time-stretch", false);
    var_Create(obj, "audio-resampler", VLC_VAR_STRING);
    var_SetString(obj, "audio-resampler", "none");
    var_Create(obj, "audio-visual", VLC_VAR_STRING);
    var_SetString(obj, "audio-visual", "none");

    const vlc_fourcc_t codecs[] = {VLC_CODEC_FL32, VLC_CODEC_S16N};
    for (unsigned i = 0; i < ARRAY_SIZE(codecs); ++i)
    {
        Check(obj, codecs[i], "gain{value=0}", 1.f, 0.f);
        Check(obj, codecs[i], "gain{value=0.5}", 1.f, 0.5f);
        Check(obj, codecs[i], "gain{value=4}", 1.f, 4.f);
        Check(obj, codecs[i], "gain", 0.25f, 0.25f);
        Check(obj, codecs[i], "gain{value=0.5}", 0.25f, 0.5f);
        Check(obj, codecs[i], "gain{value=0.5}:gain", 0.25f, 0.125f);
        Check(obj, codecs[i], "gain{value=0.5}:gain{value=2}", 1.f, 1.f);
        Check(obj, codecs[i], "gain{value='0.5'} gain{value=2}", 1.f, 1.f);
        Check(obj, codecs[i], ": :gain{value=0.5}:: gain{value=2}:", 1.f, 1.f);
        Check(obj, codecs[i], "gain gain", 0.5f, 0.25f);
    }
    libvlc_release(instance);
    return 0;
}
