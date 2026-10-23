/*****************************************************************************
 * medialibrary.c: test for the medialibrary code
 *****************************************************************************
 * Copyright (C) 2023 Videolabs
 *
 * Authors: Alexandre Janniaux <ajanni@videolabs.io>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

/* Define a builtin module for mocked parts */
#define MODULE_NAME test_misc_medialibrary
#undef VLC_DYNAMIC_PLUGIN

#include "../../libvlc/test.h"

#include <vlc/vlc.h>

#include <vlc_common.h>
#include <vlc_image.h>
#include <vlc_picture.h>
#include <vlc_plugin.h>
#include <vlc_block.h>
#include <vlc_codec.h>
#include <vlc_filter.h>

#include <vlc_media_library.h>
#include <vlc_player.h>
#include <vlc_url.h>
#include <vlc_preparser.h>
#include <vlc_configuration.h>
#include <vlc_variables.h>
#include <ftw.h>

#include "../lib/libvlc_internal.h"

const char vlc_module_name[] = MODULE_STRING;

static int exitcode = 0;

static void TrackListChanged(vlc_player_t *player, enum vlc_player_list_action action,
                              const struct vlc_player_track *track, void *data)
{
    (void)player; (void)action; (void)track;
    vlc_cond_signal(data);
}

static void PlayerStateChanged(vlc_player_t *player, enum vlc_player_state state, void *data)
{
    (void)player; (void)state;
    vlc_cond_signal(data);
}

static void TestAssociatedTracks(vlc_object_t *root, vlc_medialibrary_t *ml,
                                 const vlc_ml_media_t *media, const char *mrl)
{
    char *directory = config_GetUserDir(VLC_USERDATA_DIR);
    char *path;
    assert(asprintf(&path, "%s/aux captions & accents.srt", directory) >= 0);
    free(directory);
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs("1\n00:00:00,000 --> 00:01:00,000\nAssociated subtitle\n", file);
    assert(fclose(file) == 0);
    char *subtitle = vlc_path2uri(path, NULL);
    free(path);
    assert(subtitle);
    const char *audio = "mock://audio_track_count=1;length=100000000";
    assert(vlc_ml_media_add_external_mrl(ml, media->i_id, subtitle,
                                        VLC_ML_FILE_TYPE_SUBTITLE) == VLC_SUCCESS);
    assert(vlc_ml_media_add_external_mrl(ml, media->i_id, audio,
                                        VLC_ML_FILE_TYPE_SOUNDTRACK) == VLC_SUCCESS);
    /* A secondary file must not resolve to its parent when opened on its own. */
    assert(vlc_ml_get_media_by_mrl(ml, subtitle) == NULL);
    assert(vlc_ml_get_media_by_mrl(ml, audio) == NULL);
    vlc_ml_media_t *standalone = vlc_ml_new_external_media(ml, audio);
    assert(standalone);
    vlc_ml_media_t *lookup = vlc_ml_get_media_by_mrl(ml, audio);
    assert(lookup && lookup->i_id == standalone->i_id);
    vlc_ml_media_release(lookup);
    assert(vlc_ml_media_add_external_mrl(ml, standalone->i_id, subtitle,
                                        VLC_ML_FILE_TYPE_SUBTITLE) == VLC_SUCCESS);
    vlc_ml_media_release(standalone);
    assert(vlc_ml_media_add_external_mrl(ml, media->i_id, "file:///part.mkv",
                                        VLC_ML_FILE_TYPE_PART) == VLC_SUCCESS);
    lookup = vlc_ml_get_media_by_mrl(ml, "file:///part.mkv");
    assert(lookup && lookup->i_id == media->i_id);
    vlc_ml_media_release(lookup);

    /* Track discovery does not need a desktop window. */
    var_Create(root, "video", VLC_VAR_BOOL);
    var_SetBool(root, "video", false);
    vlc_player_t *player = vlc_player_New(root, VLC_PLAYER_LOCK_NORMAL);
    assert(player);
    vlc_cond_t changed;
    vlc_cond_init(&changed);
    const struct vlc_player_cbs callbacks = {
        .on_track_list_changed = TrackListChanged,
        .on_state_changed = PlayerStateChanged,
    };
    input_item_t *item = input_item_New(mrl, "Associated tracks");
    assert(item);
    vlc_player_Lock(player);
    vlc_player_listener_id *listener = vlc_player_AddListener(player, &callbacks, &changed);
    assert(listener);
    for (unsigned attempt = 0; attempt < 2; ++attempt)
    {
        assert(vlc_player_SetCurrentMedia(player, item) == VLC_SUCCESS);
        assert(vlc_player_Start(player) == VLC_SUCCESS);
        while (vlc_player_GetState(player) != VLC_PLAYER_STATE_PLAYING ||
               vlc_player_GetTrackCount(player, AUDIO_ES) < 1 ||
               vlc_player_GetTrackCount(player, SPU_ES) < 1)
            vlc_player_CondWait(player, &changed);
        assert(vlc_player_GetTrackCount(player, AUDIO_ES) == 1);
        assert(vlc_player_GetTrackCount(player, SPU_ES) == 1);
        vlc_player_Stop(player);
        while (vlc_player_GetState(player) != VLC_PLAYER_STATE_STOPPED)
            vlc_player_CondWait(player, &changed);
    }
    vlc_player_RemoveListener(player, listener);
    vlc_player_Unlock(player);
    vlc_player_Delete(player);
    input_item_Release(item);
    free(subtitle);
}

