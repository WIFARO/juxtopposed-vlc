/*****************************************************************************
 * actions.c: handle vlc actions
 *****************************************************************************
 * Copyright (C) 2003-2016 VLC authors and VideoLAN
 *
 * Authors: Sigmund Augdal Helberg <dnumgis@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
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

/**
 * \file
 * This file defines functions and structures for hotkey handling in vlc
 */

#include <assert.h>
#include <stdlib.h>
#include <limits.h>
#ifdef HAVE_SEARCH_H
# include <search.h>
#endif
#include <errno.h>

#include <vlc_common.h>
#include <vlc_actions.h>
#include <vlc_configuration.h>
#include <vlc_charset.h>
#include "../libvlc.h"

struct action_key_context
{
    libvlc_int_t *instance;
    uint_fast32_t keycode;
};
static thread_local struct action_key_context current_key;

uint_fast32_t vlc_actions_get_current_keycode(vlc_object_t *obj)
{
    return current_key.instance == vlc_object_instance(obj)
         ? current_key.keycode : KEY_UNSET;
}

void vlc_actions_run_with_keycode(vlc_object_t *obj, uint_fast32_t keycode,
                                  void (*callback)(void *), void *data)
{
    const struct action_key_context previous = current_key;
    current_key = (struct action_key_context){vlc_object_instance(obj), keycode};
    callback(data);
    current_key = previous;
}

