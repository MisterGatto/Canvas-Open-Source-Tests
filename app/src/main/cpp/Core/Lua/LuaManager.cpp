#include "LuaManager.h"
#include "GGLib.h"
#include "CanvasLuaExt.h"
#include "LuaUIBridge.h"
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <android/log.h>

#define LOG_TAG "CanvasLuaManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace Canvas::Lua {

LuaManager& LuaManager::getInstance() {
    static LuaManager instance;
    return instance;
}

LuaManager::~LuaManager() {
    stopAll();
}

void LuaManager::scanModsDirectory(const std::string& dirPath) {
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        std::string filename = entry->d_name;
        if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".lua") {
            std::string fullPath = dirPath + "/" + filename;
            std::string baseName = filename.substr(0, filename.size() - 4);

            bool alreadyLoaded = false;
            {
                std::lock_guard<std::mutex> lock(m_scriptsMutex);
                for (const auto& s : m_scripts) {
                    if (s->path == fullPath || s->name == baseName) {
                        alreadyLoaded = true;
                        break;
                    }
                }
            }
            if (!alreadyLoaded) {
                loadScript(fullPath, baseName);
            }
        }
    }
    closedir(dir);
}

void LuaManager::rescan() {
    LOGI("Rescanning directories for .lua scripts...");
    scanModsDirectory("/data/user/0/git.artdeell.skymodloader/files/mods");
    scanModsDirectory("/data/data/git.artdeell.skymodloader/files/mods");
    scanModsDirectory("/sdcard/Download");
    scanModsDirectory("/storage/emulated/0/Download");
    scanModsDirectory("/storage/emulated/0/Android/data/git.artdeell.skymodloader/files");
    scanModsDirectory("/storage/emulated/0/Android/data/git.artdeell.skymodloader/files/mods");
}

void LuaManager::init() {
    if (m_initialized) return;
    m_initialized = true;
    LOGI("Canvas LuaManager initialized");
    rescan();
}

bool LuaManager::loadScript(const std::string& path, const std::string& name,
                            const std::string& author, const std::string& version,
                            const std::string& description) {
    std::string scriptName = name;
    if (scriptName.empty()) {
        size_t lastSlash = path.find_last_of("/\\");
        scriptName = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
    }

    auto instance = std::make_shared<LuaScriptInstance>();
    instance->name = scriptName;
    instance->path = path;
    instance->author = author;
    instance->version = version;
    instance->description = description;
    instance->scanner = std::make_unique<LuaMemoryScanner>();
    instance->isRunning = false;
    instance->shouldStop = false;
    instance->uiEnabled = false;

    {
        std::lock_guard<std::mutex> lock(m_scriptsMutex);
        m_scripts.push_back(instance);
    }

    LOGI("Discovered Lua script '%s' from %s (inactive until enabled in Canvas Menu)", scriptName.c_str(), path.c_str());
    return true;
}

bool LuaManager::runScriptString(const std::string& code, const std::string& name) {
    auto instance = std::make_shared<LuaScriptInstance>();
    instance->name = name;
    instance->path = "";
    instance->scanner = std::make_unique<LuaMemoryScanner>();
    instance->isRunning = true;
    instance->shouldStop = false;
    instance->uiEnabled = true;

    {
        std::lock_guard<std::mutex> lock(m_scriptsMutex);
        m_scripts.push_back(instance);
    }

    instance->workerThread = std::thread(&LuaManager::runScriptThread, this, instance, code, false);
    instance->workerThread.detach();

    LOGI("Started inline Lua script '%s'", name.c_str());
    return true;
}

void LuaManager::startScript(std::shared_ptr<LuaScriptInstance> script) {
    if (!script) return;
    if (script->isRunning.load()) {
        script->uiEnabled = true;
        LuaUIBridge::getInstance().setUIEnabled(true);
        return;
    }

    script->isRunning = true;
    script->shouldStop = false;
    script->uiEnabled = true;
    LuaUIBridge::getInstance().setUIEnabled(true);

    script->workerThread = std::thread(&LuaManager::runScriptThread, this, script, script->path, true);
    script->workerThread.detach();

    LOGI("Started Lua script '%s'", script->name.c_str());
}

void LuaManager::stopScript(std::shared_ptr<LuaScriptInstance> script) {
    if (!script) return;
    script->shouldStop = true;
    script->uiEnabled = false;
    LuaUIBridge::getInstance().cancelCurrentRequest();
    LOGI("Stopped Lua script '%s'", script->name.c_str());
}

void LuaManager::stopScript(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_scriptsMutex);
    for (auto& s : m_scripts) {
        if (s->name == name) {
            stopScript(s);
            break;
        }
    }
}

