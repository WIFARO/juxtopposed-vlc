// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import VLC.MainInterface
import VLC.MediaLibrary

MLFileArtwork {
    id: root
    property var seriesModel: null
    ctx: MainCtx
    onArtworkChanged: (url, artwork) => seriesModel.setArtwork(url, artwork)
    onMediaProgressChanged: (url, progress) => seriesModel.setProgress(url, progress)
    onMediaPlayedChanged: (url, played) => seriesModel.setPlayed(url, played)
    onInvalidated: seriesModel.refresh()
    property Connections modelConnections: Connections {
        target: root.seriesModel
        function onResetRequested() { root.clear() }
        function onWatchStatesRequested(urls, serial) { root.requestWatchStates(urls, serial) }
        function onArtworkRequested(url) { root.request(url, true) }
    }
}
