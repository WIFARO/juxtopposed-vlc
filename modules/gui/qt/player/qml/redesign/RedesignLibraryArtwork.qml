// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

Item {
    id: root
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property url source
    property string iconName: "video"
    property real iconSize: 24
    property real cornerRadius: 12
    // Only this provider uses a one-pixel transparent result for missing art.
    // Keep genuine one-pixel files and transparent artwork as real images.
    readonly property bool hasArtwork: artworkImage.status === Image.Ready
        && !(String(root.source).startsWith("image://mlcustomcover?")
            && artworkImage.sourceSize.width === 1 && artworkImage.sourceSize.height === 1)
    property real progress: 0
    property real progressHeight: 4
    property bool roundProgress: true
    property bool showPlaceholderBackground: true
    property color surroundColor: theme.panel
    property color placeholderColor: lightTheme ? theme.field : iconName === "folder" ? "#222222" : "#171717"
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    clip: true
    Rectangle {
        anchors.fill: parent
        visible: root.showPlaceholderBackground || root.hasArtwork
        color: root.placeholderColor
    }
    RedesignIcon { anchors.centerIn: parent; width: root.theme.dp(root.iconSize); height: width; name: root.iconName; color: root.theme.foreground; visible: !root.hasArtwork }
    Image { id: artworkImage; objectName: "libraryArtworkImage"; anchors.fill: parent; source: root.source; fillMode: Image.PreserveAspectCrop; asynchronous: true; visible: root.hasArtwork }
    Rectangle {
        anchors.left: parent.left; anchors.bottom: parent.bottom
        z: root.roundProgress ? 0 : 1
        width: parent.width * Math.max(0, Math.min(1, root.progress))
        height: root.theme.dp(root.progressHeight); color: root.theme.accent
        visible: root.progress > 0
    }
    // A border masks the image corners on both native and software renderers.
    Rectangle {
        anchors.fill: parent; anchors.margins: -root.theme.dp(root.cornerRadius)
        radius: root.theme.dp(root.cornerRadius * 2)
        color: "transparent"; border.color: root.surroundColor
        border.width: root.theme.dp(root.cornerRadius)
        visible: root.cornerRadius > 0 && (root.showPlaceholderBackground || root.hasArtwork)
    }
}
