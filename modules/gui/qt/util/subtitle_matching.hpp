// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "media_identity.hpp"
#include <QCoreApplication>
#include <cmath>
#include <atomic>

namespace SubtitleMatching {
inline QString tr(const char* text) { return QCoreApplication::translate("SubtitleMatching", text); }

inline MediaIdentity::Identity identity(const QVariantMap& source)
{
    auto release = source.value("release").toString();
    if (release.isEmpty()) {
        auto name = source.value("filename", source.value("name")).toString(); name.replace('\\', '/');
        release = QFileInfo(name).fileName();
    }
    auto result = MediaIdentity::parse(release);
    const auto feature = source.value("feature").toMap();
    QVariantMap metadata;
    const bool episode = feature.value("feature_type").toString().compare("Episode", Qt::CaseInsensitive) == 0;
    const bool movie = feature.value("feature_type").toString().compare("Movie", Qt::CaseInsensitive) == 0;
    if (episode) {
        metadata.insert("type", "episode");
        const auto title = feature.value("parent_title").toString();
        if (!title.isEmpty()) metadata.insert("showName", title);
        if (feature.contains("season_number")) metadata.insert("season", feature.value("season_number"));
        if (feature.contains("episode_number")) metadata.insert("episode", feature.value("episode_number"));
        metadata.insert("seriesImdbId", feature.value("parent_imdb_id"));
        metadata.insert("seriesTmdbId", feature.value("parent_tmdb_id"));
    } else if (movie) {
        metadata.insert("type", "movie"); metadata.insert("title", feature.value("title"));
        if (feature.contains("year")) metadata.insert("year", feature.value("year"));
    }
    if (episode || movie) {
        metadata.insert("imdbId", feature.value("imdb_id")); metadata.insert("tmdbId", feature.value("tmdb_id"));
    }
    MediaIdentity::applyMetadata(result, metadata, "provider");
    return result;
}

inline QVariantList rank(const MediaIdentity::Identity& target, const QVariantList& sources,
                         const QString& mediaUrl = {}, const QString& language = {}, const QVariantMap& preferences = {},
                         const std::atomic_bool* cancelled = nullptr)
{
    QVariantList ranked;
    for (const auto& value : sources) {
        if (cancelled && cancelled->load(std::memory_order_relaxed)) return {};
        auto source = value.toMap();
        const auto candidate = source.contains("identity") ? MediaIdentity::fromMap(source.value("identity").toMap()) : identity(source);
        const auto comparison = MediaIdentity::compare(target, candidate);
        const bool hash = !mediaUrl.isEmpty() && source.value("hashFor").toStringList().contains(mediaUrl);
        bool conflict = comparison.conflict || (!language.isEmpty() && source.value("language").toString() != language);
        int score = hash ? 500 : comparison.score;
        bool automatic = hash || comparison.strong;
        int releaseMatches = 0;
        for (const auto& key : {QStringLiteral("edition"), QStringLiteral("group"), QStringLiteral("source"),
                               QStringLiteral("fps"), QStringLiteral("resolution"), QStringLiteral("videoCodec"), QStringLiteral("audioCodec")}) {
            const auto a = target.release.value(key), b = candidate.release.value(key);
            if (a.isEmpty() || b.isEmpty()) continue;
            const bool equal = key == "fps" ? std::abs(a.toDouble() - b.toDouble()) < .02
                                            : MediaIdentity::compact(a) == MediaIdentity::compact(b);
            if (equal) { score += key == "group" ? 25 : key == "source" ? 15 : key == "fps" ? 20 : 2; ++releaseMatches; }
            else if (key == "edition" || key == "fps") { automatic = false; score -= 40; }
        }
        // Accessibility flags are preferences, not proof of media identity.
        for (const auto& flag : {QStringLiteral("forced"), QStringLiteral("hearingImpaired")}) {
            if (preferences.contains(flag)) {
                if (source.value(flag).toBool() == preferences.value(flag).toBool()) score += 5;
                else { automatic = false; score -= 20; }
            } else if (flag == "forced" && source.value(flag).toBool()) { automatic = false; score -= 10; }
        }
        // Quality is only a tie-breaker. It cannot create the margin required for automatic selection.
        if (source.value("trusted").toBool()) score += 3;
        if (source.value("votes").toInt() >= 5 && source.value("rating").toDouble() >= 8) score += 2;
        QString reason;
        if (conflict) { score = -1; automatic = false; reason = tr("Conflicting media details"); }
        else if (hash) reason = tr("Exact file match");
        else if (comparison.strong) reason = releaseMatches ? tr("Media and release details match") : tr("Media matches; release uncertain");
        else if (comparison.score > 0) reason = tr("Possible title match");
        else reason = tr("Match not confirmed");
        source.insert("matchScore", score); source.insert("matchReason", reason);
        source.insert("automaticMatch", automatic && !conflict && (hash || comparison.strong));
        source.insert("identity", MediaIdentity::toMap(candidate));
        ranked.append(source);
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value("matchScore").toInt() > b.toMap().value("matchScore").toInt();
    });
    return ranked;
}

inline QVariantMap match(const QVariantList& media, const QVariantList& sources, const QString& language,
                         const std::atomic_bool* cancelled = nullptr)
{
    QVariantMap result;
    QMap<QString, QStringList> assignments;
    QSet<QString> seen;
    for (const auto& value : media) {
        if (cancelled && cancelled->load(std::memory_order_relaxed)) return {};
        const auto row = value.toMap(); const auto url = row.value("url").toString();
        if (seen.contains(url)) continue;
        seen.insert(url);
        const auto target = row.contains("identity") ? MediaIdentity::fromMap(row.value("identity").toMap())
            : MediaIdentity::identify(QUrl(url), row.value("metadata").toMap());
        const auto ranked = rank(target, sources, url, language, {}, cancelled);
        if (cancelled && cancelled->load(std::memory_order_relaxed)) return {};
        QMap<QString, QVariantList> groups;
        for (const auto& candidate : ranked) groups[candidate.toMap().value("language").toString()].append(candidate);
        for (const auto& group : groups) {
            const auto best = group.first().toMap();
            if (!best.value("automaticMatch").toBool()) continue;
            if (group.size() > 1 && best.value("matchScore").toInt() - group[1].toMap().value("matchScore").toInt() < 10) continue;
            assignments[best.value("url").toString()].append(url);
        }
    }
    // A subtitle must have one unambiguous target, even across duplicate titles at different locations.
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
        if (it.value().size() != 1) continue;
        auto tracks = result.value(it.value().first()).toStringList(); tracks.append(it.key());
        result.insert(it.value().first(), tracks);
    }
    return result;
}
}
