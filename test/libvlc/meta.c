/*
 * meta.c - libvlc smoke test
 *
 */

/**********************************************************************
 *  Copyright (C) 2007 Rémi Denis-Courmont.                           *
 *  Copyright (C) 2008 Pierre d'Herbemont.                            *
 *  This program is free software; you can redistribute and/or modify *
 *  it under the terms of the GNU General Public License as published *
 *  by the Free Software Foundation; version 2 of the license, or (at *
 *  your option) any later version.                                   *
 *                                                                    *
 *  This program is distributed in the hope that it will be useful,   *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of    *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.              *
 *  See the GNU General Public License for more details.              *
 *                                                                    *
 *  You should have received a copy of the GNU General Public License *
 *  along with this program; if not, you can get it from:             *
 *  http://www.gnu.org/copyleft/gpl.html                              *
 **********************************************************************/

#include <string.h>

#include "test.h"
#include "media_utils.h"

#include <vlc_modules.h>
#include <vlc_fs.h>
#ifdef __linux__
# include <sys/resource.h>
#endif

static void copy_file(const char *source_path, const char *path)
{
    FILE *source = fopen(source_path, "rb");
    FILE *destination = fopen(path, "wb");
    assert(source && destination);
    char block[4096];
    size_t count;
    while ((count = fread(block, 1, sizeof(block), source)) != 0)
        assert(fwrite(block, 1, count, destination) == count);
    assert(!ferror(source));
    assert(fclose(source) == 0);
    assert(fclose(destination) == 0);
}

static libvlc_media_t *open_parsed(libvlc_instance_t *vlc, const char *path)
{
    libvlc_media_t *media = libvlc_media_new_path(path);
    assert(media);
    libvlc_media_parse_sync(vlc, media, libvlc_media_parse, -1);
    return media;
}

static void check_meta(libvlc_media_t *media, libvlc_meta_t type,
                       const char *expected)
{
    char *value = libvlc_media_get_meta(media, type);
    if (expected)
        assert(value && !strcmp(value, expected));
    else
        assert(!value || !*value);
    free(value);
}

static void test_dates(libvlc_instance_t *vlc, const char *path)
{
    test_log("Testing complete date round trips in %s\n", path);
    libvlc_media_t *media = open_parsed(vlc, path);
    libvlc_media_set_meta(media, libvlc_meta_Date, "2026-10-23");
    assert(libvlc_media_save_meta(vlc, media));
    libvlc_media_release(media);

    media = open_parsed(vlc, path);
    check_meta(media, libvlc_meta_Date, "2026-10-23");
    libvlc_media_set_meta(media, libvlc_meta_Title, "saved title");
    assert(libvlc_media_save_meta(vlc, media));
    libvlc_media_release(media);

    media = open_parsed(vlc, path);
    check_meta(media, libvlc_meta_Date, "2026-10-23");
    check_meta(media, libvlc_meta_Title, "saved title");
    libvlc_media_set_meta(media, libvlc_meta_Date, "2025-12-31");
    assert(libvlc_media_save_meta(vlc, media));
    libvlc_media_release(media);

    media = open_parsed(vlc, path);
    check_meta(media, libvlc_meta_Date, "2025-12-31");
    libvlc_media_set_meta(media, libvlc_meta_Date, "1999");
    assert(libvlc_media_save_meta(vlc, media));
    libvlc_media_release(media);

    media = open_parsed(vlc, path);
    check_meta(media, libvlc_meta_Date, "1999");
    libvlc_media_set_meta(media, libvlc_meta_Date, "");
    assert(libvlc_media_save_meta(vlc, media));
    libvlc_media_release(media);

    media = open_parsed(vlc, path);
    check_meta(media, libvlc_meta_Date, NULL);
    check_meta(media, libvlc_meta_Title, "saved title");
    libvlc_media_release(media);
}

static void test_date_roundtrip(libvlc_instance_t *vlc)
{
    char temporary[] = "meta-date-XXXXXX";
    int fd = vlc_mkstemp(temporary);
    assert(fd >= 0);
    assert(close(fd) == 0);
    char path[sizeof(temporary) + sizeof(".mp3")];
    snprintf(path, sizeof(path), "%s.mp3", temporary);
    assert(rename(temporary, path) == 0);
    copy_file(SRCDIR "/samples/meta.mp3", path);
    test_dates(vlc, path);
    assert(unlink(path) == 0);
}

#ifdef __linux__
static void test_write_failure(libvlc_instance_t *vlc)
{
    char directory[] = "meta-write-XXXXXX";
    assert(mkdtemp(directory) != NULL);
    char path[sizeof(directory) + sizeof("/copy.mp3")];
    snprintf(path, sizeof(path), "%s/copy.mp3", directory);
    copy_file(SRCDIR "/samples/meta.mp3", path);

    libvlc_media_t *media = libvlc_media_new_path(path);
    assert(media);
    libvlc_media_parse_sync(vlc, media, libvlc_media_parse, -1);
    libvlc_media_set_meta(media, libvlc_meta_Title, "metadata write check");
    assert(libvlc_media_save_meta(vlc, media));
    libvlc_media_t *saved = libvlc_media_new_path(path);
    assert(saved);
    libvlc_media_parse_sync(vlc, saved, libvlc_media_parse, -1);
    char *title = libvlc_media_get_meta(saved, libvlc_meta_Title);
    assert(title && !strcmp(title, "metadata write check"));
    free(title);
    libvlc_media_release(saved);

    char large_title[65537];
    memset(large_title, 'x', sizeof(large_title) - 1);
    large_title[sizeof(large_title) - 1] = '\0';
    libvlc_media_set_meta(media, libvlc_meta_Title, large_title);
    struct rlimit previous, limited;
    assert(getrlimit(RLIMIT_FSIZE, &previous) == 0);
    limited = previous;
    limited.rlim_cur = 8192;
    struct sigaction ignore = { .sa_handler = SIG_IGN }, previous_signal;
    sigemptyset(&ignore.sa_mask);
    assert(sigaction(SIGXFSZ, &ignore, &previous_signal) == 0);
    assert(setrlimit(RLIMIT_FSIZE, &limited) == 0);
    int result = libvlc_media_save_meta(vlc, media);
    assert(setrlimit(RLIMIT_FSIZE, &previous) == 0);
    assert(sigaction(SIGXFSZ, &previous_signal, NULL) == 0);
    libvlc_media_release(media);
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
    assert(!result);
}
#endif

static int test_meta (const char ** argv, int argc)
{
    libvlc_instance_t *vlc;
    libvlc_media_t *media;
    char * artist;

    test_log ("Testing meta\n");

    vlc = libvlc_new (argc, argv);
    assert (vlc != NULL);

    if (!module_exists("taglib"))
    {
        libvlc_release (vlc);
        return 77;
    }

    media = libvlc_media_new_path(SRCDIR "/samples/meta.mp3");
    assert( media );

    libvlc_media_parse_sync(vlc, media, libvlc_media_parse, -1);

    artist = libvlc_media_get_meta (media, libvlc_meta_Artist);

    const char *expected_artist = "mike";

    assert (artist);
    test_log ("+ got '%s' as Artist, expecting %s\n", artist, expected_artist);

    int string_compare = strcmp (artist, expected_artist);
    assert (!string_compare);

    free (artist);
    libvlc_media_release (media);
    test_date_roundtrip(vlc);
#ifdef __linux__
    test_write_failure(vlc);
#endif
    libvlc_release (vlc);

    return 0;
}


int main (void)
{
    test_init();

    return test_meta (test_defaults_args, test_defaults_nargs);
}
