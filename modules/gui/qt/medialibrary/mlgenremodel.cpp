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

#include "mlgenremodel.hpp"

// Util includes
#include "util/covergenerator.hpp"

// MediaLibrary includes
#include "mlcustomcover.hpp"
#include <QUrlQuery>
#include "mlartistmodel.hpp"

//-------------------------------------------------------------------------------------------------
// Static variables

// NOTE: We multiply by 3 to cover most dpi settings.
static const int MLGENREMODEL_COVER_WIDTH  = 260 * 3;
static const int MLGENREMODEL_COVER_HEIGHT = 130 * 3;

static const int MLGENREMODEL_COVER_COUNTX = 4;
static const int MLGENREMODEL_COVER_COUNTY = 2;

static const int MLGENREMODEL_COVER_BLUR = 4;

//-------------------------------------------------------------------------------------------------

MLGenreModel::MLGenreModel(QObject *parent)
    : MLBaseModel(parent)
{
    m_coverRefreshTimer.setSingleShot(true);
    m_coverRefreshTimer.setInterval(100);
    connect(&m_coverRefreshTimer, &QTimer::timeout, this, &MLGenreModel::refreshCovers);
}

QVariant MLGenreModel::itemRoleData(const MLItem *item, const int role) const
{
    auto ml_genre = static_cast<const MLGenre *>(item);
    if (!ml_genre)
        return QVariant();

    switch (role)
    {
        // Genres
    case GENRE_ID:
        return QVariant::fromValue( ml_genre->getId() );
    case GENRE_NAME:
        return QVariant::fromValue( ml_genre->getName() );
    case GENRE_NB_TRACKS:
        return QVariant::fromValue( ml_genre->getNbTracks() );
    case GENRE_COVER:
        return getCover(ml_genre, MLGENREMODEL_COVER_BLUR);
    case GENRE_COVER_NONBLUR:
        return getCover(ml_genre, 0);
    default :
        return QVariant();
    }
}

QHash<int, QByteArray> MLGenreModel::roleNames() const
{
    return {
        { GENRE_ID, QByteArrayLiteral("id") },
        { GENRE_NAME, QByteArrayLiteral("name") },
        { GENRE_NB_TRACKS, QByteArrayLiteral("nb_tracks") },
        { GENRE_ARTISTS, QByteArrayLiteral("artists") },
        { GENRE_TRACKS, QByteArrayLiteral("tracks") },
        { GENRE_ALBUMS, QByteArrayLiteral("albums") },
        { GENRE_COVER, QByteArrayLiteral("cover") },
        { GENRE_COVER_NONBLUR, QByteArrayLiteral("coverNonBlur") }
    };
}


QString MLGenreModel::getCoverDefault() const
{
    return m_coverDefault;
}

void MLGenreModel::setCoverDefault(const QString& defaultCover)
{
    if (m_coverDefault == defaultCover)
        return;
    m_coverDefault = defaultCover;
    emit coverDefaultChanged();
    refreshCovers();
}

void MLGenreModel::setCoverDefaultSingle(bool single)
{
    if (m_coverDefaultSingle == single) return;
    m_coverDefaultSingle = single;
    emit coverDefaultSingleChanged();
    refreshCovers();
}

void MLGenreModel::refreshCovers()
{
    ++m_coverRevision;
    const int rows = rowCount();
    if (rows > 0)
        emit dataChanged(index(0), index(rows - 1), {GENRE_COVER, GENRE_COVER_NONBLUR});
}

void MLGenreModel::onVlcMlEvent(const MLEvent &event)
{
    switch (event.i_type)
    {
        case VLC_ML_EVENT_MEDIA_THUMBNAIL_GENERATED:
            if (event.media_thumbnail_generated.b_success
                && event.media_thumbnail_generated.i_size == VLC_ML_THUMBNAIL_SMALL
                && event.media_thumbnail_generated.i_media_type == VLC_ML_MEDIA_TYPE_AUDIO)
                m_coverRefreshTimer.start();
            break;
        case VLC_ML_EVENT_ALBUM_UPDATED:
            m_coverRefreshTimer.start();
            break;
        case VLC_ML_EVENT_GENRE_ADDED:
            emit resetRequested();
            return;
        case VLC_ML_EVENT_GENRE_UPDATED:
        {
            MLItemId itemId(event.modification.i_entity_id, VLC_ML_PARENT_UNKNOWN);
            updateItemInCache(itemId);
            return;
        }
        case VLC_ML_EVENT_GENRE_DELETED:
        {
            MLItemId itemId(event.deletion.i_entity_id, VLC_ML_PARENT_UNKNOWN);
            deleteItemInCache(itemId);
            return;
        }
    }

    MLBaseModel::onVlcMlEvent(event);
}

vlc_ml_sorting_criteria_t MLGenreModel::nameToCriteria(QByteArray name) const
{
    return QHash<QByteArray, vlc_ml_sorting_criteria_t> {
        {QByteArrayLiteral("name"), VLC_ML_SORTING_ALPHA},
    }.value(name, VLC_ML_SORTING_DEFAULT);
}

QString MLGenreModel::getCover(const MLGenre * genre, int blur) const
{
    QUrl url(MLCustomCover::url(genre->getId()
                            , QSize(MLGENREMODEL_COVER_WIDTH, MLGENREMODEL_COVER_HEIGHT)
                            , m_coverDefault
                            , MLGENREMODEL_COVER_COUNTX
                            , MLGENREMODEL_COVER_COUNTY
                            , blur
                            , true, m_coverDefaultSingle));
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("revision"), QString::number(m_coverRevision));
    url.setQuery(query);
    return url.toString();
}

//-------------------------------------------------------------------------------------------------

std::unique_ptr<MLListCacheLoader>
MLGenreModel::createMLLoader() const
{
    return std::make_unique<MLListCacheLoader>(m_mediaLib, std::make_shared<MLGenreModel::Loader>(*this));
}

size_t MLGenreModel::Loader::count(vlc_medialibrary_t* ml, const vlc_ml_query_params_t* queryParams) const
{
    return vlc_ml_count_genres( ml, queryParams );
}

std::vector<std::unique_ptr<MLItem>>
MLGenreModel::Loader::load(vlc_medialibrary_t* ml, const vlc_ml_query_params_t* queryParams) const
{
    ml_unique_ptr<vlc_ml_genre_list_t> genre_list(
        vlc_ml_list_genres(ml, queryParams)
    );
    if ( genre_list == nullptr )
        return {};
    std::vector<std::unique_ptr<MLItem>> res;
    for( const vlc_ml_genre_t& genre: ml_range_iterate<vlc_ml_genre_t>( genre_list ) )
        res.emplace_back( std::make_unique<MLGenre>( &genre ) );
    return res;

}

std::unique_ptr<MLItem>
MLGenreModel::Loader::loadItemById(vlc_medialibrary_t* ml, MLItemId itemId) const
{
    assert(itemId.type == VLC_ML_PARENT_GENRE);
    ml_unique_ptr<vlc_ml_genre_t> genre(vlc_ml_get_genre(ml, itemId.id));
    if (!genre)
        return nullptr;
    return std::make_unique<MLGenre>(genre.get());
}
