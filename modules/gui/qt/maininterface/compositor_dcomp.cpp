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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "compositor_dcomp.hpp"

#include "maininterface/mainctx_win32.hpp"

#include <vlc_window.h>

#ifndef QT_GUI_PRIVATE
#warning "QRhiD3D11 and QRhi headers are required for DirectComposition compositor."
#endif

#include <QtGui/qpa/qplatformnativeinterface.h>

#if __has_include(<d3d11_1.h>)
#define QRhiD3D11_ACTIVE
#include <QtGui/private/qrhid3d11_p.h>
#endif

#if __has_include(<d3d12.h>) && __has_include(<d3d12sdklayers.h>)
#include <QtGui/private/qrhid3d12_p.h>
#ifdef QRHI_D3D12_AVAILABLE
#define QRhiD3D12_ACTIVE
#endif
#endif

#if !defined(QRhiD3D11_ACTIVE) && !defined(QRhiD3D12_ACTIVE)
#warning "Neither D3D11 nor D3D12 headers are available. compositor_dcomp will not work."
#endif

#include "compositor_dcomp_acrylicsurface.hpp"
#include "compositor_dcomp_backdrop.hpp"
#include "compositor_dcomp_background.hpp"
#include "videosurface.hpp"
#include "maininterface/interface_window_handler.hpp"
#include "maininterface/video_window_handler.hpp"

#include <memory>
#include <type_traits>
#include <QThread>
#include <QScreen>
#include <QScopedValueRollback>
#include <QQmlError>

