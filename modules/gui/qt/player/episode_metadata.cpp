// SPDX-License-Identifier: GPL-2.0-or-later
#include "episode_metadata.hpp"
#include "maininterface/mainctx.hpp"
#include "player_controller.hpp"
#include "medialibrary/medialib.hpp"
#include "medialibrary/mlhelper.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QTimer>
#include <vlc_cxx_helpers.hpp>

namespace {
const QByteArray prefix("vlc-episode-v1\n");
struct Title { QUrl url; QString title; QUrl source; };

Title cached(vlc_medialibrary_t* ml, const QUrl& url, const QVariantMap& metadata = {})
{
    Title result {url, {}, {}};
    ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media_by_mrl(ml, url.toEncoded().constData()));
    if (!media || media->p_podcast) return result;
    char* value = nullptr;
    if (vlc_ml_media_get_playback_state(ml, media->i_id, VLC_ML_PLAYBACK_STATE_APP_SPECIFIC, &value) == VLC_SUCCESS && value) {
        const QByteArray bytes(value); free(value);
        if (bytes.startsWith(prefix) && bytes.size() <= 16384) {
            const auto record = QJsonDocument::fromJson(bytes.mid(prefix.size())).object();
            const auto identity = MediaIdentity::identify(url, metadata);
            if (record.value("series").toString() == MediaIdentity::normalize(identity.title)
                && record.value("season").toInt(-1) == identity.season
                && record.value("episodes").toArray() == QJsonArray::fromVariantList(MediaIdentity::toMap(identity).value("episodes").toList())) {
                result.title = MediaIdentity::episodeTitle(url, {{"episodeTitle", record.value("title").toString()}});
                const QUrl source(record.value("source").toString());
                if (source.scheme() == "https" && source.host() == "www.tvmaze.com") result.source = source;
            }
        }
    }
    if (result.title.isEmpty())
        result.title = MediaIdentity::episodeTitle(url, {{"episodeTitle", QString::fromUtf8(media->psz_title ? media->psz_title : "")}});
    return result;
}
}

