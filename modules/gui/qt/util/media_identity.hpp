// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Provider-independent media evidence. QtCore only: no player, UI or network state.
#include <QDate>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>
#include <numeric>

namespace MediaIdentity {
enum class Kind { Unknown, Movie, Episode };

struct Identity {
    Kind kind = Kind::Unknown;
    QString title;
    QStringList aliases;
    int year = -1; // Movie release year or series start year, never episode air year.
    int season = -1;
    QList<int> episodes;
    QString absoluteEpisode; // Kept separate; may include fractional anime episodes.
    QDate airDate;
    QMap<QString, QString> ids; // imdb, seriesImdb, tmdbMovie, tmdbEpisode, seriesTmdb, tvdbEpisode
    QMap<QString, QString> release;
    QMap<QString, QString> provenance;
    QStringList conflicts;
};

inline QString normalize(QString value)
{
    static const QRegularExpression separators(QStringLiteral("[^\\p{L}\\p{N}]+"));
    return value.normalized(QString::NormalizationForm_KC).toCaseFolded()
        .replace(separators, QStringLiteral(" ")).simplified();
}

inline QString compact(QString value) { return normalize(value).remove(QLatin1Char(' ')); }

inline QString displayTitle(QString value)
{
    value.replace(QRegularExpression(QStringLiteral("[._]+")), QStringLiteral(" "));
    value.remove(QRegularExpression(QStringLiteral("^[\\s\\-]+|[\\s\\-\\[(]+$")));
    return value.simplified();
}

inline bool genericTitle(const QString& title)
{
    static const QRegularExpression generic(QStringLiteral(
        "^(?:video|movie|film|episode|feature|sample|untitled|[0-9]+|[0-9a-f]{24,})$"),
        QRegularExpression::CaseInsensitiveOption);
    // A plausible year can itself be a film title (1917, 1984).
    const int number = title.toInt();
    if (number >= 1880 && number <= 2099) return false;
    return title.isEmpty() || generic.match(title).hasMatch();
}

inline Identity parse(QString name)
{
    Identity result;
    name = name.left(1024).normalized(QString::NormalizationForm_C).trimmed();
    // Release names need not have extensions. Do not remove arbitrary final dotted words.
    const auto originalName = name;
    name.remove(QRegularExpression(QStringLiteral(
        "\\.(?:mkv|mp4|m4v|avi|mov|wmv|webm|mpg|mpeg|ts|m2ts|srt|ass|ssa|vtt|sub|idx|smi)$"),
        QRegularExpression::CaseInsensitiveOption));
    const auto insensitive = QRegularExpression::CaseInsensitiveOption;
    qsizetype end = name.size();
    const auto cut = [&end](qsizetype position) { if (position >= 0) end = std::min(end, position); };

    static const QRegularExpression episode(QStringLiteral(
        "(?:^|[ ._\\[(-])(?:s(?:eason)?[ ._-]*(\\d{1,3})[ ._-]*e(?:pisode)?[ ._-]*(\\d{1,4})"
        "|(\\d{1,3})x(\\d{1,4}))((?:(?:[ ._]*e|x|[ ._]*-[ ._]*e?)[ ._]*\\d{1,4})*)(?![\\da-z])"), insensitive);
    const auto ep = episode.match(name);
    if (ep.hasMatch()) {
        result.kind = Kind::Episode;
        result.season = ep.captured(ep.captured(1).isEmpty() ? 3 : 1).toInt();
        const int first = ep.captured(ep.captured(1).isEmpty() ? 4 : 2).toInt();
        result.episodes.append(first);
        static const QRegularExpression extra(QStringLiteral("([ex-])[ ._]*e?[ ._]*(\\d{1,4})"), insensitive);
        auto parts = extra.globalMatch(ep.captured(5));
        while (parts.hasNext()) {
            const auto part = parts.next();
            const int last = part.captured(2).toInt();
            if (part.captured(1) == "-" && last > result.episodes.last() && last - result.episodes.last() <= 100) {
                for (int value = result.episodes.last() + 1; value <= last; ++value) result.episodes.append(value);
            } else if (!result.episodes.contains(last)) result.episodes.append(last);
        }
        std::sort(result.episodes.begin(), result.episodes.end());
        cut(ep.capturedStart());
    } else {
        static const QRegularExpression date(QStringLiteral(
            "(?:^|[ ._-])((?:19|20)\\d{2})[ ._-](0[1-9]|1[0-2])[ ._-]([0-2]\\d|3[01])(?:$|[ ._-])"));
        const auto dated = date.match(name);
        if (dated.hasMatch()) {
            result.airDate = QDate(dated.captured(1).toInt(), dated.captured(2).toInt(), dated.captured(3).toInt());
            if (result.airDate.isValid()) { result.kind = Kind::Episode; cut(dated.capturedStart()); }
        }
        if (!result.airDate.isValid()) {
            static const QRegularExpression absolute(QStringLiteral(
                "(?:[ ._-]+episode[ ._-]+|[ ]+-[ ]+(?:ep[ ._-]*)?)(\\d{1,4}(?:\\.\\d{1,2})?)(?:v\\d+)?(?=$|[ ._\\[(-])"), insensitive);
            const auto numbered = absolute.match(name);
            if (numbered.hasMatch()) {
                result.kind = Kind::Episode;
                result.absoluteEpisode = numbered.captured(1);
                cut(numbered.capturedStart());
                // A leading fansub group is evidence only in an anime-style release.
                static const QRegularExpression group(QStringLiteral("^\\[([^\\]]+)\\][ _.-]*"));
                const auto leading = group.match(name);
                if (leading.hasMatch()) {
                    result.release.insert("group", leading.captured(1));
                    name.remove(0, leading.capturedLength());
                    end -= leading.capturedLength();
                }
            } else {
                static const QRegularExpression season(QStringLiteral(
                    "(?:^|[ ._(-])s(?:eason)?[ ._-]*(\\d{1,3})(?=$|[ ._\\[)-])"), insensitive);
                const auto pack = season.match(name);
                if (pack.hasMatch()) { result.kind = Kind::Episode; result.season = pack.captured(1).toInt(); cut(pack.capturedStart()); }
            }
        }
    }

    const QList<QPair<QString, QString>> releasePatterns = {
        {"resolution", "(?:480|576|720|1080|1440|2160|4320)[pi]|[48]k"},
        {"source", "web[ .-]?(?:dl|rip)|blu[ .-]?ray|b[dr]rip|hdtv|dvdrip|dvd"},
        {"videoCodec", "[xh][ .-]?26[45]|hevc|avc|av1|xvid|divx"},
        {"audioCodec", "e?[ .-]?ac[ .-]?3|dts(?:[ .-]?hd)?|aac|flac|truehd|opus"},
        {"fps", "(?:23[.]976|24|25|29[.]97|30|50|59[.]94|60)[ .-]?fps"},
    };
    bool technical = false;
    for (const auto& rule : releasePatterns) {
        const QRegularExpression pattern("(?:^|[ ._\\[(-])(" + rule.second + ")(?=$|[ ._\\[)\\]-])", insensitive);
        const auto match = pattern.match(name);
        if (match.hasMatch()) {
            result.release.insert(rule.first, rule.first == "fps"
                ? match.captured(1).remove(QRegularExpression("[ .-]*fps$", insensitive)) : compact(match.captured(1)));
            cut(match.capturedStart()); technical = true;
        }
    }
    static const QRegularExpression year(QStringLiteral("(?:^|[ ._\\[(-])((?:18[89]|19|20)\\d{2})(?=$|[ ._\\[)\\]-])"));
    auto years = year.globalMatch(name.left(end));
    qsizetype yearStart = -1;
    while (years.hasNext()) {
        const auto match = years.next();
        // The number in '1917' or '2001 A Space Odyssey' is part of the title.
        if (displayTitle(name.left(match.capturedStart())).isEmpty()) continue;
        result.year = match.captured(1).toInt(); yearStart = match.capturedStart();
    }
    if (yearStart >= 0) {
        cut(yearStart);
        if (result.kind == Kind::Unknown) result.kind = Kind::Movie;
    }
    if (technical || yearStart >= 0) {
        static const QRegularExpression edition(QStringLiteral(
            "(?:^|[ ._(-])((?:directors?|extended|theatrical|unrated|remastered)(?:[ ._-]+cut)?)(?=$|[ ._)\\]-])"), insensitive);
        const auto match = edition.match(name);
        if (match.hasMatch()) { result.release.insert("edition", normalize(match.captured(1))); cut(match.capturedStart()); }
    }
    if (technical && !result.release.contains("group")) {
        static const QRegularExpression group(QStringLiteral("-([\\p{L}\\p{N}]+)(?:\\[[^\\]]+\\])?$"));
        const auto match = group.match(name);
        if (match.hasMatch()) result.release.insert("group", match.captured(1).toCaseFolded());
    }
    static const QRegularExpression identifier(QStringLiteral(
        "[\\[{](?:imdb(?:id)?[ ._:=/-]*)?(tt\\d{5,12})[\\]}]"), insensitive);
    const auto id = identifier.match(name);
    if (id.hasMatch()) { result.ids.insert("imdb", id.captured(1).toLower()); cut(id.capturedStart()); }
    static const QRegularExpression tmdb(QStringLiteral("[\\[{]tmdb(?:id)?[ ._:=/-]*(\\d+)[\\]}]"), insensitive);
    const auto tmdbId = tmdb.match(name);
    if (tmdbId.hasMatch()) {
        if (result.kind != Kind::Unknown) result.ids.insert(result.kind == Kind::Episode ? "seriesTmdb" : "tmdbMovie", tmdbId.captured(1));
        cut(tmdbId.capturedStart());
    }
    result.title = displayTitle(name.left(std::max(qsizetype(0), end)));
    if (name != originalName && genericTitle(result.title)) result.title.clear();
    if (!result.title.isEmpty()) result.provenance.insert("title", "filename");
    if (result.year >= 0) result.provenance.insert("year", "filename");
    if (result.kind == Kind::Episode) result.provenance.insert("episode", "filename");
    return result;
}

inline void applyMetadata(Identity& identity, const QVariantMap& metadata, const QString& source = QStringLiteral("metadata"))
{
    const auto text = [&](const char* key) { return metadata.value(QLatin1String(key)).toString().trimmed(); };
    auto title = text("showName");
    if (title.isEmpty()) title = text("title");
    if (!title.isEmpty() && (source == "user" || !genericTitle(title))) {
        if (!identity.title.isEmpty() && compact(identity.title) != compact(title)) {
            identity.conflicts.append(QStringLiteral("title"));
        }
        identity.title = title.left(512); identity.provenance.insert("title", source);
    }
    const auto type = text("type");
    if (type == "movie") identity.kind = Kind::Movie;
    if (type == "episode" || !text("showName").isEmpty()) identity.kind = Kind::Episode;
    for (const auto& key : {QStringLiteral("year"), QStringLiteral("season")}) {
        if (!metadata.contains(key)) continue;
        bool ok = false; const int number = metadata.value(key).toInt(&ok);
        if (!ok || number < (key == "year" ? 1880 : 0) || number > (key == "year" ? 2099 : 999)) continue;
        int& field = key == "year" ? identity.year : identity.season;
        if (field >= 0 && field != number) identity.conflicts.append(key);
        field = number; identity.provenance.insert(key, source);
    }
    if (metadata.contains("episodes") || metadata.contains("episode")) {
        QList<int> episodes;
        const auto values = metadata.contains("episodes") ? metadata.value("episodes").toList() : QVariantList{metadata.value("episode")};
        for (const auto& value : values) {
            bool ok = false; const int episode = value.toInt(&ok);
            if (ok && episode >= 0 && episode <= 9999 && !episodes.contains(episode)) episodes.append(episode);
        }
        std::sort(episodes.begin(), episodes.end());
        if (!episodes.isEmpty()) {
            if (!identity.episodes.isEmpty() && identity.episodes != episodes) identity.conflicts.append("episode");
            identity.episodes = episodes; identity.kind = Kind::Episode; identity.provenance.insert("episode", source);
        }
    }
    const QMap<QString, QString> idKeys{{"imdbId", "imdb"}, {"seriesImdbId", "seriesImdb"},
        {"tmdbId", identity.kind == Kind::Episode ? "tmdbEpisode" : "tmdbMovie"},
        {"seriesTmdbId", "seriesTmdb"}, {"tvdbId", "tvdbEpisode"}};
    for (auto it = idKeys.cbegin(); it != idKeys.cend(); ++it) {
        auto value = metadata.value(it.key()).toString();
        if (value.isEmpty()) continue;
        if (it.value().contains("imdb", Qt::CaseInsensitive)) value.remove(QRegularExpression("^tt"));
        static const QRegularExpression digits(QStringLiteral("^[0-9]{1,12}$"));
        if (!digits.match(value).hasMatch() || value.toLongLong() <= 0) continue;
        if (it.value().contains("imdb", Qt::CaseInsensitive)) value.prepend("tt");
        identity.ids.insert(it.value(), value); identity.provenance.insert(it.value(), source);
    }
    const auto release = metadata.value("release").toMap();
    for (auto it = release.cbegin(); it != release.cend(); ++it)
        if (!it.value().toString().isEmpty()) identity.release.insert(it.key(), it.value().toString());
    identity.conflicts.removeDuplicates();
}

inline Identity identify(const QUrl& url, const QVariantMap& metadata = {})
{
    auto result = parse(url.fileName());
    auto parent = url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
    auto folder = parse(parent.fileName());
    // Only consult nearby folders for incomplete identities. Never scan the filesystem.
    if (result.title.isEmpty()) {
        if (folder.kind == Kind::Episode && folder.title.isEmpty()) {
            if (result.season < 0) result.season = folder.season;
            folder = parse(parent.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash).fileName());
        }
        if (!folder.title.isEmpty()) {
            result.title = folder.title; result.year = folder.year; result.ids = folder.ids;
            if (result.kind == Kind::Unknown) result.kind = folder.kind;
            result.provenance.insert("title", "folder");
            if (result.year >= 0) result.provenance.insert("year", "folder");
        }
    }
    if (result.kind == Kind::Unknown && result.season >= 0) result.kind = Kind::Episode;
    applyMetadata(result, metadata);
    return result;
}

