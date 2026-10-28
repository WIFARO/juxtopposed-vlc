// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_DCOMP_VISUAL_TRANSFER_HPP
#define VLC_DCOMP_VISUAL_TRANSFER_HPP
// Portable transaction core. Callers serialize geometry before invoking it.
// HRESULTs are signed 32-bit values; nonnegative means success.
struct vlc_dcomp_transfer { int detached; int healthy; };
struct vlc_dcomp_transfer_ops {
    void *context;
    int (*remove)(void *, int);
    int (*add)(void *, int);
    int (*commit)(void *);
};
static inline int vlc_dcomp_move(struct vlc_dcomp_transfer *s,
                                const struct vlc_dcomp_transfer_ops *o, int detached)
{
    if (!s->healthy) return 0;
    if (s->detached == detached) return 1;
    if (o->remove(o->context, s->detached) < 0) return 0;
    if (o->add(o->context, detached) < 0) {
        s->healthy = o->add(o->context, s->detached) >= 0;
        if (o->commit(o->context) < 0) s->healthy = 0;
        return 0;
    }
    if (o->commit(o->context) < 0) {
        s->healthy = o->remove(o->context, detached) >= 0;
        if (s->healthy) s->healthy = o->add(o->context, s->detached) >= 0;
        if (o->commit(o->context) < 0) s->healthy = 0;
        return 0;
    }
    s->detached = detached;
    return 1;
}
#ifdef __cplusplus
namespace vlc {
class DCompVisualTransfer {
    vlc_dcomp_transfer state{0, 1};
public:
    bool detached() const { return state.detached; }
    bool healthy() const { return state.healthy; }
    template<class Ops> bool move(Ops& ops, bool detached) {
        const vlc_dcomp_transfer_ops callbacks{
            &ops,
            [](void *p, int d) -> int { return static_cast<Ops*>(p)->remove(d); },
            [](void *p, int d) -> int { return static_cast<Ops*>(p)->add(d); },
            [](void *p) -> int { return static_cast<Ops*>(p)->commit(); }
        };
        return vlc_dcomp_move(&state, &callbacks, detached);
    }
};
}
#endif
#endif