void LuaManager::stopAll() {
    std::lock_guard<std::mutex> lock(m_scriptsMutex);
    for (auto& s : m_scripts) {
        stopScript(s);
    }
}

void LuaManager::renderUI() {
    LuaUIBridge::getInstance().renderImGui();
}

std::vector<LuaScriptInfo> LuaManager::getScriptsInfo() {
    std::lock_guard<std::mutex> lock(m_scriptsMutex);
    std::vector<LuaScriptInfo> list;
    for (const auto& s : m_scripts) {
        LuaScriptInfo info;
        info.name = s->name;
        info.path = s->path;
        info.author = s->author;
        info.version = s->version;
        info.description = s->description;
        info.isRunning = s->isRunning.load();
        list.push_back(info);
    }
    return list;
}

void LuaManager::runScriptThread(std::shared_ptr<LuaScriptInstance> script, std::string codeOrPath, bool isFile) {
    lua_State* L = luaL_newstate();
    if (!L) {
        LOGE("Failed to allocate lua_State for %s", script->name.c_str());
        script->isRunning = false;
        return;
    }
    script->L = L;

    luaL_openlibs(L);

    // Backward compatibility aliases for obfuscated scripts & legacy loaders
    // 1. loadstring -> load
    lua_getglobal(L, "load");
    lua_setglobal(L, "loadstring");

    // 2. unpack -> table.unpack
    lua_getglobal(L, "table");
    lua_getfield(L, -1, "unpack");
    lua_setglobal(L, "unpack");
    lua_pop(L, 1);

    // Register APIs
    GGLib::registerLib(L, script->scanner.get(), script->path);
    CanvasLuaExt::registerLib(L);

    // Run comprehensive Lua 5.1/5.2 polyfills for legacy/obfuscated GG scripts
    const char* compatBootstrap = R"LUA(
        if not loadstring then loadstring = load end
        if not unpack then unpack = table.unpack end
        if not table.unpack then table.unpack = unpack end
        if not table.getn then table.getn = function(t) return #t end end
        if not table.foreach then
            table.foreach = function(t, f) for k, v in pairs(t) do local r = f(k, v) if r ~= nil then return r end end end
        end
        if not table.foreachi then
            table.foreachi = function(t, f) for i, v in ipairs(t) do local r = f(i, v) if r ~= nil then return r end end end
        end
        if not math.mod then math.mod = math.fmod end
        if not string.gfind then string.gfind = string.gmatch end

        if not setfenv then
            setfenv = function(fn, env)
                if type(fn) == "number" then
                    local info = debug.getinfo(fn + 1, "f")
                    if info then fn = info.func end
                end
                if type(fn) == "function" then
                    local i = 1
                    while true do
                        local name = debug.getupvalue(fn, i)
                        if name == "_ENV" then
                            debug.setupvalue(fn, i, env)
                            break
                        elseif not name then
                            break
                        end
                        i = i + 1
                    end
                end
                return fn
            end
        end

        if not getfenv then
            getfenv = function(fn)
                if type(fn) == "number" then
                    local info = debug.getinfo(fn + 1, "f")
                    if info then fn = info.func end
                end
                if type(fn) == "function" then
                    local i = 1
                    while true do
                        local name, val = debug.getupvalue(fn, i)
                        if name == "_ENV" then
                            return val
                        elseif not name then
                            break
                        end
                        i = i + 1
                    end
                end
                return _G
            end
        end

        -- Built-in JSON encoder/decoder for scripts requiring JSON interface
        local json = {}
        local function kind_of(obj)
            if type(obj) ~= 'table' then return type(obj) end
            local i = 1
            for _ in pairs(obj) do
                if obj[i] ~= nil then i = i + 1 else return 'table' end
            end
            if i == 1 then return 'table' else return 'array' end
        end
        local function escape_str(s)
            local in_char  = {'\\', '"', '/', '\b', '\f', '\n', '\r', '\t'}
            local out_char = {'\\', '"', '/',  'b',  'f',  'n',  'r',  't'}
            for i, c in ipairs(in_char) do
                s = s:gsub(c, '\\' .. out_char[i])
            end
            return s
        end
        function json.encode(val)
            local t = type(val)
            if t == 'nil' then return 'null'
            elseif t == 'boolean' or t == 'number' then return tostring(val)
            elseif t == 'string' then return '"' .. escape_str(val) .. '"'
            elseif t == 'table' then
                local kind = kind_of(val)
                local parts = {}
                if kind == 'array' then
                    for _, v in ipairs(val) do parts[#parts + 1] = json.encode(v) end
                    return '[' .. table.concat(parts, ',') .. ']'
                else
                    for k, v in pairs(val) do parts[#parts + 1] = json.encode(tostring(k)) .. ':' .. json.encode(v) end
                    return '{' .. table.concat(parts, ',') .. '}'
                end
            else return '"' .. tostring(val) .. '"' end
        end

        local parse_val
        local function next_c(s, idx, set, neg)
            for i = idx, #s do
                if set[s:sub(i, i)] ~= neg then return i end
            end
            return #s + 1
        end
        local sp_chars = {[' ']=true, ['\t']=true, ['\r']=true, ['\n']=true}
        local dl_chars = {[' ']=true, ['\t']=true, ['\r']=true, ['\n']=true, [']']=true, ['}']=true, [',']=true}
        local function parse_obj(s, i)
            local res = {}
            i = i + 1
            while true do
                i = next_c(s, i, sp_chars, true)
                if s:sub(i, i) == "}" then return res, i + 1 end
                if s:sub(i, i) ~= '"' then return res, i end
                local key; key, i = parse_val(s, i)
                if not key then return res, i end
                i = next_c(s, i, sp_chars, true)
                if s:sub(i, i) ~= ":" then return res, i end
                i = next_c(s, i + 1, sp_chars, true)
                local val; val, i = parse_val(s, i)
                res[key] = val
                i = next_c(s, i, sp_chars, true)
                local chr = s:sub(i, i)
                i = i + 1
                if chr == "}" then break end
                if chr ~= "," then return res, i end
            end
            return res, i
        end
        local function parse_arr(s, i)
            local res = {}
            local n = 1
            i = i + 1
            while true do
                i = next_c(s, i, sp_chars, true)
                if s:sub(i, i) == "]" then return res, i + 1 end
                local val; val, i = parse_val(s, i)
                res[n] = val
                n = n + 1
                i = next_c(s, i, sp_chars, true)
                local chr = s:sub(i, i)
                i = i + 1
                if chr == "]" then break end
                if chr ~= "," then return res, i end
            end
            return res, i
        end
        local function parse_str(s, i)
            local has_esc = false
            local last
            for j = i + 1, #s do
                local x = s:byte(j)
                if x == 34 and last ~= 92 then
                    local sub = s:sub(i + 1, j - 1)
                    if has_esc then
                        local unesc = {['\\\\']='\\', ['\\"']='"', ['\\/']='/', ['\\b']='\b', ['\\f']='\f', ['\\n']='\n', ['\\r']='\r', ['\\t']='\t'}
                        sub = sub:gsub('\\.', unesc)
                    end
                    return sub, j + 1
                end
                if x == 92 then has_esc = true; last = (last == 92) and 0 or 92 else last = x end
            end
            return "", #s + 1
        end
        parse_val = function(s, idx)
            local c = s:sub(idx, idx)
            if c == "{" then return parse_obj(s, idx)
            elseif c == "[" then return parse_arr(s, idx)
            elseif c == '"' then return parse_str(s, idx)
            elseif c:find("[%d%-]") then
                local x = next_c(s, idx, dl_chars)
                return tonumber(s:sub(idx, x - 1)), x
            elseif s:sub(idx, idx + 3) == "true" then return true, idx + 4
            elseif s:sub(idx, idx + 4) == "false" then return false, idx + 5
            elseif s:sub(idx, idx + 3) == "null" then return nil, idx + 4 end
            return nil, idx
        end
        function json.decode(s)
            if type(s) ~= "string" then return nil end
            local i = next_c(s, 1, sp_chars, true)
            local res, _ = parse_val(s, i)
            return res
        end

        _G.json = json
        _G.json_encode = json.encode
        _G.json_decode = json.decode
    )LUA";
    luaL_dostring(L, compatBootstrap);

    int status = 0;
    if (isFile) {
        status = luaL_loadfile(L, codeOrPath.c_str());
    } else {
        status = luaL_loadstring(L, codeOrPath.c_str());
    }

    if (status != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        LOGE("Lua Load Error in %s: %s", script->name.c_str(), err ? err : "unknown error");
        LuaUIBridge::getInstance().showToast(std::string("Lua Error: ") + (err ? err : ""), false);
        lua_pop(L, 1);
    } else {
        status = lua_pcall(L, 0, LUA_MULTRET, 0);
        if (status != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            LOGE("Lua Runtime Error in %s: %s", script->name.c_str(), err ? err : "unknown error");
            LuaUIBridge::getInstance().showToast(std::string("Lua Runtime Error: ") + (err ? err : ""), false);
            lua_pop(L, 1);
        } else {
            LOGI("Lua script '%s' completed successfully", script->name.c_str());
        }
    }

    lua_close(L);
    script->L = nullptr;
    script->isRunning = false;
}

std::vector<std::shared_ptr<LuaScriptInstance>> LuaManager::getScripts() {
    std::lock_guard<std::mutex> lock(m_scriptsMutex);
    return m_scripts;
}

} // namespace Canvas::Lua