static void ValidateThumbnail(void *data, const vlc_ml_event_t *event)
{
    if (event->i_type != VLC_ML_EVENT_MEDIA_THUMBNAIL_GENERATED)
        return;

    assert(event->media_thumbnail_generated.b_success);
    vlc_sem_t *sem = data;
    vlc_sem_post(sem);
}

struct ManualThumbnail {
    vlc_sem_t seen;
    int64_t media_id;
    const char *mrl;
};

static void ValidateManualThumbnail(void *data, const vlc_ml_event_t *event)
{
    struct ManualThumbnail *expected = data;
    if (event->i_type != VLC_ML_EVENT_MEDIA_THUMBNAIL_GENERATED
        || event->media_thumbnail_generated.p_media->i_id != expected->media_id
        || event->media_thumbnail_generated.i_size != VLC_ML_THUMBNAIL_BANNER)
        return;
    assert(event->media_thumbnail_generated.b_success);
    const vlc_ml_thumbnail_t *thumbnail =
        &event->media_thumbnail_generated.p_media->thumbnails[VLC_ML_THUMBNAIL_BANNER];
    assert(thumbnail->i_status == VLC_ML_THUMBNAIL_STATUS_AVAILABLE);
    assert(thumbnail->psz_mrl && !strcmp(thumbnail->psz_mrl, expected->mrl));
    vlc_sem_post(&expected->seen);
}

static void TestManualThumbnail(vlc_medialibrary_t *ml, int64_t media_id)
{
    vlc_ml_media_t *media = vlc_ml_get_media(ml, media_id);
    assert(media);
    assert(media->thumbnails[VLC_ML_THUMBNAIL_SMALL].i_status == VLC_ML_THUMBNAIL_STATUS_AVAILABLE);
    struct ManualThumbnail expected = {
        .media_id = media_id,
        .mrl = media->thumbnails[VLC_ML_THUMBNAIL_SMALL].psz_mrl,
    };
    vlc_sem_init(&expected.seen, 0);
    vlc_ml_event_callback_t *listener =
        vlc_ml_event_register_callback(ml, ValidateManualThumbnail, &expected);
    assert(vlc_ml_media_set_thumbnail(ml, media_id, expected.mrl,
                                     (vlc_ml_thumbnail_size_t)-1) == VLC_EGENERIC);
    assert(vlc_ml_media_set_thumbnail(ml, media_id, expected.mrl,
                                     VLC_ML_THUMBNAIL_SIZE_COUNT) == VLC_EGENERIC);
    assert(vlc_ml_media_set_thumbnail(ml, media_id, expected.mrl,
                                     VLC_ML_THUMBNAIL_BANNER) == VLC_SUCCESS);
    assert(vlc_sem_timedwait(&expected.seen, vlc_tick_now() + VLC_TICK_FROM_SEC(2)) == 0);
    vlc_ml_event_unregister_callback(ml, listener);
    vlc_ml_media_release(media);
}

