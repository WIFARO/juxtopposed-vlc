// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

Item {
    id: root
    property real scaleFactor: 1
    property color color: "#222222"
    property string dashArray: ""
    readonly property real pixelRatio: Screen.devicePixelRatio
    readonly property int rasterGrid: {
        for (let step = 1; step <= 64; ++step) {
            const pixels = step * pixelRatio
            if (Math.abs(pixels - Math.round(pixels)) < 0.000001) return step
        }
        return 1
    }
    implicitHeight: scaleFactor
    RedesignSvg {
        // Qt applies the display scale to sourceSize. Keep its logical size on
        // a grid that produces whole raster pixels, with room around stroke ends.
        x: -4 * root.scaleFactor; y: -2 * root.scaleFactor
        width: Math.ceil((root.width + 8 * root.scaleFactor) / root.rasterGrid) * root.rasterGrid
        height: Math.ceil(4 * root.scaleFactor / root.rasterGrid) * root.rasterGrid
        sourceSize: Qt.size(width, height)
        svg: '<svg xmlns="http://www.w3.org/2000/svg" width="' + width + '" height="' + height
            + '" viewBox="-4 -2 ' + width / root.scaleFactor + ' ' + height / root.scaleFactor
            + '"><path d="M0 0H' + root.width / root.scaleFactor
            + '" stroke="' + root.color + '"' + (root.dashArray.length ? ' stroke-dasharray="' + root.dashArray + '"' : '') + '/></svg>'
    }
}
