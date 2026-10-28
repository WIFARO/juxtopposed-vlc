// SPDX-License-Identifier: GPL-2.0-or-later
// Optional ctypes bridge for the production transfer transaction, not a mock.
#include "maininterface/dcomp_visual_transfer.hpp"
__declspec(dllexport) int vlc_dcomp_test_move(struct vlc_dcomp_transfer *state,
    const struct vlc_dcomp_transfer_ops *ops, int detached)
{
    return vlc_dcomp_move(state, ops, detached);
}
