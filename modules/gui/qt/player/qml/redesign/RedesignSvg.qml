// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

Image {
    property string svg
    readonly property bool usesImageProvider: typeof redesignSvgImages !== "undefined" && !!redesignSvgImages
    // Qt scales image-provider requests for the display. Data-URL previews do
    // not get that adjustment, so only those need physical raster dimensions.
    readonly property real rasterScale: usesImageProvider ? 1 : Screen.devicePixelRatio
    sourceSize: Qt.size(width * rasterScale, height * rasterScale)
    fillMode: Image.PreserveAspectFit
    // Standalone QML previews do not have VLC's image providers.
    source: svg.length ? ((usesImageProvider ? "image://svgcolor/?_res_SVG=" : "data:image/svg+xml;utf8,") + encodeURIComponent(svg)) : ""
}
