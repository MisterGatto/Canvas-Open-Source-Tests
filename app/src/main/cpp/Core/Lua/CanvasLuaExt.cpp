#include "CanvasLuaExt.h"
#include "Cipher/Cipher.h"
#include "Cipher/CipherUtils.h"
#include "LuaMemoryScanner.h"
#include "LuaUIBridge.h"
#include "imgui.h"
#include <android/log.h>
#include <sstream>
#include <iomanip>

#define LOG_TAG "CanvasLuaExt"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace Canvas::Lua {

// canvas.findPattern(ida_pattern)
static int canvas_findPattern(lua_State* L) {
    const char* pattern = luaL_checkstring(L, 1);
    uintptr_t addr = Cipher::CipherScanIdaPattern(pattern);
    lua_pushinteger(L, static_cast<lua_Integer>(addr));
    return 1;
}

// canvas.findPatternAll(ida_pattern)
static int canvas_findPatternAll(lua_State* L) {
    const char* pattern = luaL_checkstring(L, 1);
    auto addrs = Cipher::CipherScanIdaPatternAll(pattern);

    lua_createtable(L, static_cast<int>(addrs.size()), 0);
    for (size_t i = 0; i < addrs.size(); ++i) {
        lua_pushinteger(L, static_cast<lua_Integer>(addrs[i]));
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

// canvas.getLibBase([libName])
static int canvas_getLibBase(lua_State* L) {
    uintptr_t base = Cipher::get_libBase();
    lua_pushinteger(L, static_cast<lua_Integer>(base));
    return 1;
}

// canvas.getGameVersion()
static int canvas_getGameVersion(lua_State* L) {
    lua_pushinteger(L, Cipher::getGameVersion());
    return 1;
}

// canvas.patch(address, hexBytes)
static int canvas_patch(lua_State* L) {
    uintptr_t address = static_cast<uintptr_t>(luaL_checkinteger(L, 1));
    const char* hexStr = luaL_checkstring(L, 2);

    // Convert hex string to byte array (e.g. "00 00 A0 E3" or "0000A0E3")
    std::vector<uint8_t> bytes;
    std::stringstream ss(hexStr);
    std::string byteStr;

    // Handle space-separated or continuous hex
    if (std::string(hexStr).find(' ') != std::string::npos) {
        while (ss >> byteStr) {
            bytes.push_back(static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16)));
        }
    } else {
        std::string raw(hexStr);
        for (size_t i = 0; i + 1 < raw.size(); i += 2) {
            bytes.push_back(static_cast<uint8_t>(std::stoul(raw.substr(i, 2), nullptr, 16)));
        }
    }

    if (bytes.empty()) {
        lua_pushboolean(L, 0);
        return 1;
    }

    bool ok = LuaMemoryScanner::safeWrite(address, bytes.data(), bytes.size());
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// canvas.setDialogSize(width, [height])
static int canvas_setDialogSize(lua_State* L) {
    float w = static_cast<float>(luaL_checknumber(L, 1));
    float h = lua_isnumber(L, 2) ? static_cast<float>(lua_tonumber(L, 2)) : 0.0f;
    LuaUIBridge::getInstance().setDefaultDialogSize(w, h);
    return 0;
}

// canvas.isBeta() -> returns false (deprecated / backward compatibility)
static int canvas_isBeta(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

// canvas.log(msg)
static int canvas_log(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    LOGI("[Lua] %s", msg);
    return 0;
}

void CanvasLuaExt::registerLib(lua_State* L) {
    lua_newtable(L);

    lua_pushcfunction(L, canvas_findPattern);    lua_setfield(L, -2, "findPattern");
    lua_pushcfunction(L, canvas_findPatternAll); lua_setfield(L, -2, "findPatternAll");
    lua_pushcfunction(L, canvas_getLibBase);     lua_setfield(L, -2, "getLibBase");
    lua_pushcfunction(L, canvas_getGameVersion); lua_setfield(L, -2, "getGameVersion");
    lua_pushcfunction(L, canvas_isBeta);         lua_setfield(L, -2, "isBeta");
    lua_pushcfunction(L, canvas_setDialogSize);  lua_setfield(L, -2, "setDialogSize");
    lua_pushcfunction(L, canvas_patch);          lua_setfield(L, -2, "patch");
    lua_pushcfunction(L, canvas_log);            lua_setfield(L, -2, "log");

    lua_setglobal(L, "canvas");
}

} // namespace Canvas::Lua
