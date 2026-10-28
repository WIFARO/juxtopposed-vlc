// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: tests
    name: "WilMinimalView"
    width: 1280; height: 720
    visible: true; when: windowShown

    Component {
        id: viewComponent
        RedesignPlayerView {
            width: 1280; height: 720
            started: true; hasVideo: true; episodesAvailable: true
            libraryContent: Component {
                Item {
                    property int category: 3
                    property int selection: 17
                }
            }
        }
    }
    Component {
        id: modeComponent
        RedesignPlayerModes {}
    }
    Component {
        id: contextComponent
        QtObject {
            property bool minimalView: false
            property bool playerView: false
            signal mainInterfaceModesChanged()
            onMinimalViewChanged: mainInterfaceModesChanged()
            onPlayerViewChanged: mainInterfaceModesChanged()
        }
    }
    function test_sharedNotificationDoesNotNavigate() {
        const view = createTemporaryObject(viewComponent, tests)
        const context = createTemporaryObject(contextComponent, tests)
        const modes = createTemporaryObject(modeComponent, tests, {
            context: context, presentation: view, started: true
        })
        verify(modes !== null)
        for (let i = 0; i < 3; ++i) {
            context.minimalView = true
            context.minimalView = false
            compare(view.libraryOpen, false, "minimal-only notify must not navigate")
            compare(view.libraryExpanded, false)
            compare(context.playerView, false, "never force the native player mode")
        }
    }
    function test_realPlayerTransitionStillNavigates() {
        const view = createTemporaryObject(viewComponent, tests)
        const context = createTemporaryObject(contextComponent, tests, {playerView: true})
        const modes = createTemporaryObject(modeComponent, tests, {
            context: context, presentation: view, started: true
        })
        verify(modes !== null)
        context.playerView = false
        compare(view.libraryOpen, true)
        compare(view.libraryExpanded, true)
    }
    function test_roundTrip_data() {
        return [
            {tag: "closed", library: false, expanded: false, mini: true, queue: false, settings: "", episodes: false},
            {tag: "compact", library: true, expanded: false, mini: true, queue: false, settings: "", episodes: false},
            {tag: "expanded", library: true, expanded: true, mini: true, queue: false, settings: "", episodes: false},
            {tag: "expanded-hidden-mini", library: true, expanded: true, mini: false, queue: false, settings: "", episodes: false},
            {tag: "expanded-with-queue", library: true, expanded: true, mini: false, queue: true, settings: "", episodes: false},
            {tag: "queue", library: false, expanded: false, mini: false, queue: true, settings: "", episodes: false},
            {tag: "settings", library: false, expanded: false, mini: true, queue: false, settings: "video", episodes: false},
            {tag: "episodes", library: false, expanded: false, mini: true, queue: false, settings: "", episodes: true}
        ]
    }
    function test_roundTrip(data) {
        const view = createTemporaryObject(viewComponent, tests)
        verify(view !== null)
        const context = createTemporaryObject(contextComponent, tests)
        view.minimalView = Qt.binding(function() { return context.minimalView })
        const modes = createTemporaryObject(modeComponent, tests, {
            context: context, presentation: view, started: true
        })
        verify(modes !== null)
        view.libraryOpen = data.library
        view.libraryExpanded = data.expanded
        view.miniPlayerVisible = data.mini
        view.queueOpen = data.queue
        view.settingsPage = data.settings
        view.episodesOpen = data.episodes
        const library = view.libraryItem
        if (library) { library.category = 5; library.selection = 42 }
        for (let i = 0; i < 3; ++i) {
            context.minimalView = true
            compare(view.libraryOpen, false)
            compare(view.queueOpen, false)
            compare(view.settingsPage, "")
            compare(view.episodesOpen, false)
            context.minimalView = false
            compare(view.libraryOpen, data.library, "library open restored")
            compare(view.libraryExpanded, data.expanded, "library size restored")
            compare(view.queueOpen, data.queue, "queue restored")
            compare(view.settingsPage, data.settings, "settings restored")
            compare(view.episodesOpen, data.episodes, "episodes restored")
            compare(view.miniPlayerVisible, data.mini, "mini visibility restored after expansion")
            if (library) {
                compare(view.libraryItem, library, "library instance retained")
                compare(library.category, 5)
                compare(library.selection, 42)
            }
        }
    }
    function test_initialMinimalModeCanBeLeftWithoutNavigation() {
        const context = createTemporaryObject(contextComponent, tests, {minimalView: true})
        const view = createTemporaryObject(viewComponent, tests, {minimalView: true})
        const modes = createTemporaryObject(modeComponent, tests, {
            context: context, presentation: view, started: true
        })
        verify(modes !== null)
        view.minimalView = Qt.binding(function() { return context.minimalView })
        context.minimalView = false
        compare(view.minimalView, false)
        compare(view.libraryOpen, false)
    }
    function test_playerTransitionWhileMinimalDoesNotNavigateOnExit() {
        const context = createTemporaryObject(contextComponent, tests, {playerView: true})
        const view = createTemporaryObject(viewComponent, tests)
        view.minimalView = Qt.binding(function() { return context.minimalView })
        const modes = createTemporaryObject(modeComponent, tests, {
            context: context, presentation: view, started: true
        })
        verify(modes !== null)
        context.minimalView = true
        context.playerView = false
        context.minimalView = false
        compare(view.libraryOpen, false)
        compare(view.libraryExpanded, false)
    }
    function test_stoppedPlayerDoesNotOpenLibraryOnModeChange() {
        const context = createTemporaryObject(contextComponent, tests, {playerView: true})
        const view = createTemporaryObject(viewComponent, tests)
        const modes = createTemporaryObject(modeComponent, tests, {
            context: context, presentation: view, started: false
        })
        verify(modes !== null)
        context.playerView = false
        compare(view.libraryOpen, false)
    }
    function test_unavailableEpisodesAreNotReopened() {
        const view = createTemporaryObject(viewComponent, tests)
        verify(view !== null)
        view.episodesOpen = true
        view.minimalView = true
        view.episodesAvailable = false
        view.minimalView = false
        compare(view.episodesOpen, false)
    }
}