static const struct key_descriptor
{
    const char psz[20];
    uint32_t i_code;
} s_keys[] =
{   /* Alphabetical order */
    { N_("Backspace"),         KEY_BACKSPACE         },
    { N_("Brightness Down"),   KEY_BRIGHTNESS_DOWN   },
    { N_("Brightness Up"),     KEY_BRIGHTNESS_UP     },
    { N_("Browser Back"),      KEY_BROWSER_BACK      },
    { N_("Browser Favorites"), KEY_BROWSER_FAVORITES },
    { N_("Browser Forward"),   KEY_BROWSER_FORWARD   },
    { N_("Browser Home"),      KEY_BROWSER_HOME      },
    { N_("Browser Refresh"),   KEY_BROWSER_REFRESH   },
    { N_("Browser Search"),    KEY_BROWSER_SEARCH    },
    { N_("Browser Stop"),      KEY_BROWSER_STOP      },
    { N_("Delete"),            KEY_DELETE            },
    { N_("Down"),              KEY_DOWN              },
    { N_("End"),               KEY_END               },
    { N_("Enter"),             KEY_ENTER             },
    { N_("Esc"),               KEY_ESC               },
    { N_("F1"),                KEY_F1                },
    { N_("F10"),               KEY_F10               },
    { N_("F11"),               KEY_F11               },
    { N_("F12"),               KEY_F12               },
    { N_("F13"),               KEY_F(13)             },
    { N_("F14"),               KEY_F(14)             },
    { N_("F15"),               KEY_F(15)             },
    { N_("F16"),               KEY_F(16)             },
    { N_("F17"),               KEY_F(17)             },
    { N_("F18"),               KEY_F(18)             },
    { N_("F19"),               KEY_F(19)             },
    { N_("F2"),                KEY_F2                },
    { N_("F20"),               KEY_F(20)             },
    { N_("F21"),               KEY_F(21)             },
    { N_("F22"),               KEY_F(22)             },
    { N_("F23"),               KEY_F(23)             },
    { N_("F24"),               KEY_F(24)             },
    { N_("F25"),               KEY_F(25)             },
    { N_("F26"),               KEY_F(26)             },
    { N_("F27"),               KEY_F(27)             },
    { N_("F28"),               KEY_F(28)             },
    { N_("F29"),               KEY_F(29)             },
    { N_("F3"),                KEY_F3                },
    { N_("F30"),               KEY_F(30)             },
    { N_("F31"),               KEY_F(31)             },
    { N_("F32"),               KEY_F(32)             },
    { N_("F33"),               KEY_F(33)             },
    { N_("F34"),               KEY_F(34)             },
    { N_("F35"),               KEY_F(35)             },
    { N_("F4"),                KEY_F4                },
    { N_("F5"),                KEY_F5                },
    { N_("F6"),                KEY_F6                },
    { N_("F7"),                KEY_F7                },
    { N_("F8"),                KEY_F8                },
    { N_("F9"),                KEY_F9                },
    { N_("Home"),              KEY_HOME              },
    { N_("Insert"),            KEY_INSERT            },
    { N_("Left"),              KEY_LEFT              },
    { N_("Media Angle"),       KEY_MEDIA_ANGLE       },
    { N_("Media Audio Track"), KEY_MEDIA_AUDIO       },
    { N_("Media Forward"),     KEY_MEDIA_FORWARD     },
    { N_("Media Menu"),        KEY_MEDIA_MENU        },
    { N_("Media Next Frame"),  KEY_MEDIA_FRAME_NEXT  },
    { N_("Media Next Track"),  KEY_MEDIA_NEXT_TRACK  },
    { N_("Media Play Pause"),  KEY_MEDIA_PLAY_PAUSE  },
    { N_("Media Prev Frame"),  KEY_MEDIA_FRAME_PREV  },
    { N_("Media Prev Track"),  KEY_MEDIA_PREV_TRACK  },
    { N_("Media Record"),      KEY_MEDIA_RECORD      },
    { N_("Media Repeat"),      KEY_MEDIA_REPEAT      },
    { N_("Media Rewind"),      KEY_MEDIA_REWIND      },
    { N_("Media Select"),      KEY_MEDIA_SELECT      },
    { N_("Media Shuffle"),     KEY_MEDIA_SHUFFLE     },
    { N_("Media Stop"),        KEY_MEDIA_STOP        },
    { N_("Media Subtitle"),    KEY_MEDIA_SUBTITLE    },
    { N_("Media Time"),        KEY_MEDIA_TIME        },
    { N_("Media View"),        KEY_MEDIA_VIEW        },
    { N_("Menu"),              KEY_MENU              },
    { N_("Mouse Wheel Down"),  KEY_MOUSEWHEELDOWN    },
    { N_("Mouse Wheel Left"),  KEY_MOUSEWHEELLEFT    },
    { N_("Mouse Wheel Right"), KEY_MOUSEWHEELRIGHT   },
    { N_("Mouse Wheel Up"),    KEY_MOUSEWHEELUP      },
    { N_("Page Down"),         KEY_PAGEDOWN          },
    { N_("Page Up"),           KEY_PAGEUP            },
    { N_("Pause"),             KEY_PAUSE             },
    { N_("Print"),             KEY_PRINT             },
    { N_("Right"),             KEY_RIGHT             },
    { N_("Space"),             ' '                   },
    { N_("Tab"),               KEY_TAB               },
    { N_("Unset"),             KEY_UNSET             },
    { N_("Up"),                KEY_UP                },
    { N_("Volume Down"),       KEY_VOLUME_DOWN       },
    { N_("Volume Mute"),       KEY_VOLUME_MUTE       },
    { N_("Volume Up"),         KEY_VOLUME_UP         },
    { N_("Zoom In"),           KEY_ZOOM_IN           },
    { N_("Zoom Out"),          KEY_ZOOM_OUT          },
};

static int keystrcmp (const void *key, const void *elem)
{
    const char *sa = key, *sb = elem;
    return strcmp (sa, sb);
}

/* Convert Unicode code point to UTF-8 */
static char *utf8_cp (uint_fast32_t cp, char *buf)
{
    if (cp < (1 << 7))
    {
        buf[1] = 0;
        buf[0] = cp;
    }
    else if (cp < (1 << 11))
    {
        buf[2] = 0;
        buf[1] = 0x80 | (cp & 0x3F);
        cp >>= 6;
        buf[0] = 0xC0 | cp;
    }
    else if (cp < (1 << 16))
    {
        buf[3] = 0;
        buf[2] = 0x80 | (cp & 0x3F);
        cp >>= 6;
        buf[1] = 0x80 | (cp & 0x3F);
        cp >>= 6;
        buf[0] = 0xE0 | cp;
    }
    else if (cp < (1 << 21))
    {
        buf[4] = 0;
        buf[3] = 0x80 | (cp & 0x3F);
        cp >>= 6;
        buf[2] = 0x80 | (cp & 0x3F);
        cp >>= 6;
        buf[1] = 0x80 | (cp & 0x3F);
        cp >>= 6;
        buf[0] = 0xE0 | cp;
    }
    else
        return NULL;
    return buf;
}

