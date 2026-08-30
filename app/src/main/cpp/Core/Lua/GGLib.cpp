#include "GGLib.h"
#include "LuaUIBridge.h"
#include "Core/Canvas/Canvas.h"
#include <jni.h>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <android/log.h>

#define LOG_TAG "CanvasGGLib"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace Canvas::Lua {

static std::string performHttpGet(const std::string& url) {
    if (!Canvas::javaVM) return "";
    JNIEnv* env = nullptr;
    bool needDetach = false;
    if (Canvas::javaVM->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (Canvas::javaVM->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            needDetach = true;
        }
    }
    if (!env) return "";

    std::string result = "";
    jclass mainActivityClass = env->FindClass("git/artdeell/skymodloader/MainActivity");
    if (mainActivityClass) {
        jmethodID httpGetMethod = env->GetStaticMethodID(mainActivityClass, "httpGet", "(Ljava/lang/String;)Ljava/lang/String;");
        if (httpGetMethod) {
            jstring jUrl = env->NewStringUTF(url.c_str());
            jstring jRes = (jstring)env->CallStaticObjectMethod(mainActivityClass, httpGetMethod, jUrl);
            env->DeleteLocalRef(jUrl);
            if (jRes) {
                const char* utf = env->GetStringUTFChars(jRes, nullptr);
                if (utf) {
                    result = utf;
                    env->ReleaseStringUTFChars(jRes, utf);
                }
                env->DeleteLocalRef(jRes);
            }
        }
        env->DeleteLocalRef(mainActivityClass);
    }
    if (needDetach) {
        Canvas::javaVM->DetachCurrentThread();
    }
    return result;
}

static LuaMemoryScanner* getScanner(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "CANVAS_SCANNER_PTR");
    auto* scanner = static_cast<LuaMemoryScanner*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return scanner;
}

