// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedAccent"
    width: 600; height: 400
    visible: true; when: windowShown
    property color accent: "#d37918"
    property bool light: false

    QtObject {
        id: store
        property int revision: 0
        function describe(name) { return {kind: name === "toggle" ? "bool" : "float", minimum: 0, maximum: 1} }
        function value(name) { return name === "toggle" ? true : 0.75 }
    }
    RedesignSeekBar {
        id: seek
        x: 20; y: 20; width: 500
        accentColor: testCase.accent; lightTheme: testCase.light
        value: 0.75
        timelineState: "ready"; durationSeconds: 100
        timelineCues: [{start: 10, stop: 30}]
    }
    RedesignSettingSlider {
        id: setting
        x: 20; y: 70; width: 500
        accentColor: testCase.accent; lightTheme: testCase.light
        label: "Speed"; currentValue: 0.75
    }
    RedesignSwitchRow {
        id: toggle
        x: 20; y: 180; width: 500
        accentColor: testCase.accent; lightTheme: testCase.light
        label: "Enabled"; checked: true
    }
    RedesignPreferenceRow {
        id: preferenceToggle
        x: 20; y: 240; width: 500
        accentColor: testCase.accent; lightTheme: testCase.light
        backend: store; optionName: "toggle"; label: "Preference"
    }
    RedesignPreferenceRow {
        id: preferenceSlider
        x: 20; y: 300; width: 500
        accentColor: testCase.accent; lightTheme: testCase.light
        backend: store; optionName: "slider"; label: "Preference slider"
        presentation: ({editor: "slider"})
    }

    function init() {
        accent = "#d37918"; light = false
        seek.timelineEnabled = false
        mouseMove(testCase, 590, 390)
    }
    function checkPixel(item, x, y, color) {
        wait(20)
        const image = testImages.grabWindow(testCase)
        const point = image.point(item, x, y)
        x = point.x; y = point.y
        const expected = Qt.color(color)
        verify(Math.abs(image.red(x, y) - Math.round(expected.r * 255)) <= 1,
               "Red channel: expected " + color + ", got " + image.red(x, y))
        verify(Math.abs(image.green(x, y) - Math.round(expected.g * 255)) <= 1,
               "Green channel: expected " + color + ", got " + image.green(x, y))
        verify(Math.abs(image.blue(x, y) - Math.round(expected.b * 255)) <= 1,
               "Blue channel: expected " + color + ", got " + image.blue(x, y))
    }
    function checkFills(color) {
        checkPixel(seek, 30, seek.height / 2, color)
        checkPixel(findChild(setting, "settingSlider").background, 12, 7, color)
        checkPixel(findChild(toggle, "settingSwitch").background, 4, 15, color)
        checkPixel(findChild(preferenceToggle, "preferenceToggleEditor").background, 4, 15, color)
        checkPixel(findChild(preferenceSlider, "preferenceSliderEditor").background, 12, 7, color)
    }
    function test_fillsMatchReferencePalette_data() {
        // Figma's seekbar uses 24% accent over #090909, or 30% for blue.
        return [
            {tag: "orange", accent: "#d37918", fill: "#371f06"},
            {tag: "red", accent: "#e15032", fill: "#3d1a13"},
            {tag: "yellow", accent: "#c49e23", fill: "#362d0f"},
            {tag: "green", accent: "#19a234", fill: "#0d2e13"},
            {tag: "cyan", accent: "#18aad3", fill: "#0d3039"},
            {tag: "blue", accent: "#185dd3", fill: "#0e2246"},
            {tag: "purple", accent: "#8e42ff", fill: "#291744"},
            {tag: "pink", accent: "#f052b3", fill: "#401b32"},
            {tag: "gray", accent: "#afafaf", fill: "#313131"},
            {tag: "light-orange", accent: "#d37918", light: true, fill: "#d8ba9a"},
            {tag: "light-green", accent: "#19a234", light: true, fill: "#99c9a3"},
            {tag: "custom", accent: "#12bc89", fill: "#0b3428"}
        ]
    }
    function test_fillsMatchReferencePalette(data) {
        accent = data.accent; light = !!data.light
        checkFills(data.fill)
        checkPixel(seek.handle, 4, 12, data.accent)
    }
    function test_liveChangesUpdateExistingComponents() {
        checkFills("#371f06")
        accent = "#19a234"; checkFills("#0d2e13")
        light = true; checkFills("#99c9a3")
        accent = "#d37918"; checkFills("#d8ba9a")
        light = false; checkFills("#371f06")
    }
    function test_playedSubtitleCuesFollowTheAccent() {
        seek.timelineEnabled = true
        const area = findChild(seek, "subtitleCueDragArea")
        const point = area.mapToItem(seek, area.width * 0.2, area.height / 2)
        checkPixel(seek, point.x, point.y, "#4e3823")
        accent = "#19a234"; checkPixel(seek, point.x, point.y, "#1e3f25")
        light = true; checkPixel(seek, point.x, point.y, "#73a37c")
    }
}