/**
 * Parse a human-readable string representation of a VLC key code.
 * @note This only works with the American English representation
 * (a.k.a. C or POSIX), not with the local representation returned from
 * vlc_keycode2str().
 * @return a VLC key code, or KEY_UNSET on failure.
 */
uint_fast32_t vlc_str2keycode (const char *name)
{
    uint_fast32_t mods = 0;
    uint32_t code;

    for (;;)
    {
        size_t len = strcspn (name, "-+");
        if (len == 0 || name[len] == '\0')
            break;

        if (len == 4 && !strncasecmp (name, "Ctrl", 4))
            mods |= KEY_MODIFIER_CTRL;
        if (len == 3 && !strncasecmp (name, "Alt", 3))
            mods |= KEY_MODIFIER_ALT;
        if (len == 5 && !strncasecmp (name, "Shift", 5))
            mods |= KEY_MODIFIER_SHIFT;
        if (len == 4 && !strncasecmp (name, "Meta", 4))
            mods |= KEY_MODIFIER_META;
        if (len == 7 && !strncasecmp (name, "Command", 7))
            mods |= KEY_MODIFIER_COMMAND;

        name += len + 1;
    }

    const struct key_descriptor *d = bsearch(name, s_keys, ARRAY_SIZE(s_keys),
                                             sizeof (s_keys[0]), keystrcmp);
    if (d != NULL)
        code = d->i_code;
    else if (vlc_towc (name, &code) <= 0)
        return KEY_UNSET;

    if (code != KEY_UNSET)
        code |= mods;
    return code;
}

static const char *nooptext (const char *txt)
{
    return txt;
}

/**
 * Format a human-readable and unique representation of a VLC key code
 * (including modifiers).
 * @param code key code to translate to a string
 * @param locale true to get a localized string,
 *               false to get a C string suitable for 'vlcrc'
 * @return a heap-allocated string, or NULL on error.
 */
char *vlc_keycode2str (uint_fast32_t code, bool locale)
{
    const char *(*tr)(const char *) = locale ? vlc_gettext : nooptext;
    const char *name;
    char *str, buf[5];
    uintptr_t key = code & ~KEY_MODIFIER;

    for (size_t i = 0; i < ARRAY_SIZE(s_keys); i++)
        if (s_keys[i].i_code == key)
        {
            name = tr(s_keys[i].psz);
            goto found;
        }

    if (utf8_cp (key, buf) == NULL)
        return NULL;
    name = buf;

found:
    if (asprintf (&str, "%s%s%s%s%s%s",
                  (code & KEY_MODIFIER_CTRL) ? tr(N_("Ctrl+")) : "",
                  (code & KEY_MODIFIER_ALT) ? tr(N_("Alt+")) : "",
                  (code & KEY_MODIFIER_SHIFT) ? tr(N_("Shift+")) : "",
                  (code & KEY_MODIFIER_META) ? tr(N_("Meta+")) : "",
                  (code & KEY_MODIFIER_COMMAND) ? tr(N_("Command+")) : "",
                  name) == -1)
        return NULL;
    return str;
}


/*** VLC key map ***/

