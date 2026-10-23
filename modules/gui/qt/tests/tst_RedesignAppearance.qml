// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedAppearance"
    width: 1280; height: 720
    visible: true; when: windowShown
    QtObject {
        id: redesignAppearance
        property bool backdropBlurEnabled: true
    }
    RedesignPlayerView {
        id: player
        anchors.fill: parent
        settingsPage: "playback"
        started: true; hasVideo: true
        videoContent: Rectangle { color: "white" }
        libraryContent: RedesignLibraryView {
            expanded: player.libraryExpanded
            scaleFactor: player.scaleFactor; lightTheme: player.lightTheme; accentColor: player.accentColor
        }
    }
    RedesignTrackPopup { id: tracks; parent: testCase; placementArea: testCase }
    RedesignMenu { id: menu; parent: testCase }
    RedesignActionMenu { id: actions; parent: testCase }
    RedesignSeekBar { id: seek; x: 100; y: 650; width: 600 }

    function init() {
        player.libraryOpen = false
        player.libraryExpanded = false
        redesignAppearance.backdropBlurEnabled = true
    }
    function cleanup() {
        player.libraryOpen = false
        player.libraryExpanded = false
        redesignAppearance.backdropBlurEnabled = true
    }

    function test_libraryTransparency_data() {
        return [{tag: "dark", light: false}, {tag: "light", light: true}]
    }
    function test_libraryTransparency(data) {
        player.lightTheme = data.light
        const video = player.videoItem
        const geometry = video.mapToItem(player, 0, 0)
        const size = Qt.size(video.width, video.height)
        player.libraryOpen = true
        const library = findChild(player, "redesignLibraryPanel")
        tryCompare(library, "reveal", 1)
        const body = findChild(player, "redesignLibraryBody")
        const rail = findChild(player.libraryItem, "libraryNavigation")
        for (const enabled of [true, false, true]) {
            redesignAppearance.backdropBlurEnabled = enabled
            fuzzyCompare(body.color.a, enabled ? 0.90 : 1, 0.001)
            fuzzyCompare(rail.color.a, data.light && enabled ? 0.75 : 1, 0.001)
            waitForRendering(player)
            const image = testImages.grabWindow(testCase)
            const bodyPoint = image.point(testCase, 300, 600), railPoint = image.point(testCase, 25, 600)
            const bodyValue = data.light ? (enabled ? 233 : 231) : (enabled ? 38 : 14)
            const railValue = data.light ? (enabled ? 224 : 213) : 25
            for (const channel of ["red", "green", "blue"]) {
                fuzzyCompare(image[channel](bodyPoint.x, bodyPoint.y), bodyValue, 1)
                fuzzyCompare(image[channel](railPoint.x, railPoint.y), railValue, 1)
            }
            compare(video.mapToItem(player, 0, 0), geometry)
            compare(Qt.size(video.width, video.height), size)
        }
        player.libraryExpanded = true
        for (const enabled of [true, false]) {
            redesignAppearance.backdropBlurEnabled = enabled
            compare(library.color.a, 1)
            compare(rail.color.a, 1)
            waitForRendering(player)
            const image = testImages.grabWindow(testCase)
            const bodyPoint = image.point(testCase, 300, 600), railPoint = image.point(testCase, 25, 600)
            const bodyValue = data.light ? 195 : 14
            const railValue = data.light ? 172 : 25
            for (const channel of ["red", "green", "blue"]) {
                fuzzyCompare(image[channel](bodyPoint.x, bodyPoint.y), bodyValue, 1)
                fuzzyCompare(image[channel](railPoint.x, railPoint.y), railValue, 1)
            }
        }
    }

    function test_liveTransparency_data() {
        return [{tag: "dark", light: false}, {tag: "light", light: true}]
    }
    function test_liveTransparency(data) {
        player.lightTheme = tracks.lightTheme = menu.lightTheme = actions.lightTheme = seek.lightTheme = data.light
        const panel = findChild(player, "redesignRightPanel")
        const preview = findChild(seek, "seekThumbnailPopup")
        const surfaces = [panel, tracks.background, menu.background, actions.background, preview.background]
        const geometry = panel.mapToItem(player, 0, 0)
        for (const enabled of [true, false, true]) {
            redesignAppearance.backdropBlurEnabled = enabled
            for (const surface of surfaces)
                fuzzyCompare(surface.color.a, enabled ? 0.90 : 1, 0.001)
            compare(panel.mapToItem(player, 0, 0), geometry)
        }
    }
}
