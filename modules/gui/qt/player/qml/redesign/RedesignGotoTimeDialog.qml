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
    property bool seekable: false
    property real durationSeconds: 0
    property alias timestampText: timeField.text
    property bool committed: false
    readonly property int parsedSeconds: parseTime(timestampText)
    readonly property bool valid: seekable && parsedSeconds >= 0
        && (!(durationSeconds > 0) || parsedSeconds <= durationSeconds)
    readonly property string helpText: !seekable ? qsTr("You cannot change the time for this media.")
        : parsedSeconds >= 0 && durationSeconds > 0 && parsedSeconds > durationSeconds
            ? qsTr("Time must be between %1 and %2.").arg(formatTime(0)).arg(formatTime(durationSeconds))
            : qsTr("Use seconds, mm:ss, or hh:mm:ss.")
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    readonly property real pixelRatio: parent ? parent.Screen.devicePixelRatio : 1
    readonly property bool compactLayout: height < theme.dp(250)
    readonly property bool stackedActions: availableWidth < Math.max(theme.dp(100), cancelMetrics.advanceWidth + theme.dp(16))
        + Math.max(theme.dp(100), goMetrics.advanceWidth + theme.dp(16)) + theme.dp(8)
    signal seekRequested(int seconds)
    title: qsTr("Go to time")
    x: parent ? Math.round((parent.width - width) * pixelRatio / 2) / pixelRatio : 0
    y: parent ? Math.round((parent.height - height) * pixelRatio / 2) / pixelRatio : 0
    width: Math.max(0, Math.min(theme.dp(460), parent ? parent.width - theme.dp(32) : theme.dp(460)))
    height: Math.min(theme.dp(276), parent ? Math.max(0, parent.height - theme.dp(32)) : theme.dp(276))
    padding: theme.dp(compactLayout ? 16 : 24)
    modal: true; dim: true; focus: true
    closePolicy: C.Popup.CloseOnEscape
    header: null; footer: null
    background: RedesignDialogSurface { theme: root.theme; videoBackdropContent: root.videoBackdropContent }
    C.Overlay.modal: Rectangle { color: "#80000000" }

    function parseTime(text) {
        const value = String(text).trim()
        if (!/^\d+(?::\d{1,2}){0,2}$/.test(value)) return -1
        const parts = value.split(":").map(Number)
        if (parts.length > 1 && parts[parts.length - 1] >= 60) return -1
        if (parts.length === 3 && parts[1] >= 60) return -1
        const seconds = parts.reduce((total, part) => total * 60 + part, 0)
        // Match the player's signed seconds API, without clock-style day wrap.
        return Number.isSafeInteger(seconds) && seconds <= 2147483647 ? seconds : -1
    }
    function formatTime(seconds) {
        seconds = Math.max(0, Math.floor(Number(seconds) || 0))
        const hours = Math.floor(seconds / 3600)
        const minutes = Math.floor(seconds / 60) % 60
        const rest = String(seconds % 60).padStart(2, "0")
        return hours > 0 ? hours + ":" + String(minutes).padStart(2, "0") + ":" + rest
            : String(minutes).padStart(2, "0") + ":" + rest
    }
    function begin(seconds) {
        if (!visible) {
            committed = false
            timestampText = formatTime(seconds)
            open()
        }
        if (seekable) { timeField.forceActiveFocus(); timeField.selectAll() }
        else cancelButton.forceActiveFocus()
    }
    function submit() {
        if (!visible || !valid || committed) return false
        committed = true
        seekRequested(parsedSeconds)
        accept()
        return true
    }
    function revealField() {
        if (!opened || !timeField.activeFocus) return
        const point = timeField.mapToItem(body, 0, 0)
        const flick = bodyScroll.contentItem
        if (point.y < flick.contentY) flick.contentY = point.y
        else if (point.y + timeField.height > flick.contentY + bodyScroll.availableHeight)
            flick.contentY = Math.max(0, point.y + timeField.height - bodyScroll.availableHeight)
    }
    onOpened: {
        if (seekable) { timeField.forceActiveFocus(); timeField.selectAll() }
        else cancelButton.forceActiveFocus()
        Qt.callLater(revealField)
    }
    onSeekableChanged: if (visible && !seekable) cancelButton.forceActiveFocus()
    TextMetrics { id: cancelMetrics; text: cancelButton.caption; font.pixelSize: root.theme.dp(14); font.weight: Font.DemiBold }
    TextMetrics { id: goMetrics; text: goButton.caption; font.pixelSize: root.theme.dp(14); font.weight: Font.DemiBold }
    contentItem: ColumnLayout {
        Accessible.role: Accessible.Dialog
        Accessible.name: root.title
        spacing: root.theme.dp(root.compactLayout ? 12 : 20)
        C.ScrollView {
            id: bodyScroll
            objectName: "gotoTimeBody"
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0
            contentWidth: availableWidth
            clip: true
            onAvailableHeightChanged: Qt.callLater(root.revealField)
            C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
            Column {
                id: body
                width: bodyScroll.availableWidth
                spacing: root.theme.dp(12)
                Text {
                    objectName: "gotoTimeTitle"
                    width: parent.width
                    text: root.title; textFormat: Text.PlainText
                    color: root.theme.bright; font.pixelSize: root.theme.dp(20)
                    wrapMode: Text.Wrap
                }
                Text {
                    width: parent.width
                    text: qsTr("Time"); textFormat: Text.PlainText
                    color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
                }
                C.TextField {
                    id: timeField
                    objectName: "gotoTimeField"
                    width: parent.width; height: root.theme.dp(37)
                    enabled: root.seekable
                    selectByMouse: true
                    inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                    font.pixelSize: root.theme.dp(14)
                    color: root.theme.playerHeading
                    selectionColor: root.theme.accent; selectedTextColor: root.theme.field
                    leftPadding: root.theme.dp(10); rightPadding: leftPadding
                    background: Rectangle {
                        color: root.theme.field; radius: root.theme.dp(6)
                        border.width: root.theme.dp(1)
                        border.color: timeField.activeFocus ? root.theme.accent : root.theme.border
                    }
                    Accessible.name: qsTr("Time")
                    Accessible.description: root.helpText
                    KeyNavigation.tab: cancelButton
                    KeyNavigation.backtab: goButton.enabled ? goButton : cancelButton
                    Keys.onReturnPressed: root.submit()
                    Keys.onEnterPressed: root.submit()
                    onActiveFocusChanged: if (activeFocus) Qt.callLater(root.revealField)
                }
                Text {
                    objectName: "gotoTimeHelp"
                    width: parent.width
                    text: root.helpText; textFormat: Text.PlainText
                    color: root.seekable && root.timestampText.trim().length && !root.valid ? root.theme.danger : root.theme.foreground
                    font.pixelSize: root.theme.dp(14); wrapMode: Text.Wrap
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
                objectName: "gotoTimeCancel"
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
                id: goButton
                objectName: "gotoTimeGo"
                Layout.fillWidth: root.stackedActions
                Layout.preferredWidth: Math.max(root.theme.dp(100), goMetrics.advanceWidth + root.theme.dp(16))
                Layout.preferredHeight: root.theme.dp(32)
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                enabled: root.valid
                caption: qsTr("Go"); captionFontWeight: Font.DemiBold; text: caption
                captionMaximumWidth: Math.max(0, width - root.theme.dp(16)); captionVerticalOffset: 1
                foreground: root.theme.accent
                background: Rectangle { color: parent.hovered ? root.theme.hover : root.theme.progress; radius: root.theme.dp(6); opacity: goButton.enabled ? 1 : 0.5; border.width: parent.visualFocus ? root.theme.dp(1) : 0; border.color: root.theme.accent }
                onClicked: root.submit()
                Keys.onReturnPressed: root.submit()
                Keys.onEnterPressed: root.submit()
            }
        }
    }
}
