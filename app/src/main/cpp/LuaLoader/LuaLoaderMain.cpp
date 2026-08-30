#include "Core/Lua/LuaManager.h"
#include "Core/Lua/LuaUIBridge.h"
#include "Core/Canvas/Canvas.h"
#include "Core/imgui/imgui.h"
#include <android/log.h>

#define LOG_TAG "LuaLoader"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// Embedded ELF metadata section required by Canvas Mod Loader (marked 'a' so it is never stripped)
__asm__(
    ".section .config,\"a\",@progbits\n"
    ".global MOD_CONFIG_JSON\n"
    "MOD_CONFIG_JSON:\n"
    ".string \""
    "{\\n"
    "  \\\"name\\\": \\\"liblualoader.so\\\",\\n"
    "  \\\"displayName\\\": \\\"Lua Loader\\\",\\n"
    "  \\\"author\\\": \\\"Antonio & Artdev\\\",\\n"
    "  \\\"description\\\": \\\"Lua & GameGuardian Script Mod Loader\\\",\\n"
    "  \\\"majorVersion\\\": 1,\\n"
    "  \\\"minorVersion\\\": 0,\\n"
    "  \\\"patchVersion\\\": 0,\\n"
    "  \\\"displaysUI\\\": true,\\n"
    "  \\\"selfManagedUI\\\": false,\\n"
    "  \\\"dependencies\\\": []\\n"
    "}\"\n"
    ".previous\n"
);

static void LuaLoader_Draw(bool* open) {
    // Render Lua UI dialogs (alerts, prompts, choices, toasts)
    Canvas::Lua::LuaManager::getInstance().renderUI();

    if (!open || !*open) return;

    // Render Lua Scripts Control Panel
    ImGui::SetNextWindowSize(ImVec2(450, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Lua Loader (GameGuardian)", open)) {
        ImGui::TextColored(ImVec4(0.40f, 0.75f, 1.0f, 1.0f), "Lua & GameGuardian Script Manager");
        ImGui::Separator();

        auto scripts = Canvas::Lua::LuaManager::getInstance().getScripts();
        if (scripts.empty()) {
            ImGui::TextDisabled("No .lua scripts found in mods directory.");
            ImGui::TextWrapped("Place your .lua or GameGuardian scripts in the mods folder, or import them via the Launcher.");
        } else {
            ImGui::Text("Available Lua Scripts (%zu):", scripts.size());
            ImGui::Separator();

            if (ImGui::BeginTable("lua_scripts_table", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Script Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableHeadersRow();

                for (const auto& script : scripts) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);

                    bool isRunning = script->isRunning.load();
                    std::string label = script->name + "##toggle_" + script->name;
                    if (ImGui::Checkbox(label.c_str(), &isRunning)) {
                        if (isRunning) {
                            Canvas::Lua::LuaManager::getInstance().startScript(script);
                        } else {
                            Canvas::Lua::LuaManager::getInstance().stopScript(script);
                        }
                    }

                    ImGui::TableSetColumnIndex(1);
                    if (script->isRunning.load()) {
                        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Running");
                    } else {
                        ImGui::TextDisabled("Stopped");
                    }
                }
                ImGui::EndTable();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("🔄 Rescan All Folders")) {
            Canvas::Lua::LuaManager::getInstance().rescan();
        }

        static char manualPath[256] = "/sdcard/Download/";
        ImGui::InputText("##manual_path", manualPath, sizeof(manualPath));
        ImGui::SameLine();
        if (ImGui::Button("📂 Load Path")) {
            if (strlen(manualPath) > 0) {
                Canvas::Lua::LuaManager::getInstance().loadScript(manualPath);
            }
        }
    }
    ImGui::End();
}

extern "C" {

__attribute__((visibility("default")))
void* Start() {
    LOGI("LuaLoader mod initialized via Canvas Start()!");
    Canvas::Lua::LuaManager::getInstance().init();
    return (void*)LuaLoader_Draw;
}

__attribute__((visibility("default")))
void InitLate() {
    LOGI("LuaLoader InitLate called");
}

}
