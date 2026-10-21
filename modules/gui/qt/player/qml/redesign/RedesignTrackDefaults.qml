// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var backend
    property bool subtitles: false
    property real textInset: 0
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    spacing: 0
    Text {
        Layout.fillWidth: true; Layout.bottomMargin: theme.dp(6); Layout.leftMargin: theme.dp(root.textInset)
        text: qsTr("Defaults"); color: theme.playerHeading; font.pixelSize: theme.dp(16); font.weight: Font.Medium
    }
    RedesignPreferenceRow {
        objectName: "defaultTrackSetting"
        Layout.fillWidth: true; Layout.leftMargin: theme.dp(root.textInset)
        backend: root.backend
        optionName: root.subtitles ? "redesign-sub-track" : "redesign-audio-track"
        label: root.subtitles ? qsTr("Subtitle Track") : qsTr("Audio Track")
        presentation: ({compact: true, rowHeight: 53})
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
    }
    RedesignPreferenceRow {
        objectName: "defaultLanguageSetting"
        Layout.fillWidth: true; Layout.leftMargin: theme.dp(root.textInset)
        backend: root.backend
        optionName: root.subtitles ? "redesign-sub-language" : "redesign-audio-language"
        label: root.subtitles ? qsTr("Subtitle Language") : qsTr("Audio Language")
        presentation: ({compact: true, rowHeight: 53})
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
    }
}
