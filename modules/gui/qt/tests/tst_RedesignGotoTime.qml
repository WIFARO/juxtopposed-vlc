// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedGotoTime"
    width: 1280; height: 900
    visible: true; when: windowShown
    RedesignGotoTimeDialog { id: dialog; parent: testCase }
    SignalSpy { id: seeks; target: dialog; signalName: "seekRequested" }
    function item(name) { return findChild(dialog, name) }
    function init() {
        width = 1280; height = 900
        dialog.scaleFactor = 1; dialog.lightTheme = false
        dialog.seekable = true; dialog.durationSeconds = 100000
        dialog.begin(0); tryCompare(dialog, "opened", true)
        seeks.clear()
    }
    function cleanup() { dialog.reject(); tryCompare(dialog, "visible", false) }
    function test_parse_data() {
        return [{tag:"seconds", text:"83", seconds:83}, {tag:"minutes", text:"01:23", seconds:83},
            {tag:"hours", text:"1:02:03", seconds:3723}, {tag:"long", text:"25:00:00", seconds:90000},
            {tag:"long-minutes", text:"90:00", seconds:5400}, {tag:"zero", text:"0", seconds:0},
            {tag:"spaces", text:"  01:23  ", seconds:83}, {tag:"maximum", text:"596523:14:07", seconds:2147483647},
            {tag:"empty", text:"", seconds:-1}, {tag:"sign", text:"-1", seconds:-1},
            {tag:"decimal", text:"1.5", seconds:-1}, {tag:"exponent", text:"1e3", seconds:-1},
            {tag:"bad-seconds", text:"1:60", seconds:-1}, {tag:"bad-minutes", text:"1:60:00", seconds:-1},
            {tag:"missing-part", text:"1::23", seconds:-1}, {tag:"extra-part", text:"1:2:3:4", seconds:-1},
            {tag:"overflow", text:"596523:14:08", seconds:-1}, {tag:"huge", text:"99999999999999999999999", seconds:-1},
            {tag:"markup", text:"<b>83</b>", seconds:-1}]
    }
    function test_parse(data) { compare(dialog.parseTime(data.text), data.seconds) }
    function test_openSelectsCurrentTimeWithoutDayWrap() {
        dialog.reject(); dialog.begin(90000); tryCompare(dialog, "opened", true)
        compare(dialog.timestampText, "25:00:00")
        verify(item("gotoTimeField").activeFocus)
        compare(item("gotoTimeField").selectedText, "25:00:00")
        compare(seeks.count, 0)
    }
    function test_enterCommitsParsedTimestampOnce() {
        dialog.timestampText = "01:23"
        keyClick(Qt.Key_Return)
        compare(seeks.count, 1); compare(seeks.signalArguments[0][0], 83)
        tryCompare(dialog, "visible", false)
        verify(!dialog.submit()); compare(seeks.count, 1)
    }
    function test_invalidAndOutOfRangeDoNotSeek() {
        for (const text of ["", "1:60", "-1", "999999:00:00"]) {
            dialog.timestampText = text
            verify(!item("gotoTimeGo").enabled)
            keyClick(Qt.Key_Return); compare(seeks.count, 0); verify(dialog.visible)
        }
        dialog.timestampText = "27:47:00"
        verify(!dialog.valid)
        compare(dialog.helpText, "Time must be between 00:00 and 27:46:40.")
        dialog.timestampText = "27:46:40"; verify(dialog.valid)
    }
    function test_unknownDurationAndNonSeekableMedia() {
        dialog.durationSeconds = 0; dialog.timestampText = "25:00:00"
        verify(dialog.valid)
        dialog.seekable = false
        verify(!item("gotoTimeField").enabled); verify(!item("gotoTimeGo").enabled)
        verify(!dialog.submit()); compare(seeks.count, 0)
        compare(dialog.helpText, "You cannot change the time for this media.")
        mouseClick(item("gotoTimeCancel")); tryCompare(dialog, "visible", false)
    }
    function test_cancelAndEscapePreserveTime() {
        dialog.timestampText = "10:00"
        mouseClick(item("gotoTimeCancel")); compare(seeks.count, 0)
        dialog.begin(15); tryCompare(dialog, "opened", true)
        compare(dialog.timestampText, "00:15")
        keyClick(Qt.Key_Escape); tryCompare(dialog, "visible", false); compare(seeks.count, 0)
    }
    function test_keyboardCanCancel() {
        keyClick(Qt.Key_Tab); verify(item("gotoTimeCancel").activeFocus)
        keyClick(Qt.Key_Return); tryCompare(dialog, "visible", false); compare(seeks.count, 0)
    }
    function test_styleAndConstrainedLayout_data() {
        return [{tag:"dark", width:1280, height:900, scale:1},
                {tag:"light", width:1280, height:900, scale:1, light:true},
                {tag:"scaled", width:1280, height:900, scale:1.5},
                {tag:"small", width:280, height:300, scale:1},
                {tag:"small-scaled", width:280, height:300, scale:1.5}]
    }
    function test_styleAndConstrainedLayout(data) {
        width = data.width; height = data.height
        dialog.scaleFactor = data.scale; dialog.lightTheme = !!data.light
        verify(waitForPolish(dialog.contentItem))
        compare(item("gotoTimeTitle").font.pixelSize, 20 * data.scale)
        compare(item("gotoTimeField").height, 37 * data.scale)
        compare(item("gotoTimeGo").height, 32 * data.scale)
        compare(item("gotoTimeHelp").textFormat, Text.PlainText)
        verify(dialog.width <= width - 32 * data.scale)
        verify(dialog.height <= height - 32 * data.scale)
        for (const button of [item("gotoTimeCancel"), item("gotoTimeGo")]) {
            const p = button.mapToItem(testCase, 0, 0)
            verify(p.x >= 0 && p.x + button.width <= width + 1)
            verify(p.y >= 0 && p.y + button.height <= height + 1)
        }
        const field = item("gotoTimeField"), scroll = item("gotoTimeBody")
        tryVerify(() => { const p = field.mapToItem(scroll, 0, 0); return p.y >= -1 && p.y + field.height <= scroll.height + 1 })
    }
}
