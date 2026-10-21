// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import VLC.Util

Item {
    id: root
    property real scaleFactor: 1
    property url source
    property string title: ""
    property string artist: ""
    property bool showTitle: true
    clip: !showTitle
    readonly property bool ready: cover.status === Image.Ready
    readonly property bool localArtwork: source.toString().startsWith("file:") || source.toString().startsWith("qrc:")
    // The full player frame places a 738 by 737 cover in a 986-pixel viewport.
    readonly property real coverSize: Math.max(0, Math.min(width * .8, height * 738 / 986))

    Image {
        objectName: "audioArtworkBackdrop"
        anchors.centerIn: parent
        width: Math.max(root.width, root.height) + 52 * root.scaleFactor
        height: width
        source: root.localArtwork && root.ready ? Effects.url(Effects.Artwork, {source: root.source, blur: 10 / 1952}) : ""
        sourceSize: Qt.size(512, 512)
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        visible: root.ready
    }
    Item {
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height * (220 / 3) / 986
        width: root.coverSize; height: width * 737 / 738
        Image {
            anchors.centerIn: parent
            width: parent.width * 1047 / 738; height: width
            source: root.ready ? Effects.url(Effects.RoundedRectDropShadow, {
                viewportWidth: 1047, viewportHeight: 1047,
                rectWidth: 866, rectHeight: 866, blurRadius: 90.5,
                color: Qt.rgba(1, 1, 1, .03), xRadius: 84, yRadius: 84
            }) : ""
            sourceSize: Qt.size(512, 512)
            asynchronous: true
        }
        Item {
            objectName: "audioArtworkFrame"
            anchors.fill: parent
            clip: true
            Image {
                id: cover
                objectName: "audioArtworkCover"
                anchors.horizontalCenter: parent.horizontalCenter
                y: (parent.height - height) / 2
                // Crop half a reference pixel at each edge without stretching the image.
                width: parent.width; height: width
                source: !root.source.toString() ? "" : root.localArtwork
                    ? Effects.url(Effects.Artwork, {source: root.source, radius: 20 / 738}) : root.source
                sourceSize: Qt.size(1024, 1024)
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                visible: root.ready
            }
        }
    }
    Text {
        objectName: "audioArtworkTitle"
        anchors.horizontalCenter: parent.horizontalCenter
        // The reference baseline is 906 2/3 pixels into its 986-pixel viewport.
        y: parent.height * 906.6667 / 986 - baselineOffset
        width: Math.max(0, parent.width - 40 * root.scaleFactor)
        text: root.title + (root.artist.length ? " - " + root.artist : "")
        textFormat: Text.PlainText
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        color: "#969696"
        font.pixelSize: 24 * root.scaleFactor
        font.weight: Font.Medium
        visible: root.ready && root.showTitle
    }
}