// gg.searchNumber(text, [type], [encrypted], [sign], [memoryFrom], [memoryTo], [limit])
static int gg_searchNumber(lua_State* L) {
    auto* scanner = getScanner(L);
    if (!scanner) {
        lua_pushboolean(L, 0);
        return 1;
    }

    const char* text = luaL_checkstring(L, 1);
    uint32_t type = lua_isnoneornil(L, 2) ? GG_TYPE_AUTO : static_cast<uint32_t>(lua_tointeger(L, 2));
    // arg 3: encrypted (ignored in-process)
    uint32_t sign = lua_isnoneornil(L, 4) ? GG_SIGN_EQUAL : static_cast<uint32_t>(lua_tointeger(L, 4));
    uintptr_t memFrom = lua_isnoneornil(L, 5) ? 0 : static_cast<uintptr_t>(lua_tointeger(L, 5));
    uintptr_t memTo = lua_isnoneornil(L, 6) ? 0 : static_cast<uintptr_t>(lua_tointeger(L, 6));
    size_t limit = lua_isnoneornil(L, 7) ? 10000 : static_cast<size_t>(lua_tointeger(L, 7));

    bool ok = scanner->searchNumber(text, type, sign, memFrom, memTo, limit);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// gg.refineNumber(text, [type], [encrypted], [sign])
static int gg_refineNumber(lua_State* L) {
    auto* scanner = getScanner(L);
    if (!scanner) {
        lua_pushboolean(L, 0);
        return 1;
    }

    const char* text = luaL_checkstring(L, 1);
    uint32_t type = lua_isnoneornil(L, 2) ? GG_TYPE_AUTO : static_cast<uint32_t>(lua_tointeger(L, 2));
    uint32_t sign = lua_isnoneornil(L, 4) ? GG_SIGN_EQUAL : static_cast<uint32_t>(lua_tointeger(L, 4));

    bool ok = scanner->refineNumber(text, type, sign);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// gg.getResults(count, [offset])
static int gg_getResults(lua_State* L) {
    auto* scanner = getScanner(L);
    if (!scanner) {
        lua_newtable(L);
        return 1;
    }

    size_t count = lua_isnoneornil(L, 1) ? 100 : static_cast<size_t>(lua_tointeger(L, 1));
    size_t offset = lua_isnoneornil(L, 2) ? 0 : static_cast<size_t>(lua_tointeger(L, 2));

    auto results = scanner->getResults(count, offset);
    lua_createtable(L, static_cast<int>(results.size()), 0);

    for (size_t i = 0; i < results.size(); ++i) {
        lua_createtable(L, 0, 4);

        lua_pushinteger(L, static_cast<lua_Integer>(results[i].address));
        lua_setfield(L, -2, "address");

        lua_pushinteger(L, results[i].type);
        lua_setfield(L, -2, "flags");

        lua_pushstring(L, results[i].valueStr.c_str());
        lua_setfield(L, -2, "value");

        lua_pushstring(L, results[i].name.c_str());
        lua_setfield(L, -2, "name");

        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }

    return 1;
}

// gg.getResultsCount()
static int gg_getResultsCount(lua_State* L) {
    auto* scanner = getScanner(L);
    lua_pushinteger(L, scanner ? static_cast<lua_Integer>(scanner->getResultsCount()) : 0);
    return 1;
}

// gg.clearResults()
static int gg_clearResults(lua_State* L) {
    auto* scanner = getScanner(L);
    if (scanner) scanner->clearResults();
    return 0;
}

// gg.setValues(table)
static int gg_setValues(lua_State* L) {
    auto* scanner = getScanner(L);
    if (!scanner || !lua_istable(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    std::vector<SearchResult> items;
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        if (lua_istable(L, -1)) {
            SearchResult sr;
            lua_getfield(L, -1, "address");
            sr.address = static_cast<uintptr_t>(lua_tointeger(L, -1));
            lua_pop(L, 1);

            lua_getfield(L, -1, "flags");
            sr.type = lua_isnoneornil(L, -1) ? GG_TYPE_DWORD : static_cast<uint32_t>(lua_tointeger(L, -1));
            lua_pop(L, 1);

            lua_getfield(L, -1, "value");
            if (lua_isnumber(L, -1) || lua_isstring(L, -1)) {
                sr.valueStr = lua_tostring(L, -1);
            }
            lua_pop(L, 1);

            if (sr.address != 0 && !sr.valueStr.empty()) {
                items.push_back(sr);
            }
        }
        lua_pop(L, 1);
    }

    bool ok = scanner->setValues(items);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// gg.getValues(table)
static int gg_getValues(lua_State* L) {
    auto* scanner = getScanner(L);
    if (!scanner || !lua_istable(L, 1)) {
        lua_pushvalue(L, 1);
        return 1;
    }

    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        if (lua_istable(L, -1)) {
            SearchResult sr;
            lua_getfield(L, -1, "address");
            sr.address = static_cast<uintptr_t>(lua_tointeger(L, -1));
            lua_pop(L, 1);

            lua_getfield(L, -1, "flags");
            sr.type = lua_isnoneornil(L, -1) ? GG_TYPE_DWORD : static_cast<uint32_t>(lua_tointeger(L, -1));
            lua_pop(L, 1);

            std::vector<SearchResult> single = { sr };
            if (scanner->getValues(single)) {
                lua_pushstring(L, single[0].valueStr.c_str());
                lua_setfield(L, -2, "value");
            }
        }
        lua_pop(L, 1);
    }

    lua_pushvalue(L, 1);
    return 1;
}

// gg.editAll(value, type)
static int gg_editAll(lua_State* L) {
    auto* scanner = getScanner(L);
    if (!scanner) {
        lua_pushinteger(L, 0);
        return 1;
    }

    const char* valStr = luaL_checkstring(L, 1);
    uint32_t type = static_cast<uint32_t>(luaL_checkinteger(L, 2));

    size_t count = scanner->editAll(valStr, type);
    lua_pushinteger(L, static_cast<lua_Integer>(count));
    return 1;
}

// gg.getRangesList([filter])
static int gg_getRangesList(lua_State* L) {
    auto* scanner = getScanner(L);
    std::string filter = lua_isstring(L, 1) ? lua_tostring(L, 1) : "";

    auto ranges = scanner ? scanner->getMemoryRanges(filter) : std::vector<MemoryRange>{};
    lua_createtable(L, static_cast<int>(ranges.size()), 0);

    for (size_t i = 0; i < ranges.size(); ++i) {
        lua_createtable(L, 0, 6);

        lua_pushinteger(L, static_cast<lua_Integer>(ranges[i].start));
        lua_setfield(L, -2, "start");

        lua_pushinteger(L, static_cast<lua_Integer>(ranges[i].end));
        lua_setfield(L, -2, "end");

        lua_pushstring(L, ranges[i].perms.c_str());
        lua_setfield(L, -2, "type");

        lua_pushstring(L, ranges[i].name.c_str());
        lua_setfield(L, -2, "name");

        lua_pushstring(L, ranges[i].path.c_str());
        lua_setfield(L, -2, "internalName");

        std::string rangeStr = "O";
        if (ranges[i].regionType & GG_REGION_CODE_APP) rangeStr = "Xa";
        else if (ranges[i].regionType & GG_REGION_CODE_SYS) rangeStr = "Xs";
        else if (ranges[i].regionType & GG_REGION_C_ALLOC) rangeStr = "Ca";
        else if (ranges[i].regionType & GG_REGION_C_DATA) rangeStr = "Cd";
        else if (ranges[i].regionType & GG_REGION_C_BSS) rangeStr = "Cb";
        else if (ranges[i].regionType & GG_REGION_C_HEAP) rangeStr = "Ch";
        else if (ranges[i].regionType & GG_REGION_JAVA_HEAP) rangeStr = "Jh";
        else if (ranges[i].regionType & GG_REGION_JAVA) rangeStr = "J";
        else if (ranges[i].regionType & GG_REGION_ANONYMOUS) rangeStr = "A";
        else if (ranges[i].regionType & GG_REGION_ASHMEM) rangeStr = "As";
        else if (ranges[i].regionType & GG_REGION_STACK) rangeStr = "S";
        else if (ranges[i].regionType & GG_REGION_BAD) rangeStr = "B";

        lua_pushstring(L, rangeStr.c_str());
        lua_setfield(L, -2, "state");

        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

// gg.getValuesRange(table)
static int gg_getValuesRange(lua_State* L) {
    auto* scanner = getScanner(L);
    if (!scanner || !lua_istable(L, 1)) {
        lua_newtable(L);
        return 1;
    }

    auto ranges = scanner->getMemoryRanges("");
    lua_newtable(L);

    int idx = 1;
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        uintptr_t addr = 0;
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "address");
            addr = static_cast<uintptr_t>(lua_tointeger(L, -1));
            lua_pop(L, 1);
        } else if (lua_isnumber(L, -1)) {
            addr = static_cast<uintptr_t>(lua_tointeger(L, -1));
        }

        std::string rangeStr = "O";
        if (addr != 0) {
            for (const auto& r : ranges) {
                if (addr >= r.start && addr < r.end) {
                    if (r.regionType & GG_REGION_CODE_APP) rangeStr = "Xa";
                    else if (r.regionType & GG_REGION_CODE_SYS) rangeStr = "Xs";
                    else if (r.regionType & GG_REGION_C_ALLOC) rangeStr = "Ca";
                    else if (r.regionType & GG_REGION_C_DATA) rangeStr = "Cd";
                    else if (r.regionType & GG_REGION_C_BSS) rangeStr = "Cb";
                    else if (r.regionType & GG_REGION_C_HEAP) rangeStr = "Ch";
                    else if (r.regionType & GG_REGION_JAVA_HEAP) rangeStr = "Jh";
                    else if (r.regionType & GG_REGION_JAVA) rangeStr = "J";
                    else if (r.regionType & GG_REGION_ANONYMOUS) rangeStr = "A";
                    else if (r.regionType & GG_REGION_ASHMEM) rangeStr = "As";
                    else if (r.regionType & GG_REGION_STACK) rangeStr = "S";
                    else if (r.regionType & GG_REGION_BAD) rangeStr = "B";
                    else rangeStr = "O";
                    break;
                }
            }
        }

        lua_pushstring(L, rangeStr.c_str());
        lua_rawseti(L, -3, idx++);
        lua_pop(L, 1);
    }

    return 1;
}

// gg.getRanges()
static int gg_getRanges(lua_State* L) {
    auto* scanner = getScanner(L);
    lua_pushinteger(L, scanner ? scanner->getTargetRegions() : GG_REGION_ALL);
    return 1;
}

// gg.setRanges(mask)
static int gg_setRanges(lua_State* L) {
    auto* scanner = getScanner(L);
    uint32_t mask = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    if (scanner) scanner->setTargetRegions(mask);
    return 0;
}

// gg.allocatePage(mode, [address])
static int gg_allocatePage(lua_State* L) {
    size_t size = 4096;
    uintptr_t addr = LuaMemoryScanner::allocatePage(size);
    lua_pushinteger(L, static_cast<lua_Integer>(addr));
    return 1;
}

// gg.sleep(ms)
static int gg_sleep(lua_State* L) {
    lua_Integer ms = luaL_checkinteger(L, 1);
    if (ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
    return 0;
}

// gg.toast(text, [isShort])
static int gg_toast(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    bool isShort = lua_isboolean(L, 2) ? (lua_toboolean(L, 2) != 0) : true;
    LuaUIBridge::getInstance().showToast(text, isShort);
    return 0;
}

// gg.alert(message, [positive], [negative], [neutral], [options/width])
static int gg_alert(lua_State* L) {
    const char* message = luaL_checkstring(L, 1);
    std::string positive = lua_isstring(L, 2) ? lua_tostring(L, 2) : "OK";
    std::string negative = lua_isstring(L, 3) ? lua_tostring(L, 3) : "";
    std::string neutral  = lua_isstring(L, 4) ? lua_tostring(L, 4) : "";
    float customWidth = 0.0f;
    float customHeight = 0.0f;

    if (lua_isnumber(L, 5)) {
        customWidth = static_cast<float>(lua_tonumber(L, 5));
    } else if (lua_istable(L, 5)) {
        lua_getfield(L, 5, "width");
        if (lua_isnumber(L, -1)) customWidth = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, 5, "height");
        if (lua_isnumber(L, -1)) customHeight = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
    }

    int btn = LuaUIBridge::getInstance().showAlert(message, positive, negative, neutral, customWidth, customHeight);
    lua_pushinteger(L, btn);
    return 1;
}

// gg.choice(items, [defaultChoice], [title], [options/width])
static int gg_choice(lua_State* L) {
    if (!lua_istable(L, 1)) {
        lua_pushnil(L);
        return 1;
    }

    std::vector<std::string> items;
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        if (lua_isstring(L, -1)) {
            items.push_back(lua_tostring(L, -1));
        }
        lua_pop(L, 1);
    }

    int defChoice = lua_isnumber(L, 2) ? static_cast<int>(lua_tointeger(L, 2)) : 1;
    std::string title = lua_isstring(L, 3) ? lua_tostring(L, 3) : "Menu";
    float customWidth = 0.0f;
    float customHeight = 0.0f;

    if (lua_isnumber(L, 4)) {
        customWidth = static_cast<float>(lua_tonumber(L, 4));
    } else if (lua_istable(L, 4)) {
        lua_getfield(L, 4, "width");
        if (lua_isnumber(L, -1)) customWidth = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, 4, "height");
        if (lua_isnumber(L, -1)) customHeight = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
    }

    int selected = LuaUIBridge::getInstance().showChoice(title, items, defChoice, customWidth, customHeight);
    if (selected <= 0) {
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, selected);
    }
    return 1;
}

