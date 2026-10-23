// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    name: "JuxtopposedWaveform"
    width: 480; height: 160
    visible: true; when: windowShown
    RedesignPlayerSeekBar {
        id: seek
        x: 20; y: 40; width: 420; height: 28
        enabled: true; mediaStarted: true; playerPosition: .25
        waveformEnabled: true
    }
    SignalSpy { id: seekSpy; target: seek; signalName: "seekRequested" }
    SignalSpy { id: finished; target: seek; signalName: "dragFinished" }
    function init() {
        seek.waveformEnabled = true; seek.mediaStarted = true; seek.enabled = true
        seek.timelineEnabled = false; seek.timelineState = "off"; seek.timelineCues = []
        seek.waveformPeaks = [0, .25, .8, .5]; seek.width = 420
        seek.scaleFactor = 1; seek.lightTheme = false; seek.accentColor = "#d37918"
        seek.LayoutMirroring.enabled = false
        seek.timeForPosition = position => position.toFixed(2)
        seek.playerPosition = .25; seekSpy.clear(); finished.clear()
        wait(20)
    }
    function test_missingDataUsesTheNormalSeekbar() {
        verify(seek.waveformActive)
        seek.waveformPeaks = []
        verify(!seek.waveformActive); compare(seek.waveformBars.length, 0)
        seek.waveformPeaks = [0, 0, 0]
        verify(seek.waveformActive); compare(seek.waveformBars.length, 3)
    }
    function test_resizePreservesPeaksAndBoundsVisibleBars() {
        const peaks = []
        for (let i = 0; i < 2048; ++i) peaks.push(i === 1100 ? .95 : .1)
        seek.waveformPeaks = peaks
        verify(seek.waveformBars.length < 100)
        verify(Math.max(...seek.waveformBars) > .9)
        seek.width = 80
        verify(seek.waveformBars.length < 20)
        verify(Math.max(...seek.waveformBars) > .9)
        compare(seek.waveformPeaks.length, 2048)
    }
    function test_waveformDoesNotInterceptSeekingOrKeyboardInput() {
        mouseClick(seek, seek.width * .75, seek.height / 2)
        tryCompare(finished, "count", 1)
        verify(finished.signalArguments[0][1])
        verify(Math.abs(finished.signalArguments[0][0] - .75) < .03)
        verify(seekSpy.count > 0)
        seek.forceActiveFocus(); keyClick(Qt.Key_Right)
        verify(seekSpy.count > 1)
    }
    function test_subtitleTimingKeepsItsOwnInteractionLayer() {
        seek.timelineEnabled = true; seek.timelineState = "ready"
        seek.durationSeconds = 100; seek.timelineCues = [{start: 10, stop: 30}]
        verify(seek.timelineActive); verify(!seek.waveformActive)
        seek.timelineEnabled = false
        verify(seek.waveformActive)
    }
    function test_disablingWaveformRemovesItsBars() {
        verify(findChild(seek, "audioWaveform").visible)
        seek.waveformEnabled = false
        verify(!findChild(seek, "audioWaveform").visible)
        compare(seek.waveformBars.length, 0)
    }
    function test_figmaAudioSeekbarAppearance() {
        seek.waveformPeaks = Array(2048).fill(1)
        const waveform = findChild(seek, "audioWaveform")
        const first = waveform.children[0]
        const second = waveform.children[1]
        const last = waveform.children[seek.waveformBars.length - 1]
        // player-8.svg: 2 px bars at 5 px intervals, with a 14 px envelope.
        compare(first.width, 2); compare(first.height, 14); compare(first.radius, 1)
        compare(second.x - first.x, 5)
        compare(first.y + first.height / 2, waveform.height / 2)
        compare(first.color, "#ab6314"); compare(last.color, "#404040")
        compare(first.opacity, 1); compare(last.opacity, 1)
        compare(seek.handle.width, 4); compare(seek.handle.height, 24)
        compare(seek.handle.radius, 2)
        seek.waveformPeaks = Array(2048).fill(0)
        compare(waveform.children[0].height, 2)
        seek.waveformPeaks = []
        compare(seek.handle.width, 8)
    }
    function test_waveformOrderFollowsPlaybackInRtl() {
        seek.waveformPeaks = [0, .25, .8, .5]
        seek.LayoutMirroring.enabled = true
        const waveform = findChild(seek, "audioWaveform")
        verify(seek.mirrored)
        verify(waveform.children[0].x > waveform.children[1].x)
        compare(waveform.children[0].color, "#ab6314")
        compare(waveform.children[3].color, "#404040")
        mouseClick(seek, seek.width * .75, seek.height / 2)
        tryCompare(finished, "count", 1)
        verify(Math.abs(finished.signalArguments[0][0] - .25) < .03)
    }
    function test_seekTimeFollowsPointerAndHandle_data() {
        return [{tag: "wavebar", waveform: true}, {tag: "normal", waveform: false}]
    }
    function test_seekTimeFollowsPointerAndHandle(data) {
        seek.waveformEnabled = data.waveform
        const tip = findChild(seek, "seekTimeToolTip")
        mouseMove(seek, seek.width * .2, seek.height / 2)
        tryCompare(tip, "opened", true)
        fuzzyCompare(tip.x + tip.width / 2, seek.width * .2, 1)
        verify(tip.y + tip.height <= 0)
        mousePress(seek, seek.handle.x + seek.handle.width / 2, seek.height / 2)
        mouseMove(seek, seek.width * .8, seek.height / 2)
        fuzzyCompare(tip.x + tip.width / 2, seek.handle.x + seek.handle.width / 2, 1)
        compare(tip.text, seek.timeForPosition(seek.value))
        mouseRelease(seek, seek.width * .8, seek.height / 2)
        mouseMove(seek, -15, -35)
        tryCompare(seek, "hovered", false)
        seek.focus = false
        seek.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Right)
        fuzzyCompare(tip.x + tip.width / 2, seek.handle.x + seek.handle.width / 2, 1)
    }
    function test_hoverTimeUsesTheMirroredSeekPosition() {
        seek.LayoutMirroring.enabled = true
        mouseMove(seek, seek.width * .2, seek.height / 2)
        const tip = findChild(seek, "seekTimeToolTip")
        tryCompare(tip, "opened", true)
        verify(Math.abs(Number(tip.text) - .8) < .02)
        fuzzyCompare(tip.x + tip.width / 2, seek.width * .2, 1)
    }
}
