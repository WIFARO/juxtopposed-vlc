// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "artwork_discovery.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <QUrl>

namespace ArtworkStore {
inline QString directory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("artwork-overrides"));
}
inline QString key(const QUrl& url)
{
    auto normalized = ArtworkDiscovery::normalizeUrl(url).toEncoded();
#ifdef Q_OS_WIN
    if (url.isLocalFile()) normalized = normalized.toLower();
#endif
    return QString::fromLatin1(QCryptographicHash::hash(normalized, QCryptographicHash::Sha256).toHex());
}
inline QImage read(const QString& path, int maximum = 1024)
{
    QImageReader reader(path); reader.setAutoTransform(true);
    if (QFileInfo(path).suffix().compare("tbn", Qt::CaseInsensitive) == 0) reader.setDecideFormatFromContent(true);
    const auto size = reader.size();
    if (!size.isValid() || size.width() > 32768 || size.height() > 32768
        || qint64(size.width()) * size.height() > 100000000) return {};
    if (size.width() > maximum || size.height() > maximum)
        reader.setScaledSize(size.scaled(QSize(maximum, maximum), Qt::KeepAspectRatio));
    return reader.read();
}
inline QString selected(const QUrl& url)
{
    QSettings settings(QDir(directory()).filePath(QStringLiteral("covers.ini")), QSettings::IniFormat);
    const auto name = settings.value(key(url)).toString();
    if (name.isEmpty() || name != QFileInfo(name).fileName()) return {};
    const auto path = QDir(directory()).filePath(name);
    return QFileInfo(path).isReadable() ? QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded) : QString();
}
// Empty source resets to automatic discovery. Copies survive moving/deleting the source image.
inline bool save(const QUrl& url, const QUrl& source)
{
    const auto folder = directory();
    if (!url.isValid() || !QDir().mkpath(folder)) return false;
    QLockFile lock(QDir(folder).filePath(QStringLiteral(".writer.lock")));
    if (!lock.tryLock(3000)) return false;
    QString name;
    if (!source.isEmpty()) {
        if (!source.isLocalFile()) return false;
        const auto image = read(source.toLocalFile());
        if (image.isNull()) return false;
        name = QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".png");
        QSaveFile file(QDir(folder).filePath(name));
        if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) return false;
    }
    QSettings settings(QDir(folder).filePath(QStringLiteral("covers.ini")), QSettings::IniFormat);
    const auto previous = settings.value(key(url)).toString();
    if (name.isEmpty()) settings.remove(key(url)); else settings.setValue(key(url), name);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (!name.isEmpty()) QFile::remove(QDir(folder).filePath(name));
        return false;
    }
    if (!previous.isEmpty() && previous == QFileInfo(previous).fileName()) QFile::remove(QDir(folder).filePath(previous));
    return true;
}
inline QImage collage(const QList<QImage>& images, QSize size = QSize(512, 320))
{
    if (images.isEmpty()) return {};
    QImage result(size, QImage::Format_ARGB32_Premultiplied); result.fill(Qt::transparent);
    QPainter painter(&result); painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const int count = std::min(4, int(images.size()));
    for (int i = 0; i < count; ++i) {
        QRect cell;
        if (count == 1) cell = result.rect();
        else if (count == 2) cell = QRect(i * size.width() / 2, 0, size.width() / 2, size.height());
        else if (count == 3 && i == 0) cell = QRect(0, 0, size.width() / 2, size.height());
        else if (count == 3) cell = QRect(size.width() / 2, (i - 1) * size.height() / 2, size.width() / 2, size.height() / 2);
        else cell = QRect((i % 2) * size.width() / 2, (i / 2) * size.height() / 2, size.width() / 2, size.height() / 2);
        const auto scaled = images[i].scaled(cell.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        painter.drawImage(cell, scaled, QRect((scaled.width() - cell.width()) / 2, (scaled.height() - cell.height()) / 2, cell.width(), cell.height()));
    }
    return result;
}
} // namespace ArtworkStore