#define MAXACTION 27
static const struct name2action
{
    char psz[MAXACTION];
    vlc_action_id_t id;
} s_names2actions[] =
{
    /* *MUST* be sorted (ASCII order) */
    { "aspect-ratio", ACTIONID_ASPECT_RATIO, },
    { "audio-track", ACTIONID_AUDIO_TRACK, },
    { "audiodelay-down", ACTIONID_AUDIODELAY_DOWN, },
    { "audiodelay-up", ACTIONID_AUDIODELAY_UP, },
    { "audiodevice-cycle", ACTIONID_AUDIODEVICE_CYCLE, },
    { "chapter-next", ACTIONID_CHAPTER_NEXT, },
    { "chapter-prev", ACTIONID_CHAPTER_PREV, },
    { "clear-playlist", ACTIONID_PLAY_CLEAR, },
    { "crop", ACTIONID_CROP, },
    { "crop-bottom", ACTIONID_CROP_BOTTOM, },
    { "crop-left", ACTIONID_CROP_LEFT, },
    { "crop-right", ACTIONID_CROP_RIGHT, },
    { "crop-top", ACTIONID_CROP_TOP, },
    { "decr-scalefactor", ACTIONID_SCALE_DOWN, },
    { "deinterlace", ACTIONID_DEINTERLACE, },
    { "deinterlace-mode", ACTIONID_DEINTERLACE_MODE, },
    { "disc-menu", ACTIONID_DISC_MENU, },
    { "faster", ACTIONID_RATE_FASTER, },
    { "frame-next", ACTIONID_FRAME_NEXT, },
    { "frame-previous", ACTIONID_FRAME_PREVIOUS, },
    { "incr-scalefactor", ACTIONID_SCALE_UP, },
    { "intf-boss", ACTIONID_INTF_BOSS, },
    { "intf-popup-menu", ACTIONID_INTF_POPUP_MENU, },
    { "intf-show", ACTIONID_INTF_TOGGLE_FSC, },
    { "jump+extrashort", ACTIONID_JUMP_FORWARD_EXTRASHORT, },
    { "jump+long", ACTIONID_JUMP_FORWARD_LONG, },
    { "jump+medium", ACTIONID_JUMP_FORWARD_MEDIUM, },
    { "jump+short", ACTIONID_JUMP_FORWARD_SHORT, },
    { "jump-extrashort", ACTIONID_JUMP_BACKWARD_EXTRASHORT, },
    { "jump-long", ACTIONID_JUMP_BACKWARD_LONG, },
    { "jump-medium", ACTIONID_JUMP_BACKWARD_MEDIUM, },
    { "jump-short", ACTIONID_JUMP_BACKWARD_SHORT, },
    { "leave-fullscreen", ACTIONID_LEAVE_FULLSCREEN, },
    { "loop", ACTIONID_LOOP, },
    { "nav-activate", ACTIONID_NAV_ACTIVATE, },
    { "nav-down", ACTIONID_NAV_DOWN, },
    { "nav-left", ACTIONID_NAV_LEFT, },
    { "nav-right", ACTIONID_NAV_RIGHT, },
    { "nav-up", ACTIONID_NAV_UP, },
    { "next", ACTIONID_NEXT, },
    { "pause", ACTIONID_PAUSE, },
    { "play", ACTIONID_PLAY, },
    { "play-bookmark1", ACTIONID_PLAY_BOOKMARK1, },
    { "play-bookmark10", ACTIONID_PLAY_BOOKMARK10, },
    { "play-bookmark2", ACTIONID_PLAY_BOOKMARK2, },
    { "play-bookmark3", ACTIONID_PLAY_BOOKMARK3, },
    { "play-bookmark4", ACTIONID_PLAY_BOOKMARK4, },
    { "play-bookmark5", ACTIONID_PLAY_BOOKMARK5, },
    { "play-bookmark6", ACTIONID_PLAY_BOOKMARK6, },
    { "play-bookmark7", ACTIONID_PLAY_BOOKMARK7, },
    { "play-bookmark8", ACTIONID_PLAY_BOOKMARK8, },
    { "play-bookmark9", ACTIONID_PLAY_BOOKMARK9, },
    { "play-pause", ACTIONID_PLAY_PAUSE, },
    { "position", ACTIONID_POSITION, },
    { "prev", ACTIONID_PREV, },
    { "program-sid-next", ACTIONID_PROGRAM_SID_NEXT, },
    { "program-sid-prev", ACTIONID_PROGRAM_SID_PREV, },
    { "projection-toggle", ACTIONID_PROJECTION_TOGGLE, },
    { "quit", ACTIONID_QUIT, },
    { "random", ACTIONID_RANDOM, },
    { "rate-faster-fine", ACTIONID_RATE_FASTER_FINE, },
    { "rate-normal", ACTIONID_RATE_NORMAL, },
    { "rate-slower-fine", ACTIONID_RATE_SLOWER_FINE, },
    { "record", ACTIONID_RECORD, },
    { "set-bookmark1", ACTIONID_SET_BOOKMARK1, },
    { "set-bookmark10", ACTIONID_SET_BOOKMARK10, },
    { "set-bookmark2", ACTIONID_SET_BOOKMARK2, },
    { "set-bookmark3", ACTIONID_SET_BOOKMARK3, },
    { "set-bookmark4", ACTIONID_SET_BOOKMARK4, },
    { "set-bookmark5", ACTIONID_SET_BOOKMARK5, },
    { "set-bookmark6", ACTIONID_SET_BOOKMARK6, },
    { "set-bookmark7", ACTIONID_SET_BOOKMARK7, },
    { "set-bookmark8", ACTIONID_SET_BOOKMARK8, },
    { "set-bookmark9", ACTIONID_SET_BOOKMARK9, },
    { "slower", ACTIONID_RATE_SLOWER, },
    { "snapshot", ACTIONID_SNAPSHOT, },
    { "stop", ACTIONID_STOP, },
    { "subdelay-down", ACTIONID_SUBDELAY_DOWN, },
    { "subdelay-up", ACTIONID_SUBDELAY_UP, },
    { "subpos-down", ACTIONID_SUBPOS_DOWN, },
    { "subpos-up", ACTIONID_SUBPOS_UP, },
    { "subsync-apply", ACTIONID_SUBSYNC_APPLY, },
    { "subsync-markaudio", ACTIONID_SUBSYNC_MARKAUDIO, },
    { "subsync-marksub", ACTIONID_SUBSYNC_MARKSUB, },
    { "subsync-reset", ACTIONID_SUBSYNC_RESET, },
    { "subtitle-control-secondary", ACTIONID_SUBTITLE_CONTROL_SECONDARY, },
    { "subtitle-revtrack", ACTIONID_SUBTITLE_REVERSE_TRACK, },
    { "subtitle-text-scale-down", ACTIONID_SUBTITLE_TEXT_SCALE_DOWN, },
    { "subtitle-text-scale-normal", ACTIONID_SUBTITLE_TEXT_SCALE_NORMAL, },
    { "subtitle-text-scale-up", ACTIONID_SUBTITLE_TEXT_SCALE_UP, },
    { "subtitle-toggle", ACTIONID_SUBTITLE_TOGGLE, },
    { "subtitle-track", ACTIONID_SUBTITLE_TRACK, },
    { "title-next", ACTIONID_TITLE_NEXT, },
    { "title-prev", ACTIONID_TITLE_PREV, },
    { "toggle-autoscale", ACTIONID_TOGGLE_AUTOSCALE, },
    { "toggle-fullscreen", ACTIONID_TOGGLE_FULLSCREEN, },
    { "uncrop-bottom", ACTIONID_UNCROP_BOTTOM, },
    { "uncrop-left", ACTIONID_UNCROP_LEFT, },
    { "uncrop-right", ACTIONID_UNCROP_RIGHT, },
    { "uncrop-top", ACTIONID_UNCROP_TOP, },
    { "unzoom", ACTIONID_UNZOOM, },
    { "viewpoint-fov-in", ACTIONID_VIEWPOINT_FOV_IN, },
    { "viewpoint-fov-out", ACTIONID_VIEWPOINT_FOV_OUT, },
    { "viewpoint-roll-anticlock", ACTIONID_VIEWPOINT_ROLL_ANTICLOCK, },
    { "viewpoint-roll-clock", ACTIONID_VIEWPOINT_ROLL_CLOCK, },
    { "vol-down", ACTIONID_VOL_DOWN, },
    { "vol-mute", ACTIONID_VOL_MUTE, },
    { "vol-up", ACTIONID_VOL_UP, },
    { "wallpaper", ACTIONID_WALLPAPER, },
    { "zoom", ACTIONID_ZOOM, },
    { "zoom-double", ACTIONID_ZOOM_DOUBLE, },
    { "zoom-half", ACTIONID_ZOOM_HALF, },
    { "zoom-original", ACTIONID_ZOOM_ORIGINAL, },
    { "zoom-quarter", ACTIONID_ZOOM_QUARTER, },
};

