// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "../player/qml/redesign"

Item {
    id: root
    width: 640; height: 800
    property string mode: "shared"
    readonly property bool ready: mode === "shared" ? true : mode === "menu" ? menu.opened : actions.opened
    function openForTest() {
        if (mode === "menu") menu.open()
        else if (mode === "action") actions.open()
    }
    RedesignIcon {
        visible: root.mode === "shared"
        width: 24; height: 24
        name: "menu-file"; color: "#8f8f8f"; detailColor: "#ab6314"
    }
    RedesignMenu { id: menu; parent: root }
    RedesignActionMenu {
        id: actions
        parent: root
        entries: [{action: "open", label: "Open", icon: "menu-file", accent: true}]
    }
}
