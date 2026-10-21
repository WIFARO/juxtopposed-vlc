// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../libvlc/test.h"
#include "../lib/libvlc_internal.h"
#include <vlc_actions.h>
#include <vlc_configuration.h>
#include <vlc_fs.h>

#ifdef ENABLE_NLS
static void test_literal_key_names(void)
{
    /* A real catalog deliberately translates the letter C as a locale name.
     * Literal keys must not use that translation, unlike named keys/modifiers. */
    const char *ids[] = {"", "C", "Ctrl+", "Space"};
    const char *texts[] = {"Content-Type: text/plain; charset=UTF-8\nLanguage: fr\n",
        "locale-name", "Control+", "Translated space"};
    unsigned char mo[512] = {0};
    SetDWLE(mo, 0x950412de);
    SetDWLE(mo + 8, 4);
    SetDWLE(mo + 12, 28);
    SetDWLE(mo + 16, 60);
    size_t offset = 92;
    for (unsigned table = 0; table < 2; ++table)
        for (unsigned i = 0; i < 4; ++i)
        {
            const char *text = table ? texts[i] : ids[i];
            size_t length = strlen(text);
            assert(offset + length + 1 < sizeof(mo));
            SetDWLE(mo + 28 + table * 32 + i * 8, length);
            SetDWLE(mo + 32 + table * 32 + i * 8, offset);
            memcpy(mo + offset, text, length + 1);
            offset += length + 1;
        }
    char directory[] = "key-locale-XXXXXX";
    int fd = vlc_mkstemp(directory);
    assert(fd >= 0);
    close(fd);
    assert(vlc_unlink(directory) == 0);
    char *messages, *file, *root, *cwd = vlc_getcwd();
    assert(cwd);
    assert(asprintf(&messages, "%s/fr/LC_MESSAGES", directory) >= 0);
    assert(asprintf(&file, "%s/vlc.mo", messages) >= 0);
    assert(asprintf(&root, "%s/%s", cwd, directory) >= 0);
    assert(vlc_mkdir_parent(messages, 0700) == 0);
    FILE *stream = vlc_fopen(file, "wb");
    assert(stream && fwrite(mo, offset, 1, stream) == 1);
    assert(fclose(stream) == 0);
    const char *previous = getenv("VLC_LOCALE_PATH");
    char *old_path = previous ? strdup(previous) : NULL;
#ifdef _WIN32
    assert(_putenv_s("VLC_LOCALE_PATH", root) == 0);
#else
    assert(setenv("VLC_LOCALE_PATH", root, 1) == 0);
#endif
    const char *language[] = {"--ignore-config", "--language=fr"};
    assert(config_ApplyInterfaceLanguage(ARRAY_SIZE(language), language) == VLC_SUCCESS);
    const char *args[] = {"--ignore-config", "--no-media-library"};
    libvlc_instance_t *vlc = libvlc_new(ARRAY_SIZE(args), args);
    assert(vlc && !strcmp(vlc_gettext("C"), "locale-name"));
    const uint_fast32_t keys[] = {'C', 'c', KEY_MODIFIER_CTRL | 'C', ' '};
    const char *expected[] = {"C", "c", "Control+C", "Translated space"};
    for (size_t i = 0; i < ARRAY_SIZE(keys); ++i)
    {
        char *name = vlc_keycode2str(keys[i], true);
        assert(name && !strcmp(name, expected[i]));
        free(name);
    }
    libvlc_release(vlc);
#ifdef _WIN32
    assert(_putenv_s("VLC_LOCALE_PATH", old_path ? old_path : "") == 0);
#else
    if (old_path) assert(setenv("VLC_LOCALE_PATH", old_path, 1) == 0);
    else assert(unsetenv("VLC_LOCALE_PATH") == 0);
#endif
    const char *restore[] = {"--ignore-config", "--language=en"};
    assert(config_ApplyInterfaceLanguage(ARRAY_SIZE(restore), restore) == VLC_SUCCESS);
    vlc_unlink(file);
    rmdir(messages);
    char *parent;
    assert(asprintf(&parent, "%s/fr", directory) >= 0);
    rmdir(parent); rmdir(directory);
    free(parent); free(old_path); free(cwd); free(root); free(file); free(messages);
}
#endif

struct observed_action
{
    unsigned count;
    vlc_action_id_t action;
    uint_fast32_t keycode;
};

static int on_action(vlc_object_t *obj, const char *name, vlc_value_t previous,
                     vlc_value_t current, void *opaque)
{
    (void)name; (void)previous;
    struct observed_action *observed = opaque;
    observed->count++;
    observed->action = current.i_int;
    observed->keycode = vlc_actions_get_current_keycode(obj);
    return VLC_SUCCESS;
}

