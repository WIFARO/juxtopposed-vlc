// SPDX-License-Identifier: GPL-2.0-or-later
#include "networkplaylistmodel.hpp"
#include "maininterface/mainctx.hpp"
#include "medialibrary/medialib.hpp"
#include "medialibrary/mlmediastore.hpp"
#include "util/podcast_metadata.hpp"
#include "playlist/media.hpp"
#include "playlist/playlist_controller.hpp"
#include <vlc_preparser.h>
#include <QUrl>
#include <algorithm>

struct NetworkPlaylistModel::Request
{
    NetworkPlaylistModel* owner;
    quint64 generation;
    size_t index;
    std::vector<SharedInputItem> children;
    bool playlist = false;
};

NetworkPlaylistModel::NetworkPlaylistModel(QObject* parent)
    : QAbstractListModel(parent)
{
    connect(this, &NetworkPlaylistModel::filterChanged, this, &NetworkPlaylistModel::rebuild);
    connect(this, &NetworkPlaylistModel::ctxChanged, this, &NetworkPlaylistModel::reload);
}

NetworkPlaylistModel::~NetworkPlaylistModel()
{
    cancel();
    // Join callbacks before the QObject and its queued completion target die.
    if (m_parser)
        vlc_preparser_Delete(m_parser);
}

int NetworkPlaylistModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> NetworkPlaylistModel::roleNames() const
{
    return {{TitleRole, "title"}, {MrlRole, "mrl"}, {ThumbnailRole, "thumbnail"},
            {IconRole, "iconName"}, {KeyRole, "channelKey"}, {PodcastRole, "podcast"},
            {DurationRole, "duration"}, {ProgressRole, "progress"}, {PlayedRole, "played"}};
}

QVariant NetworkPlaylistModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || size_t(index.row()) >= m_rows.size())
        return {};
    const auto& row = m_rows[size_t(index.row())];
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole: {
        const auto title = vlc::wrap_cptr(input_item_GetName(row.item.get()));
        return QString::fromUtf8(title.get() ? title.get() : "");
    }
    case MrlRole:
        return QUrl::fromEncoded(row.item->psz_uri).toString(QUrl::FullyEncoded);
    case ThumbnailRole: {
        const auto artwork = vlc::wrap_cptr(input_item_GetArtworkURL(row.item.get()));
        return QString::fromUtf8(artwork.get() ? artwork.get() : "");
    }
    case PodcastRole: return row.podcast;
    case DurationRole: return QVariant::fromValue(VLCDuration(input_item_GetDuration(row.item.get())));
    case ProgressRole: return m_media.value(QString::fromUtf8(row.item->psz_uri)).progress();
    case PlayedRole: return m_media.value(QString::fromUtf8(row.item->psz_uri)).playCount() > 0;
    case IconRole:
        return row.podcast.isEmpty() ? QStringLiteral("nav-video") : QStringLiteral("music");
    case KeyRole:
        return row.key;
    default:
        return {};
    }
}

QVariantMap NetworkPlaylistModel::getDataAt(int row) const
{
    if (row < 0 || size_t(row) >= m_rows.size())
        return {};
    QVariantMap result;
    const auto roles = roleNames();
    for (auto role = roles.cbegin(); role != roles.cend(); ++role)
        result.insert(QString::fromLatin1(role.value()), data(index(row, 0), role.key()));
    return result;
}

int NetworkPlaylistModel::indexForKey(const QString& key) const
{
    for (size_t i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].key == key)
            return int(i);
    return -1;
}

void NetworkPlaylistModel::play(int row, bool enqueue)
{
    if (!m_active || !m_ctx || row < 0 || size_t(row) >= m_rows.size())
        return;
    // Original saved items still use the library's per-media playback state.
    // Parsed channels use their full input item, retaining playlist options.
    if (!m_rows[size_t(row)].mediaId.isEmpty() && m_ctx->getMediaLibrary()) {
        const auto id = MediaLib::deserializeMlItemIdFromString(m_rows[size_t(row)].mediaId);
        if (enqueue)
            m_ctx->getMediaLibrary()->addToPlaylist(id);
        else
            m_ctx->getMediaLibrary()->addAndPlay(id);
        return;
    }
    const QVector<vlc::playlist::Media> media{vlc::playlist::Media{m_rows[size_t(row)].item.get()}};
    m_ctx->getIntf()->p_mainPlaylistController->append(media, !enqueue);
}

void NetworkPlaylistModel::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    emit activeChanged();
    reload();
}

void NetworkPlaylistModel::setSources(const QVariantList& sources)
{
    if (m_sources == sources)
        return;
    m_sources = sources;
    reload();
}