static void TestBookmarks(vlc_medialibrary_t *ml, int64_t media_id)
{
    assert(vlc_ml_media_add_bookmark(ml, media_id, 42023) == VLC_SUCCESS);
    assert(vlc_ml_media_update_bookmark(ml, media_id, 42023, "DON'T BLINK", NULL) == VLC_SUCCESS);
    vlc_ml_query_params_t params = vlc_ml_query_params_create();
    vlc_ml_bookmark_list_t *bookmarks = vlc_ml_list_media_bookmarks(ml, &params, media_id);
    assert(bookmarks && bookmarks->i_nb_items == 1);
    assert(bookmarks->p_items[0].i_time == 42023);
    assert(!strcmp(bookmarks->p_items[0].psz_name, "DON'T BLINK"));
    vlc_ml_release(bookmarks);
    assert(vlc_ml_media_remove_bookmark(ml, media_id, 42023) == VLC_SUCCESS);
    bookmarks = vlc_ml_list_media_bookmarks(ml, &params, media_id);
    assert(bookmarks && bookmarks->i_nb_items == 0);
    vlc_ml_release(bookmarks);
}

struct resume_test
{
    vlc_cond_t changed;
    unsigned queries;
    unsigned repeats;
    unsigned completed;
    bool restore;
    bool finish;
    double position;
};

static void ResumeStateChanged(vlc_player_t *player,
                               enum vlc_player_state state, void *data)
{
    (void)player; (void)state;
    struct resume_test *test = data;
    vlc_cond_signal(&test->changed);
}

static void ResumePositionChanged(vlc_player_t *player, vlc_tick_t time,
                                  double position, void *data)
{
    (void)player; (void)time;
    struct resume_test *test = data;
    test->position = position;
    vlc_cond_signal(&test->changed);
}

static void ResumeMediaChanged(vlc_player_t *player, input_item_t *item,
                               void *data)
{
    struct resume_test *test = data;
    /* Finish several loops quickly while retaining a realistic clip length. */
    if (item && test->finish)
        vlc_player_SetPosition(player, .97);
}

static void ResumeQueried(vlc_player_t *player, void *data)
{
    struct resume_test *test = data;
    ++test->queries;
    if (test->restore)
        vlc_player_RestorePlaybackPos(player);
}

static void ResumeStopping(vlc_player_t *player, input_item_t *item,
                           enum vlc_player_media_stopping_reason reason,
                           void *data)
{
    struct resume_test *test = data;
    if (reason == VLC_PLAYER_MEDIA_STOPPING_EOS)
    {
        ++test->completed;
        if (test->repeats > 0)
        {
            --test->repeats;
            vlc_player_SetNextMedia(player, item);
        }
    }
}

static void ResumeOpen(vlc_player_t *player, struct resume_test *test,
                       input_item_t *item, unsigned queries)
{
    test->queries = 0;
    test->position = -1.;
    /* Keep the video track for media classification, without a decoder or
     * read-ahead that would advance the reported position after a seek. */
    input_item_AddOption(item, ":no-video", VLC_INPUT_OPTION_TRUSTED);
    input_item_AddOption(item, ":mock-pts_delay=0", VLC_INPUT_OPTION_TRUSTED);
    assert(vlc_player_SetCurrentMedia(player, item) == VLC_SUCCESS);
    assert(vlc_player_Start(player) == VLC_SUCCESS);
    while (vlc_player_GetState(player) != VLC_PLAYER_STATE_PAUSED)
        vlc_player_CondWait(player, &test->changed);
    assert(test->queries == queries);
}

