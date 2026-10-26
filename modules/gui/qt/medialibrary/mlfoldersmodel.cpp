/*****************************************************************************
 * Copyright (C) 2019 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "mlfoldersmodel.hpp"
#include "medialibrary/medialib.hpp"
#include <QFileInfo>
#include <QPointer>
#include <cassert>
#include <algorithm>

static const char* const ML_FOLDER_ADD_QUEUE = "ML_FOLDER_ADD_QUEUE";
static const char* const ML_FOLDER_BAN_QUEUE = "ML_FOLDER_BAN_QUEUE";

MLFoldersBaseModel::MLFoldersBaseModel( QObject *parent )
    : QAbstractListModel( parent )
    , m_ml_event_handle( nullptr , [this](vlc_ml_event_callback_t* cb ) {
        if (!cb)
            return;
        if ( m_mediaLib )
            m_mediaLib->unregisterEventListener(cb);
    })
{
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(100);
    connect(&m_refreshTimer, &QTimer::timeout, this, &MLFoldersBaseModel::update);
    connect(this, &MLFoldersBaseModel::onMLEntryPointModified, this, [this] {
        // Coalesce folder events while the library imports or removes media.
        if (!m_refreshTimer.isActive()) m_refreshTimer.start();
    });
}

MLFoldersBaseModel::EntryPoint::EntryPoint( const vlc_ml_folder_t& entryPoint)
    : mrl(entryPoint.psz_mrl)
    , banned(entryPoint.b_banned)
    , containsAudio(entryPoint.i_nb_audio > 0)
    , containsVideo(entryPoint.i_nb_video > 0)
{
}

void MLFoldersBaseModel::setCtx(MainCtx* ctx)
{
    if (ctx)
    {
        m_ctx = ctx;
        m_mediaLib = m_ctx->getMediaLibrary();
        m_ml_event_handle.reset(m_mediaLib->registerEventListener(onMlEvent, this));
    }
    else
    {
        m_ctx = nullptr;
        //reset *before* setting m_mediaLib to nullptr
        m_ml_event_handle.reset( nullptr );
        m_mediaLib = nullptr;
    }
    update();
    emit ctxChanged();
}

int MLFoldersBaseModel::rowCount( QModelIndex const & ) const
{
    return static_cast<int>(m_mrls.size());
}

QVariant MLFoldersBaseModel::data( const QModelIndex &index ,
                              int role) const {
    if ( !index.isValid() )
        return {};

    switch ( role )
    {
        case Banned:
            return m_mrls[index.row()].banned;
        case MRL:
            return m_mrls[index.row()].mrl;
        case ContainsAudio:
            return m_mrls[index.row()].containsAudio;
        case ContainsVideo:
            return m_mrls[index.row()].containsVideo;
        case Qt::DisplayRole:
        case DisplayUrl:
        {
            QUrl url = QUrl::fromUserInput(m_mrls[index.row()].mrl);
            if (!url.isValid())
                return {};
            return QVariant::fromValue(urlToDisplayString(url));
        }
        default :
            return {};
    }
}

QHash<int, QByteArray> MLFoldersBaseModel::roleNames() const
{
    return {
        {DisplayUrl, QByteArrayLiteral("display_url")},
        {Banned, QByteArrayLiteral("banned")},
        {MRL, QByteArrayLiteral("url")},
        {ContainsAudio, QByteArrayLiteral("contains_audio")},
        {ContainsVideo, QByteArrayLiteral("contains_video")}
    };
}

void MLFoldersBaseModel::reload(const int row)
{
    const QString mrl = m_mrls.at(row).mrl;
    m_mediaLib->runOnMLThread(this, [mrl](vlc_medialibrary_t *ml)
    {
        vlc_ml_reload_folder( ml, qtu(mrl) );
    });
}

void MLFoldersBaseModel::removeAt(int index)
{
    assert(index < rowCount());
    const QModelIndex idx = this->index( index, 0 );
    if (idx.isValid())
        remove( data( idx,  MRL ).toUrl() );

}

void MLFoldersBaseModel::onMlEvent( void* data , const vlc_ml_event_t* event )
{
    auto self = static_cast<MLFoldersBaseModel *>( data );
    if ( event->i_type == VLC_ML_EVENT_ENTRY_POINT_ADDED || event->i_type == VLC_ML_EVENT_ENTRY_POINT_REMOVED ||
         event->i_type == VLC_ML_EVENT_ENTRY_POINT_UNBANNED || event->i_type == VLC_ML_EVENT_ENTRY_POINT_BANNED ||
         event->i_type == VLC_ML_EVENT_FOLDER_ADDED || event->i_type == VLC_ML_EVENT_FOLDER_UPDATED ||
         event->i_type == VLC_ML_EVENT_FOLDER_DELETED )
    {
        if (!self->failed( event ))
        {
            if (event->i_type == VLC_ML_EVENT_ENTRY_POINT_ADDED && event->entry_point_added.b_success)
                emit self->folderAdded(QUrl::fromEncoded(event->entry_point_added.psz_entry_point));
            emit self->onMLEntryPointModified( QPrivateSignal() );
        }
    }
}

void MLFoldersBaseModel::updateImpl(vlc_ml_folder_list_t* (*folderListFunc)( vlc_medialibrary_t* p_ml, const vlc_ml_query_params_t* params ))
{
    if (m_updatePending) {
        m_updateAgain = true;
        return;
    }

    m_updatePending = true;
    struct Ctx {
        std::vector<MLFoldersBaseModel::EntryPoint> r;
    };

    m_mediaLib->runOnMLThread<Ctx>(this,
    //ML thread
    [folderListFunc](vlc_medialibrary_t* ml,  Ctx& ctx){
        vlc_ml_folder_list_t* entrypoints = folderListFunc( ml, nullptr );
        if ( entrypoints != nullptr )
        {
            for ( unsigned int i = 0; entrypoints && i < entrypoints->i_nb_items; i++ )
                ctx.r.emplace_back( entrypoints->p_items[i] );
            vlc_ml_release( entrypoints );
        }
        if (folderListFunc == vlc_ml_list_entry_points) {
            // Use indexed descendants, without reading files or probing drives.
            ml_unique_ptr<vlc_ml_folder_list_t> folders(vlc_ml_list_folders(ml, nullptr));
            if (folders) {
                for (auto& root : ctx.r) {
                    const auto rootUrl = QUrl::fromEncoded(root.mrl.toUtf8()).adjusted(QUrl::StripTrailingSlash);
                    for (const auto& folder : ml_range_iterate<vlc_ml_folder_t>(folders)) {
                        if (!folder.psz_mrl) continue;
                        const auto url = QUrl::fromEncoded(folder.psz_mrl).adjusted(QUrl::StripTrailingSlash);
                        if (url == rootUrl || rootUrl.isParentOf(url)) {
                            root.containsAudio |= folder.i_nb_audio > 0;
                            root.containsVideo |= folder.i_nb_video > 0;
                            if (root.containsAudio && root.containsVideo) break;
                        }
                    }
                }
            } else {
                // A failed metadata query must not hide a registered source.
                for (auto& root : ctx.r) root.containsVideo = true;
            }
        }
    },
    //UI thread
    [this, publishRoots = folderListFunc == vlc_ml_list_entry_points](quint64, Ctx& ctx)
    {
        m_updatePending = false;
        const bool unchanged = m_mrls.size() == ctx.r.size()
            && std::equal(m_mrls.cbegin(), m_mrls.cend(), ctx.r.cbegin(),
                [](const EntryPoint& a, const EntryPoint& b) {
                    return a.mrl == b.mrl && a.banned == b.banned
                        && a.containsAudio == b.containsAudio && a.containsVideo == b.containsVideo;
                });
        if (!unchanged) {
            beginResetModel();
            m_mrls = std::move(ctx.r);
            endResetModel();
        }
        if (publishRoots && m_mediaLib) {
            QStringList roots;
            for (const auto& entry : m_mrls) if (!entry.banned && !entry.mrl.isEmpty()) roots.append(entry.mrl);
            m_mediaLib->setFolderRoots(std::move(roots));
        }
        if (m_updateAgain) {
            m_updateAgain = false;
            m_refreshTimer.start();
        }
    });
}

void MLFoldersModel::update()
{
    updateImpl(vlc_ml_list_entry_points);
}


bool MLFoldersModel::failed(const vlc_ml_event_t *event) const
{
    if ( event->i_type == VLC_ML_EVENT_ENTRY_POINT_ADDED && !event->entry_point_added.b_success )
    {
        const auto url = QUrl::fromEncoded(event->entry_point_added.psz_entry_point);
        const QPointer<const MLFoldersModel> model(this);
        // The backend also reports a failed insertion for an existing root.
        // Inspect it on the library worker, without querying from its callback.
        QMetaObject::invokeMethod(m_mediaLib, [model, url] {
            if (!model) return;
            model->m_mediaLib->runOnMLThread<bool>(model.data(),
                [url](vlc_medialibrary_t* ml, bool& registered) {
                    registered = false;
                    if (!url.isLocalFile()) return;
                    const QFileInfo folder(url.toLocalFile());
                    if (!folder.isDir() || !folder.isReadable()) return;
                    ml_unique_ptr<vlc_ml_folder_list_t> roots(vlc_ml_list_entry_points(ml, nullptr));
                    if (!roots) return;
                    for (const auto& root : ml_range_iterate<vlc_ml_folder_t>(roots))
                    {
                        if (root.psz_mrl && url.matches(QUrl::fromEncoded(root.psz_mrl),
                                QUrl::StripTrailingSlash | QUrl::NormalizePathSegments))
                        {
                            registered = true;
                            return;
                        }
                    }
                },
                [model, url](quint64, bool& registered) {
                    if (!model) return;
                    if (registered) emit model->folderAdded(url);
                    else emit model->operationFailed(Add, url);
                }, ML_FOLDER_ADD_QUEUE);
        }, Qt::QueuedConnection);
        return true;
    }
    else if ( event->i_type == VLC_ML_EVENT_ENTRY_POINT_REMOVED && !event->entry_point_removed.b_success )
    {
        emit operationFailed( Remove, QUrl::fromEncoded( event->entry_point_removed.psz_entry_point ) );
        return true;
    }

    return false;
}

void MLFoldersModel::remove( const QUrl &mrl )
{
    //UI data is updated on onMlEvent
    m_mediaLib->runOnMLThread(this,
    //ML thread
    [mrl](vlc_medialibrary_t* ml){
        vlc_ml_remove_folder( ml , qtu( mrl.toString( QUrl::FullyEncoded ) ) );
    },
    ML_FOLDER_ADD_QUEUE);
}

void MLFoldersModel::add(const QUrl &mrl )
{
    //UI data is updated on onMlEvent
    m_mediaLib->runOnMLThread<bool>(this,
    //ML thread
    [mrl](vlc_medialibrary_t* ml, bool& rejected){
        // Registration can succeed before discovery tries to read the folder.
        // Reject unavailable local paths here, without drive I/O on the UI thread.
        rejected = false;
        if (mrl.isLocalFile())
        {
            const QFileInfo folder(mrl.toLocalFile());
            rejected = !folder.isDir() || !folder.isReadable();
            if (rejected) return;
        }
        vlc_ml_add_folder( ml, qtu( mrl.toString( QUrl::FullyEncoded ) ) );
    },
    [this, mrl](quint64, bool& rejected) {
        if (rejected) emit operationFailed(Add, mrl);
    },
    ML_FOLDER_ADD_QUEUE);
}

void MLBannedFoldersModel::remove(const QUrl &mrl)
{
    //UI data is updated on onMlEvent
    m_mediaLib->runOnMLThread(this,
    //ML thread
    [mrl](vlc_medialibrary_t* ml){
        vlc_ml_unban_folder( ml , qtu( mrl.toString( QUrl::FullyEncoded ) ) );
    },
    ML_FOLDER_BAN_QUEUE);
}

void MLBannedFoldersModel::add(const QUrl &mrl)
{
    //UI data is updated on onMlEvent
    m_mediaLib->runOnMLThread(this,
    //ML thread
    [mrl](vlc_medialibrary_t* ml){
        vlc_ml_ban_folder( ml , qtu( mrl.toString( QUrl::FullyEncoded ) ) );
    },
    ML_FOLDER_BAN_QUEUE);
}

void MLBannedFoldersModel::update()
{
    updateImpl(vlc_ml_list_banned_entry_points);
}

bool MLBannedFoldersModel::failed(const vlc_ml_event_t *event) const
{
    if ( event->i_type == VLC_ML_EVENT_ENTRY_POINT_BANNED && !event->entry_point_banned.b_success )
    {
        emit operationFailed( Ban, QUrl::fromEncoded( event->entry_point_banned.psz_entry_point ) );
        return true;
    }
    else if ( event->i_type == VLC_ML_EVENT_ENTRY_POINT_UNBANNED && !event->entry_point_unbanned.b_success )
    {
        emit operationFailed( Unban, QUrl::fromEncoded( event->entry_point_unbanned.psz_entry_point ) );
        return true;
    }

    return false;
}
