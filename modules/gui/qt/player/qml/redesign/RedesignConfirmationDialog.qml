// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

C.Dialog {
    id: root
    popupType: C.Popup.Item
    property real scaleFactor: 1
    property bool lightTheme: false
    property color accentColor: "#d37918"
    property Component videoBackdropContent
    property string message
    property string confirmText: qsTr("Continue")
    property bool destructive: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    readonly property real pixelRatio: parent ? parent.Screen.devicePixelRatio : 1
    readonly property bool stackedActions: availableWidth < Math.max(theme.dp(100), cancelMetrics.advanceWidth + theme.dp(16)) + Math.max(theme.dp(100), acceptMetrics.advanceWidth + theme.dp(16)) + theme.dp(8)
    x: parent ? Math.round((parent.width - width) * pixelRatio / 2) / pixelRatio : 0
    y: parent ? Math.round((parent.height - height) * pixelRatio / 2) / pixelRatio : 0
    width: Math.max(0, Math.min(theme.dp(460), parent ? parent.width - theme.dp(32) : theme.dp(460)))
    height: Math.min(implicitHeight, parent ? Math.max(0, parent.height - theme.dp(32)) : implicitHeight)
    padding: theme.dp(24)
    modal: true; dim: true; focus: true
    closePolicy: C.Popup.CloseOnEscape
    header: null; footer: null
    background: RedesignDialogSurface { theme: root.theme; videoBackdropContent: root.videoBackdropContent }
    C.Overlay.modal: Rectangle { color: "#80000000" }
    onOpened: cancelButton.forceActiveFocus()
    TextMetrics { id: cancelMetrics; text: cancelButton.caption; font.pixelSize: root.theme.dp(14); font.weight: Font.DemiBold }
    TextMetrics { id: acceptMetrics; text: root.confirmText; font.pixelSize: root.theme.dp(14); font.weight: Font.DemiBold }
    contentItem: ColumnLayout {
        Accessible.role: Accessible.Dialog
        Accessible.name: root.title
        spacing: root.theme.dp(20)
        C.ScrollView {
            id: bodyScroll
            objectName: "confirmationBody"
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.minimumHeight: 0
            implicitHeight: body.implicitHeight
            contentWidth: availableWidth
            clip: true
            C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
            Column {
                id: body
                width: bodyScroll.availableWidth
                spacing: root.theme.dp(20)
                Text {
                    objectName: "confirmationTitle"
                    width: parent.width
                    text: root.title; textFormat: Text.PlainText
                    color: root.theme.bright; font.pixelSize: root.theme.dp(20)
                    wrapMode: Text.Wrap
                }
                Text {
                    objectName: "confirmationMessage"
                    width: parent.width
                    text: root.message; textFormat: Text.PlainText
                    color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
                    wrapMode: Text.Wrap
                }
            }
        }
        GridLayout {
            Layout.alignment: Qt.AlignRight
            Layout.fillWidth: root.stackedActions
            columns: root.stackedActions ? 1 : 2
            columnSpacing: root.theme.dp(8); rowSpacing: root.theme.dp(8)
            RedesignButton {
                id: cancelButton
                objectName: "confirmationCancel"
                Layout.fillWidth: root.stackedActions
                Layout.preferredWidth: Math.max(root.theme.dp(100), cancelMetrics.advanceWidth + root.theme.dp(16))
                Layout.preferredHeight: root.theme.dp(32)
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                caption: qsTr("Cancel"); captionFontWeight: Font.DemiBold; text: caption
                captionMaximumWidth: Math.max(0, width - root.theme.dp(16)); captionVerticalOffset: 1
                background: Rectangle { color: parent.hovered ? root.theme.hover : root.theme.background; radius: root.theme.dp(6); border.width: parent.visualFocus ? root.theme.dp(1) : 0; border.color: root.theme.accent }
                onClicked: root.reject()
                Keys.onReturnPressed: root.reject()
                Keys.onEnterPressed: root.reject()
            }
            RedesignButton {
                objectName: "confirmationAccept"
                Layout.fillWidth: root.stackedActions
                Layout.preferredWidth: Math.max(root.theme.dp(100), acceptMetrics.advanceWidth + root.theme.dp(16))
                Layout.preferredHeight: root.theme.dp(32)
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                caption: root.confirmText; captionFontWeight: Font.DemiBold; text: caption
                captionMaximumWidth: Math.max(0, width - root.theme.dp(16)); captionVerticalOffset: 1
                foreground: root.destructive ? root.theme.danger : root.theme.accent
                background: Rectangle { color: parent.hovered ? root.theme.hover : root.destructive ? root.theme.dangerBackground : root.theme.progress; radius: root.theme.dp(6); border.width: parent.visualFocus ? root.theme.dp(1) : 0; border.color: parent.foreground }
                onClicked: root.accept()
                Keys.onReturnPressed: root.accept()
                Keys.onEnterPressed: root.accept()
            }
        }
    }
}
