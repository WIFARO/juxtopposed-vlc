// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C
import VLC.Player
import VLC.Playlist
import VLC.Dialogs

C.Pane {
    id: root
    padding: 0
    palette.window: theme.panel
    palette.base: theme.field
    palette.text: theme.foreground
    palette.button: theme.background
    palette.buttonText: theme.bright
    palette.highlight: theme.accent
    palette.highlightedText: "#161616"
    background: Rectangle { color: "transparent" }
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property Component videoBackdropContent
    property string page: "playback"
    property bool showStatistics: false
    property bool showSubtitleTimeline: false
    property string subtitleTimelineState: "off"
    property int sleepTimerMinutes: 0
    required property var preferenceStore
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    signal pageRequested(string page)
    signal closeRequested()
    signal sleepTimerRequested(int minutes)
    signal statisticsRequested(bool enabled)

    function select(model, index, selected) { model.setData(model.index(index, 0), selected, Qt.CheckStateRole) }
    function disable(model) { for (let i = 0; i < model.rowCount(); ++i) select(model, i, false) }

    function chooseVisualization(index, waveform) {
        const model = Player.audioVisualization
        const value = waveform ? "" : String(model.choiceValue(index))
        if (!preferenceStore.stage("qt-audio-waveform", waveform)) return
        const effect = ["scope", "spectrum", "spectrometer", "vuMeter", "dummy"].includes(value)
        if (effect && !preferenceStore.stage("effect-list", value)) return
        if (!preferenceStore.stage("audio-visual", effect ? "visual" : value || "none")) return
        preferenceStore.save()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true; height: theme.dp(50); color: theme.field
            radius: theme.dp(10)
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: theme.dp(10); color: parent.color }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: theme.dp(14); anchors.rightMargin: theme.dp(14)
                spacing: theme.dp(root.width > theme.dp(360) ? 49 / 3 : 14)
                Repeater {
                    model: [{key: "playback", icon: "playback-selected-two-tone", label: qsTr("Playback")}, {key: "video", icon: "video-two-tone", label: qsTr("Video")}, {key: "audio", icon: "audio-two-tone", label: qsTr("Audio")}, {key: "subtitles", icon: "subtitles-two-tone", label: qsTr("Subtitles")}]
                    RedesignButton {
                        required property var modelData
                        objectName: "settingsTab_" + modelData.key
                        Layout.fillWidth: true
                        Layout.preferredWidth: root.width > theme.dp(360) ? theme.dp(modelData.key === "playback" || modelData.key === "subtitles" ? 91.5 : 70) : 1
                        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                        Layout.fillHeight: true
                        captionFontSize: 12; iconSize: theme.dp(20); captionSpacing: 8
                        topPadding: 0; bottomPadding: theme.dp(2)
                        selectedForeground: theme.foreground
                        selectedIconForeground: modelData.key === "video" ? theme.accent : modelData.key === "playback" ? theme.foreground : theme.playerLabel
                        iconForeground: root.lightTheme ? Qt.lighter(theme.foreground, 1.5) : "#404040"
                        foreground: root.lightTheme ? Qt.lighter(theme.foreground, 1.5) : theme.playerLabel
                        iconDetailColor: selected ? modelData.key === "video" ? theme.playerLabel : theme.menuAccent : iconForeground
                        background: Rectangle { color: parent.hovered ? theme.hover : "transparent"; border.width: parent.visualFocus ? 1 : 0; border.color: theme.accent }
                        iconName: modelData.key === "playback" ? modelData.icon : "nav-" + modelData.key
                        selectedIconName: modelData.icon; text: modelData.label
                        caption: root.width > theme.dp(360) ? modelData.label : ""
                        selected: root.page === modelData.key
                        onClicked: root.pageRequested(modelData.key)
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: theme.dp(2); color: theme.accent; visible: root.page === modelData.key }
                    }
                }
            }
        }
        C.ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: 0
                Item { Layout.fillWidth: true; height: theme.dp(11) }
                RedesignPlaybackSettings {
                    videoBackdropContent: root.videoBackdropContent
                    Layout.fillWidth: true; Layout.leftMargin: theme.dp(24); Layout.rightMargin: theme.dp(20)
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    visible: root.page === "playback"
                    showStatistics: root.showStatistics
                    showSubtitleTimeline: root.showSubtitleTimeline
                    subtitleTimelineState: root.subtitleTimelineState
                    sleepTimerMinutes: root.sleepTimerMinutes
                    playbackRate: Player.rate; rateAvailable: Player.ratechangable
                    reversed: Player.rate < 0; reverseAvailable: Player.rewindable && Player.ratechangable
                    visualizationModel: Player.audioVisualization
                    showWaveform: Player.audioWaveformEnabled
                    waveformState: Player.audioWaveformState
                    waveformAvailable: String(Player.url).startsWith("file:") && Player.lengthSeconds > 0
                    onStatisticsRequested: enabled => root.statisticsRequested(enabled)
                    onSubtitleTimelineRequested: enabled => root.preferenceStore.stage("qt-subtitle-timeline", enabled)
                    onReverseRequested: reversed => Player.rate = (reversed ? -1 : 1) * Math.abs(Player.rate)
                    onRateRequested: value => Player.rate = (Player.rate < 0 ? -1 : 1) * value
                    onSleepTimerRequested: minutes => root.sleepTimerRequested(minutes)
                    onVisualizationRequested: index => root.chooseVisualization(index, false)
                    onWaveformRequested: root.chooseVisualization(-1, true)
                }
                RedesignAudioSettings {
                    videoBackdropContent: root.videoBackdropContent
                    Layout.fillWidth: true; Layout.leftMargin: theme.dp(20); Layout.rightMargin: theme.dp(20)
                    visible: root.page === "audio"
                    Layout.topMargin: theme.dp(11)
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    backend: root.preferenceStore
                    stereoModel: Player.audioStereoMode
                    audioDelaySeconds: {
                        root.preferenceStore.revision
                        return Player.isStarted && !root.preferenceStore.isStaged("audio-desync") ? Player.audioDelayMS / 1000 : Number(root.preferenceStore.value("audio-desync")) / 1000
                    }
                    onStereoRequested: index => {
                        const value = Player.audioStereoMode.choiceValue(index)
                        if (value !== undefined) root.preferenceStore.stage("stereo-mode", value)
                    }
                    onDelayRequested: seconds => root.preferenceStore.stage("audio-desync", Math.round(seconds * 1000))
                }
                RedesignSubtitleSettings {
                    Layout.fillWidth: true; Layout.leftMargin: theme.dp(24); Layout.rightMargin: theme.dp(20)
                    visible: root.page === "subtitles"
                    Layout.topMargin: theme.dp(11)
                    backend: root.preferenceStore
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    liveSizeMode: {
                        root.preferenceStore.revision
                        return Player.isStarted && !root.preferenceStore.isStaged("sub-text-size-mode") ? Player.subtitleTextSizeMode : -1
                    }
                    liveTextScale: {
                        root.preferenceStore.revision
                        return Player.isStarted && !root.preferenceStore.isStaged("sub-text-scale") ? Player.subtitleTextScale : undefined
                    }
                    livePixelSize: {
                        root.preferenceStore.revision
                        return Player.isStarted && !root.preferenceStore.isStaged("sub-text-size-pixels") ? Player.subtitleTextPixelSize : undefined
                    }
                    subtitleDelaySeconds: {
                        root.preferenceStore.revision
                        return Player.isStarted && !root.preferenceStore.isStaged("redesign-subtitle-delay") ? Player.subtitleDelayMS / 1000 : Number(root.preferenceStore.value("redesign-subtitle-delay")) / 1000
                    }
                    subtitleFPS: {
                        root.preferenceStore.revision
                        return Player.isStarted && !root.preferenceStore.isStaged("sub-fps") ? Player.subtitleFPS : Number(root.preferenceStore.value("sub-fps"))
                    }
                    onDelayRequested: value => root.preferenceStore.stage("redesign-subtitle-delay", Math.round(value * 1000))
                    onFpsRequested: value => root.preferenceStore.stage("sub-fps", value)
                }
                RedesignVideoSettings {
                    videoBackdropContent: root.videoBackdropContent
                    Layout.fillWidth: true; Layout.leftMargin: theme.dp(24); Layout.rightMargin: theme.dp(20)
                    visible: root.page === "video"
                    backend: root.preferenceStore
                    rendererModel: Player.rendererManager
                    videoTracksModel: Player.videoTracks; deinterlaceModel: Player.deinterlace
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    onTrackRequested: index => root.select(Player.videoTracks, index, true)
                    onDeinterlaceRequested: index => root.select(Player.deinterlace, index, true)
                }
                Item { height: theme.dp(20) }
            }
        }
        RowLayout {
            visible: root.preferenceStore.errorMessage.length > 0
            Layout.fillWidth: true; Layout.margins: theme.dp(12)
            Text { Layout.fillWidth: true; text: root.preferenceStore.errorMessage; wrapMode: Text.WordWrap; color: theme.danger; font.pixelSize: theme.dp(12) }
            RedesignButton { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor; caption: qsTr("Retry"); text: caption; onClicked: root.preferenceStore.save() }
        }
        Rectangle {
            visible: root.page !== "playback"
            Layout.fillWidth: true; height: theme.dp(125); color: theme.panel
            radius: theme.dp(10)
            Rectangle { anchors.top: parent.top; width: parent.width; height: theme.dp(10); color: parent.color }
            Rectangle { anchors.top: parent.top; width: parent.width; height: theme.dp(1); color: theme.border }
            ColumnLayout {
                anchors.fill: parent; anchors.leftMargin: theme.dp(20); anchors.rightMargin: theme.dp(20); anchors.topMargin: theme.dp(20); anchors.bottomMargin: theme.dp(12)
                spacing: theme.dp(14)
                RedesignPlayerPreset { videoBackdropContent: root.videoBackdropContent; Layout.fillWidth: true; backend: root.preferenceStore; scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
                RedesignButton { Layout.alignment: Qt.AlignHCenter; scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor; iconName: "settings"; iconSize: theme.dp(20); caption: qsTr("Open Advanced Settings"); text: caption; onClicked: DialogsProvider.prefsDialog() }
            }
        }
    }
}