static void ResumeWaitPosition(vlc_player_t *player, struct resume_test *test,
                               double position)
{
    while (test->position < position - .000001 ||
           test->position > position + .000001)
        vlc_player_CondWait(player, &test->changed);
}

static void ResumeStop(vlc_player_t *player, struct resume_test *test)
{
    vlc_player_Stop(player);
    while (vlc_player_GetState(player) != VLC_PLAYER_STATE_STOPPED)
        vlc_player_CondWait(player, &test->changed);
}

static void ResumeCheckProgress(vlc_medialibrary_t *ml, const char *mrl,
                                float expected)
{
    vlc_ml_media_t *media = vlc_ml_get_media_by_mrl(ml, mrl);
    assert(media && media->i_duration <= 0);
    assert(media->f_progress >= expected - .000001f &&
           media->f_progress <= expected + .000001f);
    vlc_ml_media_release(media);
}

static void TestResumeProgress(vlc_object_t *root, vlc_medialibrary_t *ml)
{
    /* Only a player parented directly to the instance saves playback states. */
    vlc_player_t *player = vlc_player_New(VLC_OBJECT(vlc_object_instance(root)),
                                         VLC_PLAYER_LOCK_NORMAL);
    assert(player);
    struct resume_test test = {0};
    vlc_cond_init(&test.changed);
    const struct vlc_player_cbs callbacks = {
        .on_state_changed = ResumeStateChanged,
        .on_position_changed = ResumePositionChanged,
        .on_current_media_changed = ResumeMediaChanged,
        .on_playback_restore_queried = ResumeQueried,
        .on_stopping_current_media = ResumeStopping,
    };
    vlc_player_Lock(player);
    vlc_player_listener_id *listener =
        vlc_player_AddListener(player, &callbacks, &test);
    assert(listener);
    vlc_player_SetStartPaused(player, true);

    /* Short frames and no read-ahead keep paused seeks accurate without
     * spending time playing through these synthetic media. */
    const char *mrl = "mock://video_track_count=1;video_width=16;"
                      "video_height=16;video_frame_rate=100000;pts_delay=0;"
                      "length=33000000";
    input_item_t *item = input_item_New(mrl, "Resume progress");
    assert(item);
    ResumeOpen(player, &test, item, 0);
    vlc_player_SetPosition(player, .23);
    ResumeWaitPosition(player, &test, .23);
    ResumeStop(player, &test);
    ResumeCheckProgress(ml, mrl, .23f);

    /* Reopening mid-file asks once, and Continue restores the saved position. */
    test.restore = true;
    ResumeOpen(player, &test, item, 1);
    ResumeWaitPosition(player, &test, .23);
    test.restore = false;
    vlc_player_SetPosition(player, .99);
    ResumeWaitPosition(player, &test, .99);
    ResumeStop(player, &test);
    ResumeCheckProgress(ml, mrl, -1.f);

    /* A completed file no longer prompts; restarting near the beginning also
     * clears any old resume point. */
    ResumeOpen(player, &test, item, 0);
    vlc_player_SetPosition(player, .01);
    ResumeWaitPosition(player, &test, .01);
    ResumeStop(player, &test);
    ResumeCheckProgress(ml, mrl, -1.f);
    input_item_Release(item);

    /* The strict cutoffs retain positions exactly on each margin, including
     * float-rounded positions that used to differ from the library policy. */
    const struct { unsigned seconds; float margin; } boundaries[] = {
        {1800, .05f}, {3600, .04f}, {7200, .03f},
        {10800, .02f}, {14400, .01f},
    };
    for (size_t i = 0; i < ARRAY_SIZE(boundaries); ++i)
    {
        char *uri;
        vlc_tick_t length = VLC_TICK_FROM_SEC(boundaries[i].seconds);
        assert(asprintf(&uri, "mock://video_track_count=1;video_width=16;"
                        "video_height=16;video_frame_rate=100000;pts_delay=0;"
                        "length=%"PRId64, length) >= 0);
        item = input_item_New(uri, "Resume boundary");
        assert(item);
        const float positions[] = {
            boundaries[i].margin, 1.f - boundaries[i].margin,
        };
        for (size_t j = 0; j < ARRAY_SIZE(positions); ++j)
        {
            ResumeOpen(player, &test, item, j == 0 ? 0 : 1);
            vlc_player_SetPosition(player, positions[j]);
            ResumeWaitPosition(player, &test, positions[j]);
            ResumeStop(player, &test);
            ResumeCheckProgress(ml, uri, positions[j]);
        }
        input_item_Release(item);
        free(uri);
    }

    /* Repeat through new player inputs, as the playlist does, rather than
     * repeating inside a single input (which never restores library state). */
    const struct {
        const char *mrl;
        unsigned loops;
        bool finish;
        bool unknown_length;
    } repeats[] = {
        {"mock://video_track_count=1;video_width=16;video_height=16;"
         "video_frame_rate=100;length=33000000", 3, true, false},
        /* Play the entire short clip: the last periodic position update can
         * fall outside the end margin, even though playback reaches EOF. */
        {"mock://video_track_count=1;video_width=16;video_height=16;"
         "video_frame_rate=100;length=1000000", 2, false, false},
        /* Completion also clears progress when neither the library nor the
         * player knows the duration. */
        {"mock://video_track_count=1;video_width=16;video_height=16;"
         "video_frame_rate=100;length=250000;report_length=false", 2, false, true},
    };
    for (size_t i = 0; i < ARRAY_SIZE(repeats); ++i)
    {
        /* Each scenario has its own output clock; repeats within it still
         * open successive inputs on the same player. */
        vlc_player_RemoveListener(player, listener);
        vlc_player_Unlock(player);
        vlc_player_Delete(player);
        player = vlc_player_New(VLC_OBJECT(vlc_object_instance(root)),
                                VLC_PLAYER_LOCK_NORMAL);
        assert(player);
        vlc_player_Lock(player);
        listener = vlc_player_AddListener(player, &callbacks, &test);
        assert(listener);
        mrl = repeats[i].mrl;
        item = input_item_New(mrl, "Repeat resume progress");
        assert(item);
        test.queries = test.completed = 0;
        test.repeats = repeats[i].loops - 1;
        test.finish = repeats[i].finish;
        vlc_player_SetStartPaused(player, false);
        assert(vlc_player_SetCurrentMedia(player, item) == VLC_SUCCESS);
        assert(vlc_player_Start(player) == VLC_SUCCESS);
        while (test.completed < repeats[i].loops ||
               vlc_player_GetState(player) != VLC_PLAYER_STATE_STOPPED)
            vlc_player_CondWait(player, &test.changed);
        assert(test.completed == repeats[i].loops && test.repeats == 0);
        assert(test.queries == 0);
        ResumeCheckProgress(ml, mrl, -1.f);
        test.finish = false;
        vlc_player_SetStartPaused(player, true);
        ResumeOpen(player, &test, item, 0);
        /* A new input interrupted mid-file must still save a resume point,
         * even when the preceding input of this media completed normally. */
        vlc_player_SetPosition(player, .4);
        ResumeWaitPosition(player, &test, .4);
        if (repeats[i].unknown_length)
            assert(vlc_player_GetLength(player) == VLC_TICK_INVALID);
        ResumeStop(player, &test);
        ResumeCheckProgress(ml, mrl, .4f);
        ResumeOpen(player, &test, item, 1);
        ResumeStop(player, &test);
        input_item_Release(item);
    }

    // Audio podcasts use the same Ask/Always resume flow as video.
    item = input_item_New("mock://audio_track_count=1;video_track_count=0;length=100000000;pts_delay=0", "Audio episode");
    assert(item);
    input_item_SetMetaExtra(item, "podcast:feed", "https://example.test/audio-feed.xml");
    input_item_SetMetaExtra(item, "podcast:guid", "audio-resume");
    input_item_SetDuration(item, VLC_TICK_FROM_SEC(100));
    test.queries = 0; test.restore = false;
    ResumeOpen(player, &test, item, 0);
    vlc_player_SetPosition(player, .37);
    ResumeWaitPosition(player, &test, .37);
    ResumeStop(player, &test);
    test.restore = true;
    ResumeOpen(player, &test, item, 1);
    ResumeWaitPosition(player, &test, .37);
    ResumeStop(player, &test);
    input_item_t *direct = input_item_New(item->psz_uri, "Direct episode URL");
    assert(direct);
    ResumeOpen(player, &test, direct, 1);
    ResumeWaitPosition(player, &test, .37);
    ResumeStop(player, &test);
    input_item_Release(direct);
    input_item_Release(item);

    vlc_player_RemoveListener(player, listener);
    vlc_player_Unlock(player);
    vlc_player_Delete(player);
}

