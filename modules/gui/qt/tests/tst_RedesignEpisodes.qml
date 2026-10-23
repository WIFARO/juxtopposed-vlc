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
    function item(name) { return findChild(view, name) }
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
    function test_narrowScaledLayoutAndPartialState() {
        view.width = 300; view.scaleFactor = 1.25; view.lightTheme = true
        view.limited = true
        wait(30)
        compare(item("episodeSearchFurther"), null)
        verify(item("episodeList").width <= view.width)
        verify(item("episodeRestart_0").width > 0)
    }
}
