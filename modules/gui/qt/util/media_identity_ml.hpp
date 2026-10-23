// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "media_identity.hpp"
#include <vlc_media_library.h>

namespace MediaIdentity {
// Snapshot existing library facts while the caller owns the native media record.
inline QVariantMap libraryMetadata(const vlc_ml_media_t* media, const QUrl& url)
{
    QVariantMap metadata;
    if (!media) return metadata;
    metadata.insert("durationMs", qint64(media->i_duration));
    const QString title = QString::fromUtf8(media->psz_title ? media->psz_title : "");
    const bool episode = media->i_subtype == VLC_ML_MEDIA_SUBTYPE_SHOW_EPISODE;
    if (!episode && !title.isEmpty() && title != url.fileName() && title != QFileInfo(url.fileName()).completeBaseName())
        metadata.insert("title", title);
    if (media->i_subtype == VLC_ML_MEDIA_SUBTYPE_MOVIE) {
        metadata.insert("type", "movie");
        if (media->i_year > 0) metadata.insert("year", media->i_year);
        metadata.insert("imdbId", QString::fromUtf8(media->movie.psz_imdb_id ? media->movie.psz_imdb_id : ""));
    } else if (episode) {
        metadata.insert("type", "episode");
        metadata.insert("season", media->show_episode.i_season_number);
        metadata.insert("episode", media->show_episode.i_episode_nb);
        metadata.insert("tvdbId", QString::fromUtf8(media->show_episode.psz_tvdb_id ? media->show_episode.psz_tvdb_id : ""));
    } else if (media->i_subtype == VLC_ML_MEDIA_SUBTYPE_ALBUMTRACK) {
        metadata.insert("type", "musicTrack");
        if (media->i_year > 0) metadata.insert("year", media->i_year);
        if (media->album_track.i_track_nb > 0) metadata.insert("track", media->album_track.i_track_nb);
        if (media->album_track.i_disc_nb > 0) metadata.insert("disc", media->album_track.i_disc_nb);
    }
    return metadata;
}
} // namespace MediaIdentity