inline QVariantMap toMap(const Identity& identity)
{
    QVariantList episodes;
    for (int value : identity.episodes) episodes.append(value);
    QVariantMap ids, release, provenance;
    for (auto it = identity.ids.cbegin(); it != identity.ids.cend(); ++it) ids.insert(it.key(), it.value());
    for (auto it = identity.release.cbegin(); it != identity.release.cend(); ++it) release.insert(it.key(), it.value());
    for (auto it = identity.provenance.cbegin(); it != identity.provenance.cend(); ++it) provenance.insert(it.key(), it.value());
    return {{"title", identity.title}, {"aliases", identity.aliases}, {"year", identity.year},
        {"type", identity.kind == Kind::Movie ? "movie" : identity.kind == Kind::Episode ? "episode" : "unknown"},
        {"season", identity.season}, {"episodes", episodes}, {"absoluteEpisode", identity.absoluteEpisode},
        {"airDate", identity.airDate.toString(Qt::ISODate)}, {"ids", ids}, {"release", release},
        {"provenance", provenance}, {"conflicts", identity.conflicts}};
}

inline Identity fromMap(const QVariantMap& data)
{
    Identity identity;
    applyMetadata(identity, data);
    identity.aliases = data.value("aliases").toStringList();
    identity.absoluteEpisode = data.value("absoluteEpisode").toString();
    identity.airDate = QDate::fromString(data.value("airDate").toString(), Qt::ISODate);
    const auto ids = data.value("ids").toMap();
    for (auto it = ids.cbegin(); it != ids.cend(); ++it) identity.ids.insert(it.key(), it.value().toString());
    const auto provenance = data.value("provenance").toMap();
    for (auto it = provenance.cbegin(); it != provenance.cend(); ++it) identity.provenance.insert(it.key(), it.value().toString());
    identity.conflicts = data.value("conflicts").toStringList();
    return identity;
}