namespace vlc {

namespace {
struct PipVisualOps {
    IDCompositionDevice *device;
    IDCompositionVisual *embedded, *detached, *video, *ui, *detachedUi;
    HRESULT remove(bool pip) { return (pip ? detached : embedded)->RemoveVisual(video); }
    HRESULT add(bool pip) { return (pip ? detached : embedded)->AddVisual(video, FALSE, pip ? detachedUi : ui); }
    HRESULT commit() { return device->Commit(); }
};

// Called only on this window's render thread, after presentation. A UI visual
// from another QRhi/DComp device cannot be reparented into our video tree, but
// its composition swapchain can be used as content by a visual on our device.
Microsoft::WRL::ComPtr<IUnknown> pipUiSwapchain(QQuickWindow *window)
{
    const auto rhi = window->rhi();
    const auto swapchain = window->swapChain();
    if (!rhi || !swapchain) return {};
#ifdef QRhiD3D11_ACTIVE
    if (rhi->backend() == QRhi::D3D11)
        return static_cast<QD3D11SwapChain*>(swapchain)->swapChain;
#endif
#ifdef QRhiD3D12_ACTIVE
    if (rhi->backend() == QRhi::D3D12)
        return static_cast<QD3D12SwapChain*>(swapchain)->swapChain;
#endif
    return {};
}
}

bool CompositorDirectComposition::requestDetachedPip(bool enabled)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_pipClosing || m_pipTransition || !m_dcompDevice || !m_videoEnabled || !m_videoSurfaceProvider)
        return false;
    QScopedValueRollback<bool> transition(m_pipTransition, true);
    if (enabled == m_pipTransfer.detached()) return m_pipTransfer.healthy();
    if (!m_detachedPipOptIn || !m_pipTransfer.healthy()) return false;
    if (enabled)
    {
        if (m_quickView->windowStates() & Qt::WindowFullScreen) return false;
        // Reuse the engine: VLC's singleton instances belong to the main QML
        // engine. This independent view has its own RHI, not a second player.
        m_pipWindow = std::make_unique<QQuickView>(m_quickView->engine(), nullptr);
        // Independent top-level HWND; never parent/transient-parent it to main.
        // In particular never change the main RHI window's native flags.
        m_pipWindow->setFlags(Qt::Window | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
        m_pipWindow->setTitle(qtr("VLC — Picture in Picture (Esc to return)"));
        m_pipWindow->setColor(Qt::transparent);
        m_pipWindow->setResizeMode(QQuickView::SizeRootObjectToView);
        m_pipWindow->setMinimumSize(QSize(320, 180));
        m_pipWindow->resize(480, 270);
        if (const auto screen = m_quickView->screen())
        {
            m_pipWindow->setScreen(screen);
            const auto available = screen->availableGeometry();
            m_pipWindow->setPosition(available.right() - m_pipWindow->width() - 16,
                                     available.bottom() - m_pipWindow->height() - 16);
        }
        MainCtxWin32::createWindowWithoutRedirectionSurfaceImpl(m_pipWindow.get());
        m_pipWindow->setInitialProperties({{QStringLiteral("hostWindow"), QVariant::fromValue(static_cast<QWindow*>(m_pipWindow.get()))}});
        m_pipWindow->setSource(QUrl(QStringLiteral("qrc:/qt/qml/VLC/Player/DetachedPipWindow.qml")));
        if (m_pipWindow->status() == QQuickView::Error)
        {
            for (const auto &error : m_pipWindow->errors())
                msg_Err(m_intf, "Cannot load PiP controls: %s", qtu(error.toString()));
            destroyDetachedPip();
            return false;
        }
        HRESULT hr = m_dcompDevice->CreateTargetForHwnd(
            // Qt owns the FALSE target. Our TRUE target contains both video
            // and a mirror of the transparent UI swapchain, in one visual tree.
            reinterpret_cast<HWND>(m_pipWindow->winId()), TRUE, &m_pipTarget);
        if (SUCCEEDED(hr)) hr = m_dcompDevice->CreateVisual(&m_pipRoot);
        if (SUCCEEDED(hr)) hr = m_dcompDevice->CreateVisual(&m_pipUiVisual);
        if (SUCCEEDED(hr)) hr = m_pipRoot->AddVisual(m_pipUiVisual.Get(), FALSE, nullptr);
        if (SUCCEEDED(hr)) hr = m_pipTarget->SetRoot(m_pipRoot.Get());
        if (FAILED(hr))
        {
            msg_Err(m_intf, "Cannot create detached PiP target: 0x%lX", hr);
            destroyDetachedPip();
            return false;
        }
        const QPointer<QWindow> host = m_pipWindow.get();
        connect(m_pipWindow.get(), &QQuickWindow::sceneGraphError, this,
                [this, host]() {
                    if (host && host == m_pipWindow.get() && !m_pipClosing)
                        requestDetachedPip(false);
                }, Qt::QueuedConnection);
        // Capture a strong COM reference on the render thread, where QRhi is
        // valid. Only GUI-thread code changes the video composition tree. Also
        // rebind if Qt replaces its swapchain after a resize/device transition.
        auto lastContent = std::make_shared<Microsoft::WRL::ComPtr<IUnknown>>();
        connect(m_pipWindow.get(), &QQuickWindow::frameSwapped, this,
                [this, host, view = m_pipWindow.get(), lastContent]() {
                    auto content = pipUiSwapchain(view);
                    if (content && content.Get() == lastContent->Get()) return;
                    *lastContent = content;
                    QMetaObject::invokeMethod(this, [this, host, content]() {
                        if (!host || host != m_pipWindow.get() || m_pipClosing || !m_pipUiVisual) return;
                        HRESULT result = content ? m_pipUiVisual->SetContent(content.Get()) : E_NOINTERFACE;
                        if (SUCCEEDED(result)) result = m_dcompDevice->Commit();
                        if (FAILED(result))
                        {
                            msg_Err(m_intf, "Cannot compose PiP controls: 0x%lX", result);
                            requestDetachedPip(false);
                        }
                    }, Qt::QueuedConnection);
                }, Qt::DirectConnection);
        m_pipWindow->installEventFilter(this);
        connect(m_pipWindow.get(), &QWindow::screenChanged, this,
                [this]() { updateDetachedPipGeometry(); });
        // This waits for any direct render-thread geometry callback to finish.
        // Subsequent embedded updates are cached, never applied to detached video.
        m_videoSurfaceProvider->setExternalSurface(true);
        if (m_videoBackdrop) m_videoBackdrop->setSuspended(true);
    }
    PipVisualOps ops{m_dcompDevice, m_rootVisual.Get(), m_pipRoot.Get(),
                     m_videoVisual.Get(), m_uiVisual, m_pipUiVisual.Get()};
    if (!m_pipTransfer.move(ops, enabled))
    {
        msg_Err(m_intf, "Detached PiP transfer failed (rollback healthy: %d)", m_pipTransfer.healthy());
        if (!m_pipTransfer.healthy())
        {
            // Ownership is no longer trustworthy. Keep the HWND/target alive
            // until orderly vout disable; do not pretend an unsafe rollback won.
            if (m_mainCtx) m_mainCtx->askToQuit();
            return false;
        }
        if (enabled)
        {
            destroyDetachedPip();
            if (m_videoBackdrop) m_videoBackdrop->setSuspended(false);
            m_videoSurfaceProvider->setExternalSurface(false);
        }
        return false;
    }
    if (enabled)
    {
        m_quickView->setProperty("__vlc_detached_pip", true);
        updateDetachedPipGeometry();
        m_pipWindow->show();
        m_pipWindow->requestActivate();
    }
    else
    {
        m_quickView->setProperty("__vlc_detached_pip", false);
        destroyDetachedPip();
        if (m_videoBackdrop) m_videoBackdrop->setSuspended(false);
        // Replay the latest embedded geometry, not the pre-detach snapshot.
        m_videoSurfaceProvider->setExternalSurface(false);
        // Only the successful return path may consume deferred VIDEO topmost.
        if (m_videoWindowHandler) m_videoWindowHandler->restoreVideoOnTop();
    }
    if (m_mainCtx)
    {
        m_mainCtx->setDetachedPipActive(enabled);
        if (!enabled) m_mainCtx->emitRaise();
    }
    return true;
}