static void TestPodcastState(vlc_medialibrary_t *ml)
{
    input_item_t *item = input_item_New("https://example.test/audio.mp3?token=old", "Episode");
    input_item_SetMetaExtra(item, "podcast:feed", "https://example.test/feed.xml");
    input_item_SetMetaExtra(item, "podcast:guid", "stable-episode-guid");
    input_item_SetMetaExtra(item, "podcast:showGuid", "stable-show-guid");
    input_item_SetShowName(item, "Example show");
    input_item_SetSeason(item, "4");
    input_item_SetEpisode(item, "8");
    input_item_SetDescription(item, "A description with = and % and \n a newline.");
    input_item_SetDuration(item, VLC_TICK_FROM_SEC(100));
    vlc_ml_media_t *media = NULL;
    assert(vlc_ml_control(ml, VLC_ML_REGISTER_PODCAST, item, &media) == VLC_SUCCESS);
    assert(media && media->i_duration == 100000 && media->p_podcast);
    const int64_t id = media->i_id;
    vlc_ml_release(media);
    assert(vlc_ml_control(ml, VLC_ML_MEDIA_SAVE_PODCAST_PROGRESS, id, .98, 0) == VLC_SUCCESS);
    media = vlc_ml_get_media(ml, id);
    assert(media && fabs(media->f_progress - .98f) < .00001f && media->i_playcount == 0);
    vlc_ml_release(media);
    // A changed enclosure and feed address retain progress through the GUIDs.
    input_item_SetURI(item, "https://example.test/audio.mp3?token=new");
    input_item_SetMetaExtra(item, "podcast:feed", "https://example.test/new-feed.xml");
    input_item_SetDuration(item, VLC_TICK_FROM_SEC(200));
    assert(vlc_ml_control(ml, VLC_ML_REGISTER_PODCAST, item, &media) == VLC_SUCCESS);
    assert(media && media->i_id == id && fabs(media->f_progress - .49f) < .00001f);
    vlc_ml_release(media);
    input_item_t *saved = vlc_ml_get_input_item(ml, id);
    assert(saved && !strcmp(saved->psz_uri, item->psz_uri));
    char *value = input_item_GetShowName(saved);
    assert(value && !strcmp(value, "Example show")); free(value);
    value = input_item_GetDescription(saved);
    assert(value && !strcmp(value, "A description with = and % and \n a newline.")); free(value);
    input_item_Release(saved);
    assert(vlc_ml_control(ml, VLC_ML_MEDIA_SAVE_PODCAST_PROGRESS, id, 1., 1) == VLC_SUCCESS);
    media = vlc_ml_get_media(ml, id);
    assert(media && media->f_progress == 0 && media->i_playcount == 1);
    vlc_ml_release(media);
    assert(vlc_ml_media_set_played(ml, id, false) == VLC_SUCCESS);
    media = vlc_ml_get_media(ml, id);
    assert(media && media->f_progress == 0 && media->i_playcount == 0);
    vlc_ml_release(media);
    assert(vlc_ml_control(ml, VLC_ML_MEDIA_SAVE_PODCAST_PROGRESS, id, .42, 0) == VLC_SUCCESS);
    assert(vlc_ml_clear_history(ml, VLC_ML_HISTORY_TYPE_GLOBAL) == VLC_SUCCESS);
    media = vlc_ml_get_media(ml, id);
    assert(media && media->f_progress == 0 && media->i_playcount == 0);
    vlc_ml_release(media);
    input_item_Release(item);
}

