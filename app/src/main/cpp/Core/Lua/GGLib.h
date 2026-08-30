#pragma once

extern "C" {
#include "lua_src/lua.h"
#include "lua_src/lauxlib.h"
#include "lua_src/lualib.h"
}

#include "LuaMemoryScanner.h"

namespace Canvas::Lua {

class GGLib {
public:
    static void registerLib(lua_State* L, LuaMemoryScanner* scanner, const std::string& scriptPath = "");
};

} // namespace Canvas::Lua
