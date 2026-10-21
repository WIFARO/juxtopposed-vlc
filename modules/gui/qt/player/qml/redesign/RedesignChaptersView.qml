// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

FocusScope {
    id: root
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property var chaptersModel
    property alias currentIndex: list.currentIndex
    property bool keyboardNavigation: false
    readonly property int count: list.count
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    signal chapterRequested(int index)
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Chapters")

    function clockText(time) {
        const seconds = typeof time === "number" ? time
            : time && typeof time.toSeconds === "function" ? time.toSeconds() : NaN
        if (!Number.isFinite(seconds) || seconds < 0) return "--:--:--"
        const total = Math.floor(seconds)
        const pad = value => String(value).padStart(2, "0")
        return pad(Math.floor(total / 3600)) + ":" + pad(Math.floor(total / 60) % 60) + ":" + pad(total % 60)
    }
    function activateCurrent() {
        if (currentIndex >= 0 && currentIndex < count) chapterRequested(currentIndex)
    }
    Keys.onReturnPressed: activateCurrent()
    Keys.onEnterPressed: activateCurrent()
    Keys.onSpacePressed: activateCurrent()
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            if (count > 0) {
                keyboardNavigation = true
                currentIndex = Math.max(0, Math.min(count - 1, currentIndex + (event.key === Qt.Key_Up ? -1 : 1)))
                list.positionViewAtIndex(currentIndex, ListView.Contain)
            }
            event.accepted = true
        } else event.accepted = false
    }
    Connections {
        target: root.chaptersModel
        ignoreUnknownSignals: true
        function onModelReset() { root.currentIndex = -1; root.keyboardNavigation = false }
        function onRowsRemoved() { root.currentIndex = -1; root.keyboardNavigation = false }
    }
    TextMetrics { id: numberMetrics; text: String(list.count); font.pixelSize: theme.dp(14); font.weight: Font.Medium }
    ListView {
        id: list
        objectName: "chapterList"
        anchors.fill: parent
        anchors.leftMargin: theme.dp(20); anchors.rightMargin: theme.dp(24)
        anchors.topMargin: theme.dp(16); anchors.bottomMargin: theme.dp(16)
        model: root.chaptersModel
        clip: true
        C.ScrollBar.vertical: C.ScrollBar {}
        delegate: RedesignButton {
            id: row
            required property int index
            required property var model
            readonly property string title: model.display.trim()
            readonly property bool currentChapter: model.checked
            readonly property var chapterTime: model.time
            objectName: "chapterRow" + index
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            width: list.width; height: theme.dp(44); padding: 0
            bottomPadding: theme.dp(2)
            text: (index + 1) + " " + row.title + " " + root.clockText(row.chapterTime)
            selected: currentChapter
            Keys.forwardTo: [root]
            function followCurrentChapter() {
                if (currentChapter && !root.keyboardNavigation) root.currentIndex = index
            }
            Component.onCompleted: Qt.callLater(followCurrentChapter)
            onCurrentChapterChanged: Qt.callLater(followCurrentChapter)
            onClicked: {
                row.forceActiveFocus(Qt.MouseFocusReason)
                root.keyboardNavigation = false; root.currentIndex = index
                root.chapterRequested(index)
            }
            background: Rectangle {
                radius: theme.dp(4)
                color: row.selected ? theme.selected : row.hovered ? theme.hover : "transparent"
                border.color: root.keyboardNavigation && root.activeFocus && root.currentIndex === row.index ? theme.accent : "transparent"
            }
            contentItem: RowLayout {
                spacing: theme.dp(32)
                Text {
                    objectName: "chapterNumber" + row.index
                    Layout.preferredWidth: Math.max(theme.dp(16), numberMetrics.width + theme.dp(4))
                    leftPadding: theme.dp(4)
                    text: row.index + 1
                    color: theme.accent; font.pixelSize: theme.dp(14); font.weight: Font.Medium
                }
                Text {
                    objectName: "chapterTitle" + row.index
                    Layout.fillWidth: true
                    text: row.title; elide: Text.ElideRight
                    color: theme.foreground; font.pixelSize: theme.dp(14)
                }
                Text {
                    objectName: "chapterTime" + row.index
                    text: root.clockText(row.chapterTime)
                    color: theme.playerLabel; font.pixelSize: theme.dp(14)
                }
            }
        }
        Text {
            anchors.fill: parent; visible: list.count === 0
            text: qsTr("This media has no chapters.")
            color: theme.foreground; font.pixelSize: theme.dp(14); wrapMode: Text.WordWrap
        }
    }
}
