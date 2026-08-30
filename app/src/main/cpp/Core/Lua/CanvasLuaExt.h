#pragma once

extern "C" {
#include "lua_src/lua.h"
#include "lua_src/lauxlib.h"
#include "lua_src/lualib.h"
}

#include <functional>
#include <string>

namespace Canvas::Lua {

class CanvasLuaExt {
public:
    static void registerLib(lua_State* L);
};

} // namespace Canvas::Lua