struct mapping
{
    uint32_t     key; ///< Key code
    vlc_action_id_t action; ///< Action ID
};

static int keycmp (const void *a, const void *b)
{
    const struct mapping *ka = a, *kb = b;

    return (ka->key < kb->key) ? -1 : (ka->key > kb->key) ? +1 : 0;
}

struct action_map
{
    vlc_mutex_t lock;
    void *root;
};

struct vlc_actions_t
{
    struct action_map map; /* Key map */
    struct action_map global_map; /* Grabbed/global key map */
    const char *ppsz_keys[];
};

static vlc_action_id_t wheel_action(vlc_object_t *obj, uint32_t keycode)
{
    const char *axis;
    bool more;
    switch (keycode)
    {
        case KEY_MOUSEWHEELUP: axis = "hotkeys-y-wheel-mode"; more = true; break;
        case KEY_MOUSEWHEELDOWN: axis = "hotkeys-y-wheel-mode"; more = false; break;
        case KEY_MOUSEWHEELRIGHT: axis = "hotkeys-x-wheel-mode"; more = true; break;
        case KEY_MOUSEWHEELLEFT: axis = "hotkeys-x-wheel-mode"; more = false; break;
        default: return ACTIONID_NONE;
    }
    if (var_InheritBool(obj, "hotkeys-wheel-direction")) more = !more;
    switch (var_InheritInteger(obj, axis))
    {
        case 0: return more ? ACTIONID_COMBO_VOL_FOV_UP : ACTIONID_COMBO_VOL_FOV_DOWN;
        case 2: return more ? ACTIONID_JUMP_FORWARD_EXTRASHORT : ACTIONID_JUMP_BACKWARD_EXTRASHORT;
        case 3: return more ? ACTIONID_JUMP_BACKWARD_EXTRASHORT : ACTIONID_JUMP_FORWARD_EXTRASHORT;
        default: return ACTIONID_NONE;
    }
}

