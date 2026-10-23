// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import VLC.MediaLibrary
import VLC.MainInterface
import VLC.Network

Item {
    id: root
    objectName: "redesignNetworkStreamLibrary"
    property alias streams: streams
    property bool active: false
    property var location: ({source: "", title: "", nodes: []})
    property string searchText: ""
    property string sortField: "title"
    property bool descending: false
    readonly property bool atRoot: !location.source
    readonly property bool saved: location.source === "saved"
    readonly property bool linked: location.source === "linked"
    readonly property var model: atRoot ? roots : linked ? linkedModel : saved ? streams : browser.item
    readonly property int count: model ? model.count : 0
    readonly property bool loading: linked ? linkedModel.loading : !!model && !atRoot && !saved && model.loading
    readonly property bool podcast: location.source.startsWith("podcast")
    readonly property var sourceTitles: [qsTr("Podcasts"), qsTr("Radio"), qsTr("Saved streams")]

    function rebuild() {
        if (!sources || !streams || !roots) return
        const rows = []
        for (let i = 0; i < sources.count; ++i) {
            const source = sources.getDataAt(i)
            if (source.type !== NetworkSourcesModel.TYPE_SOURCE) continue
            const title = source.name.startsWith("podcast") ? sourceTitles[0]
                : source.name === "radio" ? sourceTitles[1] : source.long_name
            rows.push({title: title, sourceName: source.name, iconName: "network"})
        }
        if (streams.count > 0) rows.push({title: sourceTitles[2], sourceName: "saved", iconName: "network"})
        rows.sort((a, b) => (descending ? -1 : 1) * a.title.localeCompare(b.title))
        roots.clear()
        for (const row of rows) {
            if (!searchText || row.title.toLocaleLowerCase().includes(searchText.toLocaleLowerCase())) roots.append(row)
        }
    }
    function dataAt(index) {
        if (!model || index < 0 || index >= count) return null
        return atRoot ? roots.get(index) : model.getDataAt(index)
    }
    function linkName(url) {
        let name = String(url || "").split(/[?#]/)[0].replace(/\/+$/, "").split("/").pop()
        try { name = decodeURIComponent(name) } catch (error) {}
        return name.replace(/\.(m3u8?|pls|xspf|asx|wpl|b4s|vlc)$/i, "") || qsTr("Streams")
    }
    function playlistLink(url) { return /\.(m3u8?|pls|xspf|asx|wpl|b4s|vlc)(?:[?#]|$)/i.test(String(url || "")) }
    function loadLinkedSources() {
        linkedModel.setSources(linked ? (location.urls || []).map(url => ({mrl: String(url), title: linkName(url), remember: true})) : [])
    }
    onLocationChanged: loadLinkedSources()
    function branch(data) {
        return !!data && (atRoot || saved && (data.canBrowse === true || data.canBrowse === undefined && playlistLink(data.url)) || !saved && !linked && [NetworkDeviceModel.TYPE_NODE,
            NetworkDeviceModel.TYPE_DIRECTORY, NetworkDeviceModel.TYPE_PLAYLIST].includes(data.type))
    }
    function locationAt(index) {
        const data = dataAt(index)
        if (!branch(data)) return null
        if (saved) return {source: "linked", title: linkName(data.url), urls: [String(data.url)], nodes: []}
        return atRoot ? {source: data.sourceName, title: data.title, nodes: []}
            : {source: location.source, title: location.title,
                nodes: [...location.nodes, {title: data.name, tree: data.tree}]}
    }
    function indexForUrl(url, key) {
        if (linked && key) return linkedModel.indexForKey(key)
        for (let i = 0; i < count; ++i) {
            const data = dataAt(i)
            if (String(data.mrl || data.url || "") === url) return i
        }
        return -1
    }
    function play(index) {
        const data = dataAt(index)
        if (!data || atRoot) return
        if (linked) linkedModel.play(index)
        else if (saved) MediaLib.addAndPlay(data.id)
        else model.addAndPlay(index)
    }
    function playEpisode(index, video, fromStart) {
        if (linked) linkedModel.playEpisode(index, video, fromStart)
        else if (model && model.playEpisode) model.playEpisode(index, video, fromStart)
    }
    function enqueue(index) {
        const data = dataAt(index)
        if (!data || atRoot) return
        if (linked) linkedModel.play(index, true)
        else if (saved) MediaLib.addToPlaylist(data.id)
        else model.addToPlaylist(index)
    }
    onSearchTextChanged: rebuild()
    onDescendingChanged: rebuild()
    onSourceTitlesChanged: rebuild()
    NetworkSourcesModel {
        id: sources
        ctx: MainCtx
        onCountChanged: root.rebuild()
    }
    Connections {
        target: sources
        function onDataChanged() { root.rebuild() }
        function onModelReset() { root.rebuild() }
    }
    ListModel { id: roots }
    MLUrlModel {
        id: streams
        ml: MediaLib
        searchPattern: root.saved ? root.searchText : ""
        sortCriteria: root.sortField === "title" ? "title" : root.sortField
        sortOrder: root.descending ? Qt.DescendingOrder : Qt.AscendingOrder
        onCountChanged: root.rebuild()
    }
    NetworkPlaylistModel {
        id: linkedModel
        ctx: MainCtx
        active: root.active && root.enabled && root.visible && root.linked
        searchPattern: root.searchText
        sortCriteria: root.sortField
        sortOrder: root.descending ? Qt.DescendingOrder : Qt.AscendingOrder
        onActiveChanged: if (active) { setSources([]); Qt.callLater(root.loadLinkedSources) }
    }
    Loader {
        id: browser
        active: root.active && !root.atRoot && !root.saved && !root.linked
        sourceComponent: root.location.nodes.length ? treeComponent : sourceBrowserComponent
    }
    Component {
        id: sourceBrowserComponent
        NetworkDeviceModel {
            ctx: MainCtx
            sd_source: NetworkDeviceModel.CAT_INTERNET
            source_name: root.location.source
            searchPattern: root.searchText
            sortCriteria: root.sortField === "uri" ? "mrl" : "name"
            sortOrder: root.descending ? Qt.DescendingOrder : Qt.AscendingOrder
        }
    }
    Component {
        id: treeComponent
        NetworkMediaModel {
            ctx: MainCtx
            tree: root.location.nodes[root.location.nodes.length - 1].tree
            searchPattern: root.searchText
            sortCriteria: root.sortField === "uri" ? "mrl" : "name"
            sortOrder: root.descending ? Qt.DescendingOrder : Qt.AscendingOrder
        }
    }
}
