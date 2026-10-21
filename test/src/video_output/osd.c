// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_vout.h>
#include <vlc_vout_osd.h>
#include <vlc_subpicture.h>
#include <vlc_actions.h>
#include <locale.h>
#include "../../../src/video_output/vout_internal.h"

/* Compile the real OSD builders with only the output queue replaced. Inspect
 * their timestamps without starting a display or waiting for wall-clock time. */
static subpicture_t *captured;

void CaptureSubpicture(vout_thread_t *vout, subpicture_t *subpicture)
{
    (void)vout;
    assert(captured == NULL);
    captured = subpicture;
}

static void ExpectDuration(unsigned milliseconds)
{
    assert(captured != NULL);
    assert(captured->i_stop - captured->i_start == VLC_TICK_FROM_MS(milliseconds));
    assert(captured->b_ephemer && captured->b_fade);
    subpicture_Delete(captured);
    captured = NULL;
}

static void Message(vout_thread_t *vout, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vout_OSDMessageVa(vout, VOUT_SPU_CHANNEL_OSD, format, args);
    va_end(args);
}

static void CheckFeedback(vout_thread_t *vout, unsigned duration)
{
    Message(vout, "Speed: %s", "1.25");
    ExpectDuration(duration);
    vout_OSDSlider(vout, VOUT_SPU_CHANNEL_OSD, 75, OSD_HOR_SLIDER);
    ExpectDuration(duration);
    vout_OSDSlider(vout, VOUT_SPU_CHANNEL_OSD, 75, OSD_VERT_SLIDER);
    ExpectDuration(duration);
    vout_OSDIcon(vout, VOUT_SPU_CHANNEL_OSD, OSD_PLAY_ICON);
    ExpectDuration(duration);
}

static void ExpectText(const char *expected, bool caption)
{
    assert(captured);
    video_format_t before, current;
    video_format_Init(&before, 0);
    video_format_Init(&current, 0);
    video_format_Setup(&current, VLC_CODEC_I420, 1920, 1080, 1920, 1080, 1, 1);
    const struct vlc_spu_updater_configuration cfg = {
        .previous.video_dst = &before,
        .current.video_src = &current, .current.video_dst = &current,
        .current.display_width = 1920, .current.display_height = 1080,
    };
    captured->updater.ops->update(captured, &cfg);
    subpicture_region_t *region = vlc_spu_regions_first_or_null(&captured->regions);
    assert(region && region->p_text);
    assert(!strcmp(region->p_text->psz_text, expected));
    if (caption)
    {
        assert(region->i_align & SUBPICTURE_ALIGN_RIGHT);
        assert(region->text_flags & SUBPICTURE_ALIGN_LEFT);
        assert(!(region->text_flags & SUBPICTURE_ALIGN_RIGHT));
    }
    video_format_Clean(&before);
    video_format_Clean(&current);
}

static void ScopedMessage(void *opaque)
{
    Message(opaque, "Crop: %s", "2.35:1");
}

static void ScopedTitle(void *opaque)
{
    vout_OSDTitle(opaque, SUBPICTURE_ALIGN_BOTTOM, VLC_TICK_FROM_SEC(8), "Filename");
}

static int OnAction(vlc_object_t *obj, const char *name, vlc_value_t before,
                    vlc_value_t after, void *opaque)
{
    (void)obj; (void)name; (void)before; (void)after;
    ScopedMessage(opaque);
    return VLC_SUCCESS;
}

