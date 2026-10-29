/*****************************************************************************
 * Copyright (C) 2020 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
import QtQuick
import QtQuick.Window
import QtQuick.Templates as T

import VLC.MainInterface
import VLC.Style
import VLC.Widgets as Widgets
import VLC.Playlist
import VLC.Player
import VLC.Util

T.Control {
    id: root
    width: Helpers.alignRound(320, _alignNumber)
    height: Helpers.alignRound(180, _alignNumber)

    // Default size is always aligned, this only
    // controls position alignment. Note that
    // setting this may make dragging less smooth,
    // depending on how big is the denominator
    // for the fractional DPR (the bigger, the
    // worse). See `Helpers::denominatorForFloat()`.
    property bool pixelAlignedForDPR: !MainCtx.videoSurfaceCanHaveFractionalPosition

    transform: Translate {
        x: (root.x !== targetX) ? (targetX - root.x) : 0.0
        y: (root.y !== targetY) ? (targetY - root.y) : 0.0

        readonly property real targetX: root.pixelAlignedForDPR ? Helpers.alignRound(root.x, root._alignNumber)
                                                                : (MainCtx.videoSurfaceCanHaveFractionalPosition ? root.x
                                                                                                                 : Math.round(root.x))
        readonly property real targetY: root.pixelAlignedForDPR ? Helpers.alignRound(root.y, root._alignNumber)
                                                                : (MainCtx.videoSurfaceCanHaveFractionalPosition ? root.y
                                                                                                                 : Math.round(root.y))
    }

    objectName: "pip window"

    property real dragXMin: 0
    property real dragXMax: 0
    property real dragYMin: undefined
    property real dragYMax: undefined

    property int textStyle: Text.Outline

    Accessible.role: Accessible.Graphic
    Accessible.focusable: false
    Accessible.name: qsTr("video content")

    property real _eDPR: MainCtx.effectiveDevicePixelRatio(Window.window) || 1.0
    readonly property int _alignNumber: pixelAlignedForDPR ? Helpers.denominatorForFloat(_eDPR) : 1

    Connections {
        target: MainCtx

        function onIntfDevicePixelRatioChanged() {
            root._eDPR = MainCtx.effectiveDevicePixelRatio(root.Window.window) || 1.0
        }
    }

    Drag.active: controls.dragging

    Drag.onActiveChanged: {
        root.anchors.left = undefined
        root.anchors.right = undefined
        root.anchors.top = undefined
        root.anchors.bottom = undefined
        root.anchors.verticalCenter = undefined
        root.anchors.horizontalCenter = undefined
    }

    background: VideoSurface {
        id: videoSurface
        videoSurfaceProvider: MainCtx.videoSurfaceProvider
        color: controls.controlsVisible ? "#10000000" : "transparent"

        // It is acceptable for effects to exceed the boundaries of the
        // parent. They are an exception to the "hypothetical clip test".
        Widgets.DefaultShadow {

        }
    }

    contentItem: PipPlayerControls {
        id: controls
        z: 1
        dragTarget: root
        dragXMin: root.dragXMin; dragXMax: root.dragXMax
        dragYMin: root.dragYMin; dragYMax: root.dragYMax
        onReturnRequested: MainCtx.playerView = true
        onCloseRequested: MainPlaylistController.stop()
    }
}