inline double similarity(const QString& first, const QString& second)
{
    const auto a = compact(first).left(512), b = compact(second).left(512);
    if (a.isEmpty() || b.isEmpty()) return 0;
    if (a == b) return 1;
    // Fuzzy matches for very short titles are too ambiguous to be useful.
    if (std::min(a.size(), b.size()) < 5) return 0;
    QList<int> previous(b.size() + 1), current(b.size() + 1);
    std::iota(previous.begin(), previous.end(), 0);
    for (qsizetype i = 1; i <= a.size(); ++i) {
        current[0] = int(i);
        for (qsizetype j = 1; j <= b.size(); ++j)
            current[j] = std::min({current[j - 1] + 1, previous[j] + 1, previous[j - 1] + (a[i - 1] != b[j - 1])});
        previous.swap(current);
    }
    return 1.0 - double(previous.last()) / std::max(a.size(), b.size());
}

struct Comparison {
    int score = 0; // Evidence weight, not a probability.
    bool conflict = false;
    bool strong = false;
    bool exactTitle = false;
    QStringList agreements;
    QStringList conflicts;
};

inline Comparison compare(const Identity& target, const Identity& candidate)
{
    Comparison result;
    const auto reject = [&](const QString& reason) { result.conflict = true; result.conflicts.append(reason); };
    if (target.kind != Kind::Unknown && candidate.kind != Kind::Unknown && target.kind != candidate.kind) reject("type");
    bool idMatch = false;
    for (auto it = target.ids.cbegin(); it != target.ids.cend(); ++it) {
        if (!candidate.ids.contains(it.key())) continue;
        if (candidate.ids.value(it.key()) != it.value()) reject(it.key());
        else { idMatch = true; result.agreements.append(it.key()); }
    }
    if (target.year >= 0 && candidate.year >= 0 && target.year != candidate.year) reject("year");
    if (target.season >= 0 && candidate.season >= 0 && target.season != candidate.season) reject("season");
    if (!target.episodes.isEmpty() && !candidate.episodes.isEmpty() && target.episodes != candidate.episodes) reject("episode");
    if (!target.absoluteEpisode.isEmpty() && !candidate.absoluteEpisode.isEmpty() && target.absoluteEpisode != candidate.absoluteEpisode) reject("absoluteEpisode");
    if (target.airDate.isValid() && candidate.airDate.isValid() && target.airDate != candidate.airDate) reject("airDate");
    double title = similarity(target.title, candidate.title);
    for (const auto& alias : candidate.aliases) title = std::max(title, similarity(target.title, alias));
    for (const auto& alias : target.aliases) title = std::max(title, similarity(alias, candidate.title));
    result.exactTitle = title == 1;
    if (title < .86 && !idMatch) return result;
    if (result.conflict) return result;
    result.score = idMatch ? 120 : result.exactTitle ? 70 : 40;
    result.agreements.append(result.exactTitle ? "title" : "similarTitle");
    const bool year = target.year >= 0 && target.year == candidate.year;
    const bool episode = target.season >= 0 && target.season == candidate.season
        && !target.episodes.isEmpty() && target.episodes == candidate.episodes;
    const bool absolute = !target.absoluteEpisode.isEmpty() && target.absoluteEpisode == candidate.absoluteEpisode;
    const bool date = target.airDate.isValid() && target.airDate == candidate.airDate;
    if (year) { result.score += 20; result.agreements.append("year"); }
    if (episode || absolute || date) { result.score += 30; result.agreements.append("episode"); }
    const bool episodic = target.kind == Kind::Episode || candidate.kind == Kind::Episode;
    // A parent series ID identifies the series, not an episode.
    const bool workId = result.agreements.contains("imdb") || result.agreements.contains("tmdbMovie")
        || result.agreements.contains("tmdbEpisode") || result.agreements.contains("tvdbEpisode");
    result.strong = episodic ? (episode || absolute || date) && (result.exactTitle || idMatch || workId)
                            : workId || (result.exactTitle && year);
    return result;
}
}
