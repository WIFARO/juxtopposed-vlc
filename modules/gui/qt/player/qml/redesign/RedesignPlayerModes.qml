// SPDX-License-Identifier: GPL-2.0-or-later
import QtQml

// The native context uses one notify signal for both interface mode flags.
// Keep mode navigation separate from the heavyweight native player integration.
Connections {
    required property QtObject context
    required property QtObject presentation
    required property bool started
    target: context
    property var _previousPlayerView: undefined
    Component.onCompleted: _previousPlayerView = context.playerView

    function onMainInterfaceModesChanged() {
        const playerView = context.playerView
        const changed = _previousPlayerView !== undefined && playerView !== _previousPlayerView
        _previousPlayerView = playerView
        // A minimal-view toggle emits this same signal; it is not navigation.
        if (changed && !context.minimalView && !playerView && started) {
            presentation.libraryOpen = true
            presentation.libraryExpanded = true
        }
    }
}