static int vlc_key_to_action (vlc_object_t *obj, const char *varname,
                              vlc_value_t prevkey, vlc_value_t curkey, void *d)
{
    struct action_map *map = d;
    const void **pent;
    uint32_t keycode = curkey.i_int;

    vlc_mutex_lock(&map->lock);
    pent = tfind (&keycode, &map->root, keycmp);
    /* Explicit user bindings take precedence over the live wheel defaults. */
    const struct mapping *entry = pent != NULL ? *pent : NULL;
    vlc_action_id_t action = entry != NULL ? entry->action
        : strcmp(varname, "key-pressed") == 0 ? wheel_action(obj, keycode) : ACTIONID_NONE;
    vlc_mutex_unlock(&map->lock);
    (void) prevkey;
    if (action == ACTIONID_NONE)
        return VLC_SUCCESS;
    const struct action_key_context previous = current_key;
    current_key = (struct action_key_context){vlc_object_instance(obj), keycode};
    int ret = var_SetInteger(obj, "key-action", action);
    current_key = previous;
    return ret;
}

/**
 * Adds a mapping from a certain key code to a certain action.
 */
static int add_mapping (void **map, uint32_t keycode, vlc_action_id_t action)
{
    struct mapping *entry = malloc (sizeof (*entry));
    if (entry == NULL)
        return ENOMEM;
    entry->key = keycode;
    entry->action = action;

    void **pent = tsearch (entry, map, keycmp);
    if (unlikely(pent == NULL))
    {
        free(entry);
        return ENOMEM;
    }
    if (*pent != entry)
    {
        free (entry);
        return EEXIST;
    }
    return 0;
}

