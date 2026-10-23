// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window
import QtTest

TestCase {
    id: testCase
    name: "JuxtopposedWindowCapture"
    width: 600; height: 400
    visible: true; when: windowShown

    Rectangle { anchors.fill: parent; color: "#203040" }
    Item {
        id: nested
        x: 41.5; y: 33.25
        width: 200; height: 150
        transformOrigin: Item.TopLeft
        Rectangle {
            id: outer
            x: 13; y: 17; width: 80; height: 60
            color: "#d37918"
            Rectangle { id: inner; x: 11; y: 9; width: 20; height: 20; color: "#19a234" }
        }
    }

    function test_presentedPixelsUseSceneCoordinates_data() {
        return [{tag: "nested", translation: 0, scale: 1},
                {tag: "translated", translation: 0.25, scale: 1},
                {tag: "scaled", translation: 0.5, scale: 1.5}]
    }
    function test_presentedPixelsUseSceneCoordinates(data) {
        testCase.x = data.translation; testCase.y = data.translation
        nested.scale = data.scale
        inner.color = "#19a234"
        const window = testCase.Window.window
        window.update()
        verify(waitForRendering(inner))
        // Passing a nested item must still capture the complete presented window.
        const image = testImages.grabWindow(inner)
        compare(image.width, Math.round(window.width * Screen.devicePixelRatio))
        compare(image.height, Math.round(window.height * Screen.devicePixelRatio))
        const p = image.point(inner, 10, 10)
        // Derive the expected position from the authored fixture dimensions,
        // independently of the helper's mapToScene implementation.
        compare(p.x, Math.floor((data.translation + 41.5 + (13 + 11 + 10) * data.scale) * image.width / window.width))
        compare(p.y, Math.floor((data.translation + 33.25 + (17 + 9 + 10) * data.scale) * image.height / window.height))
        compare(image.red(p.x, p.y), 25)
        compare(image.green(p.x, p.y), 162)
        compare(image.blue(p.x, p.y), 52)
        const background = image.point(testCase, 350, 250)
        compare(image.red(background.x, background.y), 32)
        compare(image.green(background.x, background.y), 48)
        compare(image.blue(background.x, background.y), 64)
        const border = image.point(outer, 60, 40)
        compare(image.red(border.x, border.y), 211)
        compare(image.green(border.x, border.y), 121)
        compare(image.blue(border.x, border.y), 24)
        compare(image.red(-1, -1), -1)
        compare(image.green(image.width, image.height), -1)

        inner.color = "#185dd3"
        verify(waitForRendering(inner))
        const changed = testImages.grabWindow(inner)
        compare(changed.red(p.x, p.y), 24)
        compare(changed.green(p.x, p.y), 93)
        compare(changed.blue(p.x, p.y), 211)
        // A capture is a snapshot, not a live view of the next frame.
        compare(image.green(p.x, p.y), 162)
    }
    function cleanup() { testCase.x = 0; testCase.y = 0; nested.scale = 1 }
}
