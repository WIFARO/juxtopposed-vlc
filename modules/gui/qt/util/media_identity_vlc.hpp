// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "media_identity.hpp"
#include <vlc_common.h>
#include <vlc_input_item.h>

namespace MediaIdentity {
// Read existing tags only. Identification never starts playback or network preparsing.
inline QVariantMap inputMetadata(input_item_t* item)
{
    QVariantMap metadata;
    if (!item) return metadata;
    const QMap<QString, vlc_meta_type_t> fields{{"title", vlc_meta_Title}, {"showName", vlc_meta_ShowName},
        {"season", vlc_meta_Season}, {"episode", vlc_meta_Episode}};
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        char* value = input_item_GetMeta(item, it.value());
        if (value && *value) metadata.insert(it.key(), QString::fromUtf8(value));
        free(value);
    }
    // An embedded movie date is useful; a TV episode date is not its series start year.
    if (!metadata.contains("showName") && !metadata.contains("episode")) {
        char* date = input_item_GetMeta(item, vlc_meta_Date);
        const auto match = QRegularExpression(QStringLiteral("^((?:18[89]|19|20)\\d{2})(?:$|-)"))
            .match(date ? QString::fromUtf8(date) : QString());
        if (match.hasMatch()) metadata.insert("year", match.captured(1).toInt());
        free(date);
    }
    char* uri = input_item_GetURI(item);
    const auto filename = uri ? QUrl(QString::fromUtf8(uri)).fileName() : QString(); free(uri);
    const auto title = metadata.value("title").toString();
    if (title == filename || title == QFileInfo(filename).completeBaseName()) metadata.remove("title");
    const auto duration = input_item_GetDuration(item);
    if (duration != VLC_TICK_INVALID) metadata.insert("durationMs", qint64(MS_FROM_VLC_TICK(duration)));
    return metadata;
}
}
