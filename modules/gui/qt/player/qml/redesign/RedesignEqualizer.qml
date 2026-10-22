// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

ColumnLayout {
    id: root
    required property var backend
    property real scaleFactor: 1
    property bool lightTheme: false
    property color accentColor: "#d37918"
    property bool expanded: false
    property bool compact: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    readonly property var bands: { backend.revision; return backend.equalizerBands() }
    readonly property real preamp: { backend.revision; return Number(backend.value("equalizer-preamp")) }
    readonly property var frequencies: { backend.revision; return backend.value("equalizer-vlcfreqs") === false
        ? ["Preamp", "31 Hz", "63 Hz", "125 Hz", "250 Hz", "500 Hz", "1 KHz", "2 KHz", "4 KHz", "8 KHz", "16 KHz"]
        : ["Preamp", "60 Hz", "170 Hz", "310 Hz", "600 Hz", "1 KHz", "3 KHz", "6 KHz", "12 KHz", "14 KHz", "16 KHz"] }
    spacing: 0
    function setGain(index, gain) {
        if (!Number.isFinite(gain) || gain < -20 || gain > 20) return false
        if (index !== 0) return backend.setEqualizerBand(index - 1, gain)
        if (!backend.stage("equalizer-preamp", gain)) return false
        return backend.stage("filter:audio-filter:equalizer", true)
    }
    RedesignButton {
        objectName: "equalizerExpander"
        Layout.fillWidth: true; Layout.preferredHeight: theme.dp(root.compact ? 48 : 41)
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        text: root.compact ? root.expanded ? qsTr("Collapse Settings") : qsTr("Expand Settings")
            : root.expanded ? qsTr("Close Equalizer") : qsTr("Open Equalizer")
        background: Rectangle { radius: root.compact ? theme.dp(4) : 0; color: parent.hovered ? theme.hover : root.compact ? theme.field : root.lightTheme ? theme.background : "#191919" }
        contentItem: Item {
            RowLayout {
                anchors.verticalCenter: parent.verticalCenter
                anchors.horizontalCenter: root.compact ? undefined : parent.horizontalCenter
                anchors.left: root.compact ? parent.left : undefined
                anchors.right: root.compact ? parent.right : undefined
                anchors.leftMargin: root.theme.dp(16); anchors.rightMargin: root.theme.dp(16)
                spacing: theme.dp(10)
                Text { Layout.fillWidth: root.compact; text: root.compact ? root.expanded ? qsTr("Collapse Settings") : qsTr("Expand Settings") : root.expanded ? qsTr("Close Equalizer") : qsTr("Open Equalizer"); color: root.theme.foreground; font.pixelSize: root.theme.dp(14) }
                RedesignIcon { name: root.expanded ? "chevron-down" : "chevron-right"; color: root.theme.foreground; Layout.preferredWidth: root.theme.dp(20); Layout.preferredHeight: root.theme.dp(20) }
            }
        }
        onClicked: root.expanded = !root.expanded
    }
    Rectangle {
        Layout.fillWidth: true; Layout.preferredHeight: theme.dp(root.compact ? 300 : 448)
        visible: root.expanded
        color: root.lightTheme ? theme.background : "#191919"
        C.ScrollView {
            id: scroll
            objectName: "equalizerScroll"
            wheelEnabled: false
            anchors.fill: parent
            contentWidth: Math.max(availableWidth, root.theme.dp(640))
            contentHeight: root.theme.dp(root.compact ? 300 : 448)
            clip: true
            C.ScrollBar.vertical.policy: C.ScrollBar.AlwaysOff
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                onWheel: event => {
                    const pixels = event.pixelDelta.x !== 0 || event.pixelDelta.y !== 0
                    const delta = pixels ? event.pixelDelta.x : event.angleDelta.x / 120 * root.theme.dp(48)
                    const vertical = pixels ? event.pixelDelta.y : event.angleDelta.y / 120 * root.theme.dp(48)
                    if (Math.abs(delta) <= Math.abs(vertical) || scroll.contentWidth <= scroll.availableWidth) { event.accepted = false; return }
                    const view = scroll.contentItem
                    view.contentX = Math.max(0, Math.min(scroll.contentWidth - scroll.availableWidth, view.contentX - delta))
                    event.accepted = true
                }
            }
            Row {
                width: scroll.contentWidth - root.theme.dp(root.compact ? 32 : 36)
                x: root.theme.dp(root.compact ? 16 : 18)
                y: root.theme.dp(root.compact ? 12 : 13)
                Repeater {
                    model: 11
                    ColumnLayout {
                        id: band
                        required property int index
                        width: parent.width / 11
                        readonly property real gain: index === 0 ? root.preamp : Number(root.bands[index - 1])
                        spacing: root.theme.dp(10)
                        C.Slider {
                            id: slider
                            objectName: "equalizerSlider_" + band.index
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredHeight: root.theme.dp(root.compact ? 200 : 320)
                            implicitWidth: root.theme.dp(32)
                            orientation: Qt.Vertical
                            from: -20; to: 20; stepSize: 0.1
                            topPadding: root.compact ? padding : 0
                            bottomPadding: topPadding
                            value: band.gain
                            Accessible.name: root.frequencies[band.index]
                            background: Rectangle {
                                x: (slider.width - width) / 2; y: slider.topPadding
                                width: root.theme.dp(root.compact ? 14 : 16); height: slider.availableHeight
                                radius: root.theme.dp(root.compact ? 5 : 6); color: root.theme.field
                                Rectangle {
                                    id: sliderFill
                                    readonly property real inset: root.compact ? 0 : root.theme.dp(2)
                                    x: inset
                                    y: inset + (root.compact ? Math.min(slider.visualPosition, 0.5) : slider.visualPosition) * (parent.height - 2 * inset)
                                    width: parent.width - 2 * inset
                                    height: (root.compact ? Math.abs(slider.visualPosition - 0.5) : 1 - slider.visualPosition) * (parent.height - 2 * inset)
                                    radius: parent.radius - inset; color: root.theme.progress
                                    border.width: root.compact ? 1 : 0; border.color: root.theme.field
                                }
                                Rectangle {
                                    objectName: "equalizerCenterMark_" + band.index
                                    visible: !root.compact && band.index > 0
                                    anchors.centerIn: parent
                                    width: root.theme.dp(8); height: root.theme.dp(2); radius: root.theme.dp(1)
                                    color: {
                                        if (root.lightTheme) return root.theme.sliderTick
                                        const center = parent.height / 2
                                        const base = sliderFill.height > 0 && center >= sliderFill.y && center <= sliderFill.y + sliderFill.height
                                            ? root.theme.progress : root.theme.field
                                        const ink = root.theme.sliderTick
                                        return Qt.rgba(1 - (1 - base.r) * (1 - ink.r),
                                            1 - (1 - base.g) * (1 - ink.g),
                                            1 - (1 - base.b) * (1 - ink.b), 1)
                                    }
                                }
                            }
                            handle: Rectangle {
                                x: (slider.width - width) / 2
                                y: slider.topPadding + slider.visualPosition * (slider.availableHeight - height)
                                width: root.theme.dp(24); height: root.theme.dp(8)
                                radius: root.theme.dp(4); color: root.compact ? root.theme.accent : root.theme.menuAccent
                                border.width: slider.visualFocus ? 1 : 0; border.color: root.theme.bright
                            }
                            onMoved: root.setGain(band.index, value)
                            TapHandler { onDoubleTapped: root.setGain(band.index, band.index === 0 ? 12 : 0) }
                            RedesignToolTip { theme: root.theme; text: root.frequencies[band.index] + ": " + Number(slider.value).toFixed(1) + " dB"; visible: parent.hovered || parent.pressed; delay: 0 }
                        }
                        Text {
                            Layout.fillWidth: true; Layout.preferredHeight: root.theme.dp(root.compact ? 24 : 19)
                            Layout.topMargin: root.theme.dp(root.compact ? 0 : 24)
                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                            text: root.frequencies[band.index]; color: root.theme.foreground
                            font.pixelSize: root.theme.dp(root.compact ? 12 : 14)
                        }
                        C.TextField {
                            id: valueField
                            objectName: "equalizerValue_" + band.index
                            Layout.fillWidth: true; Layout.leftMargin: root.theme.dp(root.compact ? 3 : 4); Layout.rightMargin: root.theme.dp(root.compact ? 3 : 4)
                            Layout.preferredHeight: root.theme.dp(32)
                            property bool invalid: false
                            text: Number(band.gain).toFixed(1)
                            font.pixelSize: root.theme.dp(root.compact ? 11 : 12); color: invalid ? "#e56b6b" : root.theme.foreground
                            horizontalAlignment: Text.AlignHCenter
                            leftPadding: root.theme.dp(2); rightPadding: activeFocus ? root.theme.dp(2) : root.theme.dp(18)
                            Text { anchors.right: parent.right; anchors.rightMargin: root.theme.dp(5); anchors.verticalCenter: parent.verticalCenter; text: "dB"; visible: !valueField.activeFocus; color: root.theme.foreground; font.pixelSize: root.theme.dp(root.compact ? 11 : 12) }
                            selectByMouse: true
                            Accessible.name: root.frequencies[band.index] + qsTr(" gain in dB")
                            background: Rectangle { color: root.theme.field; radius: root.theme.dp(root.compact ? 4 : 6); border.color: valueField.invalid ? "#e56b6b" : valueField.activeFocus ? root.theme.accent : "transparent" }
                            onEditingFinished: invalid = !text.trim().length || !root.setGain(band.index, Number(text))
                            onActiveFocusChanged: if (activeFocus) selectAll()
                            RedesignToolTip { theme: root.theme; text: qsTr("Gain in dB (−20 to 20)"); visible: parent.hovered; delay: 0 }
                        }
                    }
                }
            }
        }
    }
}
