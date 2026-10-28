#!/usr/bin/env python
"""Native Windows regression: the direct player menu toggles real topmost state.

Uses an isolated COPY of a supplied VLC distribution and generated silent audio.
Never attaches to or changes an existing VLC instance/profile. TMPDIR must point
at a private scratch directory (Hermes and the GitHub workflow supply it). Run with:
uv run --isolated --no-project --python 3.12 --with pywinauto==0.6.9 \
  python extras/package/wil/test_player_context_menu.py --vlc C:/path/to/vlc.exe
"""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest
import wave

from pywinauto import Desktop

USER32 = ctypes.WinDLL('user32', use_last_error=True)
ENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
USER32.EnumWindows.argtypes = [ENUMPROC, wintypes.LPARAM]
USER32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
USER32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
USER32.IsWindowVisible.argtypes = [wintypes.HWND]
USER32.GetWindowLongPtrW.argtypes = [wintypes.HWND, ctypes.c_int]
USER32.GetWindowLongPtrW.restype = ctypes.c_ssize_t
USER32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
USER32.ChildWindowFromPointEx.argtypes = [wintypes.HWND, wintypes.POINT, wintypes.UINT]
USER32.ChildWindowFromPointEx.restype = wintypes.HWND


def main_windows(pid):
    result = []
    @ENUMPROC
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        USER32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        rect = wintypes.RECT()
        if owner.value == pid and USER32.IsWindowVisible(hwnd):
            USER32.GetClientRect(hwnd, ctypes.byref(rect))
            area = (rect.right - rect.left) * (rect.bottom - rect.top)
            result.append((area, hwnd, rect))
        return True
    USER32.EnumWindows(visit, 0)
    return sorted(result, key=lambda item: item[0], reverse=True)


