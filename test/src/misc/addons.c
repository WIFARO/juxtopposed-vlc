// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#define MODULE_NAME test_misc_addons
#undef VLC_DYNAMIC_PLUGIN
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_addons.h>
#include <vlc_interrupt.h>
#include <vlc_plugin.h>
#include <limits.h>

const char vlc_module_name[] = MODULE_STRING;

struct test_state {
    addon_entry_t *entries[2];
    unsigned count;
    vlc_sem_t started, ended;
    int statuses[8];
    unsigned discoveries;
};

static int Find(addons_finder_t *finder)
{
    for (unsigned i = 0; i < 2; ++i) {
        addon_entry_t *entry = addon_entry_New();
        assert(entry != NULL);
        entry->e_type = ADDON_EXTENSION;
        entry->e_state = ADDON_INSTALLED;
        entry->e_flags = ADDON_MANAGEABLE;
        entry->uuid[0] = i + 1;
        entry->psz_name = strdup(i ? "Second fixture" : "First fixture");
        ARRAY_APPEND(finder->entries, entry);
    }
    return VLC_SUCCESS;
}

static vlc_sem_t finder_started;
static int FindRepository(addons_finder_t *finder)
{
    if (!strcmp(finder->psz_uri, "fixture://empty")) return VLC_SUCCESS;
    if (!strcmp(finder->psz_uri, "fixture://failed")) return VLC_EGENERIC;
    if (!strcmp(finder->psz_uri, "fixture://memory")) return VLC_ENOMEM;
    vlc_sem_post(&finder_started);
    vlc_poll_i11e(NULL, 0, -1);
    return VLC_EGENERIC;
}

static int OpenFinder(vlc_object_t *object)
{
    addons_finder_t *finder = (addons_finder_t *)object;
    finder->pf_find = finder->psz_uri ? FindRepository : Find;
    return VLC_SUCCESS;
}

static int StoreAction(addons_storage_t *storage, addon_entry_t *entry)
{
    (void)storage; (void)entry;
    return VLC_SUCCESS;
}

static int StoreCatalog(addons_storage_t *storage, addon_entry_t **entries, int count)
{
    (void)storage; (void)entries; (void)count;
    return VLC_SUCCESS;
}

static int OpenStorage(vlc_object_t *object)
{
    addons_storage_t *storage = (addons_storage_t *)object;
    storage->pf_install = StoreAction;
    storage->pf_remove = StoreAction;
    storage->pf_catalog = StoreCatalog;
    return VLC_SUCCESS;
}

vlc_module_begin()
    set_capability("addons finder", INT_MAX)
    add_shortcut("addons.store.list")
    set_callback(OpenFinder)
    add_submodule()
        set_capability("addons storage", INT_MAX)
        add_shortcut("addons.store.install")
        set_callback(OpenStorage)
vlc_module_end()

VLC_EXPORT const vlc_plugin_cb vlc_static_modules[] = {VLC_SYMBOL(vlc_entry), NULL};

static void Found(addons_manager_t *manager, addon_entry_t *entry)
{
    struct test_state *state = manager->owner.sys;
    assert(state->count < ARRAY_SIZE(state->entries));
    state->entries[state->count++] = addon_entry_Hold(entry);
}

static void Ended(addons_manager_t *manager)
{
    struct test_state *state = manager->owner.sys;
    assert(state->discoveries < ARRAY_SIZE(state->statuses));
    state->statuses[state->discoveries++] = addons_manager_GetDiscoveryStatus(manager);
    vlc_sem_post(&state->ended);
}

static void Changed(addons_manager_t *manager, addon_entry_t *entry)
{
    struct test_state *state = manager->owner.sys;
    vlc_mutex_lock(&entry->lock);
    bool block = entry == state->entries[0] && entry->e_state == ADDON_UNINSTALLING;
    vlc_mutex_unlock(&entry->lock);
    if (block) {
        vlc_sem_post(&state->started);
        vlc_poll_i11e(NULL, 0, -1);
    }
}

int main(void)
{
    test_init();
    const char *args[] = {"--ignore-config", "--no-media-library", "--no-metadata-network-access"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance != NULL);
    struct test_state state = {0};
    vlc_sem_init(&state.started, 0);
    vlc_sem_init(&state.ended, 0);
    vlc_sem_init(&finder_started, 0);
    struct addons_manager_owner owner = {&state, Found, Ended, Changed};
    addons_manager_t *manager = addons_manager_New(VLC_OBJECT(instance->p_libvlc_int), &owner);
    assert(manager != NULL);
    assert(addons_manager_LoadCatalog(manager) == VLC_SUCCESS);
    assert(state.count == 2);
    assert(addons_manager_Remove(manager, state.entries[0]->uuid) == VLC_SUCCESS);
    vlc_sem_wait(&state.started);
    assert(addons_manager_Remove(manager, state.entries[1]->uuid) == VLC_SUCCESS);
    addons_manager_Delete(manager);
    // Held entries remain valid, including work cancelled before it starts.
    assert(state.entries[0]->e_state == ADDON_NOTINSTALLED);
    assert(state.entries[1]->e_state == ADDON_INSTALLED);
    addon_entry_Release(state.entries[0]);
    addon_entry_Release(state.entries[1]);

    manager = addons_manager_New(VLC_OBJECT(instance->p_libvlc_int), &owner);
    assert(manager != NULL);
    assert(addons_manager_GetDiscoveryStatus(manager) == VLC_SUCCESS);
    const char *uris[] = {"fixture://empty", "fixture://failed", "fixture://memory",
                          "fixture://empty", "fixture://failed"};
    const int expected[] = {VLC_SUCCESS, VLC_EGENERIC, VLC_ENOMEM, VLC_SUCCESS,
                            VLC_EGENERIC};
    for (size_t i = 0; i < ARRAY_SIZE(uris); ++i)
        addons_manager_Gather(manager, uris[i]);
    for (size_t i = 0; i < ARRAY_SIZE(uris); ++i) {
        vlc_sem_wait(&state.ended);
        assert(state.statuses[i] == expected[i]);
    }
    assert(state.discoveries == ARRAY_SIZE(uris));
    assert(addons_manager_GetDiscoveryStatus(manager) == VLC_EGENERIC);
    assert(state.count == 2); /* Empty success is not inferred from a row count. */
    addons_manager_Delete(manager);

    manager = addons_manager_New(VLC_OBJECT(instance->p_libvlc_int), &owner);
    assert(manager != NULL);
    addons_manager_Gather(manager, "fixture://repository");
    vlc_sem_wait(&finder_started);
    addons_manager_Delete(manager);
    libvlc_release(instance);
    printf("Add-on discovery: %zu queued results passed.\n", ARRAY_SIZE(uris));
    return 0;
}
