#!/usr/bin/env python
"""Run standalone Qt Quick UI tests without building VLC's native modules.

Example (from repository root):
uv run --isolated --no-project --python 3.12 --with PySide6==6.8.3 \
    python extras/package/wil/run_qml_tests.py -input modules/gui/qt/tests/tst_Redesign.qml

This uses the real project QML and mirrors tests/qml_test.cpp's image helper.
It does not load the VLC native plugin or exercise decoding/compositors.
"""
import math
import os
import sys

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
if os.name == 'nt':
    # The offscreen QPA plugin does not discover Windows fonts automatically.
    fonts = os.path.join(os.environ.get('WINDIR', 'C:/Windows'), 'Fonts')
    if os.path.isdir(fonts):
        os.environ.setdefault('QT_QPA_FONTDIR', fonts)
from PySide6.QtCore import QObject, Property, QPoint, QPointF, Slot
from PySide6.QtQml import QQmlEngine, qmlRegisterModule

# TEST ONLY: permit video/UI tests to parse AudioArtwork's native import.
# No Effects implementation is mocked: artwork/effects playback is not tested.
qmlRegisterModule('VLC.Util', 1, 0)
from PySide6.QtQuick import QQuickItem
from PySide6.QtQuickTest import QUICK_TEST_MAIN_WITH_SETUP
from shiboken6 import getCppPointer


class WindowImage(QObject):
    def __init__(self, window, parent):
        super().__init__(parent)
        self._window_id = getCppPointer(window)[0]
        self._size = window.size()
        self._image = window.grabWindow()

    @Property(int, constant=True)
    def width(self):
        return self._image.width()

    @Property(int, constant=True)
    def height(self):
        return self._image.height()

    @Slot(QQuickItem, float, float, result=QPoint)
    def point(self, item, x, y):
        window = item.window() if item else None
        window_id = getCppPointer(window)[0] if window else 0
        if not item or window_id != self._window_id or self._size.isEmpty():
            return QPoint(-1, -1)
        scene = item.mapToScene(QPointF(x, y))
        result = QPoint(math.floor(scene.x() * self.width / self._size.width()),
                        math.floor(scene.y() * self.height / self._size.height()))
        return result

    def _channel(self, x, y, channel):
        if not self._image.valid(x, y):
            return -1
        return getattr(self._image.pixelColor(x, y), channel)()

    @Slot(int, int, result=int)
    def red(self, x, y):
        return self._channel(x, y, 'red')

    @Slot(int, int, result=int)
    def green(self, x, y):
        return self._channel(x, y, 'green')

    @Slot(int, int, result=int)
    def blue(self, x, y):
        return self._channel(x, y, 'blue')


class Images(QObject):
    def __init__(self, parent):
        super().__init__(parent)
        self._captures = []

    @Slot(QQuickItem, result=QObject)
    def grabWindow(self, item):
        if not item or not item.window():
            return None
        image = WindowImage(item.window(), self)
        self._captures.append(image)
        # Tests use captures immediately; keep memory bounded on full-suite runs.
        if len(self._captures) > 16:
            self._captures.pop(0).deleteLater()
        return image


class Setup(QObject):
    @Slot(QQmlEngine)
    def qmlEngineAvailable(self, engine):
        self.images = Images(engine)
        engine.rootContext().setContextProperty('testImages', self.images)


if __name__ == '__main__':
    raise SystemExit(QUICK_TEST_MAIN_WITH_SETUP('VLCWil', Setup, sys.argv))
