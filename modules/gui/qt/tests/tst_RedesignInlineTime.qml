// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedInlineTime"
    width: 1280; height: 900
    visible: true; when: windowShown
    RedesignPlayerView {
        id: player
        anchors.fill: parent
        started: true; seekable: true
        timeText: "00:02:10"; durationSeconds: 100000
    }
    SignalSpy { id: seeks; target: player; signalName: "seekToSecondsRequested" }
    SignalSpy { id: play; target: player; signalName: "togglePlayback" }
    SignalSpy { id: fullscreen; target: player; signalName: "fullscreenRequested" }
    function item(name) { return findChild(player, name) }
    function init() {
        player.cancelTimeEdit()
        player.started = true; player.seekable = true; player.presentationOnly = false
        player.timeText = "00:02:10"; player.durationSeconds = 100000
        player.fullscreen = false; player.hasVideo = false
        player.scaleFactor = 1; player.lightTheme = false
        player.playerLayout = ""; player.layoutModel.reload()
        width = 1280; height = 900
        wait(0) // Let the queued chrome reparent finish before sending input.
        seeks.clear(); play.clear(); fullscreen.clear()
        tryVerify(() => player.layoutReady && item("elapsedTimeLabel").visible)
        verify(waitForPolish(player))
    }
    function cleanup() { player.cancelTimeEdit() }
    function begin() {
        mouseClick(item("elapsedTimeLabel"))
        tryCompare(item("elapsedTimeEditor"), "activeFocus", true)
        verify(item("elapsedTimeLabel").editing)
    }
    function type(text) {
        keyClick(Qt.Key_A, Qt.ControlModifier)
        for (const character of text) keyClick(character)
    }
    function test_clickSelectsTimeAndLiveUpdatesKeepDraft() {
        const label = item("elapsedTimeLabel"), bar = item("seekBar")
        const before = bar.mapToItem(player, 0, 0), beforeWidth = bar.width
        begin()
        compare(item("elapsedTimeEditor").selectedText, "00:02:10")
        type("01:23")
        player.timeText = "00:02:11"
        compare(item("elapsedTimeEditor").text, "01:23")
        compare(bar.mapToItem(player, 0, 0), before); compare(bar.width, beforeWidth)
        keyClick(Qt.Key_Escape)
        verify(!label.editing); verify(!item("elapsedTimeEditor").activeFocus)
        compare(label.text, "00:02:11"); compare(seeks.count, 0)
    }
    function test_enter_data() {
        return [{tag:"minutes", text:"01:23", seconds:83, key:Qt.Key_Return},
            {tag:"seconds", text:"83", seconds:83, key:Qt.Key_Enter},
            {tag:"zero", text:"0", seconds:0, key:Qt.Key_Return},
            {tag:"hours-over-day", text:"25:00:00", seconds:90000, key:Qt.Key_Return}]
    }
    function test_enter(data) {
        begin(); type(data.text); keyClick(data.key)
        compare(seeks.count, 1); compare(seeks.signalArguments[0][0], data.seconds)
        verify(!item("elapsedTimeLabel").editing); verify(!item("elapsedTimeEditor").activeFocus)
        compare(play.count, 0)
    }
    function test_invalidAndRangeDoNotSeek() {
        player.durationSeconds = 125
        begin()
        for (const value of ["1:60", "126", "-1", "", "999999:00:00"]) {
            type(value); keyClick(Qt.Key_Return)
            compare(seeks.count, 0); verify(item("elapsedTimeLabel").editing)
            verify(item("elapsedTimeEditor").activeFocus)
        }
        type("02:05"); keyClick(Qt.Key_Return)
        compare(seeks.count, 1); compare(seeks.signalArguments[0][0], 125)
    }
    function test_unknownDurationStillAllowsAbsoluteTime() {
        player.durationSeconds = 0
        begin(); type("25:00:00"); keyClick(Qt.Key_Return)
        compare(seeks.count, 1); compare(seeks.signalArguments[0][0], 90000)
    }
    function test_invalidHelpKeepsFocus() {
        player.durationSeconds = 125
        begin(); type("126"); wait(450)
        verify(item("elapsedTimeLabel").editing)
        verify(item("elapsedTimeEditor").activeFocus)
        compare(item("elapsedTimeLabel").helpText, "Time must be between 00:00 and 02:05.")
        keyClick(Qt.Key_Escape); compare(seeks.count, 0)
    }
    function test_outsideClickCancelsAndStillActivatesControl() {
        begin(); type("83")
        mouseClick(item("playButton"))
        verify(!item("elapsedTimeLabel").editing); compare(seeks.count, 0); compare(play.count, 1)
        begin(); type("83")
        mouseClick(player, width / 2, height / 2)
        verify(!item("elapsedTimeLabel").editing); compare(seeks.count, 0)
    }
    function test_clickInsideEditorPaddingDoesNotCancel() {
        begin(); type("83")
        const field = item("elapsedTimeEditor")
        mouseClick(field, field.width / 2, 1)
        verify(item("elapsedTimeLabel").editing)
        mouseClick(field, field.width / 2, field.height - 1)
        verify(item("elapsedTimeLabel").editing)
        keyClick(Qt.Key_Escape); compare(seeks.count, 0)
    }
    function test_focusLossAndTabCancel() {
        begin(); type("83"); keyClick(Qt.Key_Tab)
        verify(!item("elapsedTimeLabel").editing); compare(seeks.count, 0)
        begin(); type("83"); item("seekBar").forceActiveFocus()
        verify(!item("elapsedTimeLabel").editing); compare(seeks.count, 0)
    }
    function test_keyboardCanBegin() {
        item("elapsedTimeLabel").forceActiveFocus()
        keyClick(Qt.Key_Return)
        verify(item("elapsedTimeEditor").activeFocus)
        type("83"); keyClick(Qt.Key_Return)
        compare(seeks.count, 1); compare(seeks.signalArguments[0][0], 83)
    }
    function test_stoppedNonSeekableAndPreviewCannotEdit() {
        for (const property of ["started", "seekable", "presentationOnly"]) {
            player.started = true; player.seekable = true; player.presentationOnly = false
            begin(); type("83")
            player[property] = property === "presentationOnly"
            verify(!item("elapsedTimeLabel").editing)
            mouseClick(item("elapsedTimeLabel")); verify(!item("elapsedTimeLabel").editing)
            compare(seeks.count, 0)
        }
    }
    function test_mediaChangeCancelAndReopen() {
        begin(); type("83"); player.cancelTimeEdit()
        player.timeText = "00:00:00"
        begin(); compare(item("elapsedTimeEditor").selectedText, "00:00:00")
        compare(seeks.count, 0)
    }
    function test_fullscreenKeepsControlsAndEscapeDoesNotExit() {
        player.hasVideo = true; player.fullscreen = true
        player.controlsHideTimeout = 30; player.revealControls()
        begin(); wait(80)
        verify(player.bottomVisible); verify(player.controlsLocked)
        keyClick(Qt.Key_Escape)
        verify(!item("elapsedTimeLabel").editing); compare(fullscreen.count, 0)
    }
    function test_styleAndAlternateLayout_data() {
        return [{tag:"dark", scale:1, light:false}, {tag:"light", scale:1, light:true},
            {tag:"scaled", scale:1.5, light:false}]
    }
    function test_styleAndAlternateLayout(data) {
        width = 1920; height = 1080
        player.scaleFactor = data.scale; player.lightTheme = data.light
        player.layoutModel.begin(); player.layoutModel.move("timeline", "above", 0)
        tryVerify(() => player.layoutReady)
        begin()
        const field = item("elapsedTimeEditor"), label = item("elapsedTimeLabel")
        compare(field.font.pixelSize, label.font.pixelSize)
        compare(field.width, label.width)
        const p = field.mapToItem(player, 0, 0)
        verify(p.x >= 0 && p.x + field.width <= width)
        verify(p.y >= 0 && p.y + field.height <= height)
        type("83"); keyClick(Qt.Key_Return)
        compare(seeks.count, 1)
        player.layoutModel.finish(false)
    }
}
