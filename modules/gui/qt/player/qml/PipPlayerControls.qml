// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import VLC.MainInterface
import VLC.Player
import VLC.Playlist
import VLC.Dialogs

RedesignPipControls {
    id: root
    accentColor: MainCtx.redesignAccent
    started: Player.isStarted
    playing: Player.playingState === Player.PLAYING_STATE_PLAYING
    pausable: Player.pausable
    seekable: Player.seekable
    position: Player.position
    volume: Player.volume
    maxVolume: MainCtx.maxVolume / 100
    muted: Player.muted
    mediaTitle: Player.title || Player.name
    timeText: Player.time.formatHMS(0)
    lengthText: Player.length.formatHMS(0)
    timeForPosition: position => Player.length.scale(position).formatHMS(0)
    subtitleTracks: Player.subtitleTracks

    onTogglePlayback: MainPlaylistController.togglePlayPause()
    onJumpRequested: seconds => Player.jumpSeconds(seconds)
    onSeekRequested: position => Player.position = position
    onSeekDragStarted: Player.beginSeekDrag()
    onSeekDragFinished: (position, commit) => Player.finishSeekDrag(position, commit)
    onVolumeRequested: volume => { Player.muted = false; Player.volume = volume }
    onMuteRequested: Player.muted = !Player.muted
    onSubtitleSelectionRequested: (index, selected) => Player.subtitleTracks.setData(Player.subtitleTracks.index(index, 0), selected, Qt.CheckStateRole)
    onSubtitlesOffRequested: {
        for (let i = 0; i < Player.subtitleTracks.count; ++i)
            Player.subtitleTracks.setData(Player.subtitleTracks.index(i, 0), false, Qt.CheckStateRole)
    }
    onAddSubtitlesRequested: DialogsProvider.loadSubtitlesFile()
    onKeyPressed: event => {
        if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) { event.accepted = false; return }
        if (MainCtx.useGlobalShortcuts) {
            MainCtx.sendHotkey(event.key, event.modifiers)
            event.accepted = true
        } else event.accepted = false
    }
    Connections {
        target: Player
        function onInputChanged() { root.dismissSubtitles() }
    }
}
