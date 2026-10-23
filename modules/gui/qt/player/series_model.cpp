// SPDX-License-Identifier: GPL-2.0-or-later
#include "series_model.hpp"
#include <QThreadPool>
#include <cmath>

SeriesModel::SeriesModel(QObject* parent) : QAbstractListModel(parent), m_results(std::make_shared<SeriesScanResult>())
{
    qRegisterMetaType<SeriesDiscovery::Result>();
    connect(m_results.get(), &SeriesScanResult::ready, this, [this](quint64 serial, const SeriesDiscovery::Result& result, bool complete) {
        if (serial == m_serial) publish(result, complete);
    }, Qt::QueuedConnection);
    m_refreshTimer.setInterval(300); m_refreshTimer.setSingleShot(true);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_refreshTimer, qOverload<>(&QTimer::start));
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] { refresh(); });
}

SeriesModel::~SeriesModel() { cancel(); }
void SeriesModel::cancel() { ++m_serial; if (m_cancelled) m_cancelled->store(true); }

bool SeriesModel::available() const
{
    return !m_target.feedUrl.isEmpty() || (m_current.isLocalFile() && m_target.kind == MediaIdentity::Kind::Episode
        && !m_target.title.isEmpty() && !SeriesDiscovery::episodeKey(m_target).isEmpty());
}
QString SeriesModel::showTitle() const { return m_target.feedUrl.isEmpty() ? m_target.title : m_target.series; }
int SeriesModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : m_visible.size(); }
QHash<int, QByteArray> SeriesModel::roleNames() const
{
    return {{Title, "title"}, {Number, "number"}, {Description, "description"}, {Thumbnail, "thumbnail"},
        {Urls, "urls"}, {Current, "current"}, {Progress, "progress"}, {Played, "played"}, {FeedIndex, "feedIndex"}};
}
bool SeriesModel::isCurrent(const Row& row) const
{
    return row.episode.urls.contains(m_current) || (!m_target.feedUrl.isEmpty() && !m_target.guid.isEmpty() && row.guid == m_target.guid);
}
QVariant SeriesModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size()) return {};
    const auto& row = m_rows[m_visible[index.row()]];
    const auto& identity = row.episode.identity;
    const auto url = row.feedIndex < 0 && row.episode.urls.contains(m_current) ? m_current : row.episode.urls.first();
    const auto state = m_states.value(url);
    QStringList numbers;
    for (int episode : identity.episodes) numbers.append(QString::number(episode));
    const auto number = !numbers.isEmpty() ? numbers.join(QStringLiteral("–")) : !identity.absoluteEpisode.isEmpty() ? identity.absoluteEpisode
        : identity.airDate.isValid() ? identity.airDate.toString(Qt::ISODate) : QString();
    switch (role) {
    case Title: return !row.title.isEmpty() ? row.title : !number.isEmpty() ? tr("Episode %1").arg(number) : tr("Episode");
    case Number: return number;
    case Description: return row.description;
    case Thumbnail: return state.artwork.isEmpty() ? row.thumbnail : state.artwork;
    case Urls: { QVariantList urls; for (const auto& value : row.episode.urls) urls.append(value); return urls; }
    case Current: return isCurrent(row);
    case Progress: return state.progress;
    case Played: return state.played;
    case FeedIndex: return row.feedIndex;
    default: return {};
    }
}
QVariantMap SeriesModel::get(int row) const
{
    QVariantMap result;
    const auto roles = roleNames();
    if (row < 0 || row >= rowCount()) return result;
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) result.insert(QString::fromUtf8(it.value()), data(index(row), it.key()));
    return result;
}

