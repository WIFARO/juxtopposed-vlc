// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_DEFERRED_VIDEO_ON_TOP_HPP
#define VLC_DEFERRED_VIDEO_ON_TOP_HPP
#include <optional>
namespace vlc {
class DeferredVideoOnTop
{
public:
    void defer(bool value) { m_pending = value; }
    // Consume only after a successful reattach; a failed transfer keeps it pending.
    std::optional<bool> take()
    {
        const auto pending = m_pending;
        m_pending.reset();
        return pending;
    }
    void clear() { m_pending.reset(); }
private:
    std::optional<bool> m_pending;
};
}
#endif