int main(void)
{
    const char *args[] = {"--ignore-config", "--no-plugins-cache", "--no-media-library",
        "--intf=dummy", "--key-crop=c", "--global-key-crop=Ctrl+Alt+X"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance != NULL);
    vlc_object_t *parent = VLC_OBJECT(instance->p_libvlc_int);
    vout_thread_t *vout = vlc_object_create(parent, sizeof(*vout));
    assert(vout != NULL);
    setlocale(LC_ALL, "C");

    vlc_actions_run_with_keycode(VLC_OBJECT(vout), 'c', ScopedMessage, vout);
    ExpectText("Crop: 2.35:1\nShortcut: C", true);
    ExpectDuration(1000);
    vlc_actions_run_with_keycode(VLC_OBJECT(vout), KEY_MODIFIER_CTRL | KEY_MODIFIER_ALT | 'x', ScopedMessage, vout);
    ExpectText("Crop: 2.35:1\nShortcut: Ctrl+Alt+X", true);
    ExpectDuration(1000);
    ScopedMessage(vout);
    ExpectText("Crop: 2.35:1", false);
    ExpectDuration(1000);
    const struct {uint_fast32_t key; const char *text;} names[] = {
        {KEY_F8, "Crop: 2.35:1\nShortcut: F8"},
        {KEY_MODIFIER_SHIFT | ' ', "Crop: 2.35:1\nShortcut: Shift+Space"},
        {'%', "Crop: 2.35:1\nShortcut: %"},
        {0xf1, "Crop: 2.35:1\nShortcut: \xc3\xb1"},
    };
    for (size_t i = 0; i < ARRAY_SIZE(names); ++i)
    {
        vlc_actions_run_with_keycode(VLC_OBJECT(vout), names[i].key, ScopedMessage, vout);
        ExpectText(names[i].text, true);
        ExpectDuration(1000);
    }
    var_AddCallback(parent, "key-action", OnAction, vout);
    var_SetInteger(parent, "key-pressed", 'c');
    ExpectText("Crop: 2.35:1\nShortcut: C", true);
    ExpectDuration(1000);
    var_SetInteger(parent, "global-key-pressed", vlc_str2keycode("Ctrl+Alt+X"));
    ExpectText("Crop: 2.35:1\nShortcut: Ctrl+Alt+X", true);
    ExpectDuration(1000);
    var_SetInteger(parent, "key-action", ACTIONID_CROP);
    ExpectText("Crop: 2.35:1", false);
    ExpectDuration(1000);
    var_DelCallback(parent, "key-action", OnAction, vout);

    CheckFeedback(vout, 1000);
    var_Create(parent, "osd-duration", VLC_VAR_INTEGER);
    var_SetInteger(parent, "osd-duration", 2750);
    CheckFeedback(vout, 2750);

    var_Create(vout, "osd-duration", VLC_VAR_INTEGER);
    const unsigned durations[] = {500, 1500, 5000};
    for (size_t i = 0; i < ARRAY_SIZE(durations); ++i) {
        var_SetInteger(vout, "osd-duration", durations[i]);
        CheckFeedback(vout, durations[i]);
        vlc_actions_run_with_keycode(VLC_OBJECT(vout), 'c', ScopedMessage, vout);
        ExpectText("Crop: 2.35:1\nShortcut: C", true);
        ExpectDuration(durations[i]);
    }

    /* Explicit text lifetimes (such as the filename) remain independent. */
    vout_OSDText(vout, VOUT_SPU_CHANNEL_OSD, SUBPICTURE_ALIGN_BOTTOM,
                 VLC_TICK_FROM_SEC(8), "Filename");
    ExpectDuration(8000);

    var_Create(vout, "osd", VLC_VAR_BOOL);
    var_SetBool(vout, "osd", false);
    Message(vout, "Hidden");
    vlc_actions_run_with_keycode(VLC_OBJECT(vout), 'c', ScopedMessage, vout);
    vout_OSDSlider(vout, VOUT_SPU_CHANNEL_OSD, 75, OSD_HOR_SLIDER);
    vout_OSDIcon(vout, VOUT_SPU_CHANNEL_OSD, OSD_PAUSE_ICON);
    assert(captured == NULL);
    vlc_actions_run_with_keycode(VLC_OBJECT(vout), 'c', ScopedTitle, vout);
    assert(captured && captured->i_channel == VOUT_SPU_CHANNEL_OSD_TITLE);
    ExpectText("Filename", false);
    ExpectDuration(8000);
    var_SetBool(vout, "osd", true);
    CheckFeedback(vout, 5000);

    vlc_object_delete(vout);
    libvlc_release(instance);
    return 0;
}
