// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedAddToPlaylist"
    width: 1280; height: 800
    visible: true; when: windowShown
    ListModel {
        id: playlists
        Component.onCompleted: append([
            {id: "playlist-17", name: "Films", thumbnail: "", readOnly: false},
            {id: "playlist-23", name: "Films", thumbnail: "", readOnly: false},
            {id: "playlist-31", name: "Imported", thumbnail: "", readOnly: true}
        ])
    }
    RedesignAddToPlaylistDialog { id: dialog; parent: testCase; playlistModel: playlists }
    SignalSpy { id: saveSpy; target: dialog; signalName: "saveRequested" }
    function init() {
        width = 1280; height = 800
        dialog.busy = false; dialog.close(); saveSpy.clear()
        dialog.scaleFactor = 1; dialog.lightTheme = false; dialog.accentColor = "#d37918"
        mouseMove(testCase, 1, 1)
        dialog.begin(2); tryCompare(dialog, "opened", true)
    }
    function cleanup() {
        dialog.busy = false; dialog.close(); tryCompare(dialog, "visible", false)
        if (playlists.count > 3) playlists.remove(3, playlists.count - 3)
    }
    function row(index) {
        const list = findChild(dialog, "playlistDestinations")
        tryVerify(() => list.itemAtIndex(index) !== null)
        return list.itemAtIndex(index)
    }
    function test_existingPlaylistUsesIdentityRatherThanDuplicateName() {
        verify(!findChild(dialog, "addToPlaylistSave").enabled)
        mouseClick(row(1)); compare(dialog.selectedKey, "playlist-23")
        mouseClick(findChild(dialog, "addToPlaylistSave"))
        compare(saveSpy.signalArguments[0][0], "playlist-23"); compare(saveSpy.signalArguments[0][1], "")
        verify(dialog.busy)
    }
    function test_newNameAndExistingDestinationAreExclusive() {
        mouseClick(row(0)); dialog.newName = " New playlist "
        compare(dialog.selectedKey, "")
        dialog.submit(); compare(saveSpy.signalArguments[0][0], ""); compare(saveSpy.signalArguments[0][1], "New playlist")
        dialog.finish(false, "Retry")
        mouseClick(row(1)); compare(dialog.newName, ""); compare(dialog.selectedKey, "playlist-23")
        dialog.submit(); compare(saveSpy.signalArguments[1][0], "playlist-23"); compare(saveSpy.signalArguments[1][1], "")
    }
    function test_readOnlyDestinationCannotBeSelected() {
        verify(!row(2).enabled)
        mouseClick(row(2)); compare(dialog.selectedKey, ""); verify(!dialog.valid)
    }
    function test_searchClearsStaleDestination() {
        mouseClick(row(0)); dialog.filterText = "other"
        compare(dialog.selectedKey, ""); verify(!dialog.valid)
    }
    function test_busyPreventsDuplicateSubmitAndDismissal() {
        mouseClick(row(0)); dialog.submit(); dialog.submit()
        compare(saveSpy.count, 1)
        verify(!findChild(dialog, "addToPlaylistCancel").enabled)
        verify(!findChild(dialog, "addToPlaylistClose").enabled)
        verify(!findChild(dialog, "addToPlaylistName").enabled)
        keyClick(Qt.Key_Escape); verify(dialog.visible)
        verify(!dialog.begin(5)); compare(dialog.itemCount, 2); compare(dialog.selectedKey, "playlist-17")
    }
    function test_errorRetainsChoiceAndCanRetry() {
        dialog.newName = "Retry"; dialog.submit(); dialog.finish(false, "Cannot write")
        verify(dialog.visible); verify(!dialog.busy); compare(dialog.newName, "Retry")
        compare(dialog.errorMessage, "Cannot write")
        verify(findChild(dialog, "addToPlaylistError").visible)
        dialog.submit(); compare(saveSpy.count, 2)
        dialog.finish(true, ""); tryCompare(dialog, "visible", false)
    }
    function test_smallWindowKeepsFooterAndErrorVisible() {
        width = 560; height = 430
        dialog.newName = "Retry"; dialog.submit(); dialog.finish(false, "The selected playlist is no longer available.")
        verify(dialog.width <= width - 32); verify(dialog.height <= height - 32)
        const save = findChild(dialog, "addToPlaylistSave")
        const point = save.mapToItem(testCase, 0, 0)
        verify(point.y >= 0 && point.y + save.height <= height)
        verify(findChild(dialog, "addToPlaylistError").visible)
    }
    function test_reopenStartsWithNoDestinationOrName() {
        dialog.newName = "Old draft"; dialog.filterText = "old"; dialog.close()
        tryCompare(dialog, "visible", false)
        dialog.begin(1); tryCompare(dialog, "opened", true)
        compare(dialog.newName, ""); compare(dialog.filterText, ""); compare(dialog.selectedKey, "")
    }
    function test_smallWindowRevealsFocusedFieldsAndDestinations() {
        width = 500; height = 400
        function reveal(item) {
            item.forceActiveFocus()
            tryVerify(() => {
                const p = item.mapToItem(dialog.background, 0, 0)
                return p.y >= dialog.contentItem.y && p.y + item.height <= dialog.contentItem.y + dialog.contentItem.height
            })
        }
        const name = findChild(dialog, "addToPlaylistName")
        reveal(name); keyClick(Qt.Key_A)
        compare(dialog.newName, "a")
        reveal(row(1)); keyClick(Qt.Key_Space)
        compare(dialog.selectedKey, "playlist-23"); compare(dialog.newName, "")
        reveal(findChild(dialog, "addToPlaylistSearch"))
        reveal(name)
        height = 800; wait(20); height = 400
        tryVerify(() => {
            const p = name.mapToItem(dialog.background, 0, 0)
            return p.y >= dialog.contentItem.y && p.y + name.height <= dialog.contentItem.y + dialog.contentItem.height
        })
    }
    function test_keyboardDestinationNavigation() {
        playlists.append({id: "playlist-37", name: "Other", thumbnail: "", readOnly: false})
        const list = findChild(dialog, "playlistDestinations")
        row(0).forceActiveFocus()
        keyClick(Qt.Key_Down)
        tryCompare(list, "currentIndex", 1)
        verify(row(1).activeFocus)
        keyClick(Qt.Key_Space); compare(dialog.selectedKey, "playlist-23")
        keyClick(Qt.Key_Down)
        tryCompare(list, "currentIndex", 3)
        verify(row(3).activeFocus)
        keyClick(Qt.Key_Space); compare(dialog.selectedKey, "playlist-37")
        keyClick(Qt.Key_Down); compare(list.currentIndex, 3); verify(row(3).activeFocus)
        keyClick(Qt.Key_Up); compare(list.currentIndex, 1); verify(row(1).activeFocus)
        keyClick(Qt.Key_Home); compare(list.currentIndex, 0); verify(row(0).activeFocus)
        keyClick(Qt.Key_End); compare(list.currentIndex, 3); verify(row(3).activeFocus)
    }
    function test_keyboardCanReachAnUnloadedDestination() {
        for (let index=0; index<40; ++index)
            playlists.append({id: "playlist-" + (100+index), name: "Playlist " + index, thumbnail: "", readOnly: false})
        playlists.append({id: "readonly-last", name: "Read only", thumbnail: "", readOnly: true})
        const list=findChild(dialog,"playlistDestinations")
        row(0).forceActiveFocus(); keyClick(Qt.Key_End)
        compare(list.currentIndex,42); verify(row(42).activeFocus)
        keyClick(Qt.Key_Space); compare(dialog.selectedKey,"playlist-139")
        keyClick(Qt.Key_Home); compare(list.currentIndex,0); verify(row(0).activeFocus)
    }
    function test_themeAndScale_data() {
        return [
            {tag: "dark", scale: 1, accent: "#d37918", fill: "#371f06"},
            {tag: "green", scale: 1, accent: "#19a234", fill: "#0d2e13"},
            {tag: "light", scale: 1, light: true, accent: "#d37918", fill: "#d8ba9a"},
            {tag: "scaled", scale: 1.5, accent: "#d37918", fill: "#371f06"}
        ]
    }
    function test_themeAndScale(data) {
        width = 1600; height = 1200
        dialog.scaleFactor = data.scale; dialog.lightTheme = !!data.light; dialog.accentColor = data.accent
        dialog.newName = "Styled"
        wait(20)
        fuzzyCompare(dialog.width, 713 * data.scale, .1)
        fuzzyCompare(dialog.height, 589 * data.scale, .1)
        for (const objectName of ["addToPlaylistSearch", "addToPlaylistName"])
            fuzzyCompare(findChild(dialog, objectName).height, 37 * data.scale, .1)
        const add = findChild(dialog, "addToPlaylistSave")
        const image = testImages.grabWindow(testCase), color = Qt.color(data.fill)
        const p = image.point(add.background, 10 * data.scale, 16 * data.scale)
        compare(image.red(p.x, p.y), Math.round(color.r * 255))
        compare(image.green(p.x, p.y), Math.round(color.g * 255))
        compare(image.blue(p.x, p.y), Math.round(color.b * 255))
        compare(add.foreground, Qt.color(data.accent))
    }
}