// gg.multiChoice(items, [defaultSelections], [title], [options/width])
static int gg_multiChoice(lua_State* L) {
    if (!lua_istable(L, 1)) {
        lua_pushnil(L);
        return 1;
    }

    std::vector<std::string> items;
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        if (lua_isstring(L, -1)) {
            items.push_back(lua_tostring(L, -1));
        }
        lua_pop(L, 1);
    }

    std::map<int, bool> defaults;
    if (lua_istable(L, 2)) {
        lua_pushnil(L);
        while (lua_next(L, 2) != 0) {
            int k = static_cast<int>(lua_tointeger(L, -2));
            bool v = lua_toboolean(L, -1) != 0;
            defaults[k] = v;
            lua_pop(L, 1);
        }
    }

    std::string title = lua_isstring(L, 3) ? lua_tostring(L, 3) : "Select Options";
    float customWidth = 0.0f;
    float customHeight = 0.0f;

    if (lua_isnumber(L, 4)) {
        customWidth = static_cast<float>(lua_tonumber(L, 4));
    } else if (lua_istable(L, 4)) {
        lua_getfield(L, 4, "width");
        if (lua_isnumber(L, -1)) customWidth = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, 4, "height");
        if (lua_isnumber(L, -1)) customHeight = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
    }

    std::map<int, bool> results;
    bool ok = LuaUIBridge::getInstance().showMultiChoice(title, items, defaults, results, customWidth, customHeight);

    if (!ok) {
        lua_pushnil(L);
        return 1;
    }

    lua_createtable(L, 0, static_cast<int>(results.size()));
    for (const auto& [k, v] : results) {
        lua_pushboolean(L, v ? 1 : 0);
        lua_rawseti(L, -2, k);
    }
    return 1;
}

