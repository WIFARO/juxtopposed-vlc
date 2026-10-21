// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    property string label
    property bool expanded: false
    property bool showSeparator: true
    property real contentTopMargin: 10
    property real contentBottomMargin: 16
    property Component settings
    readonly property alias contentItem: contentLoader.item
    property real scaleFactor: 1
    property bool lightTheme: false
    property color accentColor: "#d37918"
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    Layout.leftMargin: -theme.dp(4)
    spacing: 0
    Text {
        Layout.fillWidth: true; Layout.leftMargin: theme.dp(4); Layout.topMargin: theme.dp(20); Layout.bottomMargin: theme.dp(18)
        // Preserve the label box while aligning the Inter outline.
        text: root.label; color: theme.playerLabel; font.pixelSize: theme.dp(14); topPadding: theme.dp(-1.5625); bottomPadding: -topPadding
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: column.implicitHeight
        radius: theme.dp(6); color: root.expanded ? theme.background : theme.field
        ColumnLayout {
            id: column
            width: parent.width; spacing: 0
            RedesignButton {
                objectName: "filterExpander"
                Layout.fillWidth: true; Layout.preferredHeight: theme.dp(48)
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                text: root.expanded ? qsTr("Collapse Settings") : qsTr("Expand Settings")
                Accessible.name: root.label + ": " + text
                background: Rectangle { radius: theme.dp(6); color: parent.hovered ? theme.hover : "transparent"; border.width: parent.visualFocus ? 1 : 0; border.color: theme.accent }
                contentItem: RowLayout {
                    spacing: theme.dp(8)
                    Text { Layout.fillWidth: true; Layout.leftMargin: theme.dp(16); text: parent.parent.text; color: theme.playerLabel; font.pixelSize: theme.dp(14) }
                    RedesignIcon { Layout.rightMargin: theme.dp(12); name: root.expanded ? "chevron-down" : "chevron-right"; color: theme.foreground; Layout.preferredWidth: theme.dp(20); Layout.preferredHeight: theme.dp(20) }
                }
                onClicked: root.expanded = !root.expanded
            }
            Loader {
                id: contentLoader
                objectName: "filterContent"
                Layout.fillWidth: true; Layout.leftMargin: theme.dp(16); Layout.rightMargin: theme.dp(16)
                Layout.topMargin: visible ? theme.dp(root.contentTopMargin) : 0; Layout.bottomMargin: visible ? theme.dp(root.contentBottomMargin) : 0
                visible: root.expanded
                sourceComponent: root.settings
            }
        }
    }
    Rectangle {
        Layout.fillWidth: true; Layout.leftMargin: -theme.dp(20); Layout.rightMargin: -theme.dp(20)
        Layout.topMargin: theme.dp(20)
        Layout.preferredHeight: root.showSeparator ? theme.dp(1) : 0
        Layout.maximumHeight: Layout.preferredHeight
        color: theme.border
    }
}
