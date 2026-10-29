// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: tests
    name: "WilPipControls"
    width: 700; height: 450
    visible: true; when: windowShown
    Component {
        id: overlayComponent
        RedesignPipControls {
            width: 480; height: 270
            started: true; playing: true; seekable: true
            position: 0.3; volume: 0.7
            mediaTitle: "Picture in Picture"
            onTogglePlayback: playing = !playing
            onSeekRequested: position => this.position = position
            onVolumeRequested: volume => this.volume = volume
        }
    }
    Component { id: spyComponent; SignalSpy {} }
    Component { id: dragTargetComponent; Item { width: 480; height: 270 } }
    Component {
        id: tracksComponent
        ListModel {
            ListElement { display: "English"; checked: true }
            ListElement { display: "Bahasa Indonesia"; checked: false }
        }
    }
    function overlay(properties) {
        const item = createTemporaryObject(overlayComponent, tests, properties || {})
        verify(item !== null)
        verify(waitForRendering(item))
        return item
    }
    function spy(item, signal) {
        const result = createTemporaryObject(spyComponent, tests, {target: item, signalName: signal})
        verify(result.valid)
        return result
    }
    function test_pictureClickDoesNotPause() {
        const item = overlay()
        const play = spy(item, "togglePlayback")
        const move = spy(item, "moveRequested")
        mouseClick(item, 100, 100)
        compare(play.count, 0)
        compare(move.count, 0)
        compare(item.playing, true)
    }
    function test_pictureDragRequestsMoveWithoutPausingOrSeeking() {
        const item = overlay()
        const move = spy(item, "moveRequested")
        const play = spy(item, "togglePlayback")
        const seek = spy(item, "seekRequested")
        mouseDrag(item, 100, 100, 100, 20)
        compare(move.count, 1)
        compare(play.count, 0)
        compare(seek.count, 0)
        compare(item.playing, true)
    }
    function test_playAndTimeSkipButtonsAreIndependent() {
        const item = overlay()
        const move = spy(item, "moveRequested")
        const jump = spy(item, "jumpRequested")
        mouseClick(findChild(item, "pipPlayButton"))
        compare(item.playing, false)
        mouseClick(findChild(item, "pipBackwardButton"))
        mouseClick(findChild(item, "pipForwardButton"))
        compare(jump.count, 2)
        compare(jump.signalArguments[0][0], -10)
        compare(jump.signalArguments[1][0], 10)
        compare(move.count, 0)
    }
    function test_embeddedDragTargetMovesWithinBoundsWithoutPausing() {
        const target = createTemporaryObject(dragTargetComponent, tests, {x: 40, y: 40})
        const item = createTemporaryObject(overlayComponent, target, {
            dragTarget: target, dragXMin: 20, dragXMax: 100, dragYMin: 20, dragYMax: 100
        })
        verify(waitForRendering(item))
        mouseDrag(item, 100, 100, 90, 80)
        compare(target.x, 100)
        compare(target.y, 100)
        compare(item.playing, true)
        mouseClick(findChild(item, "pipPlayButton"))
        compare(item.playing, false)
        compare(target.x, 100)
        compare(target.y, 100)
    }
    function test_unseekableStreamAndUnpausablePlayback() {
        const item = overlay({seekable: false, pausable: false})
        verify(!findChild(item, "pipBackwardButton").enabled)
        verify(!findChild(item, "pipForwardButton").enabled)
        verify(!findChild(item, "pipTimeline").enabled)
        verify(!findChild(item, "pipPlayButton").enabled)
    }
    function test_timelineScrubDoesNotMoveWindow() {
        const item = overlay()
        const move = spy(item, "moveRequested")
        const start = spy(item, "seekDragStarted")
        const finish = spy(item, "seekDragFinished")
        const timeline = findChild(item, "pipTimeline")
        mouseDrag(timeline, timeline.width * 0.3, timeline.height / 2, timeline.width * 0.4, 0)
        compare(move.count, 0)
        compare(start.count, 1)
        tryCompare(finish, "count", 1)
        compare(finish.signalArguments[0][1], true)
        verify(item.position > 0.6)
    }
    function test_hidingDuringScrubCancels() {
        const item = overlay()
        const finish = spy(item, "seekDragFinished")
        const timeline = findChild(item, "pipTimeline")
        mousePress(timeline, timeline.width / 2, timeline.height / 2)
        item.visible = false
        tryCompare(finish, "count", 1)
        compare(finish.signalArguments[0][1], false)
        mouseRelease(tests, 650, 400)
    }
    function test_volumeAndMuteDoNotMoveOrPause() {
        const item = overlay()
        const move = spy(item, "moveRequested")
        const mute = spy(item, "muteRequested")
        const slider = findChild(item, "pipVolumeSlider")
        mouseDrag(slider, slider.width * 0.5, slider.height / 2, slider.width * 0.4, 0)
        verify(item.volume > 0.9)
        item.volume = 0.2
        compare(slider.value, 0.2)
        mouseClick(findChild(item, "pipMuteButton"))
        compare(mute.count, 1)
        compare(move.count, 0)
        compare(item.playing, true)
    }
    function test_volumeWheelDoesNotMoveOrPause() {
        const item = overlay()
        const move = spy(item, "moveRequested")
        const slider = findChild(item, "pipVolumeSlider")
        const before = item.volume
        mouseWheel(slider, slider.width / 2, slider.height / 2, 0, 120)
        verify(item.volume > before)
        compare(move.count, 0)
        compare(item.playing, true)
    }
    function test_subtitleTrackSelectionOffAndAdd() {
        const tracks = createTemporaryObject(tracksComponent, tests)
        const item = overlay({subtitleTracks: tracks})
        const select = spy(item, "subtitleSelectionRequested")
        const off = spy(item, "subtitlesOffRequested")
        const add = spy(item, "addSubtitlesRequested")
        const move = spy(item, "moveRequested")
        const button = findChild(item, "pipSubtitleButton")
        const popup = findChild(item, "pipSubtitlePopup")
        mouseClick(button)
        tryCompare(popup, "opened", true)
        tryVerify(() => popup.height > 100)
        const english = findChild(popup.contentItem, "pipSubtitleTrack_0")
        compare(english.highlighted, true)
        // This popup scrolls rather than growing beyond a small PiP window.
        const indonesia = findChild(popup.contentItem, "pipSubtitleTrack_1")
        indonesia.forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(select.count, 1)
        compare(select.signalArguments[0][0], 1)
        compare(select.signalArguments[0][1], true)
        mouseClick(button)
        tryCompare(popup, "opened", true)
        tryVerify(() => popup.height > 100)
        mouseClick(findChild(popup.contentItem, "pipSubtitlesOff"))
        compare(off.count, 1)
        mouseClick(button)
        tryCompare(popup, "opened", true)
        findChild(popup.contentItem, "pipAddSubtitles").forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(add.count, 1)
        compare(move.count, 0)
        compare(item.playing, true)
    }
    function test_escapeDismissesPopupBeforeReturning() {
        const item = overlay()
        const back = spy(item, "returnRequested")
        mouseClick(findChild(item, "pipSubtitleButton"))
        const popup = findChild(item, "pipSubtitlePopup")
        tryCompare(popup, "opened", true)
        keyClick(Qt.Key_Escape)
        tryCompare(popup, "visible", false)
        compare(back.count, 0)
        item.forceActiveFocus()
        keyClick(Qt.Key_Escape)
        compare(back.count, 1)
    }
    function test_subtitleButtonTogglesPopupWithoutReopening() {
        const item = overlay()
        const button = findChild(item, "pipSubtitleButton")
        const popup = findChild(item, "pipSubtitlePopup")
        mouseClick(button)
        tryCompare(popup, "opened", true)
        tryVerify(() => popup.height > 16)
        mouseClick(button)
        tryCompare(popup, "visible", false)
    }
    function test_subtitlePopupKeepsControlsVisibleAndTracksLiveSelection() {
        const tracks = createTemporaryObject(tracksComponent, tests)
        const item = overlay({subtitleTracks: tracks, controlsHideTimeout: 50})
        const popup = findChild(item, "pipSubtitlePopup")
        mouseClick(findChild(item, "pipSubtitleButton"))
        tryCompare(popup, "opened", true)
        tryVerify(() => popup.height > 100)
        tracks.setProperty(0, "checked", false)
        tracks.setProperty(1, "checked", true)
        compare(findChild(popup.contentItem, "pipSubtitleTrack_0").highlighted, false)
        compare(findChild(popup.contentItem, "pipSubtitleTrack_1").highlighted, true)
        mouseMove(tests, 650, 400)
        wait(100)
        compare(item.controlsVisible, true)
        verify(popup.visible)
    }
    function test_controlsFitMinimumWindow() {
        const item = overlay({width: 320, height: 180})
        const names = ["pipMuteButton", "pipVolumeSlider", "pipBackwardButton", "pipPlayButton", "pipForwardButton", "pipSubtitleButton"]
        let right = 0
        for (const name of names) {
            const control = findChild(item, name)
            const point = control.mapToItem(item, 0, 0)
            verify(point.x >= right - 1, name + " must not overlap")
            verify(point.x + control.width <= item.width, name + " must fit")
            right = point.x + control.width
        }
    }
    function test_autoHideRevealsOnMotionAndKeepsPausedControls() {
        const item = overlay({controlsHideTimeout: 50})
        mouseMove(tests, 650, 400)
        item.forceActiveFocus()
        tryCompare(item, "controlsVisible", false)
        mouseMove(item, 100, 100)
        tryCompare(item, "controlsVisible", true)
        item.playing = false
        wait(100)
        compare(item.controlsVisible, true)
    }
    function test_mousePlaybackButtonDoesNotPinControlsForever() {
        const item = overlay({controlsHideTimeout: 100})
        const play = findChild(item, "pipPlayButton")
        mouseClick(play)
        compare(item.playing, false)
        mouseClick(play)
        compare(item.playing, true)
        mouseMove(tests, 650, 400)
        tryCompare(item, "controlsVisible", false)
    }
    function test_keyboardControlFocusKeepsControlsVisible() {
        const item = overlay({controlsHideTimeout: 50})
        item.forceActiveFocus()
        keyClick(Qt.Key_Tab)
        tryCompare(item, "keyboardFocus", true)
        wait(100)
        compare(item.controlsVisible, true)
    }
    function test_resizeEdgeDoesNotMoveOrPause() {
        const item = overlay({resizeEnabled: true})
        const resize = spy(item, "resizeRequested")
        const move = spy(item, "moveRequested")
        mouseClick(item, 1, 100)
        compare(resize.count, 1)
        compare(resize.signalArguments[0][0], Qt.LeftEdge)
        compare(move.count, 0)
        compare(item.playing, true)
    }
}
