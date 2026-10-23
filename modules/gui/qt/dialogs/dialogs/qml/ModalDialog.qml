/*****************************************************************************
 * Copyright (C) 2019 VLC authors and VideoLAN
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
import QtQuick.Controls.Basic as C
import QtQuick.Layouts

import VLC.MainInterface
import VLC.Player as Redesign
import VLC.Style

C.Dialog {
    id: control

    property Item rootWindow: null
    property Component videoBackdropContent: Component {
        Redesign.RedesignSurfaceBackdrop { sourceItem: control.rootWindow; scaleFactor: control.theme.scaleFactor }
    }
    readonly property Redesign.RedesignTheme theme: Redesign.RedesignTheme {
        scaleFactor: VLCStyle.scale
        lightTheme: !VLCStyle.palette.isDark
        accentColor: MainCtx.redesignAccent
    }
    readonly property color onAccent: {
        const linear = value => value <= 0.04045 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4)
        const luminance = 0.2126 * linear(theme.accent.r) + 0.7152 * linear(theme.accent.g) + 0.0722 * linear(theme.accent.b)
        return luminance > 0.179 ? "#161616" : "#ffffff"
    }
    property Item activeField: null
    default property alias body: bodyColumn.data
    readonly property Item bodyViewport: bodyScroll.contentItem

    popupType: C.Popup.Item
    parent: C.Overlay.overlay
    focus: true
    modal: true
    dim: true
    closePolicy: C.Popup.CloseOnEscape
    readonly property real pixelRatio: parent ? parent.Screen.devicePixelRatio : 1
    x: parent ? Math.round((parent.width - width) * pixelRatio / 2) / pixelRatio : 0
    y: parent ? Math.round((parent.height - height) * pixelRatio / 2) / pixelRatio : 0
    width: Math.max(0, Math.min(theme.dp(560), parent ? parent.width - theme.dp(32) : theme.dp(560)))
    height: Math.min(implicitHeight, parent ? Math.max(0, parent.height - theme.dp(32)) : implicitHeight)
    padding: theme.dp(24)
    spacing: theme.dp(20)
    header: null
    footer: null

    // Keep the title and all request text in the scroll area; actions stay outside it.
    contentItem: C.ScrollView {
        id: bodyScroll
        objectName: "interactionBody"
        implicitHeight: bodyColumn.implicitHeight
        contentWidth: availableWidth
        clip: true
        Keys.onEscapePressed: event => { event.accepted = control.closePolicy === C.Popup.NoAutoClose }
        C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
        C.ScrollBar.vertical: C.ScrollBar { policy: C.ScrollBar.AsNeeded; active: true }
        ColumnLayout {
            id: bodyColumn
            width: bodyScroll.availableWidth
            spacing: control.theme.dp(16)
            Text {
                objectName: "interactionTitle"
                Layout.fillWidth: true
                text: control.title
                visible: text.length > 0
                textFormat: Text.PlainText
                color: control.theme.bright
                font.pixelSize: control.theme.dp(20)
                wrapMode: Text.Wrap
                Accessible.role: Accessible.Heading
            }
        }
    }
    function scrollKey(key) {
        if (!bodyViewport) return false
        const maximum = Math.max(0, bodyViewport.contentHeight - bodyViewport.height)
        let target = bodyViewport.contentY
        if (key === Qt.Key_PageDown) target += bodyViewport.height * 0.8
        else if (key === Qt.Key_PageUp) target -= bodyViewport.height * 0.8
        else if (key === Qt.Key_Home) target = 0
        else if (key === Qt.Key_End) target = maximum
        else return false
        bodyViewport.contentY = Math.max(0, Math.min(maximum, target))
        return true
    }
    function revealField(field) {
        if (!opened || !field || !bodyViewport) return
        const point = field.mapToItem(bodyColumn, 0, 0)
        const top = bodyViewport.contentY
        if (point.y < top) bodyViewport.contentY = Math.max(0, point.y - theme.dp(8))
        else if (point.y + field.height > top + bodyViewport.height)
            bodyViewport.contentY = Math.max(0, point.y + field.height + theme.dp(8) - bodyViewport.height)
    }
    function revealActiveField() { revealField(activeField) }
    onOpened: if (bodyViewport) bodyViewport.contentY = 0
    onWidthChanged: Qt.callLater(control.revealActiveField)
    onHeightChanged: Qt.callLater(control.revealActiveField)

    C.Overlay.modal: Rectangle { color: "#80000000" }
    background: Redesign.RedesignDialogSurface {
        theme: control.theme
        videoBackdropContent: control.videoBackdropContent
    }
    enter: Transition { OpacityAnimator { from: 0; to: 1; duration: 100 } }
    exit: Transition { OpacityAnimator { from: 1; to: 0; duration: 100 } }

    component ActionButton: Redesign.RedesignButton {
        id: action
        required property ModalDialog dialog
        readonly property real naturalWidth: Math.max(dialog.theme.dp(100), metrics.advanceWidth + dialog.theme.dp(24))
        scaleFactor: dialog.theme.scaleFactor
        lightTheme: dialog.theme.lightTheme
        accentColor: dialog.theme.accentColor
        text: caption
        tooltipEnabled: metrics.advanceWidth > captionMaximumWidth
        tooltipTextFormat: Text.PlainText
        contentItem: Text {
            text: action.caption
            textFormat: Text.PlainText
            color: action.foreground
            font.pixelSize: action.theme.dp(14)
            font.weight: Font.DemiBold
            leftPadding: action.theme.dp(8); rightPadding: leftPadding
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            opacity: action.enabled ? 1 : 0.5
        }
        captionFontWeight: Font.DemiBold
        captionMaximumWidth: Math.max(0, width - dialog.theme.dp(16))
        Layout.fillWidth: true
        Layout.preferredWidth: naturalWidth
        Layout.preferredHeight: dialog.theme.dp(32)
        filled: true
        TextMetrics { id: metrics; text: action.caption; font.pixelSize: action.theme.dp(14); font.weight: Font.DemiBold }
        Keys.onPressed: event => { event.accepted = dialog.scrollKey(event.key) }
        Keys.onReturnPressed: event => { if (enabled && !event.isAutoRepeat) click() }
        Keys.onEnterPressed: event => { if (enabled && !event.isAutoRepeat) click() }
    }
    component Field: C.TextField {
        id: field
        required property ModalDialog dialog
        Layout.fillWidth: true
        implicitHeight: dialog.theme.dp(36)
        padding: dialog.theme.dp(10)
        color: dialog.theme.bright
        font.pixelSize: dialog.theme.dp(14)
        selectByMouse: true
        selectionColor: dialog.theme.accent
        selectedTextColor: dialog.onAccent
        background: Rectangle {
            color: field.dialog.theme.field
            radius: field.dialog.theme.dp(4)
            border.width: field.dialog.theme.dp(1)
            border.color: field.activeFocus ? field.dialog.theme.accent : field.dialog.theme.border
        }
        onActiveFocusChanged: {
            if (!dialog) return
            if (activeFocus) { dialog.activeField = field; Qt.callLater(dialog.revealActiveField) }
            else if (dialog.activeField === field) dialog.activeField = null
        }
    }
}
