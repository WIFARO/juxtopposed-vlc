// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

Item {
    id: root
    property real scaleFactor: 1
    property color color: "#222222"
    implicitHeight: scaleFactor
    RedesignSvg {
        y: -root.scaleFactor; width: parent.width; height: 2 * root.scaleFactor
        svg: '<svg xmlns="http://www.w3.org/2000/svg" width="' + width + '" height="' + height
            + '" viewBox="0 -1 ' + width / root.scaleFactor + ' 2"><path d="M0 0H' + width / root.scaleFactor
            + '" stroke="' + root.color + '"/></svg>'
    }
}
