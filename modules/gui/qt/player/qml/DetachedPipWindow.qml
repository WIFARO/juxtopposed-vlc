// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import VLC.MainInterface

PipPlayerControls {
    required property var hostWindow
    focus: true
    resizeEnabled: true
    onMoveRequested: hostWindow.startSystemMove()
    onResizeRequested: edges => hostWindow.startSystemResize(edges)
    // Reattachment destroys this view: never do it during pointer dispatch.
    onReturnRequested: Qt.callLater(() => MainCtx.requestDetachedPip(false))
    onCloseRequested: Qt.callLater(() => MainCtx.requestDetachedPip(false))
}
