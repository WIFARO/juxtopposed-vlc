// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedDialogSurfaces"
    width: 1400; height: 1000
    visible: true; when: windowShown
    property int activeBackdrops: 0
    QtObject { id: redesignAppearance; property bool backdropBlurEnabled: true }
    Component {
        id: backdropProbe
        Item {
            objectName: "dialogBackdropProbe"
            property real cornerRadius: 0
            property real blurRadius: 0
            property bool menuShadow: false
            Component.onCompleted: testCase.activeBackdrops++
            Component.onDestruction: testCase.activeBackdrops--
        }
    }
    Component { id: network; RedesignNetworkDialog { } }
    Component { id: playlist; RedesignPlaylistDialog { } }
    Component { id: addToPlaylist; RedesignAddToPlaylistDialog { } }
    Component { id: tracks; RedesignTrackAssignmentDialog { } }
    Component { id: shortcuts; RedesignShortcutEditor { } }
    Component { id: confirmation; RedesignConfirmationDialog { title: "Use shortcut"; message: "Use F for this action?" } }
    Component { id: smallHost; Item { width: 260; height: 360 } }
    SignalSpy { id: acceptedSpy; signalName: "accepted" }
    SignalSpy { id: rejectedSpy; signalName: "rejected" }
    function test_referenceSurface_data() {
        const rows = []
        for (const light of [false, true]) {
            for (const spec of [{name: "network", component: network}, {name: "playlist", component: playlist},
                {name: "add-to-playlist", component: addToPlaylist}, {name: "tracks", component: tracks, fill: "#0e0e0e"},
                {name: "shortcuts", component: shortcuts}, {name: "confirmation", component: confirmation}])
                rows.push({tag: spec.name + "-" + light, light, component: spec.component, fill: spec.fill || "#191919"})
        }
        return rows
    }
    function test_referenceSurface(data) {
        redesignAppearance.backdropBlurEnabled = true
        const dialog = createTemporaryObject(data.component, testCase,
            {parent: testCase, lightTheme: data.light, videoBackdropContent: backdropProbe})
        verify(dialog !== null)
        const surface = dialog.background
        const loader = findChild(surface, "dialogBackdropLoader")
        compare(testCase.activeBackdrops, 0)
        dialog.open(); tryCompare(dialog, "opened", true)
        tryCompare(testCase, "activeBackdrops", 1)
        const backdrop = findChild(surface, "dialogBackdropProbe")
        verify(backdrop !== null)
        compare(backdrop.width, surface.width); compare(backdrop.height, surface.height)
        compare(backdrop.cornerRadius, 14); compare(backdrop.blurRadius, 25)
        compare(backdrop.menuShadow, true)
        compare(surface.radius, 14)
        compare(Qt.rgba(surface.color.r, surface.color.g, surface.color.b, 1),
                data.light ? dialog.theme.panel : Qt.color(data.fill))
        for (let parent = surface; parent; parent = parent.parent)
            verify(parent !== testCase, "Dialog surfaces must stay outside the captured interface.")
        for (const enabled of [true, false, true]) {
            redesignAppearance.backdropBlurEnabled = enabled
            fuzzyCompare(surface.color.a, enabled ? 0.90 : 1, 0.001)
        }
        dialog.scaleFactor = 1.5
        compare(surface.radius, 21); compare(backdrop.cornerRadius, 21)
        compare(backdrop.blurRadius, 37.5)
        dialog.close(); tryCompare(dialog, "visible", false)
        tryCompare(testCase, "activeBackdrops", 0)
        verify(!loader.active)
    }
    function test_confirmationFitsShortText() {
        const dialog = createTemporaryObject(confirmation, testCase, {parent: testCase})
        dialog.open(); tryCompare(dialog, "opened", true)
        const body = findChild(dialog.contentItem, "confirmationBody")
        const title = findChild(dialog.contentItem, "confirmationTitle")
        const message = findChild(dialog.contentItem, "confirmationMessage")
        tryVerify(() => body.height > 0 && body.contentHeight > 0)
        const bottom = message.mapToItem(body, 0, message.height).y
        verify(bottom <= body.height + 0.1, "The complete short message must fit without scrolling.")
        verify(title.mapToItem(body, 0, 0).y >= 0)
        const accept = findChild(dialog.contentItem, "confirmationAccept")
        verify(accept.mapToItem(dialog.background, 0, accept.height).y <= dialog.height - dialog.bottomPadding + 0.1)
        dialog.close(); tryCompare(dialog, "visible", false)
    }
    function test_confirmationKeepsActionsVisible_data() {
        return [{tag: "normal", scale: 1, light: false}, {tag: "large-light", scale: 1.5, light: true}]
    }
    function test_confirmationKeepsActionsVisible(data) {
        const host = createTemporaryObject(smallHost, testCase)
        const dialog = createTemporaryObject(confirmation, testCase, {
            parent: host, scaleFactor: data.scale, lightTheme: data.light,
            title: "Delete <Draft> & keep literal text",
            message: "This is a long file name with <tags> & spaces. ".repeat(30),
            confirmText: "Use this shortcut"
        })
        verify(dialog !== null)
        acceptedSpy.target = dialog; rejectedSpy.target = dialog
        acceptedSpy.clear(); rejectedSpy.clear()
        dialog.open(); tryCompare(dialog, "opened", true)
        const cancel = findChild(dialog.contentItem, "confirmationCancel")
        const accept = findChild(dialog.contentItem, "confirmationAccept")
        const body = findChild(dialog.contentItem, "confirmationBody")
        tryCompare(cancel, "activeFocus", true)
        tryVerify(() => body.contentHeight > 0 && body.height > 0)
        verify(dialog.height <= host.height - dialog.theme.dp(32))
        verify(dialog.stackedActions)
        for (const button of [cancel, accept]) {
            const point = button.mapToItem(dialog.background, 0, 0)
            verify(point.x >= 0 && point.y >= 0)
            verify(point.x + button.width <= dialog.width + 0.1)
            verify(point.y + button.height <= dialog.height + 0.1)
        }
        verify(body.contentItem.contentHeight > body.height, "scroll=" + body.contentHeight + " viewport=" + body.height + " content=" + body.contentItem + " height=" + body.contentItem.contentHeight)
        compare(findChild(dialog.contentItem, "confirmationTitle").textFormat, Text.PlainText)
        compare(findChild(dialog.contentItem, "confirmationMessage").text, dialog.message)
        keyClick(Qt.Key_Return)
        tryCompare(dialog, "visible", false)
        compare(acceptedSpy.count, 0); compare(rejectedSpy.count, 1)
        dialog.open(); tryCompare(dialog, "opened", true)
        mouseClick(accept)
        compare(acceptedSpy.count, 1)
        tryCompare(dialog, "visible", false)
        acceptedSpy.target = null; rejectedSpy.target = null
    }

}
