// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QtEndian>
#include <functional>

namespace MediaFingerprint {
struct Fingerprint { QString hash; qint64 size = 0; };

// OpenSubtitles' release fingerprint, not a cryptographic content identifier.
// Read only the head/tail of a regular local file; never seek a playing stream.
inline Fingerprint openSubtitles(const QUrl& url, const std::function<bool()>& cancelled = {})
{
    constexpr qint64 blockSize = 65536;
    if (!url.isLocalFile() || (cancelled && cancelled())) return {};
    const QFileInfo before(url.toLocalFile());
    if (!before.isFile() || before.size() < blockSize * 2) return {};
    QFile file(before.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly) || file.size() != before.size()) return {};
    quint64 hash = quint64(before.size());
    for (const auto position : {qint64(0), before.size() - blockSize}) {
        if ((cancelled && cancelled()) || !file.seek(position)) return {};
        const auto bytes = file.read(blockSize);
        if (bytes.size() != blockSize) return {};
        for (qsizetype offset = 0; offset < bytes.size(); offset += 8)
            hash += qFromLittleEndian<quint64>(bytes.constData() + offset);
    }
    const QFileInfo after(file.fileName());
    if (before.size() != after.size() || before.lastModified() != after.lastModified()
        || (cancelled && cancelled())) return {};
    return {QStringLiteral("%1").arg(hash, 16, 16, QLatin1Char('0')), before.size()};
}
}
