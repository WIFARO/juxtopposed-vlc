// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C
import QtQuick.Templates as T

ColumnLayout {
    id: root
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property string label
    property color labelColor: theme.foreground
    property real labelLeftPadding: 0
    property color valueColor: theme.foreground
    property color fillColor: theme.progress
    property color handleColor: theme.menuAccent
    property string suffix
    property int decimals: 0
    property real currentValue: 0
    property real from: 0
    property real to: 1
    property real inputFrom: from
    property real inputTo: to
    property real stepSize: 0.1
    property real resetValue: 1
    property real fillOrigin: Math.min(from, to) < 0 && Math.max(from, to) > 0 ? 0 : from
    property string minimumValueText
    property bool trimTrailingZeros: false
    property var ticks: []
    property color tickColor: theme.background
    property real tickWidth: 1
    property real tickHeight: 7
    property real tickRadius: 0
    property bool ticksUseFullTrack: false
    property string tickSuffix
    property bool positiveTickSign: false
    property real trackHeight: 16
    property real trackRadius: 6
    property real trackInset: 2
    property real sliderTopMargin: 0
    property real sliderLeftMargin: 0
    property bool scaleLabelsVisible: true
    property bool displayValueAsText: false
    property bool valueFieldVisible: true
    property real valueFieldHeight: 34
    property bool valueFieldOutline: true
    property bool sliderVisible: true
    property string valueLabel
    property var valueToPosition: null
    property var positionToValue: null
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    signal moved(real value)
    function positionFor(value) { return valueToPosition ? valueToPosition(value) : (value - from) / (to - from) }
    function visualPositionFor(value) { return slider.mirrored ? 1 - positionFor(value) : positionFor(value) }
    spacing: theme.dp(8)
    RowLayout {
        Layout.fillWidth: true
        Text { Layout.fillWidth: true; leftPadding: theme.dp(root.labelLeftPadding); text: root.label; color: root.labelColor; font.pixelSize: theme.dp(14); wrapMode: Text.WordWrap }
        T.SpinBox {
            id: valueField
            objectName: "settingValueField"
            property real factor: Math.pow(10, root.decimals)
            Layout.preferredWidth: theme.dp(140)
            visible: root.valueFieldVisible && !root.displayValueAsText
            implicitHeight: theme.dp(root.valueFieldHeight)
            leftPadding: theme.dp(16); rightPadding: theme.dp(10)
            font.pixelSize: theme.dp(14)
            editable: true
            from: Math.round(root.inputFrom * factor); to: Math.round(root.inputTo * factor)
            stepSize: Math.max(1, Math.round(root.stepSize * factor))
            value: Math.round(root.currentValue * factor)
            textFromValue: (value, locale) => {
                if (root.minimumValueText && value === from) return root.minimumValueText
                const number = value / factor
                let precision = root.decimals
                if (root.trimTrailingZeros) {
                    while (precision > 0 && Math.round(number * Math.pow(10, precision - 1)) / Math.pow(10, precision - 1) === number) --precision
                }
                return Number(number).toLocaleString(locale, 'f', precision) + root.suffix
            }
            valueFromText: (text, locale) => {
                let input = text.trim()
                if (root.minimumValueText && input.toLocaleLowerCase() === root.minimumValueText.toLocaleLowerCase()) return from
                if (root.suffix && input.endsWith(root.suffix)) input = input.slice(0, -root.suffix.length).trim()
                let number = NaN
                try { if (input.length) number = Number.fromLocaleString(locale, input) }
                catch (error) { return value }
                const scaled = Math.round(number * factor)
                return Number.isFinite(number) && number >= root.inputFrom && number <= root.inputTo ? scaled : value
            }
            // The displayed value can contain decimals, a unit or Automatic.
            // The integer validator of the standard SpinBox rejects those.
            contentItem: TextInput {
                text: valueField.displayText
                font: valueField.font; color: root.valueColor
                selectionColor: theme.accent; selectedTextColor: theme.background
                verticalAlignment: TextInput.AlignVCenter
                selectByMouse: true; clip: true
                onActiveFocusChanged: if (activeFocus) selectAll()
            }
            background: Rectangle {
                y: root.valueFieldOutline ? -theme.dp(1) : 0
                height: valueField.height + (root.valueFieldOutline ? theme.dp(2) : 0)
                radius: theme.dp(6); color: theme.field
                border.width: valueField.activeFocus || root.valueFieldOutline ? theme.dp(2) : 0
                border.color: valueField.activeFocus ? theme.accent : root.lightTheme ? theme.border : "#191919"
            }
            Accessible.name: root.label
            onValueModified: root.moved(value / factor)
        }
        Text {
            visible: root.valueFieldVisible && root.displayValueAsText
            Layout.preferredWidth: theme.dp(138)
            horizontalAlignment: Text.AlignRight
            text: root.valueLabel; color: root.labelColor; font.pixelSize: theme.dp(14)
        }
    }
    C.Slider {
        id: slider
        visible: root.sliderVisible
        objectName: "settingSlider"
        Layout.fillWidth: true
        Layout.leftMargin: theme.dp(root.sliderLeftMargin)
        Layout.topMargin: theme.dp(root.sliderTopMargin)
        padding: 0
        implicitHeight: theme.dp(24)
        from: root.valueToPosition ? 0 : root.from
        to: root.valueToPosition ? 1 : root.to
        stepSize: root.valueToPosition ? 0.005 : root.stepSize
        Binding on value { when: !slider.pressed; value: root.valueToPosition ? root.valueToPosition(root.currentValue) : root.currentValue }
        onMoved: root.moved(root.positionToValue ? root.positionToValue(value) : value)
        Keys.onPressed: event => {
            let value
            if (event.key === Qt.Key_Home) value = root.from
            else if (event.key === Qt.Key_End) value = root.to
            else if (event.key === Qt.Key_Left || event.key === Qt.Key_Down) value = root.currentValue - root.stepSize
            else if (event.key === Qt.Key_Right || event.key === Qt.Key_Up) value = root.currentValue + root.stepSize
            else return
            root.moved(Math.max(root.from, Math.min(root.to, value)))
            event.accepted = true
        }
        Accessible.name: root.label
        hoverEnabled: true
        RedesignToolTip { theme: root.theme; text: Number(root.currentValue).toLocaleString(Qt.locale(), 'f', root.decimals) + root.suffix; visible: parent.hovered || parent.pressed; delay: 500 }
        background: Rectangle {
            x: slider.leftPadding; y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth; height: theme.dp(root.trackHeight); radius: theme.dp(root.trackRadius); color: theme.field
            Rectangle {
                readonly property real inset: theme.dp(root.trackInset)
                readonly property real span: Math.max(0, parent.width - 2 * inset)
                readonly property real origin: {
                    const position = Math.max(0, Math.min(1, root.positionFor(root.fillOrigin)))
                    return slider.mirrored ? 1 - position : position
                }
                readonly property real travel: Math.max(0, span - sliderHandle.width)
                readonly property bool zeroDelay: root.fillOrigin === 0 && root.from < 0 && root.to > 0
                    && slider.visualPosition === origin
                x: inset + Math.min(slider.visualPosition, origin) * travel
                y: inset
                width: zeroDelay ? 0 : Math.min(span, Math.abs(slider.visualPosition - origin) * travel + sliderHandle.width)
                height: Math.max(0, parent.height - 2 * inset)
                radius: Math.max(0, parent.radius - inset); color: root.fillColor
            }
            Repeater {
                model: root.ticks
                Rectangle {
                    required property real modelData
                    width: theme.dp(root.tickWidth); height: theme.dp(root.tickHeight); radius: theme.dp(root.tickRadius)
                    x: root.ticksUseFullTrack
                        ? theme.dp(root.trackInset) + root.visualPositionFor(modelData) * (parent.width - 2 * theme.dp(root.trackInset)) - width / 2
                        : theme.dp(root.trackInset) + sliderHandle.width / 2 + root.visualPositionFor(modelData) * (parent.width - 2 * theme.dp(root.trackInset) - sliderHandle.width)
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.tickColor
                }
            }
        }
        handle: Rectangle {
            id: sliderHandle
            x: slider.leftPadding + theme.dp(root.trackInset) + slider.visualPosition * Math.max(0, slider.availableWidth - 2 * theme.dp(root.trackInset) - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: theme.dp(8); height: theme.dp(24); radius: theme.dp(4); color: root.handleColor
            border.width: slider.visualFocus ? 1 : 0; border.color: theme.bright
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: root.sliderVisible && root.scaleLabelsVisible && root.ticks.length === 0
        Text { text: root.from + root.suffix; color: root.labelColor; font.pixelSize: theme.dp(11) }
        Item { Layout.fillWidth: true }
        RedesignButton { objectName: "settingResetButton"; scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor; caption: qsTr("Reset"); text: qsTr("Reset %1").arg(root.label); implicitHeight: theme.dp(24); onClicked: root.moved(root.resetValue) }
        Item { Layout.fillWidth: true }
        Text { text: root.to + root.suffix; color: root.labelColor; font.pixelSize: theme.dp(11) }
    }
    Item {
        Layout.fillWidth: true
        implicitHeight: theme.dp(16)
        visible: root.sliderVisible && root.scaleLabelsVisible && root.ticks.length > 0
        Repeater {
            model: root.ticks
            Text {
                required property int index
                required property real modelData
                objectName: "settingTick_" + index
                x: Math.max(0, Math.min(parent.width - width, slider.leftPadding + sliderHandle.width / 2
                    + theme.dp(root.trackInset) + root.visualPositionFor(modelData) * (slider.availableWidth - 2 * theme.dp(root.trackInset) - sliderHandle.width) - width / 2))
                text: (root.positiveTickSign && modelData > 0 ? "+" : "") + Number(modelData).toLocaleString(Qt.locale(), 'f', Number.isInteger(modelData) ? 0 : String(modelData).split('.')[1].length) + root.tickSuffix
                color: root.labelColor; font.pixelSize: theme.dp(11)
            }
        }
    }
}