void SeriesModel::setCurrent(const QUrl& url, const QVariantMap& metadata)
{
    if (m_current == url && m_metadata == metadata) return;
    const auto target = SeriesDiscovery::identify(url, metadata);
    const bool followSeason = url != m_current || target.season != m_target.season;
    const bool same = !m_current.isEmpty() && ((!target.feedUrl.isEmpty() && target.feedUrl == m_target.feedUrl)
        || (target.feedUrl.isEmpty() && m_target.feedUrl.isEmpty() && SeriesDiscovery::sameSeries(target, m_target)));
    bool retained = false;
    if (same) for (const auto& row : m_rows)
        if (row.episode.urls.contains(url) || (!target.guid.isEmpty() && row.guid == target.guid)) { retained = true; break; }
    m_current = url; m_metadata = metadata; m_target = target;
    if (!same) {
        cancel(); m_refreshTimer.stop();
        if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
        m_rows.clear(); m_root.clear(); m_states.clear(); m_requestedStates.clear(); m_requestedArtwork.clear();
        m_loading = false; m_limited = false; m_message.clear();
        emit resetRequested();
    }
    if (!same || followSeason) m_selectedSeason = target.season;
    if (!retained && m_current.isLocalFile() && !m_root.isEmpty()
        && QDir(m_root.toLocalFile()).relativeFilePath(m_current.toLocalFile()).startsWith("../")) m_root.clear();
    // Already discovered sibling episodes reuse the live directory snapshot.
    if (retained) {
        for (auto& row : m_rows) if (isCurrent(row)) {
            if (!metadata.value("episodeTitle").toString().isEmpty()) row.title = metadata.value("episodeTitle").toString();
            if (!metadata.value("description").toString().isEmpty()) row.description = metadata.value("description").toString();
        }
    }
    project(); emit contextChanged(); emit statusChanged(); emit selectedSeasonChanged();
    if (!retained && available() && target.feedUrl.isEmpty()) refresh();
}

void SeriesModel::refresh(const QUrl& folder)
{
    if (!available() || !feed().isEmpty()) return;
    cancel();
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_cancelled;
    const auto results = m_results;
    const auto serial = m_serial;
    const auto current = m_current;
    const auto target = m_target;
    const auto root = folder.isEmpty() ? m_root : folder;
    m_loading = true; m_message.clear(); emit statusChanged();
    QThreadPool::globalInstance()->start([results, serial, cancelled, current, target, root] {
        QElapsedTimer notifications; notifications.start();
        bool first = true;
        auto result = SeriesDiscovery::scan(current, target, root, {}, cancelled.get(), [&](const SeriesDiscovery::Result& partial) {
            if (first || notifications.elapsed() >= 100) {
                emit results->ready(serial, partial, false);
                first = false; notifications.restart();
            }
        });
        if (!cancelled->load()) emit results->ready(serial, result, true);
    });
}

void SeriesModel::publish(const SeriesDiscovery::Result& result, bool complete)
{
    m_root = result.root; m_limited = result.limited;
    m_rows.clear();
    auto episodes = result.episodes;
    std::sort(episodes.begin(), episodes.end(), SeriesDiscovery::less);
    for (const auto& episode : episodes) {
        Row row; row.episode = episode;
        if (isCurrent(row)) {
            row.title = m_metadata.value("episodeTitle").toString();
            row.description = m_metadata.value("description").toString();
        }
        m_rows.append(row);
    }
    if (complete) {
        m_loading = false;
        m_message = result.error.isEmpty() ? QString() : tr("This folder could not be read. Choose the series folder to try again.");
        if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
        if (!result.directories.isEmpty()) m_watcher.addPaths(result.directories);
    }
    project(); requestStates(); emit statusChanged();
}