void NetworkPlaylistModel::cancel()
{
    ++m_generation;
    if (m_store) m_store->clear();
    m_media.clear();
    for (auto& source : m_work) {
        if (!source.request)
            continue;
        vlc_preparser_Cancel(m_parser, source.request);
        vlc_preparser_req_Release(source.request);
    }
    m_work.clear();
    m_pending = 0;
    emit loadingChanged();
}

void NetworkPlaylistModel::reload()
{
    cancel();
    if (!m_active || !m_ctx) {
        // Keep the last rows during the closing slide; an explicit source reset
        // still clears them. Disabled views cannot play this retained snapshot.
        if (m_sources.isEmpty())
            rebuild();
        return;
    }
    if (!m_parser) {
        vlc_preparser_cfg cfg{};
        cfg.types = VLC_PREPARSER_TYPE_PARSE;
        cfg.max_parser_threads = 2;
        cfg.timeout = VLC_TICK_FROM_SEC(10);
        m_parser = vlc_preparser_New(VLC_OBJECT(m_ctx->getIntf()), &cfg);
    }
    static const vlc_preparser_cbs callbacks = {
        [](vlc_preparser_req*, int status, void* opaque) {
            std::unique_ptr<Request> request(static_cast<Request*>(opaque));
            QMetaObject::invokeMethod(request->owner,
                [owner = request->owner, generation = request->generation, index = request->index,
                 playlist = request->playlist && status == VLC_SUCCESS,
                 children = std::move(request->children)]() mutable {
                    owner->finished(generation, index, playlist, std::move(children));
                }, Qt::QueuedConnection);
        },
        [](vlc_preparser_req*, input_item_node_t* subtree, void* opaque) {
            auto* request = static_cast<Request*>(opaque);
            request->playlist = true;
            const auto collect = [&](auto&& self, input_item_node_t* node) -> void {
                if (node->i_children > 0) {
                    for (int i = 0; i < node->i_children; ++i)
                        self(self, node->pp_children[i]);
                } else if (node->p_item && node->p_item->psz_uri && *node->p_item->psz_uri) {
                    request->children.emplace_back(node->p_item);
                }
            };
            for (int i = 0; i < subtree->i_children; ++i)
                collect(collect, subtree->pp_children[i]);
            input_item_node_Delete(subtree);
        }, nullptr
    };
    for (const auto& value : m_sources) {
        const auto map = value.toMap();
        const auto url = QUrl(map.value("mrl").toString());
        if (!url.isValid() || url.scheme().isEmpty())
            continue;
        const auto uri = url.toEncoded();
        const auto title = map.value("title").toString().toUtf8();
        input_item_t* raw = input_item_New(uri.constData(), title.isEmpty() ? nullptr : title.constData());
        if (!raw)
            continue;
        Source source;
        source.item = SharedInputItem(raw, false);
        source.mediaId = map.value("id").toString();
        source.remember = map.value("remember").toBool();
        m_work.push_back(std::move(source));
        auto& work = m_work.back();
        // HTTP links can be extensionless lists. Other protocols keep their
        // original input. A stream that has no playlist subtree also stays whole.
        if (!m_parser || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")))
            continue;
        auto request = std::make_unique<Request>(Request{this, m_generation, m_work.size() - 1, {}});
        work.request = vlc_preparser_req_NewParse(m_parser, raw,
            VLC_PREPARSER_TYPE_PARSE | VLC_PREPARSER_OPTION_SUBITEMS, &callbacks, request.get());
        if (!work.request)
            continue;
        if (vlc_preparser_Submit(m_parser, work.request) == VLC_SUCCESS) {
            ++m_pending;
            request.release();
        } else {
            vlc_preparser_req_Release(work.request);
            work.request = nullptr;
        }
    }
    rebuild();
    emit loadingChanged();
}

void NetworkPlaylistModel::finished(quint64 generation, size_t index, bool playlist,
                                   std::vector<SharedInputItem> children)
{
    if (generation != m_generation || index >= m_work.size())
        return;
    auto& work = m_work[index];
    vlc_preparser_req_Release(work.request);
    work.request = nullptr;
    work.playlist = playlist;
    if (playlist && work.remember && m_ctx->getMediaLibrary())
        m_ctx->getMediaLibrary()->recordNetworkPlaylistAccess(QUrl::fromEncoded(work.item->psz_uri));
    work.children = playlist ? std::move(children) : std::vector<SharedInputItem>{};
    if (m_ctx && m_ctx->getMediaLibrary()) {
        if (!m_store) {
            m_store = std::make_unique<MLMediaStore>(m_ctx->getMediaLibrary());
            connect(m_store.get(), &MLMediaStore::updated, this, [this](const QString& uri, const MLMedia& media) {
                m_media[uri] = media;
                for (int row = 0; row < int(m_rows.size()); ++row)
                    if (QString::fromUtf8(m_rows[row].item->psz_uri) == uri) emit dataChanged(this->index(row), this->index(row));
            });
        }
        QStringList urls;
        QHash<QString, SharedInputItem> podcasts;
        for (const auto& source : m_work) for (const auto& child : source.children) {
            const auto uri = QString::fromUtf8(child->psz_uri);
            urls.push_back(uri);
            if (!podcastMetadata(child.get()).isEmpty()) podcasts.insert(uri, child);
        }
        m_store->insertMany(urls, {}, podcasts);
    }
    --m_pending;
    rebuild();
    emit loadingChanged();
}

void NetworkPlaylistModel::rebuild()
{
    beginResetModel();
    m_rows.clear();
    bool expanded = false;
    for (size_t i = 0; i < m_work.size(); ++i) {
        const auto& work = m_work[i];
        expanded |= work.playlist;
        const auto add = [&](const SharedInputItem& item, size_t child, const QString& mediaId) {
            const auto name = vlc::wrap_cptr(input_item_GetName(item.get()));
            if (m_search.isEmpty() || QString::fromUtf8(name.get() ? name.get() : "").contains(m_search, Qt::CaseInsensitive)) {
                m_rows.push_back({item, QString::number(m_generation) + QLatin1Char('/')
                    + QString::number(i) + QLatin1Char('/') + QString::number(child), mediaId, podcastMetadata(item.get())});
            }
        };
        if (!work.playlist)
            add(work.item, 0, work.mediaId);
        else
            for (size_t n = 0; n < work.children.size(); ++n)
                add(work.children[n], n, {});
    }
    if (m_sort == QLatin1String("title") || m_sort == QLatin1String("uri") || m_sort == QLatin1String("published") || m_sort == QLatin1String("episode") || m_sort == QLatin1String("duration")) {
        std::stable_sort(m_rows.begin(), m_rows.end(), [&](const Row& a, const Row& b) {
            int result;
            if (m_sort == QLatin1String("published")) {
                const auto left = QDateTime::fromString(a.podcast.value("date").toString(), Qt::RFC2822Date);
                const auto right = QDateTime::fromString(b.podcast.value("date").toString(), Qt::RFC2822Date);
                result = left == right ? 0 : left < right ? -1 : 1;
            } else if (m_sort == QLatin1String("episode")) {
                const auto left = std::make_pair(a.podcast.value("season").toInt(), a.podcast.value("episode").toDouble());
                const auto right = std::make_pair(b.podcast.value("season").toInt(), b.podcast.value("episode").toDouble());
                result = left == right ? 0 : left < right ? -1 : 1;
            } else if (m_sort == QLatin1String("duration")) {
                const auto left = input_item_GetDuration(a.item.get()), right = input_item_GetDuration(b.item.get());
                result = left == right ? 0 : left < right ? -1 : 1;
            } else if (m_sort == QLatin1String("uri")) {
                result = QString::compare(QString::fromUtf8(a.item->psz_uri), QString::fromUtf8(b.item->psz_uri));
            } else {
                const auto an = vlc::wrap_cptr(input_item_GetName(a.item.get()));
                const auto bn = vlc::wrap_cptr(input_item_GetName(b.item.get()));
                result = QString::localeAwareCompare(QString::fromUtf8(an.get() ? an.get() : ""),
                                                     QString::fromUtf8(bn.get() ? bn.get() : ""));
            }
            return m_order == Qt::AscendingOrder ? result < 0 : result > 0;
        });
    }
    endResetModel();
    emit countChanged();
    if (m_expanded != expanded) {
        m_expanded = expanded;
        emit expandedChanged();
    }
}

void NetworkPlaylistModel::playEpisode(int row, bool video, bool fromStart)
{
    if (!m_active || !m_ctx || row < 0 || size_t(row) >= m_rows.size()) return;
    auto* original = m_rows[size_t(row)].item.get();
    auto* controller = m_ctx->getIntf()->p_mainPlaylistController;
    auto item = SharedInputItem(input_item_Copy(original), false);
    if (!item) return;
    if (video) {
        const auto url = vlc::wrap_cptr(input_item_GetMetaExtra(item.get(), "podcast:video"));
        if (!url || !*url) return;
        input_item_SetURI(item.get(), url.get());
    }
    input_item_AddOption(item.get(), fromStart ? "restore-playback-pos=0" : "restore-playback-pos=2", VLC_INPUT_OPTION_TRUSTED);
    const QVector<vlc::playlist::Media> media{vlc::playlist::Media{item.get()}};
    controller->append(media, true);
}