void CompositorDirectComposition::updateDetachedPipGeometry()
{
    if (m_pipClosing || !m_pipWindow || !m_pipTransfer.detached() || !m_videoSurfaceProvider)
        return;
    RECT rect{};
    if (GetClientRect(reinterpret_cast<HWND>(m_pipWindow->winId()), &rect))
        m_videoSurfaceProvider->reportExternalSurface(
            QSizeF(rect.right - rect.left, rect.bottom - rect.top), m_pipWindow->devicePixelRatio());
}

void CompositorDirectComposition::destroyDetachedPip()
{
    if (m_quickView) m_quickView->setProperty("__vlc_detached_pip", false);
    // Release the target before its native HWND (and before Qt's device).
    if (m_pipTarget) m_pipTarget->SetRoot(nullptr);
    if (m_pipRoot && m_videoVisual) m_pipRoot->RemoveVisual(m_videoVisual.Get());
    if (m_pipUiVisual) m_pipUiVisual->SetContent(nullptr);
    if (m_pipRoot && m_pipUiVisual) m_pipRoot->RemoveVisual(m_pipUiVisual.Get());
    if (m_dcompDevice && m_pipTarget) m_dcompDevice->Commit();
    m_pipTarget.Reset();
    m_pipRoot.Reset();
    m_pipUiVisual.Reset();
    if (m_pipWindow) m_pipWindow->removeEventFilter(this);
    m_pipWindow.reset();
    m_pipTransfer = {};
    if (m_mainCtx) m_mainCtx->setDetachedPipActive(false);
}

void CompositorDirectComposition::windowSetFullscreen(const char *)
{
    QMetaObject::invokeMethod(this, [this]() {
        if (m_pipClosing || !m_videoEnabled) return;
        if (m_pipTransfer.detached() && !requestDetachedPip(false)) return;
        CompositorVideo::windowSetFullscreen(nullptr);
    }, Qt::QueuedConnection);
}

void CompositorDirectComposition::windowResize(unsigned width, unsigned height)
{
    QMetaObject::invokeMethod(this, [this, width, height]() {
        // Decoder autoresize belongs to the embedded window only. The native
        // PiP client rectangle is authoritative while video is detached.
        if (!m_pipClosing && !m_pipTransfer.detached() && m_videoEnabled)
            CompositorVideo::windowResize(width, height);
    }, Qt::QueuedConnection);
}

int CompositorDirectComposition::windowEnable(const vlc_window_cfg_t *)
{
    if (m_pipClosing || !m_dcompDevice || !m_rootVisual || !m_videoVisual)
        return VLC_EGENERIC;
    assert(m_dcompDevice);
    assert(m_rootVisual);
    assert(m_videoVisual);
    assert(m_uiVisual);

    commonWindowEnable();

    const auto ret = m_rootVisual->AddVisual(m_videoVisual.Get(), FALSE, m_uiVisual);
    if (ret == S_OK && m_videoBackdrop)
    {
        m_videoBackdrop->setSuspended(false);
        m_videoBackdrop->setEnabled(true);
    }
    m_dcompDevice->Commit();

    m_videoEnabled = ret == S_OK;
    if (m_videoEnabled) m_videoSurfaceProvider->setExternalSurface(false);
    if (ret == S_OK)
        return VLC_SUCCESS;
    else
        return VLC_EGENERIC;
}

