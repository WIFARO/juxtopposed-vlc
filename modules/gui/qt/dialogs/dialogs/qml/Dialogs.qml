/*****************************************************************************
 * Copyright (C) 2019 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C
import QtQuick.Templates as T

import VLC.MainInterface
import VLC.Dialogs
import VLC.Widgets as Widgets
import VLC.Util
import VLC.Style

Item {
    id: root
    objectName: "interactionDialogs"

    //---------------------------------------------------------------------------------------------
    // Properties
    //---------------------------------------------------------------------------------------------

    property Item bgContent: null

    //---------------------------------------------------------------------------------------------
    // Signal
    //---------------------------------------------------------------------------------------------

    signal restoreFocus();

    //---------------------------------------------------------------------------------------------
    // Events
    //---------------------------------------------------------------------------------------------

    Component.onCompleted: if (DialogErrorModel.notificationAllowed && DialogErrorModel.repeatedMessageCount) showNotice()

    property var requests: []
    property var activeRequest: null
    property bool shuttingDown: false

    property bool cancelingAll: false
    Component.onDestruction: {
        shuttingDown = true
        cancelAllRequests()
    }
    function cancelAllRequests() {
        if (cancelingAll) return
        cancelingAll = true
        const remaining = requests
        requests = []
        activeRequest = null
        loginDialog.close()
        questionDialog.close()
        progressDialog.close()
        customDialog.close()
        for (const request of remaining) {
            if (request.kind === "custom") {
                if (request.reject) request.reject()
            } else vlcDialog.dismiss(request.id)
        }
        cancelingAll = false
    }

    function ask(text, acceptCb, rejectCb, buttons) {
        enqueue({kind: "custom", text: text, accept: acceptCb, reject: rejectCb, buttons: buttons})
    }
    function enqueue(request) {
        if (shuttingDown || cancelingAll) return
        requests = requests.concat([request])
        showNext()
    }
    function findRequest(id) {
        return requests.find(request => request.kind !== "custom" && request.id === id)
    }
    function showNext() {
        if (shuttingDown || (activeRequest && activeRequest.kind !== "progress")) return
        // A background operation may need a sign-in or an answer to continue.
        // Hide its progress view while retaining its ID and latest updates.
        const next = requests.find(request => request.kind !== "progress") || requests[0]
        if (loginDialog.visible || questionDialog.visible || progressDialog.visible || customDialog.visible) {
            if (activeRequest && activeRequest.kind === "progress" && next && next.kind !== "progress") {
                activeRequest = null
                progressDialog.close()
            }
            return
        }
        if (!next) return
        activeRequest = next
        if (next.kind === "login") {
            loginDialog.request = next
            loginDialog.title = next.title
            username.text = next.username
            password.clear()
            savePassword.checked = false
            loginDialog.open()
        } else if (next.kind === "question") {
            questionDialog.request = next
            questionDialog.title = next.title
            questionDialog.open()
        } else if (next.kind === "progress") {
            progressDialog.request = next
            progressDialog.title = next.title
            progressDialog.text = next.text
            progressDialog.position = next.position
            progressDialog.open()
        } else {
            customDialog.ask(next.text, () => finishRequest(next, "accept"),
                             () => finishRequest(next, "dismiss"), next.buttons)
        }
    }
    function finishRequest(request, action, result) {
        if (!request || requests.indexOf(request) < 0) return
        requests = requests.filter(item => item !== request)
        if (activeRequest === request) {
            activeRequest = null
            if (request.kind === "login") loginDialog.close()
            else if (request.kind === "question") questionDialog.close()
            else if (request.kind === "progress") progressDialog.close()
        }
        // Remove the request before posting: posting releases its native ID.
        if (request.kind === "custom") {
            const callback = action === "accept" ? request.accept : request.reject
            if (callback) callback()
        } else if (action === "login") vlcDialog.post_login(request.id, result.username, result.password, request.askStore && result.store)
        else if (action === "action1") vlcDialog.post_action1(request.id)
        else if (action === "action2") vlcDialog.post_action2(request.id)
        else vlcDialog.dismiss(request.id)
        Qt.callLater(showNext)
    }
    function requestClosed() {
        if (!shuttingDown && VLCDialogModel.provider === vlcDialog && requests.length === 0) restoreFocus()
        Qt.callLater(showNext)
    }
    function progressValue(value) { return isFinite(value) ? Math.max(0, Math.min(1, value)) : 0 }

    VLCDialog {
        id: vlcDialog
        objectName: "interactionProvider"
        Component.onCompleted: VLCDialogModel.provider = vlcDialog
        Component.onDestruction: {
            root.shuttingDown = true
            root.cancelAllRequests()
            if (VLCDialogModel.provider === vlcDialog) VLCDialogModel.provider = null
        }
        onCancelAll: root.cancelAllRequests()
        onLogin: (dialogId, title, text, defaultUsername, askStore) => {
            root.enqueue({kind: "login", id: dialogId, title: title, text: text, username: defaultUsername, askStore: askStore})
        }
        onQuestion: (dialogId, title, text, type, cancel, action1, action2) => {
            root.enqueue({kind: "question", id: dialogId, title: title, text: text, severity: type, cancel: cancel, action1: action1, action2: action2})
        }
        onProgress: (dialogId, title, text, indeterminate, position, cancel) => {
            root.enqueue({kind: "progress", id: dialogId, title: title, text: text, indeterminate: indeterminate, position: root.progressValue(position), cancel: cancel})
        }
        onProgressUpdated: (dialogId, position, text) => {
            const request = root.findRequest(dialogId)
            if (!request || request.kind !== "progress") return
            request.text = text
            request.position = root.progressValue(position)
            if (root.activeRequest === request) {
                progressDialog.text = text
                progressDialog.position = request.position
            }
        }
        onCancelled: (dialogId) => {
            const request = root.findRequest(dialogId)
            // A queued cancellation can arrive after the user has replied.
            // Its ID must not be released a second time.
            if (request) root.finishRequest(request, "dismiss")
        }
    }

    property Item noticeReturnFocus: null
    function isNoticeItem(item) {
        for (let current = item; current; current = current.parent)
            if (current === errorPopup) return true
        return false
    }
    function showNotice() {
        if (!DialogErrorModel.notificationAllowed) return
        const window = root.Window.window
        if (window && window.activeFocusItem && !isNoticeItem(window.activeFocusItem))
            noticeReturnFocus = window.activeFocusItem
        errorPopup.state = "visible"
        if (errorPopup.contentItem && errorPopup.contentItem.held) hideErrorPopupTimer.stop()
        else hideErrorPopupTimer.restart()
    }
    function hideNotice() {
        const restore = errorPopup.activeFocus
        hideErrorPopupTimer.stop()
        errorPopup.state = "hidden"
        DialogErrorModel.resetRepeatedMessageCount()
        if (restore && noticeReturnFocus && noticeReturnFocus.visible && noticeReturnFocus.enabled)
            noticeReturnFocus.forceActiveFocus(Qt.OtherFocusReason)
    }
    Connections {
        target: DialogErrorModel
        function onNotificationsEnabledChanged() {
            if (!DialogErrorModel.notificationAllowed) root.hideNotice()
        }
        function onNotificationRequested() { root.showNotice() }
    }
    Connections {
        target: root.Window.window
        enabled: errorPopup.state === "visible"
        function onActiveFocusItemChanged() {
            const item = root.Window.window.activeFocusItem
            if (item && !root.isNoticeItem(item)) root.noticeReturnFocus = item
        }
    }
    Widgets.DrawerExt {
        id: errorPopup
        objectName: "errorNotificationPopup"
        anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: VLCStyle.margin_small }
        edge: Widgets.DrawerExt.Edges.Bottom
        width: Math.max(0, Math.min(VLCStyle.dp(560, VLCStyle.scale), root.width - VLCStyle.dp(32, VLCStyle.scale)))
        height: contentItem ? contentItem.height : 0
        z: 10
        state: "hidden"
        component: MessageNotice {
            scaleFactor: VLCStyle.scale
            lightTheme: !VLCStyle.palette.isDark
            accentColor: MainCtx.redesignAccent
            title: DialogErrorModel.notificationTitle
            details: DialogErrorModel.notificationDetails
            count: DialogErrorModel.repeatedMessageCount
            warning: DialogErrorModel.notificationIsWarning
            maximumHeight: {
                const window = root.Window.window
                const localY = root.y // Keep this binding tied to bottom-anchor changes.
                return window ? Math.max(0, root.mapToItem(window.contentItem, 0, root.height).y - VLCStyle.dp(24, VLCStyle.scale)) : VLCStyle.dp(240, VLCStyle.scale)
            }
            onHeldChanged: {
                if (held) hideErrorPopupTimer.stop()
                else if (errorPopup.state === "visible") hideErrorPopupTimer.restart()
            }
            onDismissRequested: root.hideNotice()
            onCopyRequested: if (DialogErrorModel.copyNotification()) copyDone()
            onDetailsRequested: {
                root.hideNotice()
                DialogsProvider.messagesDialog(1)
            }
        }
        Timer { id: hideErrorPopupTimer; interval: 5000; onTriggered: root.hideNotice() }
    }

    ModalDialog {
        id: loginDialog
        objectName: "interactionLogin"
        property var request: null
        rootWindow: root.bgContent
        onOpened: { username.forceActiveFocus(); username.selectAll() }
        onAccepted: root.finishRequest(request, "login", {username: username.text, password: password.text, store: savePassword.checked})
        onRejected: root.finishRequest(request, "dismiss")
        onClosed: { if (root.activeRequest === request) root.finishRequest(request, "dismiss"); password.clear(); savePassword.checked = false; request = null; root.requestClosed() }

        TextEdit {
            objectName: "interactionLoginMessage"
            Layout.fillWidth: true
            visible: text.length > 0
            text: loginDialog.request ? loginDialog.request.text : ""
            textFormat: TextEdit.PlainText
            readOnly: true; selectByMouse: true; activeFocusOnTab: true
            wrapMode: TextEdit.Wrap
            color: loginDialog.theme.foreground
            selectionColor: loginDialog.theme.accent
            selectedTextColor: loginDialog.onAccent
            font.pixelSize: loginDialog.theme.dp(14)
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: loginDialog.theme.dp(8)
            Text {
                Layout.fillWidth: true
                text: qsTr("User name")
                color: loginDialog.theme.foreground
                font.pixelSize: loginDialog.theme.dp(14)
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
            ModalDialog.Field {
                id: username
                objectName: "interactionUsername"
                dialog: loginDialog
                Accessible.name: qsTr("User name")
                Keys.onReturnPressed: event => { if (!event.isAutoRepeat) password.forceActiveFocus() }
                Keys.onEnterPressed: event => { if (!event.isAutoRepeat) password.forceActiveFocus() }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: loginDialog.theme.dp(8)
            Text {
                Layout.fillWidth: true
                text: qsTr("Password")
                color: loginDialog.theme.foreground
                font.pixelSize: loginDialog.theme.dp(14)
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
            ModalDialog.Field {
                id: password
                objectName: "interactionPassword"
                dialog: loginDialog
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhHiddenText | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                Accessible.name: qsTr("Password")
                Keys.onReturnPressed: event => { if (!event.isAutoRepeat) loginDialog.accept() }
                Keys.onEnterPressed: event => { if (!event.isAutoRepeat) loginDialog.accept() }
            }
        }
        C.CheckBox {
            id: savePassword
            objectName: "interactionStore"
            Layout.fillWidth: true
            visible: !!loginDialog.request && loginDialog.request.askStore
            text: qsTr("Save password")
            spacing: loginDialog.theme.dp(8)
            padding: loginDialog.theme.dp(4)
            indicator: Rectangle {
                implicitWidth: loginDialog.theme.dp(16)
                implicitHeight: implicitWidth
                x: savePassword.leftPadding
                y: (savePassword.height - height) / 2
                color: savePassword.checked ? loginDialog.theme.accent : loginDialog.theme.field
                radius: loginDialog.theme.dp(3)
                border.width: loginDialog.theme.dp(1)
                border.color: savePassword.visualFocus ? loginDialog.theme.accent : loginDialog.theme.border
                Text {
                    anchors.centerIn: parent
                    visible: savePassword.checked
                    text: "✓"; color: loginDialog.onAccent
                    font.pixelSize: loginDialog.theme.dp(13)
                }
            }
            contentItem: Text {
                text: savePassword.text
                textFormat: Text.PlainText
                leftPadding: savePassword.indicator.width + savePassword.spacing
                color: loginDialog.theme.foreground
                font.pixelSize: loginDialog.theme.dp(14)
                wrapMode: Text.Wrap
            }
            onActiveFocusChanged: {
                if (!loginDialog) return
                if (activeFocus) { loginDialog.activeField = savePassword; Qt.callLater(loginDialog.revealActiveField) }
                else if (loginDialog.activeField === savePassword) loginDialog.activeField = null
            }
        }
        footer: Item {
            visible: true
            implicitHeight: visible ? actions1.implicitHeight + loginDialog.bottomPadding : 0
            GridLayout {
                id: actions1
                objectName: "interactionLoginActions"
                anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: loginDialog.leftPadding; rightMargin: loginDialog.rightPadding }
                columns: width < loginCancel.naturalWidth + loginOk.naturalWidth + columnSpacing ? 1 : 2
                columnSpacing: loginDialog.theme.dp(8); rowSpacing: columnSpacing
                ModalDialog.ActionButton {
                    id: loginCancel
                    objectName: "interactionLoginCancel"
                    dialog: loginDialog
                    caption: qsTr("Cancel")
                    onClicked: loginDialog.reject()
                }
                ModalDialog.ActionButton {
                    id: loginOk
                    objectName: "interactionLoginAccept"
                    dialog: loginDialog
                    caption: qsTr("Sign in")
                    primaryAction: true
                    onClicked: loginDialog.accept()
                }

            }
        }
    }

    ModalDialog {
        id: progressDialog
        objectName: "interactionProgress"
        property var request: null
        property string text: ""
        property real position: 0
        readonly property bool canCancel: !!request && request.cancel.length > 0
        rootWindow: root.bgContent
        closePolicy: canCancel ? C.Popup.CloseOnEscape : C.Popup.NoAutoClose
        onOpened: { if (canCancel) progressCancel.forceActiveFocus(); else bodyViewport.forceActiveFocus() }
        onRejected: if (canCancel) root.finishRequest(request, "dismiss")
        onClosed: { if (root.activeRequest === request) root.finishRequest(request, "dismiss"); request = null; root.requestClosed() }
        TextEdit {
            objectName: "interactionProgressMessage"
            visible: text.length > 0
            Layout.fillWidth: true
            text: progressDialog.text
            textFormat: TextEdit.PlainText
            readOnly: true; selectByMouse: true; activeFocusOnTab: true
            wrapMode: TextEdit.Wrap
            color: progressDialog.theme.foreground
            selectionColor: progressDialog.theme.accent
            selectedTextColor: progressDialog.onAccent
            font.pixelSize: progressDialog.theme.dp(14)
        }
        C.ProgressBar {
            id: progressBar
            objectName: "interactionProgressBar"
            Layout.fillWidth: true
            implicitHeight: progressDialog.theme.dp(8)
            padding: 0
            from: 0; to: 1
            value: progressDialog.position
            indeterminate: !!progressDialog.request && progressDialog.request.indeterminate
            background: Rectangle { color: progressDialog.theme.field; radius: progressDialog.theme.dp(4) }
            contentItem: Item {
                clip: true
                Rectangle {
                    id: progressChunk
                    width: progressBar.indeterminate ? parent.width / 3 : parent.width * progressBar.position
                    height: parent.height
                    x: progressBar.indeterminate ? -width : progressBar.mirrored ? parent.width - width : 0
                    color: progressDialog.theme.accent
                    radius: progressDialog.theme.dp(4)
                }
                NumberAnimation {
                    target: progressChunk; property: "x"
                    from: -progressChunk.width; to: progressBar.availableWidth
                    duration: 1200; loops: Animation.Infinite
                    running: progressBar.indeterminate && progressDialog.visible
                }
            }
        }
        Text {
            objectName: "interactionProgressPercent"
            Layout.fillWidth: true
            visible: !progressBar.indeterminate
            text: qsTr("%1%").arg(Math.round(progressDialog.position * 100))
            color: progressDialog.theme.foreground
            font.pixelSize: progressDialog.theme.dp(14)
            textFormat: Text.PlainText
        }
        footer: Item {
            visible: progressDialog.canCancel
            implicitHeight: visible ? actions2.implicitHeight + progressDialog.bottomPadding : 0
            GridLayout {
                id: actions2
                objectName: "interactionProgressActions"
                anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: progressDialog.leftPadding; rightMargin: progressDialog.rightPadding }
                columns: 1
                ModalDialog.ActionButton {
                    id: progressCancel
                    objectName: "interactionProgressCancel"
                    dialog: progressDialog
                    caption: progressDialog.request ? progressDialog.request.cancel : ""
                    onClicked: progressDialog.reject()
                }

            }
        }
    }

    ModalDialog {
        id: questionDialog
        objectName: "interactionQuestion"
        property var request: null
        rootWindow: root.bgContent
        onOpened: { if (cancel.visible) cancel.forceActiveFocus(); else if (action1.visible) action1.forceActiveFocus(); else action2.forceActiveFocus() }
        onRejected: root.finishRequest(request, "dismiss")
        onClosed: { if (root.activeRequest === request) root.finishRequest(request, "dismiss"); request = null; root.requestClosed() }
        Text {
            objectName: "interactionSeverity"
            Layout.fillWidth: true
            visible: !!questionDialog.request && questionDialog.request.severity !== VLCDialog.QUESTION_NORMAL
            text: questionDialog.request && questionDialog.request.severity === VLCDialog.QUESTION_CRITICAL ? qsTr("Error") : qsTr("Warning")
            color: questionDialog.request && questionDialog.request.severity === VLCDialog.QUESTION_CRITICAL ? questionDialog.theme.danger : questionDialog.theme.accent
            font.pixelSize: questionDialog.theme.dp(14)
            font.weight: Font.DemiBold
        }
        TextEdit {
            objectName: "interactionQuestionMessage"
            visible: text.length > 0
            Layout.fillWidth: true
            text: questionDialog.request ? questionDialog.request.text : ""
            textFormat: TextEdit.PlainText
            readOnly: true; selectByMouse: true; activeFocusOnTab: true
            wrapMode: TextEdit.Wrap
            color: questionDialog.theme.foreground
            selectionColor: questionDialog.theme.accent
            selectedTextColor: questionDialog.onAccent
            font.pixelSize: questionDialog.theme.dp(14)
        }
        footer: Item {
            visible: true
            implicitHeight: visible ? actions3.implicitHeight + questionDialog.bottomPadding : 0
            GridLayout {
                id: actions3
                objectName: "interactionQuestionActions"
                anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: questionDialog.leftPadding; rightMargin: questionDialog.rightPadding }
                columns: width < (cancel.visible ? cancel.naturalWidth : 0) + (action1.visible ? action1.naturalWidth : 0) + (action2.visible ? action2.naturalWidth : 0) + columnSpacing * Math.max(0, (cancel.visible ? 1 : 0) + (action1.visible ? 1 : 0) + (action2.visible ? 1 : 0) - 1) ? 1 : 3
                columnSpacing: questionDialog.theme.dp(8); rowSpacing: columnSpacing
                ModalDialog.ActionButton {
                    id: cancel
                    objectName: "interactionQuestionCancel"
                    dialog: questionDialog
                    caption: questionDialog.request ? questionDialog.request.cancel : ""
                    visible: caption.length > 0
                    onClicked: questionDialog.reject()
                }
                ModalDialog.ActionButton {
                    id: action1
                    objectName: "interactionAction1"
                    dialog: questionDialog
                    caption: questionDialog.request ? questionDialog.request.action1 : ""
                    visible: caption.length > 0
                    primaryAction: true
                    onClicked: root.finishRequest(questionDialog.request, "action1")
                }
                ModalDialog.ActionButton {
                    id: action2
                    objectName: "interactionAction2"
                    dialog: questionDialog
                    caption: questionDialog.request ? questionDialog.request.action2 : ""
                    visible: caption.length > 0
                    onClicked: root.finishRequest(questionDialog.request, "action2")
                }

            }
        }
    }
    CustomDialog {
        id: customDialog
        objectName: "interactionCustom"
        rootWindow: root.bgContent
        onClosed: root.requestClosed()
    }
}
