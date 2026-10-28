// SPDX-License-Identifier: GPL-2.0-or-later
// Portable ownership tests: no Qt, HWND or decoder required.
#include "maininterface/dcomp_visual_transfer.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>

struct Tree
{
    int owner = 0; // 0 embedded, 1 detached, -1 unparented
    int failAdd = -2;
    int failCommit = 0;
    int commits = 0;
    std::int32_t remove(bool detached)
    {
        if (owner != int(detached)) return -1;
        owner = -1;
        return 0;
    }
    std::int32_t add(bool detached)
    {
        if (owner != -1 || failAdd == int(detached) || failAdd == 2) return -2;
        owner = int(detached);
        return 0;
    }
    std::int32_t commit()
    {
        ++commits;
        if (failCommit) { --failCommit; return -3; }
        return 0;
    }
};
int main()
{
    Tree tree;
    vlc::DCompVisualTransfer transfer;
    assert(!transfer.detached());
    for (int i = 0; i < 32; ++i)
    {
        assert(transfer.move(tree, true));
        assert(transfer.detached() && tree.owner == 1);
        assert(transfer.move(tree, false));
        assert(!transfer.detached() && tree.owner == 0);
    }
    const auto commits = tree.commits;
    assert(transfer.move(tree, false));
    assert(tree.commits == commits); // idempotent, no native mutation
    tree.failAdd = 1;
    assert(!transfer.move(tree, true));
    assert(tree.owner == 0 && !transfer.detached() && transfer.healthy());
    tree.failAdd = -2;
    tree.failCommit = 1;
    assert(!transfer.move(tree, true));
    assert(tree.owner == 0 && !transfer.detached() && transfer.healthy());
    tree.failAdd = 2;
    assert(!transfer.move(tree, true));
    assert(!transfer.healthy());
    const auto failedCommits = tree.commits;
    assert(!transfer.move(tree, false));
    assert(tree.commits == failedCommits);
    std::cout << "ownership roundtrips/idempotence PASS\n";
}