static void check_key(libvlc_int_t *core, struct observed_action *observed,
                      uint32_t key, vlc_action_id_t expected)
{
    observed->count = 0;
    var_SetInteger(core, "key-pressed", key);
    assert(observed->count == (expected == ACTIONID_NONE ? 0 : 1));
    if (expected != ACTIONID_NONE)
    {
        assert(observed->action == expected);
        assert(observed->keycode == key);
    }
    assert(vlc_actions_get_current_keycode(VLC_OBJECT(core)) == KEY_UNSET);
}

static void test_wheel(bool explicit_binding)
{
    const char *args[test_defaults_nargs + 2];
    memcpy(args, test_defaults_args, sizeof(test_defaults_args));
    args[test_defaults_nargs] = "--ignore-config";
    args[test_defaults_nargs + 1] = explicit_binding ? "--key-stop=Mouse Wheel Up" : "--key-stop=s";
    libvlc_instance_t *vlc = libvlc_new(ARRAY_SIZE(args), args);
    assert(vlc != NULL);
    libvlc_int_t *core = vlc->p_libvlc_int;
    struct observed_action observed = {0};
    var_AddCallback(core, "key-action", on_action, &observed);
    var_Create(core, "hotkeys-y-wheel-mode", VLC_VAR_INTEGER);
    var_Create(core, "hotkeys-x-wheel-mode", VLC_VAR_INTEGER);
    var_Create(core, "hotkeys-wheel-direction", VLC_VAR_BOOL);

    if (explicit_binding)
    {
        var_SetInteger(core, "hotkeys-y-wheel-mode", -1);
        check_key(core, &observed, KEY_MOUSEWHEELUP, ACTIONID_STOP);
        var_SetBool(core, "hotkeys-wheel-direction", true);
        check_key(core, &observed, KEY_MOUSEWHEELUP, ACTIONID_STOP);
        check_key(core, &observed, KEY_MOUSEWHEELDOWN, ACTIONID_NONE);
    }
    else
    {
        var_SetInteger(core, "hotkeys-y-wheel-mode", 0);
        var_SetInteger(core, "hotkeys-x-wheel-mode", 2);
        var_SetBool(core, "hotkeys-wheel-direction", false);
        check_key(core, &observed, KEY_MOUSEWHEELUP, ACTIONID_COMBO_VOL_FOV_UP);
        check_key(core, &observed, KEY_MOUSEWHEELDOWN, ACTIONID_COMBO_VOL_FOV_DOWN);
        check_key(core, &observed, KEY_MOUSEWHEELRIGHT, ACTIONID_JUMP_FORWARD_EXTRASHORT);
        check_key(core, &observed, KEY_MOUSEWHEELLEFT, ACTIONID_JUMP_BACKWARD_EXTRASHORT);

        observed.count = 0;
        var_SetInteger(core, "global-key-pressed", KEY_MOUSEWHEELUP);
        assert(observed.count == 0);

        /* A preferences change must work without restarting the instance. */
        var_SetBool(core, "hotkeys-wheel-direction", true);
        check_key(core, &observed, KEY_MOUSEWHEELUP, ACTIONID_COMBO_VOL_FOV_DOWN);
        check_key(core, &observed, KEY_MOUSEWHEELRIGHT, ACTIONID_JUMP_BACKWARD_EXTRASHORT);
        var_SetInteger(core, "hotkeys-y-wheel-mode", 2);
        check_key(core, &observed, KEY_MOUSEWHEELUP, ACTIONID_JUMP_BACKWARD_EXTRASHORT);
        var_SetInteger(core, "hotkeys-y-wheel-mode", 3);
        check_key(core, &observed, KEY_MOUSEWHEELUP, ACTIONID_JUMP_FORWARD_EXTRASHORT);
        var_SetBool(core, "hotkeys-wheel-direction", false);
        check_key(core, &observed, KEY_MOUSEWHEELUP, ACTIONID_JUMP_BACKWARD_EXTRASHORT);
        var_SetInteger(core, "hotkeys-y-wheel-mode", -1);
        check_key(core, &observed, KEY_MOUSEWHEELUP, ACTIONID_NONE);
        check_key(core, &observed, KEY_MOUSEWHEELDOWN, ACTIONID_NONE);
    }
    var_DelCallback(core, "key-action", on_action, &observed);
    libvlc_release(vlc);
}

static int reload_in_callback(vlc_object_t *obj, const char *name,
                              vlc_value_t before, vlc_value_t after, void *opaque)
{
    (void)name; (void)before; (void)after; (void)opaque;
    assert(vlc_actions_update(obj) == VLC_SUCCESS);
    return VLC_SUCCESS;
}

