// SPDX-License-Identifier: GPL-2.0-or-later
import QtQml

// Shared action seam: native requests must not mutate the in-window layout.
QtObject {
    required property QtObject context
    required property QtObject presentation

    function trigger() {
        if (context.detachedPipAvailable)
            return context.requestDetachedPip(!context.detachedPipActive)
        presentation.libraryOpen = true
        presentation.libraryExpanded = true
        presentation.miniPlayerVisible = true
        return true
    }
}
