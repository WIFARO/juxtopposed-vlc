// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#undef NDEBUG
#include <assert.h>
#include <vlc_common.h>
#include <vlc_block.h>
#include <vlc_sout.h>
#include <vlc/vlc.h>
#include "../../../lib/libvlc_internal.h"

struct output {
    uint8_t *bytes;
    size_t size, position;
};

static ssize_t Write(sout_access_out_t *access, block_t *blocks)
{
    struct output *out = access->p_sys;
    ssize_t written = 0;
    while (blocks != NULL)
    {
        block_t *next = blocks->p_next;
        const size_t end = out->position + blocks->i_buffer;
        if (end > out->size)
        {
            out->bytes = realloc(out->bytes, end);
            assert(out->bytes != NULL);
            memset(out->bytes + out->size, 0, end - out->size);
            out->size = end;
        }
        memcpy(out->bytes + out->position, blocks->p_buffer, blocks->i_buffer);
        out->position = end;
        written += blocks->i_buffer;
        blocks->p_next = NULL;
        block_Release(blocks);
        blocks = next;
    }
    return written;
}

static ssize_t Read(sout_access_out_t *access, block_t *block)
{
    struct output *out = access->p_sys;
    if (out->position >= out->size) return 0;
    const size_t count = __MIN(block->i_buffer, out->size - out->position);
    memcpy(block->p_buffer, out->bytes + out->position, count);
    block->i_buffer = count;
    out->position += count;
    return count;
}

static int Seek(sout_access_out_t *access, uint64_t position)
{
    struct output *out = access->p_sys;
    if (position > SIZE_MAX) return VLC_EGENERIC;
    out->position = position;
    return VLC_SUCCESS;
}

static int Control(sout_access_out_t *access, int query, va_list args)
{
    (void)access;
    if (query == ACCESS_OUT_CAN_SEEK || query == ACCESS_OUT_CONTROLS_PACE)
    {
        *va_arg(args, bool *) = query == ACCESS_OUT_CAN_SEEK;
        return VLC_SUCCESS;
    }
    return VLC_EGENERIC;
}

static void Check(libvlc_instance_t *instance, unsigned count,
                  bool faststart, unsigned prefix_size, bool known_length)
{
    struct output out = {0};
    sout_access_out_t *access = vlc_object_create(instance->p_libvlc_int, sizeof(*access));
    assert(access != NULL);
    access->psz_access = strdup("memory");
    assert(access->psz_access != NULL);
    access->psz_path = NULL;
    access->p_cfg = NULL;
    access->p_module = NULL;
    access->p_sys = &out;
    access->pf_write = Write;
    access->pf_read = Read;
    access->pf_seek = Seek;
    access->pf_control = Control;
    sout_mux_t *mux = sout_MuxNew(access, faststart ? "mp4{faststart}" : "mp4{no-faststart}");
    assert(mux != NULL);
    es_format_t format;
    es_format_Init(&format, VIDEO_ES, VLC_CODEC_H264);
    format.video.i_width = format.video.i_visible_width = 16;
    format.video.i_height = format.video.i_visible_height = 16;
    format.video.i_frame_rate = 25;
    format.video.i_frame_rate_base = 1;
    sout_input_t *input = sout_MuxAddStream(mux, &format);
    assert(input != NULL);
    mux->b_waiting_stream = false;

    const unsigned nal_size = 2;
    for (unsigned i = 0; i < count; ++i)
    {
        /* Annex B input must become a four-byte length-prefixed MP4 sample. */
        const uint8_t nal[] = {0, 0, 0, 1, 0x65, 0x80};
        block_t *block = block_Alloc(prefix_size + nal_size);
        assert(block != NULL);
        memcpy(block->p_buffer, nal + 4 - prefix_size, block->i_buffer);
        block->p_buffer[block->i_buffer - 1] = 0x80 + i;
        block->i_pts = block->i_dts = VLC_TICK_0 + VLC_TICK_FROM_MS(40 * i);
        block->i_length = known_length ? VLC_TICK_FROM_MS(40) : 0;
        block->i_flags = BLOCK_FLAG_TYPE_I;
        assert(sout_MuxSendBuffer(mux, input, block) == VLC_SUCCESS);
    }
    vlc_fifo_Lock(input->p_fifo);
    assert(vlc_fifo_GetCount(input->p_fifo) == 1);
    vlc_fifo_Unlock(input->p_fifo);
    sout_MuxDeleteStream(mux, input);
    sout_MuxDelete(mux);
    sout_AccessOutDelete(access);
    es_format_Clean(&format);

    bool found = false;
    for (size_t at = 0; at + 8 <= out.size;)
    {
        const uint32_t size = GetDWBE(out.bytes + at);
        assert(size >= 8 && size <= out.size - at);
        if (!memcmp(out.bytes + at + 4, "mdat", 4))
        {
            assert(size == 8 + count * (4 + nal_size));
            const uint8_t *sample = out.bytes + at + 8;
            for (unsigned i = 0; i < count; ++i, sample += 4 + nal_size)
            {
                assert(GetDWBE(sample) == nal_size);
                assert(sample[4] == 0x65);
                assert(sample[4 + nal_size - 1] == 0x80 + i);
            }
            found = true;
        }
        at += size;
    }
    assert(found);
    bool timed = false;
    for (size_t at = 4; at + 20 <= out.size; ++at)
        if (!memcmp(out.bytes + at, "stts", 4))
        {
            const uint32_t entries = GetDWBE(out.bytes + at + 8);
            assert(entries > 0 && entries <= (out.size - at - 12) / 8);
            unsigned samples = 0;
            for (unsigned i = 0; i < entries; ++i)
            {
                samples += GetDWBE(out.bytes + at + 12 + 8 * i);
                assert(GetDWBE(out.bytes + at + 16 + 8 * i) > 0);
            }
            assert(samples == count);
            timed = true;
        }
    assert(timed);
    free(out.bytes);
}

int main(void)
{
    const char *args[] = {"--ignore-config", "--no-plugins-cache", "--no-media-library"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance != NULL);
    for (unsigned prefix_size = 3; prefix_size <= 4; ++prefix_size)
        for (unsigned count = 1; count <= 3; count += 2)
            for (unsigned faststart = 0; faststart < 2; ++faststart)
                for (unsigned known_length = 0; known_length < 2; ++known_length)
                    Check(instance, count, faststart, prefix_size, known_length);
    libvlc_release(instance);
    return 0;
}