void SeriesModel::setFeedEpisodes(const QUrl& source, const QVariantList& episodes)
{
    if (source != feed() || source.isEmpty()) return;
    m_rows.clear();
    for (const auto& value : episodes) {
        const auto item = value.toMap(); const auto podcast = item.value("podcast").toMap();
        if (QUrl(podcast.value("feed").toString()) != source) continue;
        Row row;
        row.feedIndex = item.value("sourceIndex").toInt();
        row.title = item.value("title").toString(); row.description = podcast.value("description").toString();
        row.thumbnail = item.value("thumbnail").toString(); row.guid = podcast.value("guid").toString();
        row.published = MediaIdentity::publicationDate(podcast.value("date").toString());
        const auto url = QUrl(item.value("mrl").toString());
        if (url.isEmpty()) continue;
        row.episode.urls.append(url);
        const auto video = QUrl(podcast.value("video").toString());
        if (!video.isEmpty()) row.episode.urls.append(video);
        auto& identity = row.episode.identity;
        bool ok = false; const auto season = podcast.value("season").toString().toInt(&ok);
        identity.season = ok && season >= 0 ? season : -1;
        const auto episode = podcast.value("episode").toString().toInt(&ok);
        if (ok && episode >= 0) identity.episodes.append(episode);
        m_states[url].progress = item.value("progress").toDouble(); m_states[url].played = item.value("played").toBool();
        m_rows.append(row);
    }
    std::stable_sort(m_rows.begin(), m_rows.end(), [](const Row& a, const Row& b) {
        const auto& x = a.episode.identity; const auto& y = b.episode.identity;
        if (x.season != y.season) return x.season < y.season;
        if (!x.episodes.isEmpty() && !y.episodes.isEmpty() && x.episodes != y.episodes) return x.episodes.first() < y.episodes.first();
        return a.published < b.published;
    });
    project();
}
void SeriesModel::setFeedStatus(bool loading, bool failed)
{
    if (feed().isEmpty()) return;
    m_loading = loading; m_message = failed ? tr("The podcast feed could not be loaded. Try refreshing it.") : QString();
    emit statusChanged();
}
void SeriesModel::setSelectedSeason(int season)
{
    if (m_selectedSeason == season) return;
    m_selectedSeason = season; project(); emit selectedSeasonChanged();
}
void SeriesModel::project()
{
    beginResetModel(); m_visible.clear();
    QSet<int> seasons;
    for (const auto& row : m_rows) seasons.insert(row.episode.identity.season);
    bool selectedChanged = false;
    if (!seasons.isEmpty() && !seasons.contains(m_selectedSeason)) {
        m_selectedSeason = *std::min_element(seasons.cbegin(), seasons.cend());
        for (const auto& row : m_rows) if (isCurrent(row)) { m_selectedSeason = row.episode.identity.season; break; }
        selectedChanged = true;
    }
    for (int i = 0; i < m_rows.size(); ++i) {
        const auto season = m_rows[i].episode.identity.season;
        if (season == m_selectedSeason) m_visible.append(i);
    }
    auto ordered = seasons.values(); std::sort(ordered.begin(), ordered.end());
    QVariantList values; for (int season : ordered) values.append(season);
    const bool changed = m_seasons != values; m_seasons = values;
    endResetModel(); emit countChanged(); if (changed) emit seasonsChanged();
    if (selectedChanged) emit selectedSeasonChanged();
}
void SeriesModel::requestStates()
{
    QList<QUrl> urls;
    for (const auto& row : m_rows) for (const auto& url : row.episode.urls)
        if (!m_requestedStates.contains(url)) { m_requestedStates.insert(url); urls.append(url); }
    if (!urls.isEmpty()) emit watchStatesRequested(urls, m_serial);
}
void SeriesModel::loadStates() { m_requestedStates.clear(); requestStates(); }
void SeriesModel::update(const QUrl& url, int role)
{
    for (int i = 0; i < m_visible.size(); ++i)
        if (m_rows[m_visible[i]].episode.urls.contains(url)) emit dataChanged(index(i), index(i), {role});
}
void SeriesModel::setProgress(const QUrl& url, qreal progress)
{
    if (url != m_current && !m_requestedStates.contains(url)) return;
    const auto bounded = std::isfinite(progress) ? std::clamp(progress, qreal(0), qreal(1)) : 0;
    auto key = url;
    if (url == m_current && !feed().isEmpty())
        for (const auto& row : m_rows) if (isCurrent(row)) { key = row.episode.urls.first(); break; }
    if (m_states.value(key).progress == bounded) return;
    m_states[key].progress = bounded; update(key, Progress);
}
void SeriesModel::setPlayed(const QUrl& url, bool played)
{
    if (!m_requestedStates.contains(url) || m_states.value(url).played == played) return;
    m_states[url].played = played; update(url, Played);
}
void SeriesModel::setArtwork(const QUrl& url, const QString& artwork)
{
    if (!m_requestedArtwork.contains(url)) return;
    m_states[url].artwork = artwork; update(url, Thumbnail);
}
void SeriesModel::requestDetails(int row)
{
    if (row < 0 || row >= rowCount()) return;
    const auto& entry = m_rows[m_visible[row]];
    if (entry.feedIndex >= 0) return;
    const auto url = entry.episode.urls.contains(m_current) ? m_current : entry.episode.urls.first();
    if (m_requestedArtwork.contains(url)) return;
    m_requestedArtwork.insert(url); emit artworkRequested(url);
}
