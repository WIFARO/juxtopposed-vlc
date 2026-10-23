/*****************************************************************************
 * Copyright (C) 2024 VLC authors and VideoLAN
 *
 * Authors: Prince Gupta <guptaprince8832@gmail.com>
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


#include "mlmediastore.hpp"

#include "mlhelper.hpp"
#include "medialib.hpp"
#include "mlmedia.hpp"
#include <QTimer>

static const char *UPDATE_QUEUE = "MLMEDIASTORE_UPDATEQUEUE";

MLMediaStore::~MLMediaStore() = default;

MLMediaStore::Details MLMediaStore::details(vlc_medialibrary_t* ml, const vlc_ml_media_t* media, bool audioMetadata,
                                          QHash<int64_t, QString>* artists, bool identityMetadata)
{
    if (!media) return {};
    Details result {MLMedia(media), {}};
    if (audioMetadata && media->i_type == VLC_ML_MEDIA_TYPE_AUDIO
            && media->i_subtype == VLC_ML_MEDIA_SUBTYPE_ALBUMTRACK && media->album_track.i_artist_id > 0) {
        const auto id = media->album_track.i_artist_id;
        if (artists && artists->contains(id)) result.artist = artists->value(id);
        else {
            ml_unique_ptr<vlc_ml_artist_t> artist(vlc_ml_get_artist(ml, id));
            if (artist) result.artist = qfu(artist->psz_name);
            if (artists) artists->insert(id, result.artist);
        }
    }
    if (identityMetadata && media->i_subtype == VLC_ML_MEDIA_SUBTYPE_ALBUMTRACK && media->album_track.i_album_id > 0) {
        auto metadata = result.media.identityMetadata();
        if (!result.artist.isEmpty()) metadata.insert("artist", result.artist);
        ml_unique_ptr<vlc_ml_album_t> album(vlc_ml_get_album(ml, media->album_track.i_album_id));
        if (album) {
            if (album->psz_title) metadata.insert("album", qfu(album->psz_title));
            if (album->psz_artist) metadata.insert("albumArtist", qfu(album->psz_artist));
        }
        result.media.setIdentityMetadata(metadata);
    }
    return result;
}

void MLMediaStore::notifyMetadata(const QString& mrl, const Details& details)
{
    if (m_readAudioMetadata && m_files.contains(mrl) && details.media.valid()
            && details.media.type() == VLC_ML_MEDIA_TYPE_AUDIO)
        emit audioMetadataUpdated(mrl, details.media.title(), details.artist);
}

MLMediaStore::MLMediaStore(MediaLib *ml, QObject *parent)
    : QObject(parent)
    , m_ml {ml}
    , m_ml_event_handle( nullptr, [this](vlc_ml_event_callback_t* cb )
    {
        assert( m_ml != nullptr );
        m_ml->unregisterEventListener( cb );
    })
{
    m_ml_event_handle.reset
    (
        m_ml->registerEventListener(MLMediaStore::onVlcMlEvent, this)
    );
}

void MLMediaStore::insert(const QString &mrl, bool createMissing)
{
    using Ctx = Details;

    m_files.insert(mrl);
    const auto revision = ++m_revisions[mrl];

    m_ml->runOnMLThread<Ctx>(this,
    //ML thread
    [mrl, createMissing, audioMetadata = m_readAudioMetadata, identityMetadata = m_readIdentityMetadata](vlc_medialibrary_t *ml, Ctx &ctx)
    {
        ml_unique_ptr<vlc_ml_media_t> media {vlc_ml_get_media_by_mrl(ml, qtu(mrl))};
        if (!media && createMissing)
            media.reset(vlc_ml_new_external_media(ml, qtu(mrl)));
        if (media)
            ctx = details(ml, media.get(), audioMetadata, nullptr, identityMetadata);
    },
    //UI thread
    [this, mrl, revision, generation = m_generation](quint64, Ctx &ctx)
    {
        if (generation != m_generation || m_revisions.value(mrl) != revision)
            return;
        if (!ctx.media.valid()) {
            emit updated(mrl, ctx.media);
            return;
        }
        setMedia(mrl, ctx.media);
        notifyMetadata(mrl, ctx);
    });
}

void MLMediaStore::remove(const MLItemId &id)
{
    if (!m_mrls.contains(id))
        return;

    const QString mrl = m_mrls[id];

    m_mrls.remove(id);
    m_files.remove(mrl);
    m_revisions.remove(mrl);
}

void MLMediaStore::insertMany(const QStringList &mrls, std::function<void()> completed)
{
    auto batch = std::make_shared<Batch>();
    batch->generation = m_generation;
    batch->completed = std::move(completed);
    QSet<QString> unique;
    batch->entries.reserve(mrls.size());
    for (const auto& mrl : mrls) {
        if (unique.contains(mrl)) continue;
        unique.insert(mrl);
        m_files.insert(mrl);
        batch->entries.push_back({mrl, ++m_revisions[mrl]});
    }
    // Replace queued work, but let the one in-flight chunk finish before starting
    // another. Rapid searches cannot build up a queue of database jobs.
    cancelBatch();
    m_batch = std::move(batch);
    QTimer::singleShot(0, this, &MLMediaStore::readNextBatch);
}

void MLMediaStore::readNextBatch()
{
    if (m_batchRunning || !m_batch) return;
    const auto batch = m_batch;
    if (batch->offset == batch->entries.size()) {
        m_batch.reset();
        if (batch->completed) batch->completed();
        return;
    }
    const auto chunk = batch->entries.mid(batch->offset, 64);
    m_batchRunning = true;
    m_ml->runOnMLThread<QVector<Details>>(this,
        [batch, chunk, audioMetadata = m_readAudioMetadata](vlc_medialibrary_t* ml, QVector<Details>& result) {
            result.reserve(chunk.size());
            for (const auto& entry : chunk) {
                if (batch->cancelled) break;
                ml_unique_ptr<vlc_ml_media_t> media {vlc_ml_get_media_by_mrl(ml, qtu(entry.mrl))};
                result.push_back(details(ml, media.get(), audioMetadata, &batch->artists));
            }
        },
        [this, batch, chunk](quint64, QVector<Details>& result) {
            m_batchRunning = false;
            if (batch == m_batch && batch->generation == m_generation) {
                for (qsizetype i = 0; i < chunk.size(); ++i) {
                    if (batch != m_batch || batch->generation != m_generation) break;
                    const auto& entry = chunk[i];
                    if (m_revisions.value(entry.mrl) != entry.revision) continue;
                    if (result[i].media.valid()) setMedia(entry.mrl, result[i].media);
                    else emit updated(entry.mrl, result[i].media);
                    notifyMetadata(entry.mrl, result[i]);
                }
                batch->offset += chunk.size();
            }
            QTimer::singleShot(0, this, &MLMediaStore::readNextBatch);
        });
}

void MLMediaStore::cancelBatch()
{
    if (m_batch) m_batch->cancelled = true;
    m_batch.reset();
}

void MLMediaStore::clear()
{
    ++m_generation;
    cancelBatch();
    m_files.clear();
    m_mrls.clear();
    m_revisions.clear();
}

void MLMediaStore::onVlcMlEvent(void *data, const vlc_ml_event_t *event)
{
    auto self = static_cast<MLMediaStore*>(data);
    switch (event->i_type)
    {
    case VLC_ML_EVENT_MEDIA_ADDED:
    case VLC_ML_EVENT_MEDIA_THUMBNAIL_GENERATED:
    {
        if (event->i_type == VLC_ML_EVENT_MEDIA_THUMBNAIL_GENERATED
                && event->media_thumbnail_generated.i_size != VLC_ML_THUMBNAIL_SMALL)
            break;
        const vlc_ml_media_t *media = event->i_type == VLC_ML_EVENT_MEDIA_ADDED
                ? event->creation.p_media : event->media_thumbnail_generated.p_media;
        QString mrl;
        for (const vlc_ml_file_t &file : ml_range_iterate<vlc_ml_file_t>(media->p_files))
        {
            if (file.i_type == VLC_ML_FILE_TYPE_MAIN)
            {
                mrl = QString::fromUtf8(file.psz_mrl);
                break;
            }
        }

        if (mrl.isEmpty())
            break;

        MLMedia mlMedia (media);
        QMetaObject::invokeMethod(self, [self, mrl, mlMedia]()
        {
            if (self->m_readAudioMetadata && self->m_files.contains(mrl)
                    && mlMedia.type() == VLC_ML_MEDIA_TYPE_AUDIO)
                self->insert(mrl);
            else self->setMedia(mrl, mlMedia);
        });

        break;
    }

    case VLC_ML_EVENT_MEDIA_UPDATED:
    {
        const MLItemId id(event->modification.i_entity_id, VLC_ML_PARENT_UNKNOWN);
        QMetaObject::invokeMethod(self, [self, id] () mutable
        {
            self->update(id);
        });

        break;
    }

    case VLC_ML_EVENT_MEDIA_DELETED:
    {
        const MLItemId id{ event->deletion.i_entity_id, VLC_ML_PARENT_UNKNOWN };
        QMetaObject::invokeMethod(self, [self, id]()
        {
            self->remove(id);
        });

        break;
    }

    default:
        break;
    }
}

void MLMediaStore::update(const MLItemId &id)
{
    if (!m_mrls.contains(id))
        return;

    using Ctx = Details;

    const auto mrl = m_mrls[id];
    const auto revision = ++m_revisions[mrl];

    m_ml->runOnMLThread<Ctx>(this,
    //ML thread
    [id, audioMetadata = m_readAudioMetadata, identityMetadata = m_readIdentityMetadata](vlc_medialibrary_t *ml, Ctx &ctx)
    {
        ml_unique_ptr<vlc_ml_media_t> media {vlc_ml_get_media(ml, id.id)};
        ctx = details(ml, media.get(), audioMetadata, nullptr, identityMetadata);
    },
    //UI thread
    [this, id, mrl, revision, generation = m_generation](quint64, Ctx &ctx)
    {
        if (generation != m_generation || m_revisions.value(mrl) != revision || !m_mrls.contains(id))
            return; // item was removed?

        emit updated(mrl, ctx.media);
        notifyMetadata(mrl, ctx);
    }, UPDATE_QUEUE); // update in a single queue in case there
                      // is a overlap of same media update
}

void MLMediaStore::setMedia(const QString &mrl, MLMedia media)
{
    if (!m_files.contains(mrl))
        return;

    // A newer event must win over a lookup started before that event.
    ++m_revisions[mrl];
    m_mrls[media.getId()] = mrl;
    emit updated(mrl, media);
}
