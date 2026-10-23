// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QAbstractListModel>
#include <QVariantList>
#include "util/shared_input_item.hpp"
#include <vector>
class MainCtx;
class MLMediaStore;
#include "medialibrary/mlmedia.hpp"
#include <memory>
struct vlc_preparser_t;
struct vlc_preparser_req;
Q_MOC_INCLUDE("maininterface/mainctx.hpp")

// A saved online playlist keeps its source links. Browse current channel items
// from the core parser, including their input options, without rewriting the DB.
class NetworkPlaylistModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(MainCtx* ctx MEMBER m_ctx NOTIFY ctxChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool expanded READ expanded NOTIFY expandedChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString searchPattern MEMBER m_search NOTIFY filterChanged)
    Q_PROPERTY(QString sortCriteria MEMBER m_sort NOTIFY filterChanged)
    Q_PROPERTY(Qt::SortOrder sortOrder MEMBER m_order NOTIFY filterChanged)
public:
    explicit NetworkPlaylistModel(QObject* parent = nullptr);
    ~NetworkPlaylistModel() override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool active() const { return m_active; }
    bool expanded() const { return m_expanded; }
    bool loading() const { return m_pending > 0; }
    void setActive(bool active);
    Q_INVOKABLE void setSources(const QVariantList& sources);
    Q_INVOKABLE QVariantMap getDataAt(int row) const;
    Q_INVOKABLE int indexForKey(const QString& key) const;
    Q_INVOKABLE void play(int row, bool enqueue = false);
    Q_INVOKABLE void playEpisode(int row, bool video, bool fromStart);
signals:
    void ctxChanged();
    void activeChanged();
    void expandedChanged();
    void loadingChanged();
    void countChanged();
    void filterChanged();
private:
    enum Role { TitleRole = Qt::UserRole + 1, MrlRole, ThumbnailRole, IconRole, KeyRole, PodcastRole, DurationRole, ProgressRole, PlayedRole };
    struct Row {
        SharedInputItem item;
        QString key;
        QString mediaId;
        QVariantMap podcast;
    };
    struct Source {
        SharedInputItem item;
        QString mediaId;
        std::vector<SharedInputItem> children;
        vlc_preparser_req* request = nullptr;
        bool playlist = false;
        bool remember = false;
    };
    struct Request;
    void reload();
    void cancel();
    void rebuild();
    void finished(quint64 generation, size_t index, bool playlist, std::vector<SharedInputItem> children);
    std::unique_ptr<MLMediaStore> m_store;
    QHash<QString, MLMedia> m_media;
    MainCtx* m_ctx = nullptr;
    vlc_preparser_t* m_parser = nullptr;
    QVariantList m_sources;
    std::vector<Source> m_work;
    std::vector<Row> m_rows;
    QString m_search, m_sort;
    Qt::SortOrder m_order = Qt::AscendingOrder;
    quint64 m_generation = 0;
    size_t m_pending = 0;
    bool m_active = false, m_expanded = false;
};