class ContextMenuRegression(unittest.TestCase):
    vlc: Path
    output: Path

    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix='wil-vlc-menu-', dir=os.environ['TMPDIR'])
        cls.addClassCleanup(cls.cleanup_player)
        cls.root = Path(cls.work.name)
        # portable/ forces both VLC and QSettings to this private test copy.
        cls.dist = cls.root / 'player'
        shutil.copytree(cls.vlc.parent, cls.dist)
        shutil.rmtree(cls.dist / 'portable', ignore_errors=True)
        (cls.dist / 'portable').mkdir()
        fixture = cls.root / 'wil-context-menu-fixture.wav'
        with wave.open(str(fixture), 'wb') as wav:
            wav.setparams((1, 2, 8000, 0, 'NONE', 'not compressed'))
            wav.writeframes(b'\x00\x00' * 16000)
        cls.log = open(cls.root / 'vlc-test.log', 'wb')
        cls.process = subprocess.Popen([
            str(cls.dist / 'vlc.exe'), '--ignore-config', '--language=en', '--no-one-instance',
            '--no-one-instance-when-started-from-file', '--no-qt-privacy-ask',
            '--no-qt-updates-notif', '--no-media-library', '--no-metadata-network-access',
            '--no-audio', '--no-video-on-top', '--loop', str(fixture),
        ], stdout=cls.log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            if cls.process.poll() is not None:
                raise RuntimeError('Isolated VLC exited before its window appeared')
            windows = main_windows(cls.process.pid)
            if windows and windows[0][0] > 100000:
                cls.hwnd = windows[0][1]
                time.sleep(2)
                return
            time.sleep(.2)
        raise RuntimeError('No usable window for the isolated VLC process')

    @classmethod
    def cleanup_player(cls):
        if getattr(cls, 'process', None) and cls.process.poll() is None:
            if getattr(cls, 'hwnd', None):
                USER32.PostMessageW(cls.hwnd, 0x0010, 0, 0)  # WM_CLOSE
            try:
                cls.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                cls.process.terminate()
                cls.process.wait(timeout=10)
        if getattr(cls, 'log', None):
            cls.log.close()
        if (cls.root / 'vlc-test.log').is_file():
            cls.output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(cls.root / 'vlc-test.log', cls.output.with_suffix('.vlc.log'))
        cls.work.cleanup()

    def topmost(self):
        return bool(USER32.GetWindowLongPtrW(self.hwnd, -20) & 0x00000008)

    def visible_popup_items(self):
        items = []
        for window in Desktop(backend='uia').windows(process=self.process.pid, visible_only=True):
            items.extend(item for item in window.descendants(control_type='MenuItem') if item.is_visible())
        names = [item.window_text().split('\t')[0].replace('&', '') for item in items]
        if any(name in ('Play', 'Pause', 'Hide UI', 'Show UI') for name in names):
            return items
        return []

    def popup_items(self):
        # Qt's accessibility Invoke activates the QAction without dismissing
        # its QMenu. Posting another right-click would close that existing menu.
        existing = self.visible_popup_items()
        if existing:
            return existing
        rect = wintypes.RECT()
        self.assertTrue(USER32.GetClientRect(self.hwnd, ctypes.byref(rect)))
        x, y = (rect.right - rect.left) // 2, (rect.bottom - rect.top) // 2
        # The software compositor hosts QML in a child QQuickView. Its root
        # forwards drag events, not mouse input. Keep self.hwnd as the root for
        # actual WS_EX_TOPMOST assertions; route clicks to the visible child.
        self.mouse_target = USER32.ChildWindowFromPointEx(
            self.hwnd, wintypes.POINT(x, y), 3) or self.hwnd  # skip hidden/disabled
        owner = wintypes.DWORD()
        USER32.GetWindowThreadProcessId(self.mouse_target, ctypes.byref(owner))
        self.assertEqual(owner.value, self.process.pid, 'Mouse target must belong to the isolated player')
        self.assertTrue(USER32.GetClientRect(self.mouse_target, ctypes.byref(rect)))
        x, y = (rect.right - rect.left) // 2, (rect.bottom - rect.top) // 2
        pos = (y << 16) | x
        # Background messages only: no cursor movement or foreground activation.
        self.assertTrue(USER32.PostMessageW(self.mouse_target, 0x0204, 2, pos))  # WM_RBUTTONDOWN
        self.assertTrue(USER32.PostMessageW(self.mouse_target, 0x0205, 0, pos))  # WM_RBUTTONUP
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            items = self.visible_popup_items()
            if items:
                return items
            time.sleep(.1)
        self.fail('Right-click did not expose a menu in the isolated player')

    def test_direct_menu_can_toggle_native_always_on_top(self):
        self.assertFalse(self.topmost(), 'Isolated player should start non-topmost')
        items = self.popup_items()
        names = [item.window_text().split('\t')[0].replace('&', '') for item in items]
        evidence = {'pid': self.process.pid, 'hwnd': self.hwnd,
                    'input_hwnd': getattr(self, 'mouse_target', self.hwnd), 'menu_items': names,
                    'before_topmost': self.topmost()}
        self.output.parent.mkdir(parents=True, exist_ok=True)
        self.output.write_text(json.dumps(evidence, indent=2), encoding='utf-8')
        matches = [item for item, name in zip(items, names) if name == 'Always on top']
        self.assertTrue(matches, 'Direct right-click player menu is missing Always on top; items=' + repr(names))
        matches[0].invoke()
        deadline = time.monotonic() + 3
        while not self.topmost() and time.monotonic() < deadline:
            time.sleep(.05)
        self.assertTrue(self.topmost(), 'Menu action must set WS_EX_TOPMOST on the real player window')
        items = self.popup_items()
        action = next(item for item in items if item.window_text().split('\t')[0].replace('&', '') == 'Always on top')
        action.invoke()
        deadline = time.monotonic() + 3
        while self.topmost() and time.monotonic() < deadline:
            time.sleep(.05)
        self.assertFalse(self.topmost(), 'Second toggle must remove WS_EX_TOPMOST')
        evidence.update(enabled_then_disabled=True)
        self.output.write_text(json.dumps(evidence, indent=2), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vlc', required=True, type=Path)
    scratch = os.environ.get('TMPDIR')
    if not scratch:
        parser.error('Set TMPDIR to a private scratch directory; system temp fallback is disabled')
    parser.add_argument('--output', type=Path,
                        default=Path(scratch) / 'wil-vlc-tests' / 'context-menu.json')
    args = parser.parse_args()
    ContextMenuRegression.vlc = args.vlc.resolve(strict=True)
    ContextMenuRegression.output = args.output
    unittest.main(argv=[__file__], verbosity=2)
