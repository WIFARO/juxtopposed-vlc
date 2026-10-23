// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedSubtitleTimeline"
    width: 800; height: 240
    visible: true; when: windowShown
    RedesignSeekBar {
        id: seek
        x: 20; y: 100; width: 760
        enabled: true
        timeForPosition: position => position.toFixed(3)
        onSubtitleDelayRequested: milliseconds => subtitleDelayMS = milliseconds
    }
    SignalSpy { id: precisionSpy; target: seek; signalName: "precisePositionUpdatesRequested" }
    SignalSpy { id: seekSpy; target: seek; signalName: "moved" }
    SignalSpy { id: delaySpy; target: seek; signalName: "subtitleDelayRequested" }
    SignalSpy { id: previewSpy; target: seek; signalName: "previewRequested" }
    function init() {
        seek.visible = true; seek.enabled = true
        seek.scaleFactor = 1; seek.lightTheme = false
        seek.timelineEnabled = false
        seek.timelineState = "ready"; seek.durationSeconds = 100
        seek.timelineCues = [{start: 10, stop: 15}, {start: 12, stop: 20}, {start: 40, stop: 50}, {start: 80, stop: 85}]
        seek.subtitleDelayMS = 0; seek.previewEnabled = false
        seek.playbackPosition = Qt.binding(() => seek.value)
        seek.value = 0.25
        seekSpy.clear(); delaySpy.clear(); previewSpy.clear(); precisionSpy.clear()
        mouseMove(testCase, 0, 0)
    }
    function dragArea() { return findChild(seek, "subtitleCueDragArea") }
    function xAt(position) { return (position - seek.from) / (seek.to - seek.from) * dragArea().width }
    function button(name) { return findChild(seek, name) }
    function test_offByDefaultAndNoInventedCues() {
        verify(!seek.timelineActive)
        seek.timelineEnabled = true
        verify(seek.timelineActive)
        seek.timelineState = "loading"; verify(!seek.timelineActive)
        seek.timelineState = "unavailable"; verify(!seek.timelineActive)
        seek.timelineState = "no-track"; verify(!seek.timelineActive)
        seek.timelineState = "ready"; seek.timelineCues = []
        verify(!seek.timelineActive)
        verify(!button("subtitleEarlierButton").visible)
        compare(seek.from, 0); compare(seek.to, 1)
    }
    function test_overlapsAndDenseIntervalsStayBoundedByScreenWidth() {
        seek.timelineEnabled = true
        compare(seek.mergedCues.length, 3)
        const cues = []
        for (let i = 0; i < 10000; ++i) cues.push({start: i / 100, stop: i / 100 + 0.002})
        seek.timelineCues = cues
        verify(seek.projectedCues.length <= dragArea().width)
        verify(seek.projectedCues.length > 0)
    }
    function test_timingArrowsUseSavedDelayAndKeyboard() {
        seek.timelineEnabled = true
        mouseClick(button("subtitleEarlierButton"))
        compare(seek.subtitleDelayMS, -100)
        const later = button("subtitleLaterButton")
        later.forceActiveFocus(); keyClick(Qt.Key_Space)
        compare(seek.subtitleDelayMS, 0)
        compare(delaySpy.count, 2); compare(seekSpy.count, 0)
    }
    function test_dragMovesCuesWithoutSeekingAndClampsNoVideo() {
        seek.timelineEnabled = true
        const area = dragArea(), initial = seek.value
        const start = xAt(0.45), move = area.width * 0.05
        mousePress(area, start, area.height / 2)
        mouseMove(area, start + move, area.height / 2, 30)
        verify(seek.cueDragging)
        verify(Math.abs(seek.displayedDelayMS - 5000) < 200)
        compare(seek.subtitleDelayMS, 0)
        compare(seek.value, initial)
        mouseRelease(area, start + move, area.height / 2)
        verify(Math.abs(seek.subtitleDelayMS - 5000) < 200)
        compare(delaySpy.count, 1); compare(seekSpy.count, 0)
    }
    function test_clickOnCueSeeksAndBlankPartRetainsNormalSeek() {
        seek.timelineEnabled = true
        const area = dragArea()
        mouseClick(area, xAt(0.45), area.height / 2)
        verify(Math.abs(seek.value - 0.45) < 0.01)
        compare(seekSpy.count, 1); compare(delaySpy.count, 0)
        mouseClick(area, xAt(0.65), area.height / 2)
        verify(Math.abs(seek.value - 0.65) < 0.01)
        verify(seekSpy.count >= 2)
    }
    function test_trackChangeDuringDragDoesNotEditTheNewTrack() {
        seek.timelineEnabled = true
        const area = dragArea(), start = xAt(0.45)
        mousePress(area, start, area.height / 2)
        mouseMove(area, start + 30, area.height / 2, 30)
        verify(seek.cueDragging)
        seek.timelineCues = [{start: 3, stop: 6}]
        verify(!seek.cueDragging)
        mouseRelease(area, start + 30, area.height / 2)
        compare(seek.subtitleDelayMS, 0)
        compare(delaySpy.count, 0); compare(seekSpy.count, 0)
    }
    function test_zoomKeepsGlobalSeekAndPreviewPositions() {
        seek.timelineEnabled = true
        seek.value = 0.45
        mouseClick(button("subtitleZoomInButton"))
        compare(seek.timelineZoom, 2)
        verify(Math.abs(seek.from - 0.2) < 0.001)
        verify(Math.abs(seek.to - 0.7) < 0.001)
        seek.previewEnabled = true
        const area = dragArea()
        mouseMove(area, xAt(0.6), area.height / 2)
        tryVerify(() => previewSpy.count > 0)
        verify(Math.abs(seek.previewPosition - 0.6) < 0.01)
        mouseClick(area, xAt(0.6), area.height / 2)
        verify(Math.abs(seek.value - 0.6) < 0.01)
        mouseClick(button("subtitleZoomOutButton"))
        compare(seek.from, 0); compare(seek.to, 1)
        verify(Math.abs(seek.value - 0.6) < 0.01)
    }
    function test_zoomFollowsPlaybackAndResetsForNewTrack() {
        seek.timelineEnabled = true; seek.value = 0.45
        seek.zoomTimeline(4)
        seek.playbackPosition = 0.9
        seek.value = 0.9
        verify(seek.from <= 0.9 && seek.to >= 0.9)
        seek.timelineCues = [{start: 1, stop: 2}]
        compare(seek.timelineZoom, 1)
        compare(seek.from, 0); compare(seek.to, 1)
    }
    function test_delayChangesAndLightScaledThemeUseRealIntervals() {
        seek.timelineEnabled = true
        const before = seek.projectedCues[0].start
        seek.subtitleDelayMS = 1000
        verify(seek.projectedCues[0].start > before)
        seek.lightTheme = true; seek.scaleFactor = 1.5
        compare(seek.height, 42)
        verify(seek.projectedCues[0].start >= 0)
        seek.subtitleDelayMS = -90000
        compare(seek.projectedCues.length, 0)
    }
    function labels() {
        const result = []
        function visit(item) {
            if (item.objectName === "subtitleCueText") result.push(item)
            for (const child of item.children) visit(child)
        }
        visit(seek)
        return result
    }
    function visibleLabels() { return labels().filter(label => label.visible) }
    function test_textAppearsOnlyWhenZoomedAndWideEnough() {
        seek.timelineCues = [{start: 40, stop: 50, text: "A real subtitle line with a long ending that needs more room to display"}]
        seek.timelineEnabled = true; seek.value = 0.45
        compare(visibleLabels().length, 0)
        seek.zoomTimeline(4)
        tryVerify(() => visibleLabels().length === 1)
        const label = visibleLabels()[0]
        compare(label.text, "A real subtitle line with a long ending that needs more room to display")
        compare(label.textFormat, Text.PlainText)
        compare(label.elide, Text.ElideRight)
        verify(label.truncated)
        seek.zoomTimeline(2)
        tryVerify(() => visibleLabels().length === 1 && !visibleLabels()[0].truncated)
        seek.timelineCues = [{start: 44, stop: 44.01, text: "Too narrow"}]
        seek.zoomTimeline(4)
        compare(visibleLabels().length, 0)
    }
    function test_touchingCuesKeepDistinctTextAtHighZoom() {
        seek.timelineCues = [{start: 40, stop: 45, text: "First"}, {start: 45, stop: 50, text: "Second"}]
        seek.timelineEnabled = true; seek.value = 0.45; seek.zoomTimeline(4)
        compare(seek.mergedCues.length, 2)
        tryVerify(() => visibleLabels().length === 2)
        compare(visibleLabels()[0].text, "First"); compare(visibleLabels()[1].text, "Second")
    }
    function test_overlappingCuesIdentifyAllDialogueInTheBox() {
        seek.timelineCues = [{start: 40, stop: 46, text: "First"}, {start: 44, stop: 50, text: "Second"}]
        seek.timelineEnabled = true; seek.value = 0.45; seek.zoomTimeline(4)
        compare(seek.mergedCues.length, 1)
        tryVerify(() => visibleLabels().length === 1)
        compare(visibleLabels()[0].text, "First · Second")
    }
    function test_literalUnicodeTextAndImageOnlyCues() {
        seek.timelineCues = [{start: 40, stop: 50, text: "<b>Café 日本語</b>"}]
        seek.timelineEnabled = true; seek.value = 0.45; seek.zoomTimeline(4)
        tryVerify(() => visibleLabels().length === 1)
        compare(visibleLabels()[0].text, "<b>Café 日本語</b>")
        compare(visibleLabels()[0].textFormat, Text.PlainText)
        seek.timelineCues = [{start: 40, stop: 50}]; seek.zoomTimeline(4)
        compare(visibleLabels().length, 0)
        compare(seek.projectedCues.length, 1)
    }
    function test_precisionLeaseFollowsZoomVisibilityAndEnabledState() {
        seek.timelineEnabled = true; seek.value = 0.45; seek.zoomTimeline(2)
        compare(precisionSpy.count, 1); compare(precisionSpy.signalArguments[0][0], true)
        seek.zoomTimeline(2); compare(precisionSpy.count, 1)
        seek.visible = false; compare(precisionSpy.count, 2); compare(precisionSpy.signalArguments[1][0], false)
        seek.visible = true; compare(precisionSpy.count, 3)
        seek.enabled = false; compare(precisionSpy.count, 4)
        seek.enabled = true; compare(precisionSpy.count, 5)
        seek.zoomTimeline(0.25); compare(precisionSpy.count, 6); compare(precisionSpy.signalArguments[5][0], false)
    }
    function test_trackChangeReleasesPrecisionAndClearsOldText() {
        seek.timelineCues = [{start: 40, stop: 50, text: "Old track"}]
        seek.timelineEnabled = true; seek.value = 0.45; seek.zoomTimeline(4)
        verify(seek.precisePositionRequested)
        seek.timelineState = "loading"
        verify(!seek.precisePositionRequested)
        compare(seek.projectedCues.length, 0)
        compare(visibleLabels().length, 0)
    }

    Component { id: temporarySeek; RedesignSeekBar {} }
    function test_destroyReleasesExactlyOnePrecisionLease() {
        const object = temporarySeek.createObject(testCase, {width: 400, height: 28, timelineEnabled: true,
            timelineState: "ready", durationSeconds: 100, timelineCues: [{start: 10, stop: 20}]})
        verify(object)
        let balance = 0
        object.precisePositionUpdatesRequested.connect(requested => balance += requested ? 1 : -1)
        object.zoomTimeline(2)
        compare(balance, 1)
        object.destroy()
        tryVerify(() => balance === 0)
    }
    function test_overlapSummaryDoesNotSplitUnicode() {
        const text = seek.combineCueText("x".repeat(2043), "😀 dialogue")
        verify(text.length <= 2048)
        verify(text.endsWith("…"))
        const last = text.charCodeAt(text.length - 2)
        verify(last < 0xd800 || last > 0xdbff)
    }

    function test_timingAndZoomButtonsDoNotOpenSeekPreview() {
        seek.timelineEnabled = true; seek.previewEnabled = true
        for (const name of ["subtitleEarlierButton", "subtitleZoomOutButton", "subtitleZoomInButton", "subtitleLaterButton"]) {
            const control = button(name)
            mouseMove(control, control.width / 2, control.height / 2)
            wait(180)
            verify(!seek.previewActive)
            verify(!findChild(seek, "seekTimeToolTip").visible)
        }
        compare(previewSpy.count, 0)
    }

    function test_textLabelKeepsCueDraggingAndClickSeeking() {
        seek.timelineCues = [{start: 40, stop: 50, text: "Dialogue under the pointer"}]
        seek.timelineEnabled = true; seek.value = 0.45; seek.zoomTimeline(4)
        tryVerify(() => visibleLabels().length === 1)
        const label = visibleLabels()[0]
        const area = dragArea()
        const point = label.mapToItem(area, label.width / 2, label.height / 2)
        const movedX = point.x + area.width * 0.04
        mousePress(area, point.x, point.y)
        mouseMove(area, movedX, point.y, 30)
        verify(seek.cueDragging)
        // Moving cues rebuilds the label delegates. Release in the stable
        // track coordinates, where the pointer started over the label.
        mouseRelease(area, movedX, point.y)
        compare(delaySpy.count, 1); compare(seekSpy.count, 0)
        verify(Math.abs(seek.subtitleDelayMS - 1000) < 150)
        tryVerify(() => visibleLabels().length === 1)
        mouseClick(visibleLabels()[0])
        compare(seekSpy.count, 1)
    }

}