// gg.prompt(prompts, [defaults], [types], [options/width])
static int gg_prompt(lua_State* L) {
    if (!lua_istable(L, 1)) {
        lua_pushnil(L);
        return 1;
    }

    std::vector<PromptItem> promptItems;
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        PromptItem item;
        item.key = lua_isstring(L, -2) ? lua_tostring(L, -2) : std::to_string(lua_tointeger(L, -2));
        item.label = lua_tostring(L, -1);
        item.value = "";
        promptItems.push_back(item);
        lua_pop(L, 1);
    }

    // Set defaults
    if (lua_istable(L, 2)) {
        for (auto& item : promptItems) {
            lua_getfield(L, 2, item.key.c_str());
            if (!lua_isnil(L, -1)) {
                item.value = lua_tostring(L, -1);
            }
            lua_pop(L, 1);
        }
    }

    float customWidth = 0.0f;
    float customHeight = 0.0f;

    if (lua_isnumber(L, 4)) {
        customWidth = static_cast<float>(lua_tonumber(L, 4));
    } else if (lua_istable(L, 4)) {
        lua_getfield(L, 4, "width");
        if (lua_isnumber(L, -1)) customWidth = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, 4, "height");
        if (lua_isnumber(L, -1)) customHeight = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
    }

    std::map<std::string, std::string> results;
    bool ok = LuaUIBridge::getInstance().showPrompt("Input Prompt", promptItems, results, customWidth, customHeight);
    if (!ok) {
        lua_pushnil(L);
        return 1;
    }

    lua_createtable(L, 0, static_cast<int>(results.size()));
    for (const auto& [k, v] : results) {
        lua_pushstring(L, v.c_str());
        lua_setfield(L, -2, k.c_str());
    }
    return 1;
}

