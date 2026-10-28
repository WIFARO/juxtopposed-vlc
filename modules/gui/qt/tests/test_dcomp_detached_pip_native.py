# SPDX-License-Identifier: GPL-2.0-or-later
"""Real Windows/WARP DirectComposition transfer spike; no VLC or Qt mocks.

Run: python modules/gui/qt/tests/test_dcomp_detached_pip_native.py
Add --qt under a PySide6 environment to use real Qt-owned QWindow HWNDs.
Uses Microsoft WinSDK COM signatures (dcomp.h, d3d11.h, dxgi1_2.h).
This proves only the native mechanism, not Qt-owned QRhi or VLC integration.
"""
import ctypes as C
from ctypes import wintypes as W
from typing import Any
import json
import sys
import uuid
import os

P = C.c_void_p
HR = C.c_long
U = C.c_uint
B = C.c_int
F = C.c_float


def guid(text):
    return (C.c_ubyte * 16).from_buffer_copy(uuid.UUID(text).bytes_le)


def call(obj, index, argtypes=(), args=(), result: Any = HR):
    table = C.cast(obj, C.POINTER(C.POINTER(P))).contents
    return C.WINFUNCTYPE(result, P, *argtypes)(table[index])(obj, *args)


def check(value, operation):
    if value < 0:
        raise AssertionError(f"{operation}: HRESULT 0x{value & 0xffffffff:08x}")
    return value


class Desc(C.Structure):
    _fields_ = [(n, U) for n in ('width', 'height', 'format', 'stereo',
        'samples', 'quality', 'usage', 'buffers', 'scaling', 'effect', 'alpha', 'flags')]


class TextureDesc(C.Structure):
    _fields_ = [(n, U) for n in ('width', 'height', 'mips', 'arrays', 'format',
        'samples', 'quality', 'usage', 'bind', 'cpu', 'misc')]


class Mapped(C.Structure):
    _fields_ = [('data', P), ('row_pitch', U), ('depth_pitch', U)]