void CompositorDirectComposition::windowDisable()
{
    m_videoEnabled = false;
    // Stop render-thread geometry before releasing a target or visual.
    if (m_videoSurfaceProvider) m_videoSurfaceProvider->setExternalSurface(true);
    destroyDetachedPip();

    if (m_videoBackdrop)
        m_videoBackdrop->setEnabled(false);
    commonWindowDisable();

    if (!m_dcompDevice || !m_rootVisual || !m_videoVisual) return;
    const auto ret = m_rootVisual->RemoveVisual(m_videoVisual.Get());
    // It may have been detached, or a failed rollback may have lost its parent.
    (void)ret;
    m_dcompDevice->Commit();
}

void CompositorDirectComposition::windowDestroy()
{
    // windowDestroy is called on a vout thread; native HWNDs belong to GUI.
    const auto releaseHost = [this]() {
        if (m_videoSurfaceProvider) m_videoSurfaceProvider->setExternalSurface(true);
        destroyDetachedPip();
    };
    if (m_detachedPipOptIn)
    {
        if (QThread::currentThread() == thread()) releaseHost();
        else QMetaObject::invokeMethod(this, releaseHost, Qt::BlockingQueuedConnection);
    }
    m_videoProxy.Reset();
    m_videoVisual.Reset();
    CompositorVideo::windowDestroy();
}

CompositorDirectComposition::CompositorDirectComposition( qt_intf_t* p_intf,  QObject *parent)
    : CompositorVideo(p_intf, parent)
{
}

CompositorDirectComposition::~CompositorDirectComposition()
{
    qApp->removeNativeEventFilter(this);
    cleanup();

    //m_acrylicSurface should be released before the RHI context is destroyed
    assert(!m_acrylicSurface);
}

bool CompositorDirectComposition::init()
{
#if !defined(QRhiD3D11_ACTIVE) && !defined(QRhiD3D12_ACTIVE)
    msg_Warn(m_intf, "compositor_dcomp was not built with D3D11 or D3D12 headers. It will not work.");
    return false;
#endif

    {
        const QString& platformName = qApp->platformName();
        if (!(platformName == QLatin1String("windows") || platformName == QLatin1String("direct2d")))
            return false;
    }

    QString sceneGraphBackend = qEnvironmentVariable("QT_QUICK_BACKEND");
    if (!sceneGraphBackend.isEmpty() /* if empty, RHI is used */ &&
        sceneGraphBackend != QLatin1String("rhi"))
    {
        // No RHI means no D3D11 or D3D12, the graphics API check
        // below is only relevant when RHI is in use.
        // If QT_QUICK_BACKEND is set to software or openvg, then
        // `QQuickWindow::graphicsApi()` might still report D3D11 or
        // D3D12 until the scene graph is initialized.
        // Unlike `QQuickWindow::graphicsApi()`, `sceneGraphBackend()`
        // is only valid after the window is constructed, so instead
        // of using `QQuickWindow::sceneGraphBackend()`, simply probe
        // the environment variable.
        return false;
    }
    else
    {
        // If `QQuickWindow::setGraphicsApi()` is used with a non-RHI
        // mode such as `software`, Qt at the moment adjusts the scene
        // graph backend but not the `QQuickWindow::graphicsApi()`.
        // Until Qt does that, we need to check it here additionally:
        sceneGraphBackend = QQuickWindow::sceneGraphBackend();
        if (!sceneGraphBackend.isEmpty() && sceneGraphBackend != QLatin1String("rhi"))
            return false;
    }

    const auto graphicsApi = QQuickWindow::graphicsApi();
    if (graphicsApi != QSGRendererInterface::Direct3D11 &&
        graphicsApi != QSGRendererInterface::Direct3D12)
        return false;

    if (Q_UNLIKELY(qEnvironmentVariableIntValue("QT_D3D_NO_FLIP")))
    {
        // Qt does not use Direct Composition with the legacy swapchain model.
        return false;
    }

    std::unique_ptr<std::remove_pointer_t<HMODULE>, BOOL WINAPI (*)(HMODULE)>
        dcomplib(::LoadLibraryEx(TEXT("dcomp.dll"), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), ::FreeLibrary);

    typedef HRESULT (__stdcall *DCompositionCreateDeviceFuncPtr)(
        _In_opt_ IDXGIDevice *dxgiDevice,
        _In_ REFIID iid,
        _Outptr_ void **dcompositionDevice);
    DCompositionCreateDeviceFuncPtr func = nullptr;
    if (dcomplib)
    {
        func = reinterpret_cast<DCompositionCreateDeviceFuncPtr>(
            GetProcAddress(dcomplib.get(), "DCompositionCreateDevice"));
    }

    Microsoft::WRL::ComPtr<IDCompositionDevice> device;
    if (!func || FAILED(func(nullptr, IID_PPV_ARGS(&device))))
    {
        msg_Warn(m_intf, "Can not create DCompositionDevice. CompositorDirectComposition will not work.");
        return false;
    }

    return true;
}