/**
 * Sets up all key mappings for a given action.
 *
 * \param obj the VLC object to inherit keys from and log to
 * \param map tree (of struct mapping entries) to write mappings to
 * \param confname VLC configuration item to read mappings from
 * \param action action ID
 */
static int init_action (vlc_object_t *obj, void **map,
                        const char *confname, vlc_action_id_t action, bool inherit)
{
    if (!inherit && config_GetType(confname) != VLC_VAR_STRING)
        return VLC_SUCCESS;
    char *keys = inherit ? var_InheritString(obj, confname) : config_GetPsz(confname);
    if (keys == NULL)
        return VLC_SUCCESS;

    for (char *buf, *key = strtok_r (keys, "\t", &buf);
         key != NULL;
         key = strtok_r (NULL, "\t", &buf))
    {
        uint32_t code = vlc_str2keycode (key);
        if (code == KEY_UNSET)
        {
            msg_Warn (obj, "Key \"%s\" unrecognized", key);
            continue;
        }

        const int result = add_mapping(map, code, action);
        if (result == ENOMEM) { free(keys); return VLC_ENOMEM; }
        if (result == EEXIST)
            msg_Warn (obj, "Key \"%s\" bound to multiple actions", key);
    }
    free (keys);
    return VLC_SUCCESS;
}

/**
 * Initializes the key map from configuration.
 */
int libvlc_InternalActionsInit (libvlc_int_t *libvlc)
{
    assert(libvlc != NULL);

    vlc_object_t *obj = VLC_OBJECT(libvlc);
    vlc_actions_t *as = malloc (sizeof (*as) + (1 + ARRAY_SIZE(s_names2actions))
                      * sizeof (*as->ppsz_keys));

    if (unlikely(as == NULL))
        return VLC_ENOMEM;
    vlc_mutex_init(&as->map.lock);
    vlc_mutex_init(&as->global_map.lock);
    as->map.root = NULL;
    as->global_map.root = NULL;

    var_Create (obj, "key-pressed", VLC_VAR_INTEGER);
    var_Create (obj, "global-key-pressed", VLC_VAR_INTEGER);
    var_Create (obj, "key-action", VLC_VAR_INTEGER);

    /* Initialize from configuration */
    for (size_t i = 0; i < ARRAY_SIZE(s_names2actions); i++)
    {
#ifndef NDEBUG
        if (i > 0
         && strcmp (s_names2actions[i-1].psz, s_names2actions[i].psz) >= 0)
        {
            msg_Err (libvlc, "key-%s and key-%s are not ordered properly",
                     s_names2actions[i-1].psz, s_names2actions[i].psz);
            abort ();
        }
#endif
        as->ppsz_keys[i] = s_names2actions[i].psz;

#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)
        char name[12 + MAXACTION];

        snprintf (name, sizeof (name), "global-key-%." STRINGIFY(MAXACTION) "s", s_names2actions[i].psz);
        init_action (obj, &as->map.root, name + 7, s_names2actions[i].id, true);
        init_action (obj, &as->global_map.root, name, s_names2actions[i].id, true);
    }
    as->ppsz_keys[ARRAY_SIZE(s_names2actions)] = NULL;

    libvlc_priv(libvlc)->actions = as;
    var_AddCallback (obj, "key-pressed", vlc_key_to_action, &as->map);
    var_AddCallback (obj, "global-key-pressed", vlc_key_to_action,
                     &as->global_map);
    return VLC_SUCCESS;
}

/* Rebuild before swapping so dispatch never sees a partial configuration.
 * Release the map lock before emitting key-action: its callbacks may reload
 * profiles and must be able to update this map again. */