struct PodcastParse { vlc_sem_t done; input_item_node_t *tree; int status; };
static void PodcastSubtree(vlc_preparser_req *req, input_item_node_t *tree, void *opaque)
{
    (void)req;
    struct PodcastParse *parse = opaque;
    if (parse->tree) input_item_node_Delete(parse->tree);
    parse->tree = tree;
}
static void PodcastParsed(vlc_preparser_req *req, int status, void *opaque)
{
    (void)req;
    struct PodcastParse *parse = opaque;
    parse->status = status;
    vlc_sem_post(&parse->done);
}
static void TestPodcastFeed(vlc_object_t *root)
{
    const char xml[] = "<?xml version='1.0'?><rss version='2.0' xmlns:apple='http://www.itunes.com/dtds/podcast-1.0.dtd' xmlns:p='https://podcastindex.org/namespace/1.0'><channel>"
        "<title>Example show</title><apple:author>Host</apple:author><p:guid>show-guid</p:guid>"
        "<apple:image href='https://example.test/show.png'/><item><title>First</title>"
        "<guid isPermaLink='false'>first-guid</guid><description><![CDATA[<p>Show notes</p>]]></description>"
        "<apple:duration>1451</apple:duration><apple:season>42</apple:season><apple:episode>47</apple:episode>"
        "<apple:episodeType>full</apple:episodeType><apple:explicit>true</apple:explicit>"
        "<apple:image href='https://example.test/episode.png'/><enclosure url='https://example.test/audio.mp3' type='audio/mpeg' length='100'/>"
        "<p:alternateEnclosure type='application/vnd.apple.mpegurl'><p:source uri='https://example.test/video.m3u8'/></p:alternateEnclosure>"
        "</item><item><title>Second</title><guid>second-guid</guid><apple:duration>1:02:03</apple:duration>"
        "<enclosure url='https://example.test/second.mp3' type='audio/mpeg' length='50'/></item></channel></rss>";
    char path[] = "/tmp/vlc-podcast-XXXXXX";
    const int fd = mkstemp(path);
    assert(fd >= 0 && write(fd, xml, sizeof(xml) - 1) == sizeof(xml) - 1);
    close(fd);
    char *uri = vlc_path2uri(path, NULL);
    input_item_t *item = input_item_New(uri, "Feed"); free(uri);
    input_item_AddOption(item, "demux=podcast", VLC_INPUT_OPTION_TRUSTED);
    const struct vlc_preparser_cfg cfg = {.types = VLC_PREPARSER_TYPE_PARSE, .max_parser_threads = 1, .timeout = VLC_TICK_FROM_SEC(5)};
    vlc_preparser_t *parser = vlc_preparser_New(root, &cfg);
    assert(parser);
    struct PodcastParse parse = {.status = VLC_EGENERIC}; vlc_sem_init(&parse.done, 0);
    const struct vlc_preparser_cbs callbacks = {.on_ended = PodcastParsed, .on_subtree_added = PodcastSubtree};
    vlc_preparser_req *req = vlc_preparser_req_NewParse(parser, item, VLC_PREPARSER_TYPE_PARSE | VLC_PREPARSER_OPTION_SUBITEMS, &callbacks, &parse);
    assert(req && vlc_preparser_Submit(parser, req) == VLC_SUCCESS);
    vlc_sem_wait(&parse.done);
    vlc_preparser_req_Release(req); vlc_preparser_Delete(parser);
    assert(parse.status == VLC_SUCCESS && parse.tree && parse.tree->i_children == 2);
    input_item_t *first = parse.tree->pp_children[0]->p_item;
    assert(input_item_GetDuration(first) == VLC_TICK_FROM_SEC(1451));
    char *value = input_item_GetMetaExtra(first, "podcast:guid"); assert(value && !strcmp(value, "first-guid")); free(value);
    value = input_item_GetMetaExtra(first, "podcast:video"); assert(value && !strcmp(value, "https://example.test/video.m3u8")); free(value);
    value = input_item_GetArtworkURL(first); assert(value && !strcmp(value, "https://example.test/episode.png")); free(value);
    value = input_item_GetArtist(first); assert(value && !strcmp(value, "Host")); free(value);
    value = input_item_GetEpisode(first); assert(value && !strcmp(value, "47")); free(value);
    input_item_t *second = parse.tree->pp_children[1]->p_item;
    assert(input_item_GetDuration(second) == VLC_TICK_FROM_SEC(3723));
    value = input_item_GetArtworkURL(second); assert(value && !strcmp(value, "https://example.test/show.png")); free(value);
    value = input_item_GetMetaExtra(second, "podcast:video"); assert(!value); free(value);
    input_item_node_Delete(parse.tree); input_item_Release(item); unlink(path);
}