static void test_live_shortcuts(void)
{
    const char *args[test_defaults_nargs + 2];
    memcpy(args, test_defaults_args, sizeof(test_defaults_args));
    args[test_defaults_nargs] = "--ignore-config";
    args[test_defaults_nargs + 1] = "--key-play-pause=Ctrl+Alt+P";
    libvlc_instance_t *vlc = libvlc_new(ARRAY_SIZE(args), args);
    assert(vlc != NULL);
    libvlc_int_t *core = vlc->p_libvlc_int;
    struct observed_action observed = {0};
    var_AddCallback(core, "key-action", on_action, &observed);
    uint32_t p = vlc_str2keycode("Ctrl+Alt+P"), q = vlc_str2keycode("Ctrl+Alt+Q");
    check_key(core, &observed, p, ACTIONID_PLAY_PAUSE);
    char *old = config_GetPsz("key-play-pause");
    config_PutPsz("key-play-pause", "Ctrl+Alt+Q");
    assert(vlc_actions_update(VLC_OBJECT(core)) == VLC_SUCCESS);
    check_key(core, &observed, p, ACTIONID_NONE);
    check_key(core, &observed, q, ACTIONID_PLAY_PAUSE);
    var_AddCallback(core, "key-action", reload_in_callback, NULL);
    check_key(core, &observed, q, ACTIONID_PLAY_PAUSE);
    var_DelCallback(core, "key-action", reload_in_callback, NULL);
    config_PutPsz("key-play-pause", "");
    assert(vlc_actions_update(VLC_OBJECT(core)) == VLC_SUCCESS);
    check_key(core, &observed, q, ACTIONID_NONE);
    config_PutPsz("key-play-pause", old); free(old);
    var_DelCallback(core, "key-action", on_action, &observed);
    libvlc_release(vlc);
}

struct key_scope
{
    vlc_object_t *object;
    vlc_object_t *other;
    uint_fast32_t keycode;
};

static void check_scope(void *opaque)
{
    struct key_scope *scope = opaque;
    assert(vlc_actions_get_current_keycode(scope->object) == scope->keycode);
    assert(vlc_actions_get_current_keycode(scope->other) == KEY_UNSET);
}

static void *scope_thread(void *opaque)
{
    struct key_scope *scope = opaque;
    assert(vlc_actions_get_current_keycode(scope->object) == KEY_UNSET);
    vlc_actions_run_with_keycode(scope->object, scope->keycode, check_scope, scope);
    assert(vlc_actions_get_current_keycode(scope->object) == KEY_UNSET);
    return NULL;
}

static void nested_scope(void *opaque)
{
    struct key_scope *scope = opaque;
    check_scope(scope);
    struct key_scope inner = {scope->other, scope->object, KEY_MODIFIER_ALT | 'x'};
    vlc_actions_run_with_keycode(inner.object, inner.keycode, check_scope, &inner);
    check_scope(scope);
    vlc_thread_t worker;
    assert(vlc_clone(&worker, scope_thread, scope) == 0);
    vlc_join(worker, NULL);
    check_scope(scope);
}

static void test_key_context(void)
{
    const char *args[] = {"--ignore-config", "--no-media-library",
        "--global-key-stop=Alt+F8"};
    libvlc_instance_t *first = libvlc_new(ARRAY_SIZE(args), args);
    libvlc_instance_t *second = libvlc_new(ARRAY_SIZE(args), args);
    assert(first && second);
    vlc_object_t *one = VLC_OBJECT(first->p_libvlc_int);
    vlc_object_t *two = VLC_OBJECT(second->p_libvlc_int);
    struct key_scope scope = {one, two, 'c'};
    vlc_actions_run_with_keycode(one, scope.keycode, nested_scope, &scope);
    assert(vlc_actions_get_current_keycode(one) == KEY_UNSET);
    struct observed_action observed = {0};
    var_AddCallback(one, "key-action", on_action, &observed);
    uint_fast32_t global = vlc_str2keycode("Alt+F8");
    var_SetInteger(one, "global-key-pressed", global);
    assert(observed.count == 1 && observed.action == ACTIONID_STOP);
    assert(observed.keycode == global);
    assert(vlc_actions_get_current_keycode(one) == KEY_UNSET);
    var_SetInteger(one, "key-action", ACTIONID_PLAY_PAUSE);
    assert(observed.count == 2 && observed.keycode == KEY_UNSET);
    var_DelCallback(one, "key-action", on_action, &observed);
    libvlc_release(second);
    libvlc_release(first);
}

int main(void)
{
    test_init();
#ifdef ENABLE_NLS
    test_literal_key_names();
#endif
    test_wheel(false);
    test_wheel(true);
    test_live_shortcuts();
    test_key_context();
    return 0;
}
