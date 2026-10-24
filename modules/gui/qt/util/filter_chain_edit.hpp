// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_FILTER_CHAIN_EDIT_HPP
#define VLC_QT_FILTER_CHAIN_EDIT_HPP
#include <vlc_common.h>
#include <vlc_configuration.h>
#include <QString>
#include <QStringList>
#include <utility>

namespace FilterChainEdit {
// Preserve native option syntax, including quoted colons, when changing a filter.
inline std::pair<QString, bool> withFilter(const QString& chain, const QString& filter, bool enable)
{
    QStringList parts;
    bool present = false;
    const auto bytes = chain.toUtf8();
    const auto target = filter.toUtf8();
    char* cursor = strdup(bytes.constData());
    if (!cursor) return {chain, false};
    while (cursor && *cursor) {
        char* name = nullptr; config_chain_t* config = nullptr;
        char* next = config_ChainCreate(&name, &config, cursor);
        const auto raw = QByteArray(cursor);
        const auto length = next ? raw.size() - qsizetype(strlen(next)) - 1 : raw.size();
        const bool duration = name && !strcmp(name, target.constData());
        present |= duration;
        if (!duration || enable) parts.append(QString::fromUtf8(raw.left(length)));
        free(name); config_ChainDestroy(config); free(cursor); cursor = next;
    }
    free(cursor);
    if (enable && !present) parts.append(filter);
    return {parts.join(QLatin1Char(':')), present};
}
}
#endif