bool CompositorDirectComposition::setup()
{
    // WARNING: This lock is released explicitly as soon as we are ready to signal readiness:
    QMutexLocker lock(&m_setupStateLock);

    switch (m_setupState)
    {
    /* [[unlikely]] */ case SetupState::Fail:
        return false;
    /* [[unlikely]] */ case SetupState::Success:
        return true; // Already called
    /* [[likely]] */ default:
        break;
    }

    //Setup shouldn't be called once we are shutting down
    assert(m_mainCtx);
    assert(m_quickView);
    const auto rhi = m_quickView->rhi();
    assert(rhi);

    QRhiImplementation* const rhiImplementation = rhi->implementation();
    assert(rhiImplementation);
    QRhiSwapChain* const rhiSwapChain = m_quickView->swapChain();
    assert(rhiSwapChain);

    assert(m_quickView->rhi()->backend() == QRhi::D3D11 || m_quickView->rhi()->backend() == QRhi::D3D12);

    IDCompositionTarget* dcompTarget;
    if (rhi->backend() == QRhi::D3D11)
    {
#ifdef QRhiD3D11_ACTIVE
        m_dcompDevice = static_cast<QRhiD3D11*>(rhiImplementation)->dcompDevice;
        auto qswapchain = static_cast<QD3D11SwapChain*>(rhiSwapChain);
        dcompTarget = qswapchain->dcompTarget;
        m_uiVisual = qswapchain->dcompVisual;
#endif
    }
    else if (rhi->backend() == QRhi::D3D12)
    {
#ifdef QRhiD3D12_ACTIVE
        m_dcompDevice = static_cast<QRhiD3D12*>(rhiImplementation)->dcompDevice;
        auto qswapchain = static_cast<QD3D12SwapChain*>(rhiSwapChain);
        dcompTarget = qswapchain->dcompTarget;
        m_uiVisual = qswapchain->dcompVisual;
#endif
    }
    else
        Q_UNREACHABLE();

    assert(m_dcompDevice);
    assert(dcompTarget);
    assert(m_uiVisual);

    HRESULT res;
    res = m_dcompDevice->CreateVisual(&m_rootVisual);
    assert(res == S_OK);

    res = dcompTarget->SetRoot(m_rootVisual.Get());
    assert(res == S_OK);

    res = m_rootVisual->AddVisual(m_uiVisual, FALSE, NULL);
    assert(res == S_OK);

    auto background = std::make_unique<DCompOpaqueBackground>();
    res = background->initialize(m_dcompDevice, nullptr);
    if (FAILED(res))
    {
        msg_Err(m_intf, "Cannot create opaque composition background: 0x%lX", res);
        m_setupState = SetupState::Fail;
        m_setupStateCond.notify_all();
        return false;
    }
    res = background->attachBelow(m_rootVisual.Get(), m_uiVisual);
    if (FAILED(res))
    {
        msg_Err(m_intf, "Cannot attach opaque composition background: 0x%lX", res);
        m_setupState = SetupState::Fail;
        m_setupStateCond.notify_all();
        return false;
    }
    m_opaqueBackground = std::move(background);
    updateOpaqueBackground();

    m_videoBackdrop = std::make_shared<DCompVideoBackdrop>(m_dcompDevice, m_rootVisual.Get(), m_uiVisual);
    m_backdropConnection = connect(m_videoSurfaceProvider.get(), &VideoSurfaceProvider::backdropRegionsChanged,
        this, [backdrop = m_videoBackdrop](const VideoBackdropState &state) { backdrop->setRegions(state); },
        Qt::DirectConnection);
    m_videoBackdrop->setRegions(m_videoSurfaceProvider->backdropRegions());

    m_detachedPipOptIn = var_InheritBool(m_intf, "qt-detached-pip");
    if (m_detachedPipOptIn && m_mainCtx)
        m_mainCtx->setDetachedPipHandler([this](bool enabled) { return requestDetachedPip(enabled); });
    m_setupState = SetupState::Success;
    m_setupStateCond.notify_all();
    lock.unlock();

    m_dcompDevice->Commit();

    if (!m_mainCtx->hasAcrylicSurface())
    {
        if (var_InheritBool(m_intf, "qt-backdrop-blur"))
        {
            QMetaObject::invokeMethod(this, [this]() {
                if (Q_UNLIKELY(!m_dcompDevice || !interfaceMainWindow()))
                    return;

                try
                {
                    m_acrylicSurface = std::make_unique<CompositorDCompositionAcrylicSurface>(m_intf, this, m_mainCtx, m_dcompDevice);
                }
                catch (const std::exception& exception)
                {
                    if (const auto what = exception.what())
                        msg_Warn(m_intf, "%s", what);
                }
            }, Qt::QueuedConnection);
        }
    }

    return true;
}

