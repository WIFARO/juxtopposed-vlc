// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property bool playing: false
    property bool started: false
    property bool seekable: false
    property bool pausable: true
    property bool hasPrevious: false
    property bool hasNext: false
    property bool hasVideo: false
    property bool audioWaveformEnabled: false
    property var audioWaveformPeaks: []
    property bool previewEnabled: false
    property real position: 0
    property real volume: 1
    property real maxVolume: 1.25
    property bool muted: false
    property string mediaTitle: ""
    property string folderTitle: ""
    property string artist: ""
    property bool canBrowseMedia: true
    property url previewImage
    property Component thumbnailContent
    property Component videoBackdropContent
    property real previewImagePosition: -1
    property var timeForPosition: function(position) { return "" }
    property bool userMoved: false
    property real draggedX: 0
    property real draggedY: 0
    property real minimumY: theme.dp(46)
    readonly property color metadataColor: lightTheme ? theme.foreground : "#969696"
    readonly property alias videoArea: videoHost
    readonly property real seekY: seek.y
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    signal togglePlayback()
    signal jumpRequested(int seconds)
    signal volumeRequested(real volume)
    signal volumeWheelRequested(var event)
    signal muteRequested()
    signal subtitleTracksRequested(Item anchor)
    signal previous()
    signal next()
    signal seekRequested(real position)
    signal seekDragStarted()
    signal seekDragFinished(real position, bool commit)
    signal previewRequested(real position)
    signal previewEnded()
    signal openPlayerRequested()
    signal closeRequested()
    signal queueRequested()
    signal folderRequested()
    signal artistRequested()

    implicitWidth: theme.dp(391)
    height: Math.ceil(videoHost.height + theme.dp(130))
    x: userMoved ? draggedX : parent ? parent.width - width - theme.dp(6) : 0
    y: userMoved ? draggedY : parent ? parent.height - height - theme.dp(6) : 0
    color: theme.background; radius: theme.dp(14)
    HoverHandler { id: hover; blocking: false }

    function refreshPreview() { seek.refreshPreview() }
    function constrainPosition() {
        if (!parent || !userMoved) return
        draggedX = Math.max(theme.dp(6), Math.min(draggedX, parent.width - width - theme.dp(6)))
        draggedY = Math.max(minimumY, Math.min(draggedY, parent.height - height - theme.dp(6)))
    }
    onWidthChanged: constrainPosition()
    onHeightChanged: constrainPosition()
    Connections {
        target: root.parent
        function onWidthChanged() { root.constrainPosition() }
        function onHeightChanged() { root.constrainPosition() }
    }
    component MoveHandler: DragHandler {
        target: null
        property real originX: 0
        property real originY: 0
        onActiveChanged: if (active) {
            originX = root.x; originY = root.y
            root.draggedX = originX; root.draggedY = originY
            root.userMoved = true
        }
        onActiveTranslationChanged: if (active) {
            root.draggedX = originX + activeTranslation.x
            root.draggedY = originY + activeTranslation.y
            root.constrainPosition()
        }
    }
    component MiniButton: RedesignButton {
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
    }
    component MetadataButton: MiniButton {
        id: metadataButton
        property string label
        Layout.fillWidth: true
        Layout.minimumWidth: Math.min(implicitWidth, metadataRow.labelSpace / (artistButton.visible ? 2 : 1))
        Layout.maximumWidth: implicitWidth
        implicitWidth: Math.round(root.theme.dp(48) + labelMetrics.advanceWidth)
        caption: label; captionFontSize: 16; captionSpacing: 8
        captionMaximumWidth: Math.max(0, width - root.theme.dp(48))
        leftAligned: true; contentLeftMargin: 8
        foreground: root.metadataColor; iconForeground: root.theme.foreground
        TextMetrics {
            id: labelMetrics
            text: metadataButton.label
            font.family: metadataButton.font.family
            font.pixelSize: root.theme.dp(16)
        }
    }
    Item {
        id: videoHost
        objectName: "miniPlayerVideoHost"
        x: theme.dp(10); y: theme.dp(10)
        width: parent.width - theme.dp(20); height: width * 9 / 16
        // Consume clicks before VideoSurface can forward them as pause hotkeys.
        // Only this picture area (not the controls above it) starts a drag.
        MouseArea {
            anchors.fill: parent
            z: 2
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.OpenHandCursor
            onDoubleClicked: root.openPlayerRequested()
            MoveHandler {}
        }
        Rectangle {
            objectName: "miniPlayerVideoBorder"
            // Paint the reference border outside the top and sides of the video.
            x: -root.theme.dp(2); y: -root.theme.dp(2)
            width: parent.width + root.theme.dp(4)
            height: Math.ceil(parent.height + root.theme.dp(2))
            z: 3; color: "transparent"; radius: root.theme.dp(6)
            border.color: root.theme.field; border.width: root.theme.dp(2)
        }
        Item {
            anchors.fill: parent; z: 4
            HoverHandler { id: videoHover; blocking: false }
            FocusScope {
                id: controlsScope
                anchors.fill: parent
                visible: hover.hovered || videoHover.hovered || activeFocus || seek.activeFocus || folder.activeFocus || artistButton.activeFocus || queueButton.activeFocus
                RowLayout {
                    anchors.top: parent.top; anchors.right: parent.right
                    anchors.margins: root.theme.dp(4); spacing: 0
                    MiniButton { objectName: "miniPlayerExpandButton"; iconName: "expand"; text: qsTr("Open player"); onClicked: root.openPlayerRequested() }
                    MiniButton { objectName: "miniPlayerCloseButton"; iconName: "close"; text: qsTr("Hide mini player"); onClicked: root.closeRequested() }
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: controls.implicitWidth; height: root.theme.dp(40)
                    radius: root.theme.dp(6); color: root.theme.panel; opacity: 0.9
                }
                RowLayout {
                    id: controls
                    anchors.centerIn: parent; spacing: root.theme.dp(4)
                    MiniButton { objectName: "miniPlayerBackwardButton"; iconName: "backward-10-sec"; text: qsTr("Back 10 seconds"); enabled: root.started && root.seekable; onClicked: root.jumpRequested(-10) }
                    MiniButton { objectName: "miniPlayerPlayButton"; iconName: root.playing ? "pause" : "play"; text: root.playing ? qsTr("Pause") : qsTr("Play"); enabled: !root.started || root.pausable; onClicked: root.togglePlayback() }
                    MiniButton { objectName: "miniPlayerForwardButton"; iconName: "forward-10-sec"; text: qsTr("Forward 10 seconds"); enabled: root.started && root.seekable; onClicked: root.jumpRequested(10) }
                }
                RowLayout {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.margins: root.theme.dp(4); spacing: root.theme.dp(4)
                    MiniButton {
                        objectName: "miniPlayerMuteButton"; iconName: root.muted || root.volume === 0 ? "muted" : "volume"
                        text: root.muted ? qsTr("Unmute") : qsTr("Mute")
                        enabled: root.volume >= 0
                        onClicked: root.muteRequested()
                    }
                    RedesignVolume {
                        objectName: "miniPlayerVolumeSlider"
                        Layout.fillWidth: true; Layout.maximumWidth: root.theme.dp(80); Layout.minimumWidth: root.theme.dp(24)
                        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                        currentVolume: root.volume; value: root.volume; to: root.maxVolume; muted: root.muted
                        onMoved: root.volumeRequested(value)
                        onWheelRequested: event => root.volumeWheelRequested(event)
                    }
                    Item { Layout.fillWidth: true }
                    MiniButton { objectName: "miniPlayerSubtitleButton"; iconName: "subtitles"; text: qsTr("Subtitles"); enabled: root.started; onClicked: root.subtitleTracksRequested(this) }
                }
            }
        }
    }
    RedesignPlayerSeekBar {
        id: seek
        objectName: "miniPlayerSeekBar"
        x: theme.dp(8); y: Math.ceil(videoHost.y + videoHost.height) + theme.dp(4)
        width: parent.width - theme.dp(16); height: theme.dp(16)
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        enabled: root.seekable
        mediaStarted: root.started; playerPosition: root.position
        waveformEnabled: root.audioWaveformEnabled
        waveformPeaks: root.audioWaveformPeaks
        previewEnabled: root.previewEnabled && root.hasVideo && root.started
        thumbnailContent: root.thumbnailContent
        videoBackdropContent: root.videoBackdropContent
        previewImage: root.previewImage; previewImagePosition: root.previewImagePosition
        timeForPosition: root.timeForPosition
        onSeekRequested: position => root.seekRequested(position)
        onDragStarted: root.seekDragStarted()
        onDragFinished: (position, commit) => root.seekDragFinished(position, commit)
        onPreviewRequested: position => root.previewRequested(position)
        onPreviewEnded: root.previewEnded()
    }
    Text {
        objectName: "miniPlayerTitle"
        x: theme.dp(16); y: parent.height - theme.dp(62) - baselineOffset
        width: parent.width - theme.dp(32)
        text: root.mediaTitle; elide: Text.ElideRight
        color: root.metadataColor; font.pixelSize: theme.dp(16)
        MoveHandler {}
    }
    RowLayout {
        id: metadataRow
        readonly property real labelSpace: Math.max(0, width - queueButton.width - spacing * (artistButton.visible ? 3 : 2))
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: theme.dp(8); anchors.rightMargin: theme.dp(13)
        y: parent.height - theme.dp(46)
        height: theme.dp(36); spacing: theme.dp(4)
        MetadataButton {
            id: folder
            objectName: "miniPlayerFolderButton"
            iconName: "folder"; label: root.folderTitle
            text: qsTr("Open media folder"); enabled: root.canBrowseMedia && root.folderTitle.length > 0
            onClicked: root.folderRequested()
        }
        MetadataButton {
            id: artistButton
            objectName: "miniPlayerArtistButton"
            iconName: "artist"; label: root.artist
            text: qsTr("Find artist"); visible: root.artist.length > 0
            enabled: root.canBrowseMedia
            onClicked: root.artistRequested()
        }
        Item { Layout.fillWidth: true }
        MiniButton { id: queueButton; objectName: "miniPlayerQueueButton"; iconName: "lists"; text: qsTr("Play queue"); onClicked: root.queueRequested() }
    }
}
