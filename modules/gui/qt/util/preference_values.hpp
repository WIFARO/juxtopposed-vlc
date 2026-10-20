// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_PREFERENCE_VALUES_HPP
#define VLC_QT_PREFERENCE_VALUES_HPP

#include <QVariant>
#include <cmath>
#include <optional>

namespace vlc::preferences
{
inline std::optional<float> floatValue(const QVariant &proposed, float minimum, float maximum)
{
    // QML uses doubles. Compare after conversion to VLC's native float storage,
    // so decimal endpoints such as 0.1 and 0.9 remain valid.
    bool valid = false;
    const float value = proposed.toFloat(&valid);
    if (!valid || !std::isfinite(value)
        || (minimum != maximum && (value < minimum || value > maximum)))
        return std::nullopt;
    return value;
}
}

#endif