void CompositorDirectComposition::cleanup()
{
    m_pipClosing = true;
    if (m_mainCtx) m_mainCtx->setDetachedPipHandler({});
    if (m_videoSurfaceProvider) m_videoSurfaceProvider->setExternalSurface(true);
    destroyDetachedPip();
    disconnect(m_backdropConnection);
    if (m_videoBackdrop)
        m_videoBackdrop->close();
    m_videoBackdrop.reset();
    // The vout keeps this raw handle until windowDestroy(). A closed backdrop
    // retains the real COM objects but cannot add new effects during teardown.
    m_videoVisual.Reset();
    m_acrylicSurface.reset();

    if (m_rootVisual && m_uiVisual)
    {
        // Just in case root visual deletes its children
        // when it is deleted: (Qt's UI visual should be
        // deleted by Qt itself)
        m_rootVisual->RemoveVisual(m_uiVisual);
    }

    if (m_opaqueBackground && m_rootVisual)
        m_rootVisual->RemoveVisual(m_opaqueBackground->visual());
    m_opaqueBackground.reset();
    m_rootVisual.Reset();

    // When the window receives the event `SurfaceAboutToBeDestroyed`,
    // the RHI and the RHI swap chain are going to be destroyed.
    // It should be noted that event filters receive events
    // before the watched object receives them.
    // Since these objects belong to Qt, we should clear them
    // in order to prevent potential dangling pointer dereference:
    m_dcompDevice = nullptr;
    m_uiVisual = nullptr;
}

