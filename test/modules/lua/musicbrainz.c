// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <stdio.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

int main(void)
{
    lua_State *state = luaL_newstate();
    if (state == NULL)
        return 1;
    luaL_openlibs(state);
    lua_pushliteral(state, TOP_SRCDIR "/share/lua/meta/art/00_musicbrainz.lua");
    lua_setglobal(state, "provider_source");
    int result = luaL_dofile(state, TOP_SRCDIR "/test/modules/lua/musicbrainz.lua");
    if (result != 0)
        fprintf(stderr, "%s\n", lua_tostring(state, -1));
    lua_close(state);
    return result != 0;
}
