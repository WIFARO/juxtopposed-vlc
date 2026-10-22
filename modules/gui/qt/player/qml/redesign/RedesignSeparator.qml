// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

Item {
    id: root
    property real scaleFactor: 1
    property color color: "#222222"
    property string dashArray: ""
    implicitHeight: scaleFactor
    // Avoid a half-pixel raster height at 125% scale.
    RedesignSvg {
        y: -2 * root.scaleFactor; width: parent.width; height: 4 * root.scaleFactor
        svg: '<svg xmlns="http://www.w3.org/2000/svg" width="' + width + '" height="' + height
            + '" viewBox="0 -2 ' + width / root.scaleFactor + ' 4"><path d="M0 0H' + width / root.scaleFactor
            + '" stroke="' + root.color + '"' + (root.dashArray.length ? ' stroke-dasharray="' + root.dashArray + '"' : '') + '/></svg>'
    }
}
