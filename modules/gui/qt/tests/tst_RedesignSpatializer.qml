// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedSpatializer"
    width: 400; height: 600
    visible: true; when: windowShown
    QtObject {
        id: store
        property int revision: 0
        property var values: ({})
        property var edits: []
        function value(name) { return values[name] }
        function describe(name) { return {kind: name.startsWith("filter:") ? "bool" : "float", minimum: 0, maximum: name === "spatializer-roomsize" ? 1.1 : 1} }
        function stage(name, value) {
            edits.push({name: name, value: value})
            values = Object.assign({}, values, {[name]: value}); revision++
            return true
        }
    }
    RedesignSpatializer { id: effect; x: 20; y: 20; width: 360; backend: store }
    Item {
        id: settings; visible: false; x: 20; y: 20; width: 360; height: 560
        RedesignPreferenceRow {
            id: settingsRow
            width: parent.width; backend: store
            optionName: "filter:audio-filter:spatializer"; label: "Spatializer"
            presentation: ({spatializer: true})
        }
    }
    function item(name) { return findChild(effect, name) }
    function slider(name) { return findChild(item(name), "settingSlider") }
    function chevron(item) {
        if (String(item.name || "").startsWith("chevron-")) return item
        for (const child of item.children || []) {
            const found = chevron(child)
            if (found) return found
        }
        return null
    }
    function test_disclosureFollowsMirroring() {
        const section = effect
        const arrow = chevron(section)
        verify(arrow !== null)
        try {
            section.LayoutMirroring.childrenInherit = true
            section.LayoutMirroring.enabled = true
            tryCompare(arrow, "name", "chevron-left")
            section.expanded = true
            tryCompare(arrow, "name", "chevron-down")
            section.expanded = false
            tryCompare(arrow, "name", "chevron-left")
        } finally {
            section.expanded = false
            section.LayoutMirroring.enabled = false
            section.LayoutMirroring.childrenInherit = false
        }
        tryCompare(arrow, "name", "chevron-right")
    }
    function init() {
        failOnWarning(/.?/)
        testCase.forceActiveFocus()
        effect.visible = true; settings.visible = false
        effect.expanded = false; effect.lightTheme = false; effect.accentColor = "#d37918"; effect.scaleFactor = 1
        store.values = {"filter:audio-filter:spatializer": false, "spatializer-roomsize": 0.85, "spatializer-width": 1,
                        "spatializer-wet": 0.4, "spatializer-dry": 0.5, "spatializer-damp": 0.5}
        store.edits = []; store.revision++
        mouseMove(testCase, 399, 599); wait(20)
    }
    function test_expansionMatchesReferenceWithoutEnablingTheEffect() {
        compare(effect.height, 48)
        mouseClick(item("spatializerExpander"))
        verify(effect.expanded); verify(!effect.effectEnabled); compare(store.edits.length, 0)
        compare(item("spatializerExpander").text, "Collapse Settings")
        tryCompare(effect, "height", 438)
        const order = ["roomsize", "width", "wet", "dry", "damp"]
        for (let i = 0; i < order.length; ++i) {
            const control = item("spatializer-" + order[i])
            compare(control.width, 328)
            compare(findChild(control, "settingSlider").background.height, 16)
            fuzzyCompare(control.mapToItem(effect, 0, 0).y, 60 + i * 76, 0.1)
        }
        mouseClick(item("spatializerExpander")); tryCompare(effect, "height", 48)
    }
    function test_disabledFilterKeepsValuesAndRejectsInput() {
        effect.expanded = true; wait(20)
        verify(!slider("spatializer-wet").enabled)
        mouseClick(slider("spatializer-wet"), 300, 12)
        compare(store.edits.length, 0); compare(store.value("spatializer-wet"), 0.4)
        compare(item("spatializer-wet").valueLabel, "4.0")
    }
    function test_keyboardChangesUseNativeUnitsAndEachControlsRange() {
        store.stage("filter:audio-filter:spatializer", true)
        effect.expanded = true; wait(20)
        for (const name of ["spatializer-roomsize", "spatializer-width", "spatializer-wet", "spatializer-dry", "spatializer-damp"]) {
            const control = slider(name)
            verify(control.enabled); control.forceActiveFocus()
            keyClick(Qt.Key_Home); compare(store.value(name), 0)
            keyClick(Qt.Key_Right); fuzzyCompare(store.value(name), 0.01, 0.00001)
            keyClick(Qt.Key_End); fuzzyCompare(store.value(name), name.endsWith("roomsize") ? 1.1 : 1, 0.00001)
            keyClick(Qt.Key_Right); fuzzyCompare(store.value(name), name.endsWith("roomsize") ? 1.1 : 1, 0.00001)
        }
        verify(effect.effectEnabled)
    }
    function test_settingsRowEnablesTheSameNativeFilter() {
        effect.visible = false; settings.visible = true
        const toggle = findChild(settingsRow, "preferenceToggleEditor")
        const expander = findChild(settingsRow, "spatializerExpander")
        verify(toggle); verify(expander)
        mouseClick(toggle); verify(store.value("filter:audio-filter:spatializer"))
        mouseClick(expander)
        const wet = findChild(settingsRow, "spatializer-wet")
        verify(findChild(wet, "settingSlider").enabled)
        compare(wet.valueLabel, "4.0")
    }
    function test_externalValuesAndThemeChangesUpdateWithoutEditing() {
        effect.expanded = true
        store.stage("spatializer-roomsize", 0.8); store.stage("spatializer-width", 0.3)
        store.edits = []
        compare(item("spatializer-roomsize").valueLabel, "8.0")
        compare(item("spatializer-width").valueLabel, "3.0")
        effect.accentColor = "#19a234"; effect.lightTheme = true
        compare(item("spatializer-width").accentColor, "#19a234")
        verify(item("spatializer-width").lightTheme)
        compare(store.edits.length, 0)
    }
}
