// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

// A transparent, signal-only PiP overlay. The picture is a drag surface, never
// a play/pause target; buttons and sliders own their input above that surface.
FocusScope {
    id: root
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool started: false
    property bool playing: false
    property bool pausable: true
    property bool seekable: false
    property bool muted: false
    property real volume: 1
    property real maxVolume: 1.25
    property real position: 0
    property string mediaTitle: ""
    property string timeText: "00:00"
    property string lengthText: "00:00"
    property var timeForPosition: function(position) { return "" }
    property var subtitleTracks: null
    property bool resizeEnabled: false
    property alias dragTarget: moveHandler.target
    property real dragXMin: -Infinity
    property real dragXMax: Infinity
    property real dragYMin: -Infinity
    property real dragYMax: Infinity
    readonly property bool dragging: moveHandler.active
    property int controlsHideTimeout: 2200
    property bool _revealed: true
    readonly property bool keyboardFocus: {
        const item = root.Window.window ? root.Window.window.activeFocusItem : null
        return (header.activeFocus || footer.activeFocus) && !!item && item.visualFocus === true
    }
    readonly property bool controlsVisible: _revealed || !playing || keyboardFocus
        || seek.dragActive || volumeSlider.pressed || subtitles.visible || moveHandler.active
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor }

    signal togglePlayback()
    signal jumpRequested(int seconds)
    signal seekRequested(real position)
    signal seekDragStarted()
    signal seekDragFinished(real position, bool commit)
    signal volumeRequested(real volume)
    signal muteRequested()
    signal moveRequested()
    signal resizeRequested(int edges)
    signal returnRequested()
    signal closeRequested()
    signal subtitleSelectionRequested(int index, bool selected)
    signal subtitlesOffRequested()
    signal addSubtitlesRequested()
    signal keyPressed(var event)

    function revealControls() { _revealed = true; hideTimer.restart() }
    function dismissSubtitles() { subtitles.close() }
    onVisibleChanged: {
        if (visible) revealControls()
        else dismissSubtitles()
    }
    Keys.priority: Keys.AfterItem
    Keys.onEscapePressed: event => {
        if (subtitles.visible) subtitles.close()
        else root.returnRequested()
        event.accepted = true
    }
    Keys.onPressed: event => {
        revealControls()
        root.keyPressed(event)
    }
    Timer {
        id: hideTimer
        interval: root.controlsHideTimeout
        running: true
        onTriggered: {
            root._revealed = false
            if (!root.keyboardFocus && (header.activeFocus || footer.activeFocus)) root.forceActiveFocus()
        }
    }
    HoverHandler {
        blocking: false
        onPointChanged: if (hovered) root.revealControls()
    }
    MouseArea {
        id: picture
        objectName: "pipDragSurface"
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        cursorShape: moveHandler.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        onPressed: { root.forceActiveFocus(); root.revealControls() }
        onDoubleClicked: root.returnRequested()
        DragHandler {
            id: moveHandler
            target: null
            acceptedButtons: Qt.LeftButton
            // Do not steal a seek, volume drag, or button press.
            grabPermissions: PointerHandler.CanTakeOverFromItems
            xAxis.minimum: root.dragXMin; xAxis.maximum: root.dragXMax
            yAxis.minimum: root.dragYMin; yAxis.maximum: root.dragYMax
            onActiveChanged: if (active) root.moveRequested()
        }
    }

    component PipButton: RedesignButton {
        scaleFactor: root.scaleFactor
        accentColor: root.accentColor
        foreground: "white"
        iconForeground: "white"
        background: Rectangle {
            radius: height / 2
            color: parent.down ? "#a0505050" : parent.hovered ? "#80505050" : "#40000000"
            border.width: parent.visualFocus ? 1 : 0
            border.color: root.accentColor
        }
    }
    FocusScope {
        id: header
        z: 1
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: theme.dp(52)
        visible: root.controlsVisible
        Rectangle {
            anchors.fill: parent
            gradient: Gradient { GradientStop { position: 0; color: "#b0000000" } GradientStop { position: 1; color: "transparent" } }
        }
        RowLayout {
            anchors.fill: parent; anchors.margins: root.theme.dp(8)
            spacing: root.theme.dp(4)
            Text {
                Layout.fillWidth: true
                text: root.mediaTitle; textFormat: Text.PlainText
                color: "white"; font.pixelSize: root.theme.dp(13); elide: Text.ElideRight
            }
            PipButton { objectName: "pipReturnButton"; iconName: "expand"; text: qsTr("Return to player"); onClicked: root.returnRequested() }
            PipButton { objectName: "pipCloseButton"; iconName: "close"; text: qsTr("Close Picture in Picture"); onClicked: root.closeRequested() }
        }
    }
    FocusScope {
        id: footer
        z: 1
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: theme.dp(96)
        visible: root.controlsVisible
        Rectangle {
            anchors.fill: parent
            gradient: Gradient { GradientStop { position: 0; color: "transparent" } GradientStop { position: 1; color: "#e6000000" } }
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: root.theme.dp(10)
            spacing: root.theme.dp(4)
            RowLayout {
                Layout.fillWidth: true
                spacing: root.theme.dp(8)
                Text { text: root.timeText; color: "white"; font.pixelSize: root.theme.dp(11) }
                RedesignPlayerSeekBar {
                    id: seek
                    objectName: "pipTimeline"
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.theme.dp(20)
                    scaleFactor: root.scaleFactor; accentColor: root.accentColor
                    enabled: root.started && root.seekable
                    mediaStarted: root.started; playerPosition: root.position
                    timeForPosition: root.timeForPosition
                    onSeekRequested: position => root.seekRequested(position)
                    onDragStarted: root.seekDragStarted()
                    onDragFinished: (position, commit) => root.seekDragFinished(position, commit)
                }
                Text { text: root.lengthText; color: "white"; font.pixelSize: root.theme.dp(11) }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: root.theme.dp(4)
                PipButton {
                    objectName: "pipMuteButton"
                    iconName: root.muted || root.volume === 0 ? "muted" : "volume"
                    text: root.muted ? qsTr("Unmute") : qsTr("Mute")
                    enabled: root.volume >= 0
                    onClicked: root.muteRequested()
                }
                C.Slider {
                    id: volumeSlider
                    objectName: "pipVolumeSlider"
                    Layout.preferredWidth: root.theme.dp(64)
                    Layout.minimumWidth: root.theme.dp(36)
                    Layout.fillWidth: true
                    Layout.maximumWidth: root.theme.dp(88)
                    from: 0; to: root.maxVolume; stepSize: 0.05
                    wheelEnabled: true
                    enabled: root.volume >= 0
                    value: root.volume
                    Accessible.name: qsTr("Volume")
                    onMoved: root.volumeRequested(value)
                    background: Rectangle {
                        x: volumeSlider.leftPadding; y: (volumeSlider.height - height) / 2
                        width: volumeSlider.availableWidth; height: root.theme.dp(4); radius: height / 2
                        color: "#666666"
                        Rectangle { width: volumeSlider.visualPosition * parent.width; height: parent.height; radius: parent.radius; color: root.muted ? "#999999" : "white" }
                    }
                    handle: Rectangle {
                        x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                        y: (volumeSlider.height - height) / 2
                        width: root.theme.dp(10); height: width; radius: width / 2
                        color: "white"; border.color: volumeSlider.visualFocus ? root.accentColor : "transparent"
                    }
                }
                Item { Layout.fillWidth: true }
                PipButton { objectName: "pipBackwardButton"; iconName: "backward-10-sec"; text: qsTr("Back 10 seconds"); enabled: root.started && root.seekable; onClicked: root.jumpRequested(-10) }
                PipButton { objectName: "pipPlayButton"; iconName: root.playing ? "pause" : "play"; text: root.playing ? qsTr("Pause") : qsTr("Play"); enabled: root.started && root.pausable; onClicked: root.togglePlayback() }
                PipButton { objectName: "pipForwardButton"; iconName: "forward-10-sec"; text: qsTr("Forward 10 seconds"); enabled: root.started && root.seekable; onClicked: root.jumpRequested(10) }
                Item { Layout.fillWidth: true }
                PipButton {
                    id: subtitleButton
                    objectName: "pipSubtitleButton"; iconName: "subtitles"; text: qsTr("Subtitles")
                    enabled: root.started
                    onClicked: subtitles.visible ? subtitles.close() : subtitles.open()
                }
            }
        }
    }

    component SubtitleChoice: C.ItemDelegate {
        id: choice
        width: subtitleScroll.availableWidth
        contentItem: Text {
            text: choice.text; textFormat: Text.PlainText
            color: "white"; font.pixelSize: root.theme.dp(13)
            elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
        }
        // Keep keyboard-selected tracks visible even in a very short PiP.
        onActiveFocusChanged: if (activeFocus) {
            const flick = subtitleScroll.contentItem
            if (y < flick.contentY) flick.contentY = y
            else if (y + height > flick.contentY + flick.height) flick.contentY = y + height - flick.height
        }
    }
    C.Popup {
        id: subtitles
        objectName: "pipSubtitlePopup"
        parent: subtitleButton
        x: { subtitleButton.x; return root.mapToItem(subtitleButton, root.width - width - root.theme.dp(8), 0).x }
        y: { subtitleButton.y; return root.mapToItem(subtitleButton, 0, Math.max(root.theme.dp(8), footer.y - height)).y }
        width: Math.max(0, Math.min(root.theme.dp(280), root.width - root.theme.dp(16)))
        height: Math.max(0, Math.min(root.height - root.theme.dp(80), subtitleChoices.implicitHeight + padding * 2))
        padding: root.theme.dp(8)
        focus: true
        palette.text: "white"
        palette.highlight: "#505058"
        palette.highlightedText: "white"
        closePolicy: C.Popup.CloseOnEscape | C.Popup.CloseOnPressOutsideParent
        background: Rectangle { color: "#202024"; radius: root.theme.dp(8); border.color: "#505054" }
        C.ScrollView {
            id: subtitleScroll
            anchors.fill: parent
            contentWidth: availableWidth
            clip: true
            Column {
                id: subtitleChoices
                width: subtitleScroll.availableWidth
                SubtitleChoice {
                    objectName: "pipSubtitlesOff"
                    text: qsTr("Subtitles off")
                    highlighted: {
                        for (let i = 0; i < trackChoices.count; ++i) {
                            const choice = trackChoices.itemAt(i)
                            if (choice && choice.highlighted) return false
                        }
                        return true
                    }
                    onClicked: { root.subtitlesOffRequested(); subtitles.close() }
                }
                Repeater {
                    id: trackChoices
                    model: root.subtitleTracks
                    SubtitleChoice {
                        required property int index
                        required property var model
                        objectName: "pipSubtitleTrack_" + index
                        text: String(model.displayLabel || model.display || "")
                        highlighted: !!model.checked
                        onClicked: { root.subtitleSelectionRequested(index, true); subtitles.close() }
                    }
                }
                SubtitleChoice {
                    objectName: "pipAddSubtitles"
                    text: qsTr("Add subtitle file…")
                    onClicked: { subtitles.close(); root.addSubtitlesRequested() }
                }
            }
        }
    }

    // Native system resize preserves Windows' DPI, minimum-size and snapping
    // behavior. These thin targets never overlap a playback control.
    Repeater {
        model: root.resizeEnabled ? [Qt.LeftEdge, Qt.RightEdge, Qt.TopEdge, Qt.BottomEdge,
            Qt.LeftEdge | Qt.TopEdge, Qt.RightEdge | Qt.TopEdge,
            Qt.LeftEdge | Qt.BottomEdge, Qt.RightEdge | Qt.BottomEdge] : []
        MouseArea {
            required property int modelData
            readonly property real border: root.theme.dp(6)
            readonly property bool horizontal: !!(modelData & (Qt.LeftEdge | Qt.RightEdge))
            readonly property bool vertical: !!(modelData & (Qt.TopEdge | Qt.BottomEdge))
            z: 3
            x: modelData & Qt.LeftEdge ? 0 : modelData & Qt.RightEdge ? root.width - border : border
            y: modelData & Qt.TopEdge ? 0 : modelData & Qt.BottomEdge ? root.height - border : border
            width: horizontal ? border : Math.max(0, root.width - border * 2)
            height: vertical ? border : Math.max(0, root.height - border * 2)
            cursorShape: !vertical ? Qt.SizeHorCursor : !horizontal ? Qt.SizeVerCursor
                : modelData === (Qt.LeftEdge | Qt.TopEdge) || modelData === (Qt.RightEdge | Qt.BottomEdge) ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
            onPressed: root.resizeRequested(modelData)
        }
    }
}
