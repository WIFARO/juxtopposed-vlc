// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C
import VLC.Player as Redesign

FocusScope {
    id: root
    property real scaleFactor: 1
    property bool lightTheme: false
    property color accentColor: "#d37918"
    property string title: ""
    property string details: ""
    property int count: 1
    property bool warning: false
    property real maximumHeight: theme.dp(240)
    property bool copied: false
    readonly property bool held: hover.hovered || activeFocus
    readonly property Redesign.RedesignTheme theme: Redesign.RedesignTheme {
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
    }
    readonly property real detailsWidth: Math.max(theme.dp(100), detailsMetrics.advanceWidth + theme.dp(24))
    readonly property real copyWidth: Math.max(theme.dp(80), copyMetrics.advanceWidth + theme.dp(24))
    TextMetrics { id: detailsMetrics; text: detailsButton.caption; font.pixelSize: root.theme.dp(14); font.weight: Font.Medium }
    TextMetrics { id: copyMetrics; text: copyButton.caption; font.pixelSize: root.theme.dp(14) }
    signal detailsRequested()
    signal copyRequested()
    signal dismissRequested()
    implicitHeight: layout.implicitHeight + theme.dp(32)
    height: Math.min(implicitHeight, maximumHeight)
    Accessible.role: Accessible.AlertMessage
    Accessible.name: (warning ? qsTr("Warning") : qsTr("Error")) + ": " + title
    Accessible.description: details
    HoverHandler { id: hover }
    function copyDone() { copied = true; copyFeedback.restart() }
    function resetCopy() { copied = false; copyFeedback.stop() }
    onTitleChanged: resetCopy()
    onDetailsChanged: resetCopy()
    onCountChanged: resetCopy()
    Timer { id: copyFeedback; interval: 1500; onTriggered: root.copied = false }
    Keys.onEscapePressed: root.dismissRequested()
    Keys.onReturnPressed: event => { if (!event.isAutoRepeat) root.detailsRequested() }
    Keys.onEnterPressed: event => { if (!event.isAutoRepeat) root.detailsRequested() }
    Rectangle {
        anchors.fill: parent
        color: root.lightTheme ? root.theme.panel : "#191919"
        border.width: root.theme.dp(1)
        border.color: root.warning ? root.theme.accent : root.theme.danger
        radius: root.theme.dp(10)
    }
    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: root.theme.dp(16)
        spacing: root.theme.dp(8)
        RowLayout {
            Layout.fillWidth: true
            spacing: root.theme.dp(8)
            Text {
                objectName: "noticeSeverity"
                text: root.warning ? qsTr("Warning") : qsTr("Error")
                textFormat: Text.PlainText
                color: root.theme.bright
                font.pixelSize: root.theme.dp(12)
                font.weight: Font.DemiBold
            }
            Rectangle {
                visible: root.count > 1
                implicitWidth: Math.max(root.theme.dp(24), countLabel.implicitWidth + root.theme.dp(12))
                implicitHeight: root.theme.dp(24)
                radius: root.theme.dp(4)
                color: root.theme.progress
                Text {
                    id: countLabel
                    objectName: "noticeCount"
                    anchors.centerIn: parent
                    text: root.count
                    color: root.theme.accent
                    font.pixelSize: root.theme.dp(12)
                }
            }
            Item { Layout.fillWidth: true }
            Redesign.RedesignButton {
                objectName: "noticeDismiss"
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                iconName: "close"; text: qsTr("Dismiss")
                Layout.preferredWidth: root.theme.dp(28); Layout.preferredHeight: root.theme.dp(28)
                onClicked: root.dismissRequested()
                Keys.onReturnPressed: event => { if (!event.isAutoRepeat) root.dismissRequested() }
                Keys.onEnterPressed: event => { if (!event.isAutoRepeat) root.dismissRequested() }
            }
        }
        C.ScrollView {
            id: scroll
            objectName: "noticeBody"
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.minimumHeight: 0
            implicitHeight: body.implicitHeight
            contentWidth: availableWidth
            clip: true
            C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
            C.ScrollBar.vertical.objectName: "noticeScrollBar"
            C.ScrollBar.vertical.policy: C.ScrollBar.AsNeeded
            C.ScrollBar.vertical.active: true
            Column {
                id: body
                width: scroll.availableWidth
                spacing: root.theme.dp(6)
                Text {
                    id: titleLabel
                    objectName: "noticeTitle"
                    width: parent.width
                    visible: text.length > 0
                    text: root.title; textFormat: Text.PlainText
                    color: root.theme.bright; font.pixelSize: root.theme.dp(14)
                    wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight

                }
                Text {
                    id: detailLabel
                    objectName: "noticeDetails"
                    width: parent.width
                    visible: text.length > 0
                    text: root.details; textFormat: Text.PlainText
                    color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
                    wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight

                }
            }
        }
        GridLayout {
            objectName: "noticeActions"
            Layout.fillWidth: true
            columns: width < root.detailsWidth + root.copyWidth + columnSpacing ? 1 : 2
            columnSpacing: root.theme.dp(8); rowSpacing: columnSpacing
            Redesign.RedesignButton {
                id: detailsButton
                objectName: "noticeShowDetails"
                Layout.fillWidth: true; Layout.preferredHeight: root.theme.dp(32)
                Layout.preferredWidth: root.detailsWidth
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                caption: qsTr("Show details"); text: caption; primaryAction: true
                captionMaximumWidth: Math.max(0, width - root.theme.dp(16))
                tooltipEnabled: detailsMetrics.advanceWidth > captionMaximumWidth
                tooltipTextFormat: Text.PlainText
                onClicked: root.detailsRequested()
                Keys.onReturnPressed: event => { if (!event.isAutoRepeat) root.detailsRequested() }
                Keys.onEnterPressed: event => { if (!event.isAutoRepeat) root.detailsRequested() }
            }
            Redesign.RedesignButton {
                id: copyButton
                objectName: "noticeCopy"
                Layout.fillWidth: true; Layout.preferredHeight: root.theme.dp(32)
                Layout.minimumWidth: 0
                Layout.preferredWidth: root.copyWidth
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                caption: root.copied ? qsTr("Copied") : qsTr("Copy"); text: caption; filled: true
                captionMaximumWidth: Math.max(0, width - root.theme.dp(16))
                tooltipEnabled: copyMetrics.advanceWidth > captionMaximumWidth
                tooltipTextFormat: Text.PlainText
                onClicked: root.copyRequested()
                Keys.onReturnPressed: event => { if (!event.isAutoRepeat) root.copyRequested() }
                Keys.onEnterPressed: event => { if (!event.isAutoRepeat) root.copyRequested() }
            }
        }
    }
}
