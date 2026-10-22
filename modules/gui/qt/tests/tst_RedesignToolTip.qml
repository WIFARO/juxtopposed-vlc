// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic as C
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedToolTip"
    width: 600; height: 400
    visible: true; when: windowShown
    property real originalWindowWidth: 0
    property real originalWindowHeight: 0
    property bool hintPreference: true
    RedesignButton { id: button; x: 20; y: 100; text: "Playback settings" }
    SignalSpy { id: closedSpy; target: findChild(button, "redesignToolTip"); signalName: "closed" }
    Window {
        id: offscreenWindow
        property var renderWindow: testCase.Window.window
        width: 300; height: 200
        visible: false
        RedesignButton { id: offscreenButton; text: "Audio" }
    }
    Component { id: mainMenuPopup; RedesignMenu { triggerItem: button } }
    Component { id: actionMenuPopup; RedesignActionMenu { entries: [{action: "first", label: "First"}] } }
    Component { id: trackPopup; RedesignTrackPopup { placementArea: testCase } }
    function test_popupCancelsTriggerHint_data() {
        return [
            {tag: "menu-pending", component: mainMenuPopup, mode: "menu", shown: false},
            {tag: "menu-shown", component: mainMenuPopup, mode: "menu", shown: true},
            {tag: "actions-pending", component: actionMenuPopup, mode: "action", shown: false},
            {tag: "actions-shown", component: actionMenuPopup, mode: "action", shown: true},
            {tag: "tracks-pending", component: trackPopup, mode: "track", shown: false},
            {tag: "tracks-shown", component: trackPopup, mode: "track", shown: true}
        ]
    }
    function test_popupCancelsTriggerHint(data) {
        testCase.height = 900
        testCase.Window.window.height = 900
        const popup = createTemporaryObject(data.component, testCase)
        verify(popup !== null)
        const activate = () => {
            if (data.mode === "track") popup.toggleFrom(button, "audio")
            else if (data.mode === "menu") popup.toggleFrom(button, testCase)
            else popup.toggleFrom(button)
        }
        button.clicked.connect(activate)
        try {
            const tip = findChild(button, "redesignToolTip")
            tip.delay = 200
            mouseMove(button, button.width / 2, button.height / 2)
            if (data.shown) tryCompare(tip, "opened", true, 2000)
            else { wait(20); compare(tip.opened, false) }
            mouseClick(button)
            tryCompare(popup, "opened", true)
            tryCompare(tip, "visible", false)
            wait(tip.delay + 100)
            verify(!tip.visible, "The trigger hint must not cover its open popup.")
            popup.close()
            tryCompare(popup, "visible", false)
            mouseMove(testCase, 500, 850)
            mouseMove(button, button.width / 2, button.height / 2)
            tryCompare(tip, "opened", true, 2000)
        } finally {
            button.clicked.disconnect(activate)
            popup.close()
        }
    }
    function test_popupRestoresTheTriggerHintBinding() {
        const popup = createTemporaryObject(actionMenuPopup, testCase)
        verify(popup !== null)
        button.tooltipEnabled = Qt.binding(() => testCase.hintPreference)
        popup.toggleFrom(button)
        tryCompare(popup, "opened", true)
        compare(button.tooltipEnabled, false)
        hintPreference = false
        popup.close()
        tryCompare(popup, "visible", false)
        compare(button.tooltipEnabled, false)
        hintPreference = true
        compare(button.tooltipEnabled, true)
    }
    function init() {
        if (typeof failOnWarning === "function") failOnWarning(/Binding loop|non-NOTIFYable/)
        originalWindowWidth = testCase.Window.window.width
        originalWindowHeight = testCase.Window.window.height
        button.tooltipEnabled = true; hintPreference = true
        testCase.Window.window.visibility = Window.Windowed
        button.scaleFactor = 1; button.lightTheme = false
        button.text = "Playback settings"; button.tooltipTextFormat = Text.AutoText
        button.x = 20; button.y = 100
        const tip = findChild(button, "redesignToolTip")
        tip.delay = 650
        tip.close(); mouseMove(testCase, 400, 300)
    }
    function cleanup() {
        findChild(button, "redesignToolTip").close()
        testCase.Window.window.width = originalWindowWidth
        testCase.height = 400
        testCase.Window.window.height = originalWindowHeight
        testCase.Window.window.visibility = Window.Windowed
    }
    function test_fileNamesCanRequestPlainText() {
        const tip = findChild(button, "redesignToolTip")
        compare(tip.contentItem.textFormat, Text.AutoText)
        button.text = "/videos/<b>Favorite</b>/"
        button.tooltipTextFormat = Text.PlainText
        mouseMove(button, button.width / 2, button.height / 2)
        tryCompare(tip, "opened", true, 2000)
        compare(tip.contentItem.text, button.text)
        compare(tip.contentItem.textFormat, Text.PlainText)
    }
    function test_longFileNameFitsWindow_data() {
        return [{tag:"dark",scale:1,light:false},{tag:"light-scaled",scale:1.5,light:true}]
    }
    function test_longFileNameFitsWindow(data) {
        button.scaleFactor = data.scale; button.lightTheme = data.light
        button.text = "/videos/" + "Very_Long_File_Name_".repeat(20)
        button.tooltipTextFormat = Text.PlainText
        const tip = findChild(button, "redesignToolTip")
        mouseMove(button, button.width / 2, button.height / 2)
        tryCompare(tip, "opened", true, 2000)
        verify(tip.width <= 360 * data.scale + 1)
        verify(tip.contentItem.lineCount > 1)
        const position = tip.contentItem.mapToItem(testCase, 0, 0)
        verify(position.x >= 0 && position.y >= 0)
        verify(position.x + tip.contentItem.width <= width)
        verify(position.y + tip.contentItem.height <= height)
    }
    function test_helpUsesAReadableWidth_data() {
        return [{tag: "wide", scale: 1, windowWidth: 600},
                {tag: "narrow", scale: 1, windowWidth: 280},
                {tag: "scaled-narrow", scale: 1.5, windowWidth: 280}]
    }
    function test_helpUsesAReadableWidth(data) {
        testCase.Window.window.width = data.windowWidth
        button.scaleFactor = data.scale
        button.text = "Include subfolders adds files from the folder and keeps its subfolders in the queue. "
            + "Open folders on playback reads a folder when you play it. "
            + "Expand all subfolders reads all files at once. Changes apply to the next folder you add."
        button.x = (data.windowWidth - button.width) / 2
        const tip = findChild(button, "redesignToolTip")
        mouseMove(button, button.width / 2, button.height / 2)
        tryCompare(tip, "opened", true, 2000)
        verify(tip.width <= Math.min(360 * data.scale, data.windowWidth - 2 * tip.margins) + 1)
        verify(tip.contentItem.lineCount > 1)
        const position = tip.contentItem.mapToItem(testCase, 0, 0)
        verify(position.x >= 0 && position.y >= 0)
        verify(position.x + tip.contentItem.width <= data.windowWidth)
        verify(position.y + tip.contentItem.height <= testCase.Window.window.height)
        fuzzyCompare(position.x + tip.contentItem.width / 2, data.windowWidth / 2, 1)
    }
    function test_hoverUsesThemeAndApplicationFont() {
        const tip = findChild(button, "redesignToolTip")
        mouseMove(button, button.width / 2, button.height / 2)
        tryCompare(tip, "opened", true, 2000)
        compare(tip.text, button.text)
        compare(tip.background.color, button.theme.panel)
        compare(tip.contentItem.color, button.theme.bright)
        compare(tip.font.family, Qt.application.font.family)
        compare(tip.font.pixelSize, 12)
        compare(tip.popupType, C.Popup.Item)
        button.lightTheme = true; button.scaleFactor = 1.5
        compare(tip.background.color, button.theme.panel)
        compare(tip.font.pixelSize, 18)
    }
    function test_hoverStaysStableAtWindowEdges_data() {
        return [{tag: "title-left", x: 0, y: 0}, {tag: "title-right", x: 564, y: 0},
                {tag: "bottom-left", x: 0, y: 364}, {tag: "bottom-right", x: 564, y: 364}]
    }
    function test_hoverStaysStableAtWindowEdges(data) {
        const tip = findChild(button, "redesignToolTip")
        button.x = data.x; button.y = data.y
        mouseMove(button, button.width / 2, button.height / 2)
        tryCompare(tip, "opened", true, 2000)
        closedSpy.clear()
        wait(1200)
        verify(button.hovered)
        verify(tip.opened)
        compare(closedSpy.count, 0)
        compare(tip.popupType, C.Popup.Item)
        const position = tip.contentItem.mapToItem(testCase, 0, 0)
        verify(position.x >= 0 && position.y >= 0)
        verify(position.x + tip.contentItem.width <= width)
        verify(position.y + tip.contentItem.height <= height)
        mouseMove(testCase, 300, 250)
        tryCompare(tip, "opened", false)
    }
    function test_offscreenRenderingUsesAnItemPopup() {
        const tip = findChild(offscreenButton, "redesignToolTip")
        compare(tip.hostWindow, testCase.Window.window)
        compare(tip.popupType, C.Popup.Item)
    }
    function test_fullscreenUsesTheControlsWindow() {
        const tip = findChild(button, "redesignToolTip")
        testCase.Window.window.visibility = Window.FullScreen
        compare(tip.popupType, C.Popup.Item)
        mouseMove(button, button.width / 2, button.height / 2)
        tryCompare(tip, "opened", true, 2000)
        compare(tip.popupType, C.Popup.Item)
    }
}