// gg.isVisible()
static int gg_isVisible(lua_State* L) {
    lua_pushboolean(L, LuaUIBridge::getInstance().isMenuVisible() ? 1 : 0);
    return 1;
}

// gg.setVisible(bool)
static int gg_setVisible(lua_State* L) {
    bool vis = lua_toboolean(L, 1) != 0;
    LuaUIBridge::getInstance().setMenuVisible(vis);
    return 0;
}

// gg.getTargetInfo() / gg.getTargetPackage()
static int gg_getTargetInfo(lua_State* L) {
    lua_createtable(L, 0, 4);
    lua_pushstring(L, "com.tgc.sky.android");
    lua_setfield(L, -2, "packageName");

    lua_pushstring(L, "Sky: Children of the Light");
    lua_setfield(L, -2, "label");

    lua_pushinteger(L, getpid());
    lua_setfield(L, -2, "processId");

    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "x64");
    return 1;
}

static int gg_getTargetPackage(lua_State* L) {
    lua_pushstring(L, "com.tgc.sky.android");
    return 1;
}

static int gg_requireVersion(lua_State* L) {
    lua_pushboolean(L, 1);
    return 1;
}

static int gg_require(lua_State* L) {
    lua_pushboolean(L, 1);
    return 1;
}

static int gg_getCacheDir(lua_State* L) {
    lua_pushstring(L, "/data/data/git.artdeell.skymodloader/cache");
    return 1;
}

static int gg_makeRequest(lua_State* L) {
    const char* url = luaL_checkstring(L, 1);
    std::string content = performHttpGet(url);
    int code = content.empty() ? 404 : 200;

    lua_newtable(L);
    lua_pushinteger(L, code);
    lua_setfield(L, -2, "code");
    lua_pushstring(L, content.c_str());
    lua_setfield(L, -2, "content");
    lua_newtable(L);
    lua_setfield(L, -2, "headers");
    return 1;
}

