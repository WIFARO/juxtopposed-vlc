// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var sourceService
    property real scaleFactor: 1
    property bool lightTheme: false
    property color accentColor: "#d37918"
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    spacing: theme.dp(10)
    Text { text: qsTr("Online Subtitles"); color: root.theme.settingsHeading; font.pixelSize: root.theme.dp(16); Layout.topMargin: root.theme.dp(24) }
    Text {
        objectName: "subtitleProviderStatus"
        Layout.fillWidth: true
        text: root.sourceService.hasCustomKey ? qsTr("OpenSubtitles uses your saved API key.")
            : root.sourceService.configured ? qsTr("OpenSubtitles uses the app key. Downloads have a daily limit for each IP address.")
            : qsTr("Add an API key to search for and download subtitles from OpenSubtitles.")
        wrapMode: Text.WordWrap; color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
    }
    RedesignButton {
        objectName: "subtitleProviderConfigure"
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        caption: root.sourceService.hasCustomKey ? qsTr("Change API key") : qsTr("Set up OpenSubtitles"); text: caption
        enabled: !root.sourceService.preparing
        onClicked: setup.open()
    }
    RedesignSubtitleProviderSetup {
        id: setup
        objectName: "subtitlePreferencesProviderSetup"
        parent: root.Window.contentItem
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        hasCustomKey: root.sourceService.hasCustomKey; hasConsumerKey: root.sourceService.hasConsumerKey
        busy: root.sourceService.preparing
        onSaveRequested: key => finish(root.sourceService.setApiKey(key))
        onRemoveRequested: finish(root.sourceService.clearApiKey())
        onHelpRequested: Qt.openUrlExternally("https://www.opensubtitles.com/consumers")
    }
}
