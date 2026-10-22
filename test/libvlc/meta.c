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
#ifdef __linux__
# include <sys/resource.h>
#endif

#ifdef __linux__
static void test_write_failure(libvlc_instance_t *vlc)
{
    char directory[] = "meta-write-XXXXXX";
    assert(mkdtemp(directory) != NULL);
    char path[sizeof(directory) + sizeof("/copy.mp3")];
    snprintf(path, sizeof(path), "%s/copy.mp3", directory);
    FILE *source = fopen(SRCDIR "/samples/meta.mp3", "rb");
    FILE *destination = fopen(path, "wb");
    assert(source && destination);
    char block[4096];
    size_t count;
    while ((count = fread(block, 1, sizeof(block), source)) != 0)
        assert(fwrite(block, 1, count, destination) == count);
    assert(!ferror(source));
    assert(fclose(source) == 0);
    assert(fclose(destination) == 0);

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
