/*****************************************************************************
 * Copyright (C) 2020 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#include "video_window_handler.hpp"
#include "mainctx.hpp"
#include "util/video_window_geometry.hpp"

#include <QApplication>
#include <QScreen>
#include <algorithm>

#include <vlc_window.h>
#include "compositor.hpp"

VideoWindowHandler::VideoWindowHandler(qt_intf_t* intf, QObject *parent)
    : QObject(parent)
    , m_intf(intf)
{
    assert(qGuiApp);
    m_hasWayland = qGuiApp->platformName().startsWith(QLatin1String("wayland"));

    connect( this, &VideoWindowHandler::askVideoToResize,
             this, &VideoWindowHandler::setVideoSize, Qt::QueuedConnection );
    connect( this, &VideoWindowHandler::askVideoSetFullScreen,
             this, &VideoWindowHandler::setVideoFullScreen, Qt::QueuedConnection );
    connect( this, &VideoWindowHandler::askVideoOnTop,
             this, &VideoWindowHandler::setVideoOnTop, Qt::QueuedConnection );
}

void VideoWindowHandler::setWindow(QWindow* window)
{
    if (m_window == window)
        return;
    if (m_window)
    {
        WindowStateHolder::holdOnTop(m_window, WindowStateHolder::VIDEO, false);
        WindowStateHolder::holdFullscreen(m_window, WindowStateHolder::VIDEO, false);
    }

    m_deferredVideoOnTop.clear();
    m_window = window;
}

void VideoWindowHandler::disable()
{
    emit askVideoSetFullScreen( false );
    // Clear on the GUI thread, after the queued VIDEO hold release.
    QMetaObject::invokeMethod(this, [this]() {
        setVideoOnTop(false);
        m_deferredVideoOnTop.clear();
    }, Qt::QueuedConnection);
}

void VideoWindowHandler::requestResizeVideo( unsigned i_width, unsigned i_height )
{
    if (!m_window)
        return;
    emit askVideoToResize( i_width, i_height, m_window->windowStates() );
}

void VideoWindowHandler::requestVideoWindowed( )
{
    /* The user left fullscreen from the video side (double-click, F, Esc): leave it
     * entirely, also when it was entered from the interface (button, F11). Only this
     * user path does it; disable() (the video closing) keeps interface fullscreen. */
    QMetaObject::invokeMethod(this, [this]() {
        setVideoFullScreen( false );
        if (m_window)
            WindowStateHolder::holdFullscreen( m_window, WindowStateHolder::INTERFACE, false );
    }, Qt::QueuedConnection);
}

void VideoWindowHandler::requestVideoFullScreen(const char * )
{
    emit askVideoSetFullScreen( true );
}

void VideoWindowHandler::requestVideoState(  unsigned i_arg )
{
    bool on_top = (i_arg & VLC_WINDOW_STATE_ABOVE) != 0;
    emit askVideoOnTop( on_top );
}

void VideoWindowHandler::setVideoSize(unsigned int w, unsigned int h, Qt::WindowStates currentStates)
{
    if (!m_window)
        return;
    const Qt::WindowStates states = m_window->windowStates();
    // A pre-detach queued autoresize must not resize the main RHI window while
    // the independent desktop host owns the video geometry.
    if (m_window->property("__vlc_detached_pip").toBool())
        return;

    // A queued video resize must not change a fullscreen/maximized window or
    // replace geometry restored during a window-state transition.
    if (currentStates != states || (states & (Qt::WindowFullScreen | Qt::WindowMaximized)))
        return;
    if (!var_InheritBool(m_intf, "qt-video-autoresize"))
        return;

    const qreal factor = m_window->devicePixelRatio();
    const QSize videoSize(qRound(w / factor), qRound(h / factor));
    if (videoSize.isEmpty())
        return;

    QSize chrome(0, 0);
    assert(m_intf && m_intf->p_compositor);
    if (m_window == m_intf->p_compositor->interfaceMainWindow())
    {
        const auto quickWindow = m_intf->p_compositor->quickWindow();
        if (quickWindow)
        {
            const auto content = quickWindow->contentItem();
            QQuickItem *area = nullptr;
            if (const auto player = content->findChild<QQuickItem*>(QStringLiteral("redesignPlayer")))
            {
                // The floating player belongs to the library layout. Its video
                // must not resize the main library window.
                if (player->property("libraryExpanded").toBool())
                    return;
                area = player->findChild<QQuickItem*>(QStringLiteral("redesignViewport"));
            }
            if (!area)
                area = content->findChild<QQuickItem*>(QStringLiteral("playerSpecializationLoader"));
            if (area && area->width() > 0 && area->height() > 0)
            {
                const auto size = area->size().toSize();
                chrome = QSize(std::max(0, m_window->width() - size.width()),
                               std::max(0, m_window->height() - size.height()));
            }
        }
    }

    const auto size = vlc::video::fitWindowSize(videoSize, chrome, m_window->screen()->availableGeometry().size(),
                                               m_window->minimumSize(), m_window->maximumSize());
    if (size.isValid())
        m_window->resize(size);
}

void VideoWindowHandler::setVideoFullScreen( bool fs )
{
    if (!m_window)
        return;
    if (fs && m_window->property("__vlc_detached_pip").toBool())
    {
        // Also catch fullscreen requests queued before detachment. The DComp
        // controller reattaches first, then re-enters the existing path.
        if (auto compositor = dynamic_cast<vlc::CompositorVideo*>(m_intf->p_compositor.get()))
            compositor->windowSetFullscreen(nullptr);
        return;
    }
    m_videoFullScreen = fs;
    if( fs )
    {
        int numscreen = var_InheritInteger( m_intf, "qt-fullscreen-screennumber" );

        auto screenList = QApplication::screens();
        if ( numscreen >= 0 && numscreen < screenList.count() )
        {
            QRect screenres = screenList[numscreen]->geometry();
            m_lastWinScreen = m_window->screen();
            if( !m_hasWayland )
                m_window->setScreen(screenList[numscreen]);

            /* To be sure window is on proper-screen in xinerama */
            if( !screenres.contains( m_window->position() ) )
            {
                m_window->setPosition(screenres.x(), screenres.y() );
            }
        }
        WindowStateHolder::holdFullscreen(m_window,  WindowStateHolder::VIDEO, true);
    }
    else
    {
        WindowStateHolder::holdFullscreen(m_window,  WindowStateHolder::VIDEO, false);

        if( m_lastWinScreen != NULL && !m_hasWayland )
            m_window->setScreen(m_lastWinScreen);
    }
}


/* Slot to change the video always-on-top flag.
 * Emit askVideoOnTop() to invoke this from other thread. */
void VideoWindowHandler::setVideoOnTop( bool on_top )
{
    if (!m_window)
        return;
    if (m_window->property("__vlc_detached_pip").toBool())
    {
        m_deferredVideoOnTop.defer(on_top);
        return;
    }
    WindowStateHolder::holdOnTop(m_window, WindowStateHolder::VIDEO, on_top);
}

void VideoWindowHandler::restoreVideoOnTop()
{
    if (!m_window || m_window->property("__vlc_detached_pip").toBool())
        return;
    if (const auto on_top = m_deferredVideoOnTop.take())
        WindowStateHolder::holdOnTop(m_window, WindowStateHolder::VIDEO, *on_top);
}