bool CompositorDirectComposition::makeMainInterface(MainCtx* mainCtx, std::function<void(QQuickWindow*)> aboutToShowQuickWindowCallback)
{
    assert(mainCtx);
    m_mainCtx = mainCtx;

    m_quickView = std::make_unique<QQuickView>();
    const auto quickViewPtr = m_quickView.get();

    m_quickView->setResizeMode(QQuickView::SizeRootObjectToView);
    m_quickView->setColor(Qt::transparent);

    m_quickView->installEventFilter(this);
    qApp->installNativeEventFilter(this);

    connect(quickViewPtr,
            &QQuickWindow::frameSwapped, // At this stage, we can be sure that QRhi and QRhiSwapChain are valid.
            this,
            [this]() {
                if (!setup())
                {
                    // Similar to `QQuickWindow::sceneGraphError()` handling:
                    if (m_mainCtx)
                        m_mainCtx->askToQuit();
                }
            },
            Qt::SingleShotConnection);

    const bool noRedirectionSurface = MainCtxWin32::createWindowWithoutRedirectionSurfaceImpl(m_quickView.get());
    if (!noRedirectionSurface)
        qDebug() << "MainCtxWin32::createWindowWithoutRedirectionSurfaceImpl() returned false for window" << m_quickView.get();

    const bool ret = commonGUICreate(quickViewPtr, quickViewPtr, CompositorVideo::CAN_SHOW_PIP | CompositorVideo::HAS_ACRYLIC);

    if (!ret)
        return false;

    connect(quickViewPtr,
            &QQuickWindow::sceneGraphError,
            this,
            [this](QQuickWindow::SceneGraphError error, const QString &message) {
                qWarning() << "CompositorDComp: Scene Graph Error: " << error << ", Message: " << message;
                QMutexLocker lock(&m_setupStateLock);
                m_setupState = SetupState::Fail;
                m_setupStateCond.notify_all();
            }, static_cast<Qt::ConnectionType>(Qt::SingleShotConnection | Qt::DirectConnection));

    // Qt "terminates the application" by default if there is no connection made to the signal
    // QQuickWindow::sceneGraphError(). We need to do the same, because by the time the error
    // is reported, it will likely be too late (`makeMainInterface()` already returned true,
    // which is the latest point recovery is still possible) to recover from that error and
    // the interface will remain unfunctional. It was proposed to wait here until the scene
    // graph is done, but that was not changed in order not to slow down the application
    // start up.
    connect(quickViewPtr,
            &QQuickWindow::sceneGraphError,
            m_mainCtx,
            &MainCtx::askToQuit);

    if (aboutToShowQuickWindowCallback)
        aboutToShowQuickWindowCallback(quickViewPtr);

    m_quickView->setVisible(true);
    return true;
}

void CompositorDirectComposition::onSurfacePositionChanged(const QPointF& position)
{
    assert(m_videoVisual);
    assert(m_dcompDevice);

    m_videoVisual->SetOffsetX(position.x());
    m_videoVisual->SetOffsetY(position.y());
    if (m_videoBackdrop)
        m_videoBackdrop->setPosition(position);

    // WARNING: Commit is requested explicitly through `::commitSurface()` after size
    //          and position changes are applied.
}

void CompositorDirectComposition::onSurfaceSizeChanged(const QSizeF&)
{
    //N/A
}

void CompositorDirectComposition::destroyMainInterface()
{
    if (m_videoVisual)
        msg_Err(m_intf, "video surface still active while destroying main interface");

    commonIntfDestroy();

    // We still need to call this explicitly, as it seems Qt does not send
    // the `QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed` event if we
    // delete the window ourselves:
    cleanup();

    m_quickView.reset();

    // These can no longer be assumed to be valid anymore, since they are
    // managed by Qt and the window is already gone:
    assert(!m_dcompDevice);
    assert(!m_uiVisual);
}

void CompositorDirectComposition::cancelVoutWindowSetup()
{
    QMutexLocker lock(&m_setupStateLock);
    m_setupState = SetupState::Fail;
    m_setupStateCond.notify_all();
}

void CompositorDirectComposition::unloadGUI()
{
    if (m_pipTransfer.detached()) requestDetachedPip(false);
    // Even failed reattachment must not leave a host using Qt's device after
    // GUI unload. The vout's subsequent disable performs ordinary cleanup.
    if (m_pipWindow)
    {
        if (m_videoSurfaceProvider) m_videoSurfaceProvider->setExternalSurface(true);
        destroyDetachedPip();
    }
    m_pipClosing = true;
    if (m_mainCtx) m_mainCtx->setDetachedPipHandler({});
    cancelVoutWindowSetup();
    m_acrylicSurface.reset();
    m_interfaceWindowHandler.reset();

    {
        if (m_quickView) {
            disconnect(m_quickView.get(), &QQuickWindow::frameSwapped,
                       this, nullptr);
        }
    }

    //at this point we need to unload the QML content but the window still need to
    //be valid as it may still be used by the vout window.
    //we cant' just delete the qmlEngine as the QmlView as the root item is parented to the QmlView
    //setSource() to nothing will effectively destroy the root item
    if (Q_LIKELY(m_quickView))
        m_quickView->setSource(QUrl());

    commonGUIDestroy();
}

