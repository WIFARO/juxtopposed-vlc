// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedCustomizationControls"
    width: 1920; height: 1080
    visible: true; when: windowShown
    RedesignCustomizationCatalog { id: catalog }
    RedesignIcon { id: icon; visible: false; width: 24; height: 24; sourceSize: Qt.size(24, 24) }
    RedesignPlayerView { id: player; anchors.fill: parent; started: true; hasVideo: true }
    SignalSpy { id: actionSpy; target: player; signalName: "customActionRequested" }
    function item(name) { return findChild(player, name) }
    function init() {
        player.playerLayout = ""; player.layoutModel.reload()
        player.customControlStates = ({})
        actionSpy.clear()
        tryVerify(() => Math.abs(item("seekBar").mapToItem(player, 0, 0).x - 254) < 1)
    }
    function test_paletteMatchesInitialReference() {
        const rows = catalog.available(player.layoutModel.layout)
        compare(rows.length, 31)
        compare(rows.slice(0, 9).map(row => row.id), ["spacer", "divider", "speed", "forward10", "backward10", "forward15", "backward15", "stop", "repeat"])
        compare(rows.slice(-4).map(row => row.id), ["favorite", "bookmark", "bookmarks", "playlists"])
        for (const row of catalog.entries) {
            verify(row.label.length > 0)
            if (row.icon) verify(!!icon.paths[row.icon], row.id + " has no icon")
            verify(["spacer", "divider"].includes(row.id) || player.layoutModel.controls.includes(row.id), row.id)
        }
    }
    function test_eachPaletteControlRenders_data() {
        return catalog.available(player.layoutModel.defaults()).map(row => ({tag: row.id, entry: row}))
    }
    function test_eachPaletteControlRenders(data) {
        player.layoutModel.begin()
        verify(player.layoutModel.move(data.entry.id, "footerLeft", 3))
        const id = player.layoutModel.layout.zones.footerLeft[3]
        tryVerify(() => item("customControl_" + id) !== null && item("customControl_" + id).parent.objectName === "customFooterLeft")
        const control = item("customControl_" + id)
        verify(control.visible)
        verify(control.width > 0 && control.height > 0)
        if (!data.entry.kind) {
            mouseClick(item("customButton_" + id))
            compare(actionSpy.count, 1); compare(actionSpy.signalArguments[0][0], id)
        }
        const available = catalog.available(player.layoutModel.layout).some(row => row.id === data.entry.id)
        compare(available, ["spacer", "divider"].includes(data.entry.id))
        player.layoutModel.cancel()
        tryVerify(() => !control.visible)
    }
    function test_dynamicControlsKeepStateAndIdentity() {
        player.layoutModel.begin(); player.layoutModel.move("favorite", "footerLeft", 3)
        tryVerify(() => item("customControl_favorite") !== null)
        const control = item("customControl_favorite"), button = item("customButton_favorite")
        player.customControlStates = ({favorite: {checked: true, icon: "favorite-active", enabled: false}})
        verify(button.selected); verify(!button.enabled)
        player.layoutModel.move("favorite", "headerLeft", 0)
        tryVerify(() => player.layoutReady && control.visible && control.parent.objectName === "customHeaderLeft")
        compare(item("customControl_favorite"), control)
        player.customControlStates = ({favorite: {checked: false, enabled: true}})
        verify(!button.selected); verify(button.enabled)
        verify(waitForRendering(player))
        mouseClick(button); compare(actionSpy.count, 1)
    }
    function test_metadataIsElidedAndMissingValuesAreClear() {
        player.layoutModel.begin(); player.layoutModel.move("fileName", "footerLeft", 3)
        tryVerify(() => item("customControl_fileName") !== null)
        const control = item("customControl_fileName")
        player.customControlStates = ({fileName: {value: "A long file name ".repeat(20)}})
        tryVerify(() => control.width <= 220 && control.width > 0)
        compare(control.valueText, "A long file name ".repeat(20))
        for (const value of ["", null, undefined]) {
            player.customControlStates = ({fileName: {value: value}})
            compare(control.valueText, "—")
        }
        player.customControlStates = ({fileName: {value: "<b>literal file name</b>"}})
        compare(item("customLabel_fileName").text, "<b>literal file name</b>")
        compare(item("customLabel_fileName").textFormat, Text.PlainText)
    }
    function test_thumbnailLoadsOnlyWhenUsedAndAtDisplaySize() {
        player.layoutModel.begin(); player.layoutModel.move("thumbnail", "headerCenter", 1)
        tryVerify(() => item("customControl_thumbnail") !== null && item("customControl_thumbnail").visible)
        const image = findChild(item("customControl_thumbnail"), "customThumbnail")
        player.customControlStates = ({thumbnail: {artwork: Qt.resolvedUrl("../pixmaps/prefs/spref_interface.png")}})
        tryCompare(image, "status", Image.Ready)
        compare(image.sourceSize, Qt.size(image.width * Screen.devicePixelRatio, image.height * Screen.devicePixelRatio))
        player.layoutModel.remove("thumbnail")
        tryVerify(() => image.source.toString() === "")
    }
    function test_flexibleSpacersFillAroundCenteredPlayback() {
        const model = player.layoutModel
        model.begin(); model.move("timeline", "above", 0)
        for (const id of ["previous", "play", "next"]) model.move(id, "footerCenter", model.layout.zones.footerCenter.length)
        model.move("spacer", "footerLeft", 0); model.move("spacer", "footerRight", 0)
        tryVerify(() => item("customControl_spacer:1") && item("customControl_spacer:2")
                  && item("customControl_spacer:1").width > testCase.width / 5 && item("customControl_spacer:2").width > testCase.width / 5)
        const play = item("playButton")
        fuzzyCompare(play.mapToItem(player, 0, 0).x + play.width / 2, width / 2, 1)
    }
}
