// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#ifndef MODULE_NAME
# define MODULE_NAME test_misc_addons
#endif
#undef NDEBUG
#include <assert.h>
#include <limits.h>
#include <vlc_common.h>
#include <vlc_addons.h>
#include <vlc_interrupt.h>
#include <vlc_plugin.h>

#ifndef VLC_DYNAMIC_PLUGIN
const char vlc_module_name[] = MODULE_STRING;
#endif

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

static int FindRepository(addons_finder_t *finder)
{
    if (!strcmp(finder->psz_uri, "fixture://empty")) return VLC_SUCCESS;
    if (!strcmp(finder->psz_uri, "fixture://failed")) return VLC_EGENERIC;
    if (!strcmp(finder->psz_uri, "fixture://memory")) return VLC_ENOMEM;
    vlc_sem_t *started = var_InheritAddress(finder, "test-addon-finder-started");
    assert(started);
    vlc_sem_post(started);
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