bool CompositorDirectComposition::setupVoutWindow(vlc_window_t *p_wnd, VoutDestroyCb destroyCb)
{
    if (m_wnd)
        return false;

    {
        QMutexLocker lock(&m_setupStateLock);
        while (m_setupState == SetupState::Uninitialized)
        {
            const bool ret = m_setupStateCond.wait(&m_setupStateLock);
            if (!ret)
                return false;
        }
        if (m_setupState != SetupState::Success)
        {
            return false;
        }
    }

    assert(m_dcompDevice);

    const HRESULT hr = m_dcompDevice->CreateVisual(&m_videoVisual);

    if (FAILED(hr))
    {
        msg_Err(p_wnd, "create to create DComp video visual");
        return false;
    }

    commonSetupVoutWindow(p_wnd, destroyCb);
    p_wnd->type = VLC_WINDOW_TYPE_DCOMP;

    p_wnd->display.dcomp_device = m_dcompDevice;
    m_videoProxy.Attach(makeVideoBackdropProxy(m_videoVisual.Get(), m_videoBackdrop));
    p_wnd->handle.dcomp_visual = m_videoProxy.Get();

    return true;
}

QWindow *CompositorDirectComposition::interfaceMainWindow() const
{
    return m_quickView.get();
}

Compositor::Type CompositorDirectComposition::type() const
{
    return Compositor::DirectCompositionCompositor;
}

void CompositorDirectComposition::addVisual(IDCompositionVisual *visual)
{
    assert(visual);
    assert(m_rootVisual);
    assert(m_dcompDevice);

    HRESULT hr = m_rootVisual->AddVisual(visual, TRUE, NULL);
    if (FAILED(hr))
        msg_Err(m_intf, "failed to add visual, code: 0x%lX", hr);

    m_dcompDevice->Commit();
}

void CompositorDirectComposition::removeVisual(IDCompositionVisual *visual)
{
    assert(visual);
	assert(m_rootVisual);
	assert(m_dcompDevice);

    auto hr = m_rootVisual->RemoveVisual(visual);
    if (FAILED(hr))
        msg_Err(m_intf, "failed to remove visual, code: 0x%lX", hr);

    m_dcompDevice->Commit();
}

QQuickItem * CompositorDirectComposition::activeFocusItem() const /* override */
{
    return m_quickView->activeFocusItem();
}

bool CompositorDirectComposition::eventFilter(QObject *watched, QEvent *event)
{
    if (m_pipWindow && watched == m_pipWindow.get())
    {
        if (event->type() == QEvent::Close)
        {
            event->ignore();
            // Never delete a QWindow in the middle of dispatching its event.
            const QPointer<QWindow> host = m_pipWindow.get();
            QMetaObject::invokeMethod(this, [this, host]() {
                if (host && host == m_pipWindow.get() && !m_pipClosing && m_pipTransfer.detached())
                    requestDetachedPip(false);
            }, Qt::QueuedConnection);
            return true;
        }
        // QML consumes button/slider keys before forwarding unhandled hotkeys;
        // Escape first dismisses its subtitle popup, then returns to the player.
        if (event->type() == QEvent::Resize || event->type() == QEvent::DevicePixelRatioChange)
            updateDetachedPipGeometry();
    }
    switch (event->type())
    {
    case QEvent::Resize:
    case QEvent::DevicePixelRatioChange:
        if (watched == m_quickView.get())
            updateOpaqueBackground();
        break;
    case QEvent::PlatformSurface:
        if (watched == m_quickView.get() &&
            static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
        {
            cleanup();
        }
        break;
    default:
        break;
    }

    return QObject::eventFilter(watched, event);
}

bool CompositorDirectComposition::nativeEventFilter(const QByteArray &, void *message, qintptr *)
{
    const auto *event = static_cast<MSG *>(message);
    if (event->message == WM_SIZE && m_quickView && m_quickView->handle() &&
        event->hwnd == reinterpret_cast<HWND>(m_quickView->winId()))
        updateOpaqueBackground();
    return false;
}

void CompositorDirectComposition::updateOpaqueBackground()
{
    if (!m_opaqueBackground || !m_quickView)
        return;
    RECT client{};
    // Native pixels include the last row/column even when Qt rounds a logical
    // window size at fractional DPI. Cover new pixels before the next UI frame.
    if (GetClientRect(reinterpret_cast<HWND>(m_quickView->winId()), &client) &&
        m_opaqueBackground->setSize({client.right - client.left, client.bottom - client.top}) == S_OK)
        m_dcompDevice->Commit();
}

void CompositorDirectComposition::commitSurface()
{
    assert(m_dcompDevice);
    m_dcompDevice->Commit();
}

}
