// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_configuration.h>
#include <vlc_fs.h>
#include <vlc_input_item.h>
#include <vlc_preparser.h>
#include <vlc_url.h>
#include <unistd.h>

struct context {
    vlc_sem_t done;
    int status;
    unsigned files;
};

static const char *const names[] = {"visible.keep", "ignored.skip", ".hidden.keep", "picture.jpg"};

static void on_subtree(vlc_preparser_req *req, input_item_node_t *node,
                       void *userdata)
{
    (void)req;
    struct context *ctx = userdata;
    for (int i = 0; i < node->i_children; ++i) {
        char *name = input_item_GetName(node->pp_children[i]->p_item);
        assert(name);
        bool found = false;
        for (unsigned j = 0; j < ARRAY_SIZE(names); ++j)
            if (!strcmp(name, names[j])) {
                ctx->files |= 1u << j;
                found = true;
            }
        assert(found);
        free(name);
    }
    input_item_node_Delete(node);
}

static void on_ended(vlc_preparser_req *req, int status, void *userdata)
{
    (void)req;
    struct context *ctx = userdata;
    ctx->status = status;
    vlc_sem_post(&ctx->done);
}

int main(void)
{
    test_init();
    char directory[] = "preparser-options-XXXXXX";
    int fd = vlc_mkstemp(directory);
    assert(fd >= 0);
    vlc_close(fd);
    assert(vlc_unlink(directory) == 0);
    assert(vlc_mkdir(directory, 0700) == 0);
    for (unsigned i = 0; i < ARRAY_SIZE(names); ++i) {
        char *path;
        assert(asprintf(&path, "%s/%s", directory, names[i]) >= 0);
        FILE *file = vlc_fopen(path, "wb");
        assert(file);
        fclose(file);
        free(path);
    }
    char *uri = vlc_path2uri(directory, NULL);
    assert(uri);
    const char *args[] = {"--ignore-config", "--no-plugins-cache",
        "--no-media-library", "--no-metadata-network-access"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance);
    static const struct {
        bool show_hidden;
        const char *ignored;
        const char *hidden_option;
        const char *filter_option;
        unsigned flags;
        unsigned expected;
    } cases[] = {
        {false, "skip", NULL, NULL, 0, 9},
        {true, "", NULL, NULL, 0, 15},
        {false, "skip", "show-hiddenfiles", "ignore-filetypes=", VLC_INPUT_OPTION_TRUSTED, 15},
        {false, "skip", "show-hiddenfiles", "ignore-filetypes=", 0, 9},
        {true, "keep", NULL, NULL, 0, 10},
        {false, "", NULL, NULL, 0, 11},
        {true, "", "no-show-hiddenfiles", "ignore-filetypes=skip,quoted\"type,Δ", VLC_INPUT_OPTION_TRUSTED, 9},
    };
    bool passed = true;
    for (unsigned external = 0; external < 2; ++external) {
#ifndef HAVE_VLC_EXTERNAL_PREPARSER
        if (external) break;
#endif
        const struct vlc_preparser_cfg cfg = {
            .types = VLC_PREPARSER_TYPE_PARSE,
            .max_parser_threads = 1,
            .timeout = VLC_TICK_FROM_SEC(5),
            .external_process = external,
        };
        vlc_preparser_t *parser = vlc_preparser_New(VLC_OBJECT(instance->p_libvlc_int), &cfg);
        assert(parser);
        for (unsigned i = 0; i < ARRAY_SIZE(cases); ++i) {
            config_PutInt("show-hiddenfiles", cases[i].show_hidden);
            config_PutPsz("ignore-filetypes", cases[i].ignored);
            input_item_t *item = input_item_New(uri, "Folder");
            assert(item);
            if (cases[i].hidden_option) {
                assert(input_item_AddOption(item, cases[i].hidden_option, cases[i].flags) == VLC_SUCCESS);
                assert(input_item_AddOption(item, cases[i].filter_option, cases[i].flags) == VLC_SUCCESS);
            }
            struct context ctx = {.status = VLC_EGENERIC};
            vlc_sem_init(&ctx.done, 0);
            static const struct vlc_preparser_cbs cbs = {
                .on_ended = on_ended,
                .on_subtree_added = on_subtree,
            };
            vlc_preparser_req *req = vlc_preparser_req_NewParse(parser, item,
                VLC_PREPARSER_TYPE_PARSE | VLC_PREPARSER_OPTION_SUBITEMS, &cbs, &ctx);
            assert(req);
            assert(vlc_preparser_Submit(parser, req) == VLC_SUCCESS);
            vlc_sem_wait(&ctx.done);
            vlc_preparser_req_Release(req);
            input_item_Release(item);
            fprintf(stderr, "folder options external=%u case=%u status=%d files=%u expected=%u\n",
                external, i, ctx.status, ctx.files, cases[i].expected);
            passed &= ctx.status == VLC_SUCCESS && ctx.files == cases[i].expected;
        }
        vlc_preparser_Delete(parser);
    }
    libvlc_release(instance);
    free(uri);
    for (unsigned i = 0; i < ARRAY_SIZE(names); ++i) {
        char *path;
        assert(asprintf(&path, "%s/%s", directory, names[i]) >= 0);
        assert(vlc_unlink(path) == 0);
        free(path);
    }
    assert(rmdir(directory) == 0);
    return passed ? 0 : 1;
}
