// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#undef NDEBUG
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <vector>
#include <vlc_common.h>
#include <vlc_modules.h>
extern "C" {
#include "../../src/input/demux-run.h"
}

using Bytes = std::vector<unsigned char>;

static void append(Bytes &destination, const Bytes &data)
{
    destination.insert(destination.end(), data.begin(), data.end());
}

static Bytes element(uint32_t id, const Bytes &data)
{
    Bytes result;
    for (int shift = 24; shift >= 0; shift -= 8)
        if ((id >> shift) != 0 || !result.empty())
            result.push_back(id >> shift);
    unsigned length = 1;
    while (data.size() >= (UINT64_C(1) << (7 * length)) - 1)
        ++length;
    const uint64_t size = data.size() | (UINT64_C(1) << (7 * length));
    for (int shift = (length - 1) * 8; shift >= 0; shift -= 8)
        result.push_back(size >> shift);
    append(result, data);
    return result;
}

static Bytes number(uint32_t id, uint32_t value)
{
    Bytes data;
    do {
        data.insert(data.begin(), value & 0xff);
        value >>= 8;
    } while (value);
    return element(id, data);
}

static Bytes text(uint32_t id, const char *value)
{
    return element(id, Bytes(value, value + strlen(value)));
}

static Bytes master(uint32_t id, std::initializer_list<Bytes> children)
{
    Bytes data;
    for (const auto &child : children)
        append(data, child);
    return element(id, data);
}

static Bytes sample(unsigned sibling)
{
    const auto header = master(0x1a45dfa3, {
        number(0x4286, 1), number(0x42f7, 1), number(0x42f2, 4),
        number(0x42f3, 8), text(0x4282, "matroska"),
        number(0x4287, 4), number(0x4285, 2) });
    const auto info = master(0x1549a966, {
        number(0x2ad7b1, 1000000),
        element(0x4489, {0x40, 0xb7, 0x70, 0, 0, 0, 0, 0}) }); // 6000ms
    const auto tracks = master(0x1654ae6b, {
        master(0xae, {number(0xd7, 1), number(0x73c5, 1),
                      number(0x83, 17), text(0x86, "S_TEXT/UTF8")}) });
    const auto cluster = master(0x1f43b675, {number(0xe7, 0),
        master(0xa0, {element(0xa1, {0x81, 0, 0, 0, 'T', 'a', 'i', 'l'}),
                      number(0x9b, 6000)}) });
    Bytes segment;
    append(segment, info);
    append(segment, tracks);
    const auto cluster_position = segment.size();
    append(segment, cluster);
    // A segment-level sibling makes the parser release both the BlockGroup
    // and its Cluster before it returns the retained subtitle block.
    if (sibling == 1)
        append(segment, master(0x1c53bb6b, {master(0xbb, {
            number(0xb3, 0), master(0xb7, {number(0xf7, 1),
                                          number(0xf1, cluster_position)})})}));
    else if (sibling == 2)
        append(segment, master(0x1254c367, {master(0x7373, {
            master(0x67c8, {text(0x45a3, "TITLE"), text(0x4487, "Tail test")})})}));
    auto result = header;
    append(result, element(0x18538067, segment));
    return result;
}

int main()
{
#ifdef TOP_BUILDDIR
    setenv("VLC_PLUGIN_PATH", TOP_BUILDDIR "/modules", 1);
    setenv("VLC_LIBEXEC_PATH", TOP_BUILDDIR "/bin", 1);
    setenv("VLC_DATA_PATH", TOP_SRCDIR "/share", 1);
#endif
    const char *options[] = {"--ignore-config", "--no-plugins-cache",
                             "--no-media-library", "--quiet"};
    libvlc_instance_t *vlc = libvlc_new(ARRAY_SIZE(options), options);
    assert(vlc != nullptr);
    if (!module_exists("mkv")) {
        libvlc_release(vlc);
        return 77;
    }
    vlc_run_args args{};
    args.name = "mkv";
    args.test_demux_controls = true;
    for (unsigned sibling = 0; sibling < 3; ++sibling) {
        const auto data = sample(sibling);
        printf("Subtitle BlockGroup tail, sibling %u\n", sibling);
        fflush(stdout);
        assert(libvlc_demux_process_memory(vlc, &args, data.data(), data.size()) == 0);
    }
    libvlc_release(vlc);
}