static int OpenIntf(vlc_object_t *root)
{
    vlc_medialibrary_t *ml = vlc_ml_instance_get(root);
    if (ml == NULL)
    {
        exitcode = 77;
        return VLC_SUCCESS;
    }

    #define MOCK_URL "mock://video_track_count=1;length=100000000;" \
                     "video_width=800;video_height=600"
    vlc_ml_media_t *media = vlc_ml_new_external_media(ml, MOCK_URL);

    vlc_sem_t sem;
    vlc_sem_init(&sem, 0);

    vlc_ml_event_callback_t *listener =
        vlc_ml_event_register_callback(ml, ValidateThumbnail, &sem);
    vlc_ml_media_generate_thumbnail(ml, media->i_id,
            VLC_ML_THUMBNAIL_SMALL, 800, 600, 0.f);

    vlc_sem_wait(&sem);
    vlc_ml_event_unregister_callback(ml, listener);

    TestManualThumbnail(ml, media->i_id);
    TestAssociatedTracks(root, ml, media, MOCK_URL);
    TestBookmarks(ml, media->i_id);
    TestResumeProgress(root, ml);
    TestPodcastState(ml);
    TestPodcastFeed(root);

    vlc_ml_media_release(media);

    return VLC_SUCCESS;
}

/** Inject the mocked modules as a static plugin: **/
vlc_module_begin()
    set_callback(OpenIntf)
    set_capability("interface", 0)
