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
import QtQuick.Layouts

ModalDialog {
    id: root
    property string text
    property alias cancelTxt: cancelButton.caption
    property alias okTxt: okButton.caption
    property var _acceptCb: undefined
    property var _rejectCb: undefined

    function ask(text, acceptCb, rejectCb, buttons) {
        root.title = qsTr("Confirm")
        root.cancelTxt = buttons && typeof buttons.cancel === "string" ? buttons.cancel : qsTr("Cancel")
        root.okTxt = buttons && typeof buttons.ok === "string" ? buttons.ok : qsTr("OK")
        root.text = text
        root._acceptCb = acceptCb
        root._rejectCb = rejectCb
        root.open()
    }
    function answer(accepted) {
        const callback = accepted ? _acceptCb : _rejectCb
        _acceptCb = undefined
        _rejectCb = undefined
        if (callback) callback()
    }
    onAccepted: answer(true)
    onRejected: answer(false)
    onClosed: if (_acceptCb || _rejectCb) answer(false)
    onOpened: { if (cancelButton.visible) cancelButton.forceActiveFocus(); else okButton.forceActiveFocus() }
    TextEdit {
        objectName: "interactionCustomMessage"
        visible: text.length > 0
        Layout.fillWidth: true
        text: root.text
        textFormat: TextEdit.PlainText
        readOnly: true; selectByMouse: true; activeFocusOnTab: true
        wrapMode: TextEdit.Wrap
        color: root.theme.foreground
        selectionColor: root.theme.accent
        selectedTextColor: root.onAccent
        font.pixelSize: root.theme.dp(14)
    }
    footer: Item {
        visible: true
        implicitHeight: visible ? actions1.implicitHeight + root.bottomPadding : 0
        GridLayout {
            id: actions1
            anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: root.leftPadding; rightMargin: root.rightPadding }
            columns: width < cancelButton.naturalWidth + okButton.naturalWidth + columnSpacing ? 1 : 2
            columnSpacing: root.theme.dp(8); rowSpacing: columnSpacing
            ModalDialog.ActionButton {
                id: cancelButton
                objectName: "interactionCustomCancel"
                dialog: root
                caption: qsTr("Cancel")
                visible: caption.length > 0
                onClicked: root.reject()
            }
            ModalDialog.ActionButton {
                id: okButton
                objectName: "interactionCustomAccept"
                dialog: root
                caption: qsTr("OK")
                primaryAction: true
                visible: caption.length > 0
                onClicked: root.accept()
            }

        }
    }
}
