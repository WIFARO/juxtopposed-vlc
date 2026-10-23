// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts

Item {
    id: root
    required property RedesignTheme theme
    required property RedesignPlayerLayout model
    required property Item headerHost
    required property Item footerHost
    required property var controls
    property Component extraControlComponent
    property var extras: ({})
    readonly property var allControls: Object.assign({}, controls, extras)
    property var renderedLayout: model.defaults()
    readonly property bool custom: JSON.stringify(renderedLayout) !== JSON.stringify(model.defaults())
    property bool reserveEditingRow: true
    readonly property real controlsHeight: theme.dp((model.editing && reserveEditingRow) || (custom && renderedLayout.zones.above.length) ? 108 : 54)
    property var originalParents: ({})
    property bool ready: false
    property bool applying: false
    readonly property string controlIds: JSON.stringify(Object.keys(allControls))
    readonly property bool compactDividerGap: renderedLayout.zones.footerLeft.some(id => id.startsWith("divider:"))
    readonly property bool audioRight: JSON.stringify(renderedLayout.zones.footerRight.filter(id => id !== "fullscreen"))
        === JSON.stringify(["mute", "volume", "audio", "queue"])
    readonly property bool standardRight: {
        const ids = renderedLayout.zones.footerRight
        const standard = model.defaults().zones.footerRight
        return audioRight || JSON.stringify(ids) === JSON.stringify(standard)
            || JSON.stringify(ids) === JSON.stringify(standard.concat(["fullscreen"]))
    }
    function gap(zone) {
        const ids = renderedLayout.zones[zone]
        if (zone === "footerLeft" && (compactDividerGap || (ids.length === 5
            && ids[0] === "previous" && ids[2] === "play" && ids[4] === "next"
            && ["backward10", "backward15"].includes(ids[1])
            && ["forward10", "forward15"].includes(ids[3])))) return 5
        if (zone === "footerRight" && standardRight) return 6
        return JSON.stringify(renderedLayout.zones[zone]) === JSON.stringify(["previous", "play", "next"]) ? 8 : 10
    }
    Instantiator {
        model: JSON.parse(root.controlIds)
        delegate: QtObject {
            required property string modelData
            readonly property Item control: root.allControls[modelData]
            readonly property Binding leftGap: Binding {
                target: control ? control.Layout : null
                property: "rightMargin"
                // Match the gaps around navigation and padded metadata.
                value: root.theme.dp(root.renderedLayout.zones.footerLeft.slice(-1)[0] === modelData || modelData === "title" ? 0
                    : modelData === "menu" && root.renderedLayout.zones.footerLeft[0] === "menu" ? 3 : modelData === "folder" ? 4 : 5)
                when: root.custom && !root.applying && root.compactDividerGap && !!control && control.parent === footerLeft
                    && !modelData.startsWith("divider:")
                restoreMode: Binding.RestoreBindingOrValue
            }
            readonly property Binding rightGap: Binding {
                target: control ? control.Layout : null
                property: "rightMargin"
                value: root.theme.dp(modelData === "mute" ? 4 : modelData === "volume" ? 9
                    : modelData === "settings" || (modelData === "audio" && root.audioRight) || (modelData === "queue" && root.renderedLayout.zones.footerRight.includes("fullscreen")) ? 8 : 0)
                when: root.custom && !root.applying && root.standardRight && !!control && control.parent === footerRight
                restoreMode: Binding.RestoreBindingOrValue
            }
        }
    }
    visible: false

    function hasTimeline(zone) { return renderedLayout.zones[zone].includes("timeline") }
    function hasSpacer(zone) { return renderedLayout.zones[zone].some(id => id.startsWith("spacer:")) }
    function hasStretch(zone) { return hasTimeline(zone) || hasSpacer(zone) }
    function preferredWidth(zone) {
        let width = 0, count = 0
        for (const id of renderedLayout.zones[zone]) {
            const control = allControls[id]
            if (!control || !control.visible) continue
            width += (control.implicitWidth || control.width) + control.Layout.leftMargin + control.Layout.rightMargin
            ++count
        }
        return width + Math.max(0, count - 1) * theme.dp(gap(zone))
    }
    function schedule() { if (ready) Qt.callLater(apply) }
    function apply() {
        const targets = {headerLeft: headerLeft, headerCenter: headerCenter, headerRight: headerRight,
            footerLeft: footerLeft, footerCenter: footerCenter, footerRight: footerRight, above: above}
        const next = model.clone(model.layout)
        const isCustom = JSON.stringify(next) !== JSON.stringify(model.defaults())
        applying = true
        const extra = Object.assign({}, extras)
        for (const zone of model.zones)
            for (const id of next.zones[zone])
                if (!controls[id] && !extra[id] && extraControlComponent)
                    extra[id] = extraControlComponent.createObject(root, {controlId: id})
        extras = extra
        // Keep the real controls alive: seeking, focus, menus and volume input
        // must keep their state when a layout moves a control to another bar.
        for (const id of Object.keys(allControls)) allControls[id].parent = root
        if (!isCustom) {
            for (const id of Object.keys(controls)) controls[id].parent = originalParents[id]
        } else {
            for (const zone of model.zones)
                for (const id of next.zones[zone])
                    if (allControls[id]) allControls[id].parent = targets[zone]
        }
        renderedLayout = next
        applying = false
    }
    Connections { target: root.model; function onLayoutChanged() { root.schedule() } }
    Component.onCompleted: {
        const parents = {}
        for (const id of Object.keys(controls)) parents[id] = controls[id].parent
        originalParents = parents
        ready = true
        schedule()
    }

    Item {
        parent: root.headerHost
        anchors.fill: parent
        visible: root.custom && !root.applying
        RowLayout {
            id: headerLeft
            objectName: "customHeaderLeft"
            x: root.theme.dp(50)
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
        }
        RowLayout {
            id: headerCenter
            objectName: "customHeaderCenter"
            anchors.centerIn: parent
            width: Math.max(0, parent.width - Math.max(headerLeft.x + headerLeft.width,
                parent.width - headerRight.x) * 2 - root.theme.dp(32))
            spacing: root.theme.dp(10)
        }
        RowLayout {
            id: headerRight
            objectName: "customHeaderRight"
            anchors.right: parent.right; anchors.rightMargin: root.allControls.close && root.allControls.close.flushToCorner ? 0 : root.theme.dp(6)
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
        }
    }
    Item {
        parent: root.footerHost
        anchors.bottom: parent.bottom
        width: parent.width; height: root.controlsHeight
        visible: root.custom && !root.applying
        RowLayout {
            id: above
            objectName: "customTimelineRow"
            x: root.theme.dp(20); y: root.theme.dp(1) + (root.theme.dp(54) - height) / 2
            width: parent.width - root.theme.dp(40)
            spacing: root.theme.dp(10)
        }
        Item {
            anchors.bottom: parent.bottom
            width: parent.width; height: root.theme.dp(54)
            RowLayout {
                id: footerLeft
                objectName: "customFooterLeft"
                x: root.theme.dp(20)
                width: root.hasSpacer("footerLeft") && !root.hasStretch("footerCenter")
                    ? Math.max(implicitWidth, (parent.width - root.preferredWidth("footerCenter")) / 2 - root.theme.dp(28)) : implicitWidth
                anchors.verticalCenter: parent.verticalCenter
                spacing: root.theme.dp(root.gap("footerLeft"))
            }
            RowLayout {
                id: footerCenter
                objectName: "customFooterCenter"
                x: root.hasStretch("footerCenter") ? footerLeft.x + footerLeft.width + root.theme.dp(10) : (parent.width - width) / 2
                width: root.hasStretch("footerCenter") ? Math.max(0, parent.width - footerLeft.width - footerRight.width - root.theme.dp(root.audioRight ? 57 : 56)) : root.preferredWidth("footerCenter")
                anchors.verticalCenter: parent.verticalCenter
                spacing: root.theme.dp(root.gap("footerCenter"))
            }
            RowLayout {
                id: footerRight
                objectName: "customFooterRight"
                anchors.right: parent.right; anchors.rightMargin: root.theme.dp(root.standardRight && !root.audioRight && !root.renderedLayout.zones.footerRight.includes("fullscreen") ? 17 : 18)
                width: root.hasSpacer("footerRight") && !root.hasStretch("footerCenter")
                    ? Math.max(implicitWidth, (parent.width - root.preferredWidth("footerCenter")) / 2 - root.theme.dp(28)) : implicitWidth
                anchors.verticalCenter: parent.verticalCenter
                spacing: root.theme.dp(root.gap("footerRight"))
            }
        }
    }
}