vlc_module_end()

VLC_EXPORT const vlc_plugin_cb vlc_static_modules[] = {
    VLC_SYMBOL(vlc_entry),
    NULL
};

static int cleanup_tmpdir(const char *dirpath, const struct stat *sb,
                          int typeflag, struct FTW *ftwbuf)
{
    (void)sb; (void)typeflag; (void)ftwbuf;
    return remove(dirpath);
}

int main(void)
{
    char template[] = "/tmp/vlc.test." MODULE_STRING ".XXXXXX";
    const char *tempdir = mkdtemp(template);
    if (tempdir == NULL)
    {
        assert(tempdir != NULL);
        return -1;
    }
    fprintf(stderr, "Using VLC_USERDATA_PATH directory %s\n", tempdir);
    setenv("VLC_USERDATA_PATH", tempdir, 1);

    test_init();

    const char * const args[] = {
        "-vvv", "--vout=dummy", "--aout=dummy", "--text-renderer=dummy",
        "--no-auto-preparse", "--media-library", "--ignore-config",
        "--no-restore-playback-states", "--no-sub-autodetect-file",
        "--restore-playback-pos=1", "--save-recentplay"
    };

    libvlc_instance_t *vlc = libvlc_new(ARRAY_SIZE(args), args);

    libvlc_InternalAddIntf(vlc->p_libvlc_int, MODULE_STRING);
    libvlc_InternalPlay(vlc->p_libvlc_int);

    libvlc_release(vlc);

    /* Remove temporary directory */
    nftw(tempdir, cleanup_tmpdir, FOPEN_MAX, FTW_DEPTH | FTW_MOUNT | FTW_PHYS);
    return exitcode;
}
