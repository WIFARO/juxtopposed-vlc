// SPDX-License-Identifier: GPL-2.0-or-later
import QtQml

QtObject {

    function parseTime(text) {
        const value = String(text).trim()
        if (!/^\d+(?::\d{1,2}){0,2}$/.test(value)) return -1
        const parts = value.split(":").map(Number)
        if (parts.length > 1 && parts[parts.length - 1] >= 60) return -1
        if (parts.length === 3 && parts[1] >= 60) return -1
        const seconds = parts.reduce((total, part) => total * 60 + part, 0)
        // Match the player's signed seconds API, without clock-style day wrap.
        return Number.isSafeInteger(seconds) && seconds <= 2147483647 ? seconds : -1
    }
    function formatTime(seconds) {
        seconds = Math.max(0, Math.floor(Number(seconds) || 0))
        const hours = Math.floor(seconds / 3600)
        const minutes = Math.floor(seconds / 60) % 60
        const rest = String(seconds % 60).padStart(2, "0")
        return hours > 0 ? hours + ":" + String(minutes).padStart(2, "0") + ":" + rest
            : String(minutes).padStart(2, "0") + ":" + rest
    }
}