def main():
    if sys.platform != 'win32':
        raise SystemExit('Native test requires Windows; no simulated fallback')
    app = None
    Qt: Any = None
    QWindow: Any = None
    qt_windows = []
    if '--qt' in sys.argv:
        from importlib import import_module
        Qt = import_module('PySide6.QtCore').Qt
        gui = import_module('PySide6.QtGui')
        QWindow = gui.QWindow
        app = gui.QGuiApplication([])
    user = C.WinDLL('user32', use_last_error=True)
    ole = C.WinDLL('ole32')
    d3d = C.WinDLL('d3d11')
    dc = C.WinDLL('dcomp')
    ole.CoInitializeEx.argtypes = [P, U]
    ole.CoInitializeEx.restype = HR
    check(ole.CoInitializeEx(None, 2 if app else 0), 'COM initialization')
    user.CreateWindowExW.argtypes = [W.DWORD, W.LPCWSTR, W.LPCWSTR, W.DWORD,
        C.c_int, C.c_int, C.c_int, C.c_int, W.HWND, P, P, P]
    user.CreateWindowExW.restype = W.HWND
    user.DestroyWindow.argtypes = [W.HWND]
    user.ShowWindow.argtypes = [W.HWND, C.c_int]
    user.GetWindow.argtypes = [W.HWND, U]
    user.GetWindow.restype = W.HWND
    user.GetWindowLongPtrW.argtypes = [W.HWND, C.c_int]
    user.GetWindowLongPtrW.restype = C.c_ssize_t
    user.IsIconic.argtypes = [W.HWND]
    user.IsWindowVisible.argtypes = [W.HWND]
    user.IsWindow.argtypes = [W.HWND]
    user.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, U]
    d3d.D3D11CreateDevice.argtypes = [P, U, P, U, P, U, U, C.POINTER(P), P, C.POINTER(P)]
    d3d.D3D11CreateDevice.restype = HR
    dc.DCompositionCreateDevice.argtypes = [P, P, C.POINTER(P)]
    dc.DCompositionCreateDevice.restype = HR
    objects, windows = [], []

    def out(operation, invoke):
        obj = P()
        check(invoke(C.byref(obj)), operation)
        if not obj:
            raise AssertionError(operation + ': null result')
        objects.append(obj)
        return obj

    def cv(obj, index, types=(), args=(), label='COM operation'):
        return check(call(obj, index, types, args), label)

    try:
        gpu, context = P(), P()
        check(d3d.D3D11CreateDevice(None, 5, None, 0x20, None, 0, 7,
            C.byref(gpu), None, C.byref(context)), 'WARP device')
        objects.extend([gpu, context])
        dxgi = out('IDXGIDevice', lambda p: call(gpu, 0, (P, P),
            (guid('54ec77fa-1377-44e6-8c32-88fd5f44c84c'), p)))
        adapter = out('adapter', lambda p: call(dxgi, 7, (P,), (p,)))
        factory = out('factory', lambda p: call(adapter, 6, (P, P),
            (guid('50c83a1c-e072-4c48-87b0-3630fa36a6d0'), p)))
        desc = Desc(64, 64, 87, 0, 1, 0, 0x20, 2, 0, 3, 3, 0)
        chain = out('composition swapchain', lambda p: call(factory, 24,
            (P, P, P, P), (gpu, C.byref(desc), None, p)))
        buffer = out('render buffer', lambda p: call(chain, 9, (U, P, P),
            (0, guid('6f15aaf2-d208-4e89-9ab4-489535d34f9c'), p)))
        view = out('render target', lambda p: call(gpu, 9, (P, P, P), (buffer, None, p)))
        # Only flip-model buffer zero is renderable. Present advances its
        # backing storage, so clear/present twice to initialize both buffers.
        for _ in range(2):
            call(context, 50, (P, P), (view, (F * 4)(1, 0, 0, 1)), result=None)
            cv(chain, 8, (U, U), (0, 0), 'present real rendered red frame')
        device = out('DComp device', lambda p: dc.DCompositionCreateDevice(dxgi,
            guid('c37ea93a-e7aa-450d-b16f-9746cb0407f3'), p))
        for i in range(2):
            if app:
                window = QWindow()
                window.setFlags(Qt.Window | (Qt.WindowStaysOnTopHint if i else Qt.WindowFlags()))
                window.setGeometry(40 + 220 * i, 40, 160, 120)
                window.create()
                hwnd = int(window.winId())
                qt_windows.append(window)
            else:
                hwnd = user.CreateWindowExW(0x00200000 | (8 if i else 0),
                    'STATIC', 'Native DComp transfer spike', 0x80000000,
                    40 + 220 * i, 40, 160, 120, None, None, None, None)
            if not hwnd:
                raise C.WinError(C.get_last_error())
            windows.append(hwnd)
        assert windows[0] != windows[1]
        assert not user.GetWindow(windows[1], 4), 'PiP must be ownerless'
        assert user.GetWindowLongPtrW(windows[1], -20) & 8, 'PiP must be topmost'
        targets = [out('native target', lambda p, h=h: call(device, 6,
            (W.HWND, B, P), (h, 1, p))) for h in windows]
        roots = [out('visual', lambda p: call(device, 7, (P,), (p,))) for _ in range(4)]
        main_root, pip_root, ui, video = roots
        cv(main_root, 16, (P, B, P), (ui, 0, None))
        cv(main_root, 16, (P, B, P), (video, 0, ui))
        cv(video, 15, (P,), (chain,), 'set actual swapchain content')
        for target, root in zip(targets, roots):
            cv(target, 3, (P,), (root,))
        cv(device, 3)
        cv(device, 4)
        for hwnd in windows:
            user.ShowWindow(hwnd, 4)
        if app:
            app.processEvents()
        already_parented = call(pip_root, 16, (P, B, P), (video, 0, None))
        assert already_parented < 0, 'must remove real visual from its old parent'
        library = os.environ.get('VLC_DCOMP_TRANSFER_TEST_DLL')
        move = None
        state: Any = None
        if library:
            class State(C.Structure):
                _fields_ = [('detached', B), ('healthy', B)]
            Change = C.CFUNCTYPE(B, P, B)
            Commit = C.CFUNCTYPE(B, P)
            class Ops(C.Structure):
                _fields_ = [('context', P), ('remove', Change), ('add', Change), ('commit', Commit)]
            sibling: list[Any] = [ui]
            remove = Change(lambda _, pip: call(pip_root if pip else main_root, 17, (P,), (video,)))
            attachment_result = [0]
            def add_visual(_, pip):
                result = call(pip_root if pip else main_root, 16, (P, B, P),
                    (video, 0, sibling[0] if pip else ui))
                if pip:
                    attachment_result[0] = result
                return result
            add = Change(add_visual)
            commit = Commit(lambda _: call(device, 3))
            state, ops = State(0, 1), Ops(None, remove, add, commit)
            native = C.CDLL(library).vlc_dcomp_test_move
            native.argtypes = [C.POINTER(State), C.POINTER(Ops), B]
            native.restype = B
            move = lambda pip: native(C.byref(state), C.byref(ops), pip)
            assert not move(True), 'invalid sibling must fail through production helper'
            assert state.healthy and not state.detached, 'production helper must restore source'
            sibling[0] = None
            invalid_sibling = attachment_result[0]
            assert invalid_sibling < 0
        else:
            cv(main_root, 17, (P,), (video,))
            invalid_sibling = call(pip_root, 16, (P, B, P), (video, 0, ui))
            assert invalid_sibling < 0, 'destination cannot reference a main-root sibling'
            cv(main_root, 16, (P, B, P), (video, 0, ui), 'rollback failed attachment')
            cv(device, 3)
        cv(device, 4)
        for i in range(32):
            if move:
                assert move(True) and state.detached
            else:
                cv(main_root, 17, (P,), (video,), 'remove from main')
                cv(pip_root, 16, (P, B, P), (video, 0, None), 'attach to PiP')
                cv(device, 3)
            cv(device, 4)
            user.ShowWindow(windows[0], 6)
            if app:
                app.processEvents()
            assert not user.IsIconic(windows[1])
            assert user.IsWindowVisible(windows[1])
            assert user.SetWindowPos(windows[1], None, 0, 0, 161 + i, 121 + i, 0x16)
            if move:
                assert move(False) and not state.detached
            else:
                cv(pip_root, 17, (P,), (video,), 'return from PiP')
                cv(main_root, 16, (P, B, P), (video, 0, ui), 'restore below UI')
                cv(device, 3)
            cv(device, 4)
            user.ShowWindow(windows[0], 9)
            if app:
                app.processEvents()
        tex = TextureDesc()
        call(buffer, 10, (P,), (C.byref(tex),), result=None)
        tex.usage, tex.bind, tex.cpu, tex.misc = 3, 0, 0x20000, 0
        staging = out('readback texture', lambda p: call(gpu, 5, (P, P, P),
            (C.byref(tex), None, p)))
        call(context, 47, (P, P), (staging, buffer), result=None)
        mapped = Mapped()
        cv(context, 14, (P, U, U, U, P), (staging, 0, 1, 0, C.byref(mapped)))
        pixel = C.cast(mapped.data, C.POINTER(U)).contents.value
        call(context, 15, (P, U), (staging, 0), result=None)
        assert pixel == 0xffff0000, f'real rendered frame changed: {pixel:#x}'
        valid = B()
        cv(device, 26, (P,), (C.byref(valid),))
        assert valid.value, 'native device invalid after transfer'
        cv(targets[1], 3, (P,), (None,))
        cv(device, 3)
        cv(device, 4)
        # Release target before destroying its HWND.
        objects.remove(targets[1])
        call(targets[1], 2, result=U)
        hwnd = windows.pop()
        if qt_windows:
            qt_windows.pop().destroy()
        else:
            assert user.DestroyWindow(hwnd)
        assert user.IsWindow(windows[0])
        cv(main_root, 17, (P,), (video,))
        cv(video, 15, (P,), (None,))
        cv(targets[0], 3, (P,), (None,))
        cv(device, 3)
        cv(device, 4)
        print(json.dumps({'result': 'PASS', 'backend': 'D3D11 WARP / DirectComposition',
            'round_trips': 32, 'same_real_visual': True, 'same_swapchain': True,
            'readback_pixel': hex(pixel), 'already_parented_hresult': hex(already_parented & 0xffffffff),
            'independent_ownerless_hwnd': True, 'main_minimize_pip_survives': True,
            'device_valid_after_transfer': bool(valid.value),
            'qt_window_validated': bool(app), 'failed_attachment_rollback': True,
            'production_transfer_helper': bool(library),
            'qt_rhi_validated': False,
            'vlc_vout_validated': False}, indent=2))
    finally:
        for obj in reversed(objects):
            call(obj, 2, result=U)
        if qt_windows:
            for window in reversed(qt_windows):
                window.destroy()
        else:
            for hwnd in reversed(windows):
                user.DestroyWindow(hwnd)
        ole.CoUninitialize()


if __name__ == '__main__':
    main()
