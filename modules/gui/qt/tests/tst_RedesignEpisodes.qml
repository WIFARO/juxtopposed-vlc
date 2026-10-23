// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedEpisodes"
    width: 500; height: 800
    visible: true; when: windowShown
    QtObject {
        id: rows
        property var values: []
        function clear() { values = [] }
        function append(row) { values = values.concat([row]) }
    }
    RedesignEpisodesView {
        id: view
        width: 400; height: 740
        episodesModel: rows.values
        seasons: [0, 1, 123]
        selectedSeason: 123; currentSeason: 123
        showTitle: "A series"
    }
    SignalSpy { id: played; target: view; signalName: "episodeRequested" }
    SignalSpy { id: season; target: view; signalName: "seasonRequested" }
    SignalSpy { id: closed; target: view; signalName: "closeRequested" }
    SignalSpy { id: refreshed; target: view; signalName: "refreshRequested" }
    Component {
        id: panelComponent
        RedesignEpisodesView {
            width: 400; height: 500
            onSeasonRequested: season => selectedSeason = season
        }
    }
    function item(name) { return findChild(view, name) }
    function manyEpisodes(currentIndex) {
        return Array.from({length: 100}, (_, index) => ({
            title: "Episode " + (index + 1), number: String(index + 1), current: index === currentIndex,
            progress: 0, played: false, description: "Episode notes. ".repeat(30), thumbnail: "",
            urls: ["file:///Show.S42E" + (index + 1) + ".mkv"], feedIndex: -1
        }))
    }
    function longPanel(model) {
        return createTemporaryObject(panelComponent, testCase, {
            episodesModel: model, seasons: Array.from({length: 60}, (_, index) => index + 1),
            currentSeason: 42, selectedSeason: 42
        })
    }
    function verifyEpisodeVisible(panel, index) {
        const list = findChild(panel, "episodeList")
        tryCompare(list, "currentIndex", index)
        tryVerify(() => {
            const row = list.itemAtIndex(index)
            if (!row) return false
            const title = findChild(row, "episodeTitle_" + index)
            const y = title.mapToItem(list, 0, 0).y
            return y >= -1 && y + title.height <= list.height + 1
        })
        tryCompare(list, "activeFocus", true)
    }
    function verifySeasonVisible(panel) {
        const list = findChild(panel, "episodeSeasonList")
        tryCompare(list, "currentIndex", 41)
        tryVerify(() => {
            const row = list.itemAtIndex(41)
            if (!row) return false
            const y = row.mapToItem(list, 0, 0).y
            return y >= -1 && y + row.height <= list.height + 1
        })
        tryCompare(list, "activeFocus", true)
    }
    function init() {
        view.showingSeasons = false; view.expandedKey = ""; view.loading = false
        view.limited = false; view.message = ""; view.podcast = false
        view.width = 400; view.scaleFactor = 1; view.lightTheme = false
        rows.clear()
        rows.append({title: "Episode 1", number: "1", current: true, progress: .5, played: false,
            description: "Episode notes", thumbnail: "", urls: ["file:///Show.S123E01.mkv"], feedIndex: -1})
        rows.append({title: "Episode 2", number: "2", current: false, progress: .25, played: false,
            description: "", thumbnail: "", urls: ["file:///Show.S123E02.mkv"], feedIndex: -1})
        played.clear(); season.clear(); closed.clear(); refreshed.clear(); wait(30)
    }
    function cleanup() { view.showingSeasons = false; wait(50) }
    function cleanupTestCase() { rows.clear(); wait(50) }
    function test_currentEpisodeExpandsAndRestartIsExplicit() {
        verify(item("episodeRow_0").expanded)
        verify(!item("episodePlay_0").enabled)
        mouseClick(item("episodeRestart_0"))
        compare(played.count, 1)
        compare(Array.from(played.signalArguments[0]), [0, 0, true])
    }
    function test_browsingDoesNotStartPlayback() {
        const list = item("episodeList")
        list.positionViewAtIndex(1, ListView.Contain)
        mouseClick(item("episodeTitle_1"))
        verify(item("episodeRow_1").expanded)
        compare(played.count, 0)
        compare(item("episodePlay_1").caption, "Resume")
        list.positionViewAtIndex(1, ListView.Contain)
        mouseClick(item("episodePlay_1"))
        compare(Array.from(played.signalArguments[0]), [1, 0, false])
    }
    function test_seasonNavigationAndKeyboardEscape() {
        mouseClick(item("episodeSeasonsButton"))
        verify(view.showingSeasons)
        tryVerify(() => item("episodeSeason_0") !== null)
        mouseClick(item("episodeSeason_0"))
        compare(season.signalArguments[0][0], 0); verify(!view.showingSeasons)
        view.showingSeasons = true; view.forceActiveFocus(); keyClick(Qt.Key_Escape)
        verify(!view.showingSeasons); compare(closed.count, 0)
        keyClick(Qt.Key_Escape); compare(closed.count, 1)
    }
    function test_titleAndSeasonLabelsShareTheCaretButton() {
        mouseClick(item("episodeShowTitle"))
        verify(view.showingSeasons)
        mouseClick(item("episodeSeasonsButton"))
        verify(!view.showingSeasons)
        mouseClick(item("episodeSeasonLabel"))
        verify(view.showingSeasons)
    }
    function test_squareHighlightsAndRowsWithoutDividers() {
        view.expandedKey = "collapsed"
        rows.values = rows.values.map(row => Object.assign({}, row, {progress: 0}))
        wait(30)
        const first = item("episodeTitle_0"), second = item("episodeTitle_1")
        compare(first.background.radius, 0)
        mouseMove(second)
        tryCompare(second, "hovered", true)
        compare(second.background.radius, 0)
        compare(item("episodeRow_0").height, first.height)
        compare(item("episodeRow_1").height, second.height)
        const end = first.mapToItem(view, 0, first.height)
        compare(second.mapToItem(view, 0, 0).y, end.y)
    }
    function test_refreshRemainsInOptionsForPartialScans() {
        view.limited = true
        compare(item("episodeSearchFurther"), null)
        mouseClick(item("episodeOptionsButton"))
        const refresh = item("libraryAction_refresh")
        verify(refresh !== null)
        mouseClick(refresh)
        compare(refreshed.count, 1)
    }
    function test_longDescriptionsAreTruncatedToThreeLines_data() {
        return [{tag: "normal", width: 400, scale: 1}, {tag: "narrow", width: 280, scale: 1},
                {tag: "scaled", width: 300, scale: 1.25}]
    }
    function test_longDescriptionsAreTruncatedToThreeLines(data) {
        view.width = data.width; view.scaleFactor = data.scale
        const notes = "Episode notes and show links.\n" + "A long episode description with details and links. ".repeat(40)
            + "https://example.invalid/" + "long-link".repeat(50)
        rows.values = rows.values.map((row, index) => index === 0 ? Object.assign({}, row, {description: notes}) : row)
        const description = item("episodeDescription_0")
        tryCompare(description, "lineCount", 3)
        tryCompare(description, "truncated", true)
        compare(description.text, notes)
        rows.values = rows.values.map((row, index) => index === 0 ? Object.assign({}, row, {description: "Short notes."}) : row)
        tryCompare(item("episodeDescription_0"), "truncated", false)
        tryCompare(item("episodeDescription_0"), "lineCount", 1)
    }
    function test_narrowScaledLayoutAndPartialState() {
        view.width = 300; view.scaleFactor = 1.25; view.lightTheme = true
        view.limited = true
        wait(30)
        compare(item("episodeSearchFurther"), null)
        verify(item("episodeList").width <= view.width)
        verify(item("episodeRestart_0").width > 0)
    }
    function test_openingPanelRevealsCurrentEpisode() {
        const panel = longPanel(manyEpisodes(74))
        verifyEpisodeVisible(panel, 74)
        verify(findChild(panel, "episodeList").contentY > 0)
        verify(findChild(panel, "episodeRow_74").expanded)
    }
    function test_episodesArrivingAfterOpeningRevealCurrentEpisode() {
        const panel = longPanel([])
        wait(30)
        panel.episodesModel = manyEpisodes(74)
        verifyEpisodeVisible(panel, 74)
    }
    function test_reopeningPanelReturnsToPlayingSeason() {
        const panel = createTemporaryObject(panelComponent, testCase, {
            episodesModel: manyEpisodes(74), seasons: [20, 42], currentSeason: 42, selectedSeason: 20
        })
        compare(panel.selectedSeason, 42)
        verifyEpisodeVisible(panel, 74)
    }
    function test_seasonNavigationRevealsPlayingSeasonAndEpisode() {
        const panel = longPanel(manyEpisodes(74))
        verifyEpisodeVisible(panel, 74)
        panel.expandedKey = "file:///Show.S42E11.mkv"
        mouseClick(findChild(panel, "episodeSeasonsButton"))
        verifySeasonVisible(panel)
        mouseClick(findChild(panel, "episodeSeason_42"))
        verifyEpisodeVisible(panel, 74)

        // Choosing another season starts at its first episode.
        panel.showingSeasons = true
        verifySeasonVisible(panel)
        panel.seasonRequested.connect(function(seasonNumber) {
            panel.selectedSeason = seasonNumber
            panel.episodesModel = manyEpisodes(seasonNumber === 42 ? 74 : -1)
        })
        const seasons = findChild(panel, "episodeSeasonList")
        seasons.positionViewAtIndex(19, ListView.Beginning)
        mouseClick(findChild(panel, "episodeSeason_20"))
        verifyEpisodeVisible(panel, 0)
        compare(findChild(panel, "episodeList").contentY, 0)

        // Returning to all seasons still follows playback, rather than the browsed season.
        panel.showingSeasons = true
        verifySeasonVisible(panel)
        mouseClick(findChild(panel, "episodeSeason_42"))
        verifyEpisodeVisible(panel, 74)
    }
}