static int gg_bytes(lua_State* L) {
    const char* str = luaL_checkstring(L, 1);
    size_t len = strlen(str);
    lua_createtable(L, static_cast<int>(len), 0);
    for (size_t i = 0; i < len; ++i) {
        lua_pushinteger(L, static_cast<unsigned char>(str[i]));
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

static int gg_saveVariable(lua_State* L) {
    lua_pushboolean(L, 1);
    return 1;
}

static int gg_loadVariable(lua_State* L) {
    if (lua_gettop(L) >= 2) {
        lua_pushvalue(L, 2);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

static int gg_addListItems(lua_State* L) {
    lua_pushboolean(L, 1);
    return 1;
}

static int gg_getListItems(lua_State* L) {
    lua_newtable(L);
    return 1;
}

// gg.loadResults(results)
static int gg_loadResults(lua_State* L) {
    auto* scanner = getScanner(L);
    if (!scanner || !lua_istable(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    std::vector<SearchResult> items;
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        if (lua_istable(L, -1)) {
            SearchResult sr;
            lua_getfield(L, -1, "address");
            sr.address = static_cast<uintptr_t>(lua_tointeger(L, -1));
            lua_pop(L, 1);

            lua_getfield(L, -1, "flags");
            sr.type = lua_isnoneornil(L, -1) ? GG_TYPE_DWORD : static_cast<uint32_t>(lua_tointeger(L, -1));
            lua_pop(L, 1);

            lua_getfield(L, -1, "value");
            if (lua_isstring(L, -1) || lua_isnumber(L, -1)) {
                sr.valueStr = lua_tostring(L, -1);
            }
            lua_pop(L, 1);

            if (sr.address != 0) {
                items.push_back(sr);
            }
        }
        lua_pop(L, 1);
    }

    scanner->setResults(items);
    lua_pushboolean(L, 1);
    return 1;
}

// gg.searchPointer(maxOffset, [memFrom], [memTo], [limit])
static int gg_searchPointer(lua_State* L) {
    auto* scanner = getScanner(L);
    if (!scanner) {
        lua_pushboolean(L, 0);
        return 1;
    }

    size_t maxOffset = lua_isnoneornil(L, 1) ? 0 : static_cast<size_t>(lua_tointeger(L, 1));
    uintptr_t memFrom = lua_isnoneornil(L, 2) ? 0 : static_cast<uintptr_t>(lua_tointeger(L, 2));
    uintptr_t memTo = lua_isnoneornil(L, 3) ? 0 : static_cast<uintptr_t>(lua_tointeger(L, 3));
    size_t limit = lua_isnoneornil(L, 4) ? 10000 : static_cast<size_t>(lua_tointeger(L, 4));

    bool ok = scanner->searchPointer(maxOffset, memFrom, memTo, limit);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

static int gg_processResume(lua_State* L) {
    return 0;
}

static int gg_processPause(lua_State* L) {
    return 0;
}

static int gg_isPackageInstalled(lua_State* L) {
    lua_pushboolean(L, 1);
    return 1;
}

// Bitwise compatibility functions (for bit and bit32 modules)
static int bit_band(lua_State* L) {
    lua_Integer r = luaL_checkinteger(L, 1);
    int n = lua_gettop(L);
    for (int i = 2; i <= n; ++i) {
        r &= luaL_checkinteger(L, i);
    }
    lua_pushinteger(L, r);
    return 1;
}

static int bit_bor(lua_State* L) {
    lua_Integer r = luaL_checkinteger(L, 1);
    int n = lua_gettop(L);
    for (int i = 2; i <= n; ++i) {
        r |= luaL_checkinteger(L, i);
    }
    lua_pushinteger(L, r);
    return 1;
}

static int bit_bxor(lua_State* L) {
    lua_Integer r = luaL_checkinteger(L, 1);
    int n = lua_gettop(L);
    for (int i = 2; i <= n; ++i) {
        r ^= luaL_checkinteger(L, i);
    }
    lua_pushinteger(L, r);
    return 1;
}

static int bit_bnot(lua_State* L) {
    lua_Integer r = luaL_checkinteger(L, 1);
    lua_pushinteger(L, ~r);
    return 1;
}

static int bit_lshift(lua_State* L) {
    lua_Integer a = luaL_checkinteger(L, 1);
    lua_Integer b = luaL_checkinteger(L, 2);
    lua_pushinteger(L, a << b);
    return 1;
}

static int bit_rshift(lua_State* L) {
    lua_Unsigned a = static_cast<lua_Unsigned>(luaL_checkinteger(L, 1));
    lua_Integer b = luaL_checkinteger(L, 2);
    lua_pushinteger(L, a >> b);
    return 1;
}

static int bit_tohex(lua_State* L) {
    lua_Unsigned a = static_cast<lua_Unsigned>(luaL_checkinteger(L, 1));
    int digits = lua_isnumber(L, 2) ? static_cast<int>(lua_tointeger(L, 2)) : 8;
    char buf[32];
    snprintf(buf, sizeof(buf), "%0*llx", digits, (unsigned long long)a);
    lua_pushstring(L, buf);
    return 1;
}

static int gg_copyText(lua_State* L) {
    // text copied
    return 0;
}

static int gg_getFile(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "CANVAS_SCRIPT_PATH");
    return 1;
}

void GGLib::registerLib(lua_State* L, LuaMemoryScanner* scanner, const std::string& scriptPath) {
    // Store scanner pointer in registry
    lua_pushlightuserdata(L, scanner);
    lua_setfield(L, LUA_REGISTRYINDEX, "CANVAS_SCANNER_PTR");

    lua_pushstring(L, scriptPath.c_str());
    lua_setfield(L, LUA_REGISTRYINDEX, "CANVAS_SCRIPT_PATH");

    // Register bit and bit32 libraries
    lua_newtable(L);
    lua_pushcfunction(L, bit_band);   lua_setfield(L, -2, "band");
    lua_pushcfunction(L, bit_bor);    lua_setfield(L, -2, "bor");
    lua_pushcfunction(L, bit_bxor);   lua_setfield(L, -2, "bxor");
    lua_pushcfunction(L, bit_bnot);   lua_setfield(L, -2, "bnot");
    lua_pushcfunction(L, bit_lshift); lua_setfield(L, -2, "lshift");
    lua_pushcfunction(L, bit_rshift); lua_setfield(L, -2, "rshift");
    lua_pushcfunction(L, bit_tohex);  lua_setfield(L, -2, "tohex");
    lua_pushvalue(L, -1);
    lua_setglobal(L, "bit32");
    lua_setglobal(L, "bit");

    // Create global 'gg' table
    lua_newtable(L);

    // Register constants
    lua_pushinteger(L, GG_TYPE_AUTO);   lua_setfield(L, -2, "TYPE_AUTO");
    lua_pushinteger(L, GG_TYPE_BYTE);   lua_setfield(L, -2, "TYPE_BYTE");
    lua_pushinteger(L, GG_TYPE_WORD);   lua_setfield(L, -2, "TYPE_WORD");
    lua_pushinteger(L, GG_TYPE_DWORD);  lua_setfield(L, -2, "TYPE_DWORD");
    lua_pushinteger(L, GG_TYPE_XOR);    lua_setfield(L, -2, "TYPE_XOR");
    lua_pushinteger(L, GG_TYPE_FLOAT);  lua_setfield(L, -2, "TYPE_FLOAT");
    lua_pushinteger(L, GG_TYPE_QWORD);  lua_setfield(L, -2, "TYPE_QWORD");
    lua_pushinteger(L, GG_TYPE_DOUBLE); lua_setfield(L, -2, "TYPE_DOUBLE");

    lua_pushinteger(L, GG_REGION_ALL);       lua_setfield(L, -2, "REGION_ALL");
    lua_pushinteger(L, GG_REGION_ANONYMOUS); lua_setfield(L, -2, "REGION_ANONYMOUS");
    lua_pushinteger(L, GG_REGION_CODE_APP);  lua_setfield(L, -2, "REGION_CODE_APP");
    lua_pushinteger(L, GG_REGION_CODE_SYS);  lua_setfield(L, -2, "REGION_CODE_SYS");
    lua_pushinteger(L, GG_REGION_C_ALLOC);   lua_setfield(L, -2, "REGION_C_ALLOC");
    lua_pushinteger(L, GG_REGION_C_DATA);    lua_setfield(L, -2, "REGION_C_DATA");
    lua_pushinteger(L, GG_REGION_C_BSS);     lua_setfield(L, -2, "REGION_C_BSS");
    lua_pushinteger(L, GG_REGION_C_HEAP);    lua_setfield(L, -2, "REGION_C_HEAP");
    lua_pushinteger(L, GG_REGION_JAVA_HEAP); lua_setfield(L, -2, "REGION_JAVA_HEAP");
    lua_pushinteger(L, GG_REGION_JAVA);      lua_setfield(L, -2, "REGION_JAVA");
    lua_pushinteger(L, GG_REGION_STACK);     lua_setfield(L, -2, "REGION_STACK");
    lua_pushinteger(L, GG_REGION_ASHMEM);    lua_setfield(L, -2, "REGION_ASHMEM");
    lua_pushinteger(L, GG_REGION_BAD);       lua_setfield(L, -2, "REGION_BAD");
    lua_pushinteger(L, GG_REGION_OTHER);     lua_setfield(L, -2, "REGION_OTHER");

    lua_pushinteger(L, GG_SIGN_EQUAL);            lua_setfield(L, -2, "SIGN_EQUAL");
    lua_pushinteger(L, GG_SIGN_NOT_EQUAL);        lua_setfield(L, -2, "SIGN_NOT_EQUAL");
    lua_pushinteger(L, GG_SIGN_LESS_OR_EQUAL);    lua_setfield(L, -2, "SIGN_LESS_OR_EQUAL");
    lua_pushinteger(L, GG_SIGN_GREATER_OR_EQUAL); lua_setfield(L, -2, "SIGN_GREATER_OR_EQUAL");
    lua_pushinteger(L, GG_SIGN_LESS);             lua_setfield(L, -2, "SIGN_LESS");
    lua_pushinteger(L, GG_SIGN_GREATER);          lua_setfield(L, -2, "SIGN_GREATER");

    lua_pushstring(L, "com.tgc.sky.android"); lua_setfield(L, -2, "PACKAGE");
    lua_pushstring(L, "101.1");               lua_setfield(L, -2, "VERSION");
    lua_pushinteger(L, 10101);                lua_setfield(L, -2, "VERSION_INT");
    lua_pushinteger(L, 16142);                lua_setfield(L, -2, "BUILD");

    // Register functions
    lua_pushcfunction(L, gg_searchNumber);    lua_setfield(L, -2, "searchNumber");
    lua_pushcfunction(L, gg_refineNumber);    lua_setfield(L, -2, "refineNumber");
    lua_pushcfunction(L, gg_getResults);      lua_setfield(L, -2, "getResults");
    lua_pushcfunction(L, gg_getResultsCount); lua_setfield(L, -2, "getResultsCount");
    lua_pushcfunction(L, gg_clearResults);    lua_setfield(L, -2, "clearResults");
    lua_pushcfunction(L, gg_setValues);       lua_setfield(L, -2, "setValues");
    lua_pushcfunction(L, gg_getValues);       lua_setfield(L, -2, "getValues");
    lua_pushcfunction(L, gg_getValuesRange);  lua_setfield(L, -2, "getValuesRange");
    lua_pushcfunction(L, gg_editAll);         lua_setfield(L, -2, "editAll");
    lua_pushcfunction(L, gg_getRangesList);   lua_setfield(L, -2, "getRangesList");
    lua_pushcfunction(L, gg_getRanges);       lua_setfield(L, -2, "getRanges");
    lua_pushcfunction(L, gg_setRanges);       lua_setfield(L, -2, "setRanges");
    lua_pushcfunction(L, gg_allocatePage);    lua_setfield(L, -2, "allocatePage");
    lua_pushcfunction(L, gg_sleep);           lua_setfield(L, -2, "sleep");
    lua_pushcfunction(L, gg_toast);           lua_setfield(L, -2, "toast");
    lua_pushcfunction(L, gg_alert);           lua_setfield(L, -2, "alert");
    lua_pushcfunction(L, gg_choice);          lua_setfield(L, -2, "choice");
    lua_pushcfunction(L, gg_multiChoice);     lua_setfield(L, -2, "multiChoice");
    lua_pushcfunction(L, gg_prompt);          lua_setfield(L, -2, "prompt");
    lua_pushcfunction(L, gg_isVisible);       lua_setfield(L, -2, "isVisible");
    lua_pushcfunction(L, gg_setVisible);      lua_setfield(L, -2, "setVisible");
    lua_pushcfunction(L, gg_getTargetInfo);   lua_setfield(L, -2, "getTargetInfo");
    lua_pushcfunction(L, gg_getTargetPackage);lua_setfield(L, -2, "getTargetPackage");
    lua_pushcfunction(L, gg_requireVersion);  lua_setfield(L, -2, "requireVersion");
    lua_pushcfunction(L, gg_require);         lua_setfield(L, -2, "require");
    lua_pushcfunction(L, gg_copyText);        lua_setfield(L, -2, "copyText");
    lua_pushcfunction(L, gg_getFile);         lua_setfield(L, -2, "getFile");
    lua_pushcfunction(L, gg_getCacheDir);     lua_setfield(L, -2, "getCacheDir");
    lua_pushcfunction(L, gg_makeRequest);     lua_setfield(L, -2, "makeRequest");
    lua_pushcfunction(L, gg_bytes);           lua_setfield(L, -2, "bytes");
    lua_pushcfunction(L, gg_saveVariable);    lua_setfield(L, -2, "saveVariable");
    lua_pushcfunction(L, gg_loadVariable);    lua_setfield(L, -2, "loadVariable");
    lua_pushcfunction(L, gg_addListItems);    lua_setfield(L, -2, "addListItems");
    lua_pushcfunction(L, gg_getListItems);    lua_setfield(L, -2, "getListItems");
    lua_pushcfunction(L, gg_loadResults);     lua_setfield(L, -2, "loadResults");
    lua_pushcfunction(L, gg_searchPointer);   lua_setfield(L, -2, "searchPointer");
    lua_pushcfunction(L, gg_processResume);   lua_setfield(L, -2, "processResume");
    lua_pushcfunction(L, gg_processPause);    lua_setfield(L, -2, "processPause");
    lua_pushcfunction(L, gg_isPackageInstalled); lua_setfield(L, -2, "isPackageInstalled");

    lua_setglobal(L, "gg");
}

} // namespace Canvas::Lua
