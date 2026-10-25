// SPDX-License-Identifier: LGPL-2.1-or-later

#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"

#include <vlc_preparser_ipc.h>

struct buffer
{
    struct VLC_VECTOR(uint8_t) bytes;
    size_t offset;
};

static ssize_t write_buffer(const void *data, size_t size, void *userdata)
{
    struct buffer *buffer = userdata;
    assert(size > 0);
    assert(vlc_vector_push_all(&buffer->bytes, data, size));
    return size;
}

static ssize_t read_buffer(void *data, size_t size, void *userdata)
{
    struct buffer *buffer = userdata;
    size = __MIN(size, buffer->bytes.size - buffer->offset);
    /* Keep successive messages in the stream until they are requested. */
    size = __MIN(size, 1);
    if (size > 0)
    {
        memcpy(data, buffer->bytes.data + buffer->offset, size);
        buffer->offset += size;
    }
    return size;
}

struct attachment
{
    const char *name;
    const void *data;
    size_t size;
};

static void serialize_attachments(struct vlc_preparser_msg_serdes *serdes,
                                  struct buffer *buffer,
                                  const struct attachment *attachments,
                                  size_t count)
{
    struct vlc_preparser_msg msg;
    vlc_preparser_msg_Init(&msg, VLC_PREPARSER_MSG_TYPE_RES,
                           VLC_PREPARSER_MSG_REQ_TYPE_PARSE);
    msg.res.status = VLC_SUCCESS;
    for (size_t i = 0; i < count; ++i)
    {
        input_attachment_t *attachment = vlc_input_attachment_New(
            attachments[i].name, "application/octet-stream", "Test attachment",
            attachments[i].data, attachments[i].size);
        assert(attachment != NULL);
        assert(vlc_vector_push(&msg.res.attachments, attachment));
    }
    assert(vlc_preparser_msg_serdes_Serialize(serdes, &msg, buffer) == VLC_SUCCESS);
    vlc_preparser_msg_Clean(&msg);
}

static void deserialize_attachments(struct vlc_preparser_msg_serdes *serdes,
                                    struct buffer *buffer,
                                    const struct attachment *attachments,
                                    size_t count)
{
    struct vlc_preparser_msg msg;
    assert(vlc_preparser_msg_serdes_Deserialize(serdes, &msg, buffer) == VLC_SUCCESS);
    assert(msg.type == VLC_PREPARSER_MSG_TYPE_RES);
    assert(msg.req_type == VLC_PREPARSER_MSG_REQ_TYPE_PARSE);
    assert(msg.res.type == VLC_PREPARSER_MSG_REQ_TYPE_PARSE);
    assert(msg.res.status == VLC_SUCCESS);
    assert(msg.res.attachments.size == count);
    for (size_t i = 0; i < count; ++i)
    {
        const input_attachment_t *attachment = msg.res.attachments.data[i];
        assert(strcmp(attachment->psz_name, attachments[i].name) == 0);
        assert(strcmp(attachment->psz_mime, "application/octet-stream") == 0);
        assert(strcmp(attachment->psz_description, "Test attachment") == 0);
        assert(attachment->i_data == attachments[i].size);
        if (attachment->i_data > 0)
            assert(memcmp(attachment->p_data, attachments[i].data,
                          attachment->i_data) == 0);
    }
    vlc_preparser_msg_Clean(&msg);
}

int main(void)
{
    test_init();
    const char *args[] = {
        "--ignore-config", "--no-plugins-cache", "--no-media-library",
        "--no-metadata-network-access",
    };
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance != NULL);

    static const struct vlc_preparser_msg_serdes_cbs cbs = {
        .write = write_buffer,
        .read = read_buffer,
    };
    vlc_object_t *obj = VLC_OBJECT(instance->p_libvlc_int);
    struct vlc_preparser_msg_serdes *writer =
        vlc_preparser_msg_serdes_Create(obj, &cbs, true);
    struct vlc_preparser_msg_serdes *reader =
        vlc_preparser_msg_serdes_Create(obj, &cbs, true);
    assert(writer != NULL && reader != NULL);

    static const uint8_t payload[] = {0, 0xff, 'A', 0, '\n', '{', '}', 0x80};
    static const struct attachment attachments[] = {
        {"empty-before", NULL, 0},
        {"payload", payload, sizeof(payload)},
        {"empty-middle", NULL, 0},
        {"second-payload", payload + 1, sizeof(payload) - 1},
        {"empty-after", NULL, 0},
    };
    static const struct {
        size_t offset;
        size_t count;
    } cases[] = {
        {0, 1}, /* A message with only an empty attachment. */
        {0, ARRAY_SIZE(attachments)}, /* Empty and binary attachments. */
        {1, 1}, /* A following message must retain its binary payload. */
        {0, 0}, /* Responses without attachments remain valid. */
    };
    struct buffer buffer = { .bytes = VLC_VECTOR_INITIALIZER };
    size_t ends[ARRAY_SIZE(cases)];
    for (size_t i = 0; i < ARRAY_SIZE(cases); ++i)
    {
        serialize_attachments(writer, &buffer, attachments + cases[i].offset,
                              cases[i].count);
        ends[i] = buffer.bytes.size;
    }
    for (size_t i = 0; i < ARRAY_SIZE(cases); ++i)
    {
        deserialize_attachments(reader, &buffer, attachments + cases[i].offset,
                                cases[i].count);
        assert(buffer.offset == ends[i]);
    }

    vlc_vector_clear(&buffer.bytes);
    vlc_preparser_msg_serdes_Delete(reader);
    vlc_preparser_msg_serdes_Delete(writer);
    libvlc_release(instance);
    return 0;
}
