# SPDX-License-Identifier: GPL-2.0-or-later
"""DComp video plus a mirrored, alpha-composited Qt Quick UI fixture.

Run on a Windows desktop with PySide6==6.8.3. This checks composition/input,
not VLC decoding or the full application's PiP lifecycle. PySide does not expose
QD3D11SwapChain::swapChain, so the UI fixture uploads Qt's actual premultiplied
render into a composition swapchain. Production shares Qt's swapchain directly;
that private-QRhi extraction still requires a compiled application test.
No global input is sent.
"""
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import sys

from test_dcomp_detached_pip_native import B, Desc, F, HR, P, U, call, check, guid


def main():
    if sys.platform != 'win32':
        raise SystemExit('Requires a real Windows desktop, not an offscreen fallback')
    from PySide6.QtCore import QPoint, Qt, QUrl
    from PySide6.QtGui import QGuiApplication, QImage
    from PySide6.QtQuick import QQuickView, QSGRendererInterface
    from PySide6.QtQml import QQmlEngine
    from PySide6.QtTest import QTest

    app = QGuiApplication([])
    def create_no_redirect(window):
        previous = os.environ.get('QT_QPA_DISABLE_REDIRECTION_SURFACE')
        os.environ['QT_QPA_DISABLE_REDIRECTION_SURFACE'] = '1'
        try:
            window.create()
        finally:
            if previous is None:
                os.environ.pop('QT_QPA_DISABLE_REDIRECTION_SURFACE', None)
            else:
                os.environ['QT_QPA_DISABLE_REDIRECTION_SURFACE'] = previous

    engine = QQmlEngine(app)
    host = QQuickView(engine, None)
    host.setFlags(Qt.Window | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint)
    host.setGeometry(60, 60, 480, 270)
    view = host
    view.setColor(Qt.transparent)
    view.setResizeMode(QQuickView.SizeRootObjectToView)
    view.resize(host.size())
    create_no_redirect(view)
    view.setInitialProperties({'started': True, 'playing': True, 'seekable': True,
                               'controlsHideTimeout': 60000, 'mediaTitle': 'PiP overlay test'})
    source = Path(__file__).resolve().parents[1] / 'player/qml/redesign/RedesignPipControls.qml'
    view.setSource(QUrl.fromLocalFile(str(source)))
    assert view.status() == QQuickView.Ready, [e.toString() for e in view.errors()]
    user = C.WinDLL('user32', use_last_error=True)
    user.GetClientRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
    user.GetWindowLongPtrW.argtypes = [W.HWND, C.c_int]
    user.GetWindowLongPtrW.restype = C.c_ssize_t
    user.GetWindow.argtypes = [W.HWND, U]
    user.GetWindow.restype = W.HWND
    assert not user.GetWindow(int(host.winId()), 4), 'host must be ownerless'
    assert user.GetWindowLongPtrW(int(host.winId()), -20) & 8, 'host must be topmost'
    assert user.GetWindowLongPtrW(int(host.winId()), -20) & 0x200000, 'redirection bitmap hides the picture'
    assert not user.GetWindowLongPtrW(int(host.winId()), -16) & 0x40000000, 'host must not be a child'
    client = W.RECT()
    assert user.GetClientRect(int(host.winId()), C.byref(client))
    d3d, dc = C.WinDLL('d3d11'), C.WinDLL('dcomp')
    d3d.D3D11CreateDevice.argtypes = [P, U, P, U, P, U, U, C.POINTER(P), P, C.POINTER(P)]
    d3d.D3D11CreateDevice.restype = HR
    dc.DCompositionCreateDevice.argtypes = [P, P, C.POINTER(P)]
    dc.DCompositionCreateDevice.restype = HR
    objects = []

    def out(operation, invoke):
        obj = P()
        check(invoke(C.byref(obj)), operation)
        assert obj, operation
        objects.append(obj)
        return obj

    target = device = None
    try:
        gpu, context = P(), P()
        check(d3d.D3D11CreateDevice(None, 5, None, 0x20, None, 0, 7,
                                  C.byref(gpu), None, C.byref(context)), 'WARP device')
        objects.extend([gpu, context])
        dxgi = out('DXGI device', lambda p: call(gpu, 0, (P, P),
                   (guid('54ec77fa-1377-44e6-8c32-88fd5f44c84c'), p)))
        adapter = out('adapter', lambda p: call(dxgi, 7, (P,), (p,)))
        factory = out('factory', lambda p: call(adapter, 6, (P, P),
                      (guid('50c83a1c-e072-4c48-87b0-3630fa36a6d0'), p)))
        desc = Desc(client.right, client.bottom, 87, 0, 1, 0, 0x20, 2, 0, 3, 3, 0)
        chain = out('swapchain', lambda p: call(factory, 24, (P, P, P, P), (gpu, C.byref(desc), None, p)))
        buffer = out('buffer', lambda p: call(chain, 9, (U, P, P),
                     (0, guid('6f15aaf2-d208-4e89-9ab4-489535d34f9c'), p)))
        rtv = out('render target', lambda p: call(gpu, 9, (P, P, P), (buffer, None, p)))
        device = out('DComp device', lambda p: dc.DCompositionCreateDevice(dxgi,
                     guid('c37ea93a-e7aa-450d-b16f-9746cb0407f3'), p))
        # Qt owns the FALSE target. Our TRUE target combines video and UI in
        # one tree; a transparent child HWND would expose the desktop instead.
        target = out('combined video/UI target', lambda p: call(device, 6, (W.HWND, B, P), (int(host.winId()), 1, p)))
        root_visual = out('root', lambda p: call(device, 7, (P,), (p,)))
        visual = out('video visual', lambda p: call(device, 7, (P,), (p,)))
        ui_visual = out('UI mirror', lambda p: call(device, 7, (P,), (p,)))
        ui_desc = Desc(client.right, client.bottom, 87, 0, 1, 0, 0x20, 2, 0, 3, 1, 0)
        ui_chain = out('premultiplied UI swapchain', lambda p: call(factory, 24, (P, P, P, P), (gpu, C.byref(ui_desc), None, p)))
        ui_buffer = out('UI buffer', lambda p: call(ui_chain, 9, (U, P, P),
                        (0, guid('6f15aaf2-d208-4e89-9ab4-489535d34f9c'), p)))
        source_device = out('independent UI composition device', lambda p: dc.DCompositionCreateDevice(None,
                            guid('c37ea93a-e7aa-450d-b16f-9746cb0407f3'), p))
        source_visual = out('original UI visual', lambda p: call(source_device, 7, (P,), (p,)))
        check(call(source_visual, 15, (P,), (ui_chain,)), 'bind original UI content')
        check(call(source_device, 3), 'commit original UI')
        check(call(ui_visual, 15, (P,), (ui_chain,)), 'mirror the same swapchain across devices')
        check(call(root_visual, 16, (P, B, P), (ui_visual, 0, None)), 'attach UI')
        check(call(root_visual, 16, (P, B, P), (visual, 0, ui_visual)), 'attach video below UI')
        check(call(visual, 15, (P,), (chain,)), 'set video content')
        check(call(target, 3, (P,), (root_visual,)), 'set root')
        check(call(device, 3), 'commit')

        def render(color):
            for _ in range(2):
                call(context, 50, (P, P), (rtv, (F * 4)(*color)), result=None)
                check(call(chain, 8, (U, U), (0, 0)), 'present')
            check(call(device, 4), 'wait for DComp')
            QTest.qWait(250)
            image = view.grabWindow().convertToFormat(QImage.Format_ARGB32_Premultiplied)
            assert image.width() == client.right and image.height() == client.bottom, 'UI fixture size differs from native client'
            assert image.pixelColor(image.width() // 3, image.height() // 3).alpha() == 0, 'Qt UI must have a transparent picture area'
            pixels = C.create_string_buffer(image.constBits().tobytes())
            for _ in range(2):
                call(context, 48, (P, U, P, P, U, U),
                     (ui_buffer, 0, None, pixels, image.bytesPerLine(), 0), result=None)
                check(call(ui_chain, 8, (U, U), (0, 0)), 'present Qt UI fixture')
            check(call(device, 4), 'wait for UI composition')
            QTest.qWait(100)

        def capture():
            image = host.screen().grabWindow(0, host.x(), host.y(), host.width(), host.height()).toImage()
            assert not image.isNull(), 'desktop capture unavailable'
            return image

        def pixel(image, x, y):
            return image.pixelColor(round(x * image.width() / host.width()),
                                    round(y * image.height() / host.height()))

        host.show()
        view.show()
        view.requestActivate()
        render((1, 0, 0, 1))
        assert view.rendererInterface().graphicsApi() == QSGRendererInterface.Direct3D11, 'must exercise Qt RHI'
        red = capture()
        if '--screenshot' in sys.argv:
            red.save(sys.argv[sys.argv.index('--screenshot') + 1])
        center = pixel(red, 100, 100)
        assert center.red() > 240 and center.green() < 15 and center.blue() < 15, center.name()
        shade = pixel(red, 15, 250)
        assert 5 < shade.red() < 180 and shade.green() < 15, ('overlay gradient must composite over video: '
            + shade.name() + f' view={view.size()} root={view.rootObject().size()} visible={view.rootObject().property("controlsVisible")}')

        root = view.rootObject()
        toggles, moves, seeks = [], [], []
        root.togglePlayback.connect(lambda: toggles.append(True))
        root.moveRequested.connect(lambda: moves.append(True))
        root.seekRequested.connect(lambda _: seeks.append(True))
        QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, QPoint(100, 100))
        assert not toggles and root.property('playing'), 'picture click paused playback'
        from PySide6.QtQuick import QQuickItem
        play = root.findChild(QQuickItem, 'pipPlayButton')
        point = play.mapToScene(play.boundingRect().center()).toPoint()
        QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, point)
        assert len(toggles) == 1 and not moves and not seeks, 'button input reached the picture'
        render((0, 0, 1, 1))
        blue = capture()
        center = pixel(blue, 100, 100)
        assert center.blue() > 240 and center.red() < 15, 'video did not keep updating under Qt UI'
        if '--screenshot' in sys.argv:
            output = Path(sys.argv[sys.argv.index('--screenshot') + 1])
            assert blue.save(str(output)), 'screenshot could not be saved'
        print(json.dumps({'result': 'PASS', 'ownerless_topmost_host': True,
                          'qt_rhi_ui_fixture': True, 'real_video_color_updates': True,
                          'transparent_picture': True, 'controls_composited_above_video': True,
                          'picture_click_does_not_pause': True, 'control_input_isolated': True,
                          'qt_private_swapchain_extraction_validated': False,
                          'vlc_vout_validated': False}, indent=2))
    finally:
        if target and device:
            call(target, 3, (P,), (None,))
            call(device, 3)
            call(device, 4)
        for obj in reversed(objects):
            call(obj, 2, result=U)
        view.hide()
        view.setSource(QUrl())
        view.destroy()


if __name__ == '__main__':
    main()
