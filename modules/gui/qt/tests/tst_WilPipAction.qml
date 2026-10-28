// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    name: "WilPipAction"
    QtObject {
        id: backend
        property bool detachedPipAvailable: false
        property bool detachedPipActive: false
        property bool acceptRequest: true
        property int requests: 0
        property var lastRequested: undefined
        function requestDetachedPip(enabled) {
            requests++
            lastRequested = enabled
            return acceptRequest
        }
    }
    QtObject {
        id: presentation
        property bool libraryOpen: false
        property bool libraryExpanded: false
        property bool miniPlayerVisible: false
    }
    RedesignPipAction { id: action; context: backend; presentation: presentation }
    function init() {
        backend.detachedPipAvailable = false
        backend.detachedPipActive = false
        backend.acceptRequest = true
        backend.requests = 0
        backend.lastRequested = undefined
        presentation.libraryOpen = false
        presentation.libraryExpanded = false
        presentation.miniPlayerVisible = false
    }
    function test_unsupportedKeepsOriginalInWindowCard() {
        compare(action.trigger(), true)
        compare(backend.requests, 0)
        compare(presentation.libraryOpen, true)
        compare(presentation.libraryExpanded, true)
        compare(presentation.miniPlayerVisible, true)
    }
    function test_availableRequestsIndependentWindowWithoutNavigating() {
        backend.detachedPipAvailable = true
        compare(action.trigger(), true)
        compare(backend.requests, 1)
        compare(backend.lastRequested, true)
        compare(presentation.libraryOpen, false)
        compare(presentation.libraryExpanded, false)
        compare(presentation.miniPlayerVisible, false)
    }
    function test_activeRequestsReturnToPlayer() {
        backend.detachedPipAvailable = true
        backend.detachedPipActive = true
        compare(action.trigger(), true)
        compare(backend.requests, 1)
        compare(backend.lastRequested, false)
        compare(presentation.libraryOpen, false)
    }
    function test_rejectedNativeRequestDoesNotPretendToDetach() {
        backend.detachedPipAvailable = true
        backend.acceptRequest = false
        compare(action.trigger(), false)
        compare(backend.requests, 1)
        compare(presentation.libraryOpen, false)
        compare(presentation.libraryExpanded, false)
        compare(presentation.miniPlayerVisible, false)
    }
}