int vlc_actions_update(vlc_object_t *obj)
{
    vlc_actions_t *as = libvlc_priv(vlc_object_instance(obj))->actions;
    if (as == NULL) return VLC_EGENERIC;
    void *local = NULL, *global = NULL;
    for (size_t i = 0; i < ARRAY_SIZE(s_names2actions); ++i)
    {
        char name[12 + MAXACTION];
        snprintf(name, sizeof(name), "global-key-%s", s_names2actions[i].psz);
        if (init_action(obj, &local, name + 7, s_names2actions[i].id, false)
            || init_action(obj, &global, name, s_names2actions[i].id, false))
        {
            tdestroy(local, free); tdestroy(global, free);
            return VLC_ENOMEM;
        }
    }
    vlc_mutex_lock(&as->map.lock);
    void *old = as->map.root; as->map.root = local;
    vlc_mutex_unlock(&as->map.lock);
    tdestroy(old, free);
    vlc_mutex_lock(&as->global_map.lock);
    old = as->global_map.root; as->global_map.root = global;
    vlc_mutex_unlock(&as->global_map.lock);
    tdestroy(old, free);
    return VLC_SUCCESS;
}

/**
 * Destroys the key map.
 */
void libvlc_InternalActionsClean (libvlc_int_t *libvlc)
{
    assert(libvlc != NULL);

    vlc_actions_t *as = libvlc_priv(libvlc)->actions;
    if (unlikely(as == NULL))
        return;

    var_DelCallback (libvlc, "global-key-pressed", vlc_key_to_action,
                     &as->global_map);
    var_DelCallback (libvlc, "key-pressed", vlc_key_to_action, &as->map);

    tdestroy (as->global_map.root, free);
    tdestroy (as->map.root, free);
    free (as);
    libvlc_priv(libvlc)->actions = NULL;
}


static int actcmp(const void *key, const void *ent)
{
    const struct name2action *act = ent;
    return strcmp(key, act->psz);
}

/**
 * Get the action ID from the action name in the configuration subsystem.
 * @return the action ID or ACTIONID_NONE on error.
 */
vlc_action_id_t
vlc_actions_get_id (const char *name)
{
    const struct name2action *act;

    if (strncmp (name, "key-", 4))
        return ACTIONID_NONE;
    name += 4;

    act = bsearch(name, s_names2actions, ARRAY_SIZE(s_names2actions),
                  sizeof(*act), actcmp);
    return (act != NULL) ? act->id : ACTIONID_NONE;
}

#undef vlc_actions_get_keycodes
size_t
vlc_actions_get_keycodes(vlc_object_t *p_obj, const char *psz_key_name,
                        bool b_global, uint_fast32_t **pp_keycodes)
{
    assert(strlen( psz_key_name ) <= MAXACTION);
    char varname[12 /* "global-key-" */ + MAXACTION];
    snprintf( varname, ARRAY_SIZE(varname), "%skey-%s", b_global ? "global-" : "", psz_key_name );

    *pp_keycodes = NULL;

    char *psz_keys = var_InheritString( p_obj, varname );
    if( psz_keys == NULL )
        return 0;

    size_t i_nb_keycodes = 0;
    for( const char* psz_it = psz_keys; *psz_it; ++psz_it )
    {
        if( *psz_it == '\t' )
            ++i_nb_keycodes;
    }
    ++i_nb_keycodes;
    *pp_keycodes = vlc_alloc( i_nb_keycodes, sizeof( **pp_keycodes ) );
    if( unlikely( !*pp_keycodes ) )
    {
        free( psz_keys );
        return 0;
    }
    size_t i = 0;
    for( char *buf, *key = strtok_r( psz_keys, "\t", &buf );
         key != NULL;
         key = strtok_r( NULL, "\t", &buf ), ++i )
    {
        (*pp_keycodes)[i] = vlc_str2keycode( key );
    }
    assert( i == i_nb_keycodes );
    free( psz_keys );
    return i_nb_keycodes;
}

#undef vlc_actions_get_key_names
const char* const*
vlc_actions_get_key_names(vlc_object_t *p_obj)
{
    vlc_actions_t *as = libvlc_priv(vlc_object_instance(p_obj))->actions;
    return as->ppsz_keys;
}
