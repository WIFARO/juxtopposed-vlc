// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_COMPOSITOR_X11_BACKDROP_HPP
#define VLC_QT_COMPOSITOR_X11_BACKDROP_HPP

#include "util/video_backdrop_region.hpp"
#include <xcb/render.h>
#include <memory>

namespace vlc {

// All calls and X resources belong to the compositor's render thread.
class X11VideoBackdrop
{
public:
    X11VideoBackdrop(xcb_connection_t *, xcb_drawable_t);
    ~X11VideoBackdrop();
    bool available() const;
    void setRegions(const VideoBackdropState &);
    void render(xcb_render_picture_t video, const QRect &videoRect,
                xcb_render_picture_t destination, const QSize &windowSize);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}
#endif
