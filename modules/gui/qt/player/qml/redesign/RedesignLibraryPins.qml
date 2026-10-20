// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

QtObject {
    id: root
    property var ctx
    property var entries: []
    readonly property string storageKey: "Juxtopposed/library-pins"
    function identity(entry) {
        return entry.kind + ":" + entry.key + (entry.kind === "collection" ? ":" + entry.section : "")
    }
    function normalize(entry) {
        if (!entry || !["folder", "playlist", "collection", "section", "home", "media", "file", "network-source"].includes(entry.kind)
                || typeof entry.key !== "string" || !entry.key.length || typeof entry.name !== "string" || !entry.name.length)
            return null
        if (entry.kind === "collection" && !["Albums", "Artists", "Genres"].includes(entry.section)) return null
        if (entry.kind === "section" && !["Songs", "Albums", "Artists", "Genres"].includes(entry.key)) return null
        if (entry.kind === "home" && !["watching", "listening", "folders", "playlists", "artists"].includes(entry.key)) return null
        return {id: identity(entry), kind: entry.kind, key: entry.key, name: entry.name,
            category: entry.kind === "home" ? 0 : entry.kind === "playlist" ? 4 : entry.kind === "network-source" || entry.kind === "media" && entry.category === 3 ? 3 : ["collection", "section"].includes(entry.kind) || entry.category === 2 ? 2 : 1,
            section: entry.kind === "collection" ? entry.section : "",
            artwork: typeof entry.artwork === "string" ? entry.artwork : "",
            icon: typeof entry.icon === "string" ? entry.icon : entry.kind === "folder" ? "folder" : entry.kind === "playlist" ? "playlist" : entry.kind === "network-source" ? "network" : "music"}
    }
    function load() {
        try {
            const saved = JSON.parse(ctx.settingValue(storageKey, "[]"))
            if (!Array.isArray(saved)) { entries = []; return }
            const restored = []
            for (const value of saved) {
                const entry = normalize(value)
                if (entry && !restored.some(item => item.id === entry.id)) restored.push(entry)
            }
            entries = restored
        } catch (error) { entries = [] }
    }
    Component.onCompleted: load()
    property Connections settingsObserver: Connections {
        target: root.ctx || null
        ignoreUnknownSignals: true
        function onSettingsReloaded() { root.load() }
    }
    function contains(entry) { return !!entry && entries.some(item => item.id === identity(entry)) }
    function save(next) {
        entries = next
        ctx.setSettingValue(storageKey, JSON.stringify(entries))
    }
    function add(value) {
        const entry = normalize(value)
        if (!entry) return
        const index = entries.findIndex(item => item.id === entry.id)
        const next = entries.slice()
        if (index < 0) next.push(entry)
        else next[index] = entry
        save(next)
    }
    function remove(entry) { save(entries.filter(item => item.id !== identity(entry))) }
    function toggle(entry) { if (contains(entry)) remove(entry); else add(entry) }
}
