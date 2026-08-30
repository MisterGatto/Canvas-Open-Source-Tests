#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>

extern "C" {
#include "lua_src/lua.h"
#include "lua_src/lauxlib.h"
#include "lua_src/lualib.h"
}

#include "LuaMemoryScanner.h"

namespace Canvas::Lua {

struct LuaScriptInfo {
    std::string name;
    std::string path;
    std::string author;
    std::string version;
    std::string description;
    bool isRunning = false;
    bool hasUI = false;
};

struct LuaScriptInstance {
    std::string name;
    std::string path;
    std::string author;
    std::string version;
    std::string description;
    lua_State* L = nullptr;
    std::unique_ptr<LuaMemoryScanner> scanner;
    std::thread workerThread;
    std::atomic<bool> isRunning{false};
    std::atomic<bool> shouldStop{false};
    std::atomic<bool> uiEnabled{true};
};

class LuaManager {
public:
    static LuaManager& getInstance();

    void init();
    bool loadScript(const std::string& path, const std::string& name = "",
                    const std::string& author = "", const std::string& version = "1.0",
                    const std::string& description = "");
    bool runScriptString(const std::string& code, const std::string& name = "inline");

    void startScript(std::shared_ptr<LuaScriptInstance> script);
    void stopScript(std::shared_ptr<LuaScriptInstance> script);
    void stopScript(const std::string& name);
    void stopAll();

    void renderUI();
    void rescan();
    void scanModsDirectory(const std::string& dirPath);
    std::vector<LuaScriptInfo> getScriptsInfo();
    std::vector<std::shared_ptr<LuaScriptInstance>> getScripts();

private:
    LuaManager() = default;
    ~LuaManager();

    void runScriptThread(std::shared_ptr<LuaScriptInstance> script, std::string codeOrPath, bool isFile);

    std::mutex m_scriptsMutex;
    std::vector<std::shared_ptr<LuaScriptInstance>> m_scripts;
    bool m_initialized = false;
};

} // namespace Canvas::Lua
