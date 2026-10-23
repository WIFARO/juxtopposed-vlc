// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QVariantMap>
#include <QTextDocument>
#include <vlc_input_item.h>
#include <vlc_cxx_helpers.hpp>
#include "vlctick.hpp"

inline QVariantMap podcastMetadata(input_item_t* item)
{
    const auto feed = vlc::wrap_cptr(input_item_GetMetaExtra(item, "podcast:feed"));
    if (!feed || !*feed) return {};
    QVariantMap data{{"feed", QString::fromUtf8(feed.get())},
        {"duration", QVariant::fromValue(VLCDuration(input_item_GetDuration(item)))}};
    const auto meta = [&](const char* name, vlc_meta_type_t type) {
        const auto value = vlc::wrap_cptr(input_item_GetMeta(item, type));
        data[name] = QString::fromUtf8(value ? value.get() : "");
    };
    meta("show", vlc_meta_ShowName); meta("author", vlc_meta_Artist);
    meta("season", vlc_meta_Season); meta("episode", vlc_meta_Episode);
    meta("date", vlc_meta_Date); meta("description", vlc_meta_Description);
    QTextDocument notes;
    const auto showNotes = vlc::wrap_cptr(input_item_GetMetaExtra(item, "content:encoded"));
    notes.setHtml(showNotes && *showNotes ? QString::fromUtf8(showNotes.get()) : data.value("description").toString());
    data["description"] = notes.toPlainText();
    for (const auto name : {"guid", "episodeType", "explicit", "video", "transcript", "chapters"}) {
        const QByteArray key = QByteArray("podcast:") + name;
        const auto value = vlc::wrap_cptr(input_item_GetMetaExtra(item, key.constData()));
        data[name] = QString::fromUtf8(value ? value.get() : "");
    }
    return data;
}