EpisodeMetadata::EpisodeMetadata(QObject* parent) : QObject(parent) {}
EpisodeMetadata::~EpisodeMetadata() { cancel(); }
MainCtx* EpisodeMetadata::ctx() const { return m_ctx; }
bool EpisodeMetadata::online() const { return m_ctx && var_InheritBool(m_ctx->getIntf(), "metadata-network-access"); }
void EpisodeMetadata::cancel()
{
    ++m_generation;
    if (m_reply) { auto reply = m_reply; m_reply = nullptr; reply->abort(); }
}
void EpisodeMetadata::setCtx(MainCtx* ctx)
{
    if (m_ctx == ctx) return;
    cancel();
    if (m_ctx) disconnect(m_ctx->getIntf()->p_mainPlayerController, nullptr, this, nullptr);
    m_ctx = ctx;
    if (m_ctx) connect(m_ctx->getIntf()->p_mainPlayerController, &PlayerController::metadataNetworkAccessChanged,
        this, [this](bool enabled) {
            cancel();
            if (enabled) {
                const auto url = m_current; const auto metadata = m_metadata;
                m_current.clear(); m_attempted.remove(url); request(url, metadata);
            }
        });
    emit ctxChanged();
}
void EpisodeMetadata::readCached(const QList<QUrl>& urls)
{
    if (!m_ctx || !m_ctx->getMediaLibrary()) return;
    // Reads use the existing bounded discovery list. They never import siblings.
    const auto batch = urls.mid(0, 64);
    const auto remaining = urls.mid(batch.size());
    m_ctx->getMediaLibrary()->runOnMLThread<QList<Title>>(this,
        [batch](vlc_medialibrary_t* ml, QList<Title>& result) {
            for (const auto& url : batch) if (url.isLocalFile()) result.append(cached(ml, url));
        }, [this, remaining](quint64, const QList<Title>& result) {
            for (const auto& entry : result) if (!entry.title.isEmpty()) emit titleReady(entry.url, entry.title, entry.source);
            if (!remaining.isEmpty()) readCached(remaining);
        }, "episode-metadata");
}
void EpisodeMetadata::request(const QUrl& url, const QVariantMap& metadata)
{
    if (url == m_current && metadata == m_metadata) return;
    cancel(); m_current = url; m_metadata = metadata;
    const auto identity = MediaIdentity::identify(url, metadata);
    if (!url.isLocalFile() || identity.kind != MediaIdentity::Kind::Episode) return;
    const auto title = MediaIdentity::episodeTitle(url, metadata);
    if (!title.isEmpty()) { emit titleReady(url, title, {}); save(url, metadata, title, {}); return; }
    const auto generation = m_generation;
    if (!m_ctx || !m_ctx->getMediaLibrary()) { lookup(generation, identity); return; }
    m_ctx->getMediaLibrary()->runOnMLThread<Title>(this,
        [url, metadata](vlc_medialibrary_t* ml, Title& result) { result = cached(ml, url, metadata); },
        [this, generation, identity](quint64, const Title& result) {
            if (generation != m_generation) return;
            if (!result.title.isEmpty()) emit titleReady(result.url, result.title, result.source);
            else lookup(generation, identity);
        }, "episode-metadata");
}
void EpisodeMetadata::save(const QUrl& url, const QVariantMap& metadata, const QString& title, const QUrl& source)
{
    if (!m_ctx || !m_ctx->getMediaLibrary()) return;
    const auto identity = MediaIdentity::identify(url, metadata);
    const QJsonObject record {{"series", MediaIdentity::normalize(identity.title)}, {"season", identity.season},
        {"episodes", QJsonArray::fromVariantList(MediaIdentity::toMap(identity).value("episodes").toList())},
        {"title", title}, {"source", source.toString()}};
    const QByteArray bytes = prefix + QJsonDocument(record).toJson(QJsonDocument::Compact);
    m_ctx->getMediaLibrary()->runOnMLThread<bool>(this,
        [url, bytes](vlc_medialibrary_t* ml, bool& saved) {
            ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media_by_mrl(ml, url.toEncoded().constData()));
            if (!media) media.reset(vlc_ml_new_external_media(ml, url.toEncoded().constData()));
            if (!media || media->p_podcast) return;
            char* value = nullptr;
            if (vlc_ml_media_get_playback_state(ml, media->i_id, VLC_ML_PLAYBACK_STATE_APP_SPECIFIC, &value) != VLC_SUCCESS) return;
            const QByteArray previous(value ? value : ""); free(value);
            // Do not replace podcast state or another application's data.
            if (!previous.isEmpty() && !previous.startsWith(prefix)) return;
            if (previous == bytes) { saved = true; return; }
            saved = vlc_ml_media_set_playback_state(ml, media->i_id, VLC_ML_PLAYBACK_STATE_APP_SPECIFIC, bytes.constData()) == VLC_SUCCESS;
        }, [](quint64, bool) {}, "episode-metadata");
}
void EpisodeMetadata::get(const QUrl& url, quint64 generation, std::function<void(const QJsonDocument&)> completed)
{
    if (generation != m_generation || !online()) return;
    // One small request at a time; rapid episode changes cannot flood the API.
    const auto delay = m_lastRequest.isValid() ? std::max<qint64>(0, 600 - m_lastRequest.elapsed()) : 0;
    if (delay) {
        QTimer::singleShot(int(delay), this, [this, url, generation, completed] { get(url, generation, completed); });
        return;
    }
    m_lastRequest.start();
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "VLC-Juxtopposed/4.0 (episode titles)");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setMaximumRedirectsAllowed(2); request.setTransferTimeout(8000);
    auto* reply = m_network.get(request); m_reply = reply;
    reply->setReadBufferSize(64 * 1024);
    QTimer::singleShot(8000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, bytes] {
        bytes->append(reply->readAll()); if (bytes->size() > 512 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, generation, completed] {
        if (reply->isReadable()) bytes->append(reply->readAll());
        const bool valid = generation == m_generation && online() && bytes->size() <= 512 * 1024
            && reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        if (m_reply == reply) m_reply = nullptr;
        reply->deleteLater();
        if (valid) completed(QJsonDocument::fromJson(*bytes));
    });
}
void EpisodeMetadata::lookup(quint64 generation, const MediaIdentity::Identity& identity)
{
    if (!online() || generation != m_generation || m_attempted.contains(m_current)
        || identity.title.isEmpty() || identity.season < 1 || identity.episodes.size() != 1
        || identity.episodes.first() < 1 || !identity.conflicts.isEmpty()) return;
    m_attempted.insert(m_current);
    QUrl search("https://api.tvmaze.com/search/shows"); QUrlQuery query; query.addQueryItem("q", identity.title); search.setQuery(query);
    get(search, generation, [this, generation, identity](const QJsonDocument& document) {
        QJsonObject match;
        for (const auto& value : document.array()) {
            const auto show = value.toObject().value("show").toObject();
            if (MediaIdentity::normalize(show.value("name").toString()) != MediaIdentity::normalize(identity.title)) continue;
            if (identity.year > 0 && QDate::fromString(show.value("premiered").toString(), Qt::ISODate).year() != identity.year) continue;
            if (!match.isEmpty()) return; // Same-name remakes need stronger evidence.
            match = show;
        }
        const auto id = match.value("id").toInt(); if (id <= 0) return;
        QUrl url(QStringLiteral("https://api.tvmaze.com/shows/%1/episodebynumber").arg(id));
        QUrlQuery query; query.addQueryItem("season", QString::number(identity.season));
        query.addQueryItem("number", QString::number(identity.episodes.first())); url.setQuery(query);
        get(url, generation, [this, identity](const QJsonDocument& document) {
            const auto episode = document.object();
            if (episode.value("season").toInt(-1) != identity.season || episode.value("number").toInt(-1) != identity.episodes.first()) return;
            const auto title = MediaIdentity::episodeTitle(m_current, {{"episodeTitle", episode.value("name").toString()}});
            const QUrl source(episode.value("url").toString());
            if (title.isEmpty() || source.scheme() != "https" || source.host() != "www.tvmaze.com") return;
            emit titleReady(m_current, title, source); save(m_current, m_metadata, title, source);
        });
    });
}
