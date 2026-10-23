// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "media_identity.hpp"
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <atomic>
#include <algorithm>

namespace TrackMatching {
inline QString normalize(const QString& name)
{
    static const QRegularExpression separators(QStringLiteral("[^\\p{L}\\p{N}]+"));
    return name.normalized(QString::NormalizationForm_KC).toCaseFolded()
        .replace(separators, QStringLiteral(" ")).trimmed();
}

inline QString stem(const QUrl& url)
{
    return normalize(QFileInfo(url.fileName()).completeBaseName());
}

inline const QSet<QString>& labels()
{
    static const QSet<QString> labels = [] {
        const auto words = QStringLiteral("en eng english es spa spanish ja jpn japanese de deu ger german "
            "nl nld dut dutch fr fra fre french ar ara arabic pt por portuguese br brazilian "
            "hi hin hindi ko kor korean zh zho chi chinese ru rus russian pl pol polish "
            "it ita italian uk ukr ukrainian sv swe swedish no nor norwegian da dan danish "
            "fi fin finnish tr tur turkish cs ces cze czech ro ron rum romanian hu hun hungarian "
            "forced sdh cc commentary default full signs songs subtitles subs audio stereo surround "
            "0 1 2 5 7 ac3 eac3 dts flac opus aac mp3 wav atmos truehd").split(QLatin1Char(' '));
        return QSet<QString>(words.cbegin(), words.cend());
    }();
    return labels;
}

struct Name {
    QUrl url;
    QString normalized;
    QString compact;
    MediaIdentity::Identity identity;
    QStringList words;
};

inline Name parse(const QUrl& url, bool track = false)
{
    auto name = stem(url);
    if (track && !name.isEmpty()) {
        const auto words = name.split(QLatin1Char(' '));
        const bool generic = std::all_of(words.cbegin(), words.cend(), [](const QString& word) {
            return labels().contains(word);
        });
        if (generic && std::any_of(name.cbegin(), name.cend(), [](QChar c) { return c.isLetter(); })) {
            const auto parent = url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash).fileName();
            name = normalize(parent) + QLatin1Char(' ') + name;
        }
    }
    auto identity = MediaIdentity::parse(url.fileName());
    if (track) identity = MediaIdentity::parse(name);
    auto compact = name;
    compact.remove(QLatin1Char(' '));
    return {url, name, compact, identity, name.split(QLatin1Char(' '))};
}

inline int score(const Name& media, const Name& track)
{
    const auto& target = media.normalized;
    const auto& source = track.normalized;
    if (target.isEmpty() || source.isEmpty()) return 0;
    if (media.compact == track.compact) return 100;
    const auto& a = media.identity;
    const auto& b = track.identity;
    const auto comparison = MediaIdentity::compare(a, b);
    if (comparison.conflict) return 0;
    if (comparison.strong) return 90;
    if (!a.episodes.isEmpty() && !b.episodes.isEmpty()) {
        if (a.episodes != b.episodes || a.season != b.season) return 0;
        return a.title.isEmpty() || b.title.isEmpty() ? 50 : 0;
    }
    const auto& words = track.words;
    QString prefix;
    int titleWords = 0;
    for (const auto& word : words) {
        prefix += word; ++titleWords;
        if (prefix == media.compact) break;
        if (prefix.size() >= media.compact.size()) return 0;
    }
    if (prefix != media.compact || titleWords == words.size()) return 0;
    // Only treat known track labels as suffixes. A short film title must not
    // silently match a different film whose name starts with the same words.
    const auto suffix = words.mid(titleWords);
    for (const auto& word : suffix) if (!labels().contains(word)) return 0;
    return 80;
}

inline QMap<QUrl, QList<QUrl>> match(const QList<QUrl>& media, const QList<QUrl>& tracks,
                                    const std::atomic_bool* cancelled = nullptr)
{
    QMap<QUrl, QList<QUrl>> result;
    QList<Name> names;
    QSet<QUrl> mediaSeen;
    for (const auto& file : media) {
        if (cancelled && cancelled->load(std::memory_order_relaxed)) return {};
        if (!mediaSeen.contains(file)) { mediaSeen.insert(file); names.append(parse(file)); }
    }
    QSet<QUrl> seen;
    for (const auto& track : tracks) {
        if (cancelled && cancelled->load(std::memory_order_relaxed)) return {};
        if (seen.contains(track)) continue;
        seen.insert(track);
        const auto source = parse(track);
        const auto hint = parse(track, true);
        int best = 0;
        QSet<QUrl> targets;
        for (const auto& file : names) {
            if (cancelled && cancelled->load(std::memory_order_relaxed)) return {};
            if (file.url == track) continue;
            const int value = std::max(score(file, source), score(file, hint));
            if (value > best) { best = value; targets.clear(); }
            if (value > 0 && value == best) targets.insert(file.url);
        }
        // A duplicate playlist entry is one media. Distinct locations with
        // the same name are ambiguous and require a manual choice.
        if (targets.size() == 1) result[*targets.cbegin()].append(track);
    }
    return result;
}
}
