// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QElapsedTimer>
#include <QSet>
#include <QVariantMap>
#include <functional>
#include "util/media_identity.hpp"

class MainCtx;
class QJsonDocument;
Q_MOC_INCLUDE("maininterface/mainctx.hpp")

// Background episode names. Playback and file tags are never modified.
class EpisodeMetadata : public QObject
{
    Q_OBJECT
    Q_PROPERTY(MainCtx* ctx READ ctx WRITE setCtx NOTIFY ctxChanged FINAL)
public:
    explicit EpisodeMetadata(QObject* parent = nullptr);
    ~EpisodeMetadata() override;
    MainCtx* ctx() const;
    void setCtx(MainCtx* ctx);
    Q_INVOKABLE void request(const QUrl& url, const QVariantMap& metadata);
    Q_INVOKABLE void readCached(const QList<QUrl>& urls);
signals:
    void ctxChanged();
    void titleReady(const QUrl& url, const QString& title, const QUrl& source);
private:
    bool online() const;
    void cancel();
    void lookup(quint64 generation, const MediaIdentity::Identity& identity);
    void get(const QUrl& url, quint64 generation, std::function<void(const QJsonDocument&)> completed);
    void save(const QUrl& url, const QVariantMap& metadata, const QString& title, const QUrl& source);
    QPointer<MainCtx> m_ctx;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QElapsedTimer m_lastRequest;
    QUrl m_current;
    QVariantMap m_metadata;
    QSet<QUrl> m_attempted;
    quint64 m_generation = 0;
};
