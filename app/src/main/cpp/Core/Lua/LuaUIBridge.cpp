#include "LuaUIBridge.h"
#include "imgui.h"
#include "Canvas/Canvas.h"
#include <android/log.h>

#define LOG_TAG "CanvasLuaUI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace Canvas::Lua {

LuaUIBridge& LuaUIBridge::getInstance() {
    static LuaUIBridge instance;
    return instance;
}

void LuaUIBridge::setDefaultDialogSize(float width, float height) {
    std::unique_lock<std::mutex> lock(m_requestMutex);
    m_defaultWidth = width;
    m_defaultHeight = height;
}

void LuaUIBridge::getDefaultDialogSize(float& width, float& height) const {
    width = m_defaultWidth;
    height = m_defaultHeight;
}

int LuaUIBridge::showChoice(const std::string& title, const std::vector<std::string>& items, int defaultChoice, float customWidth, float customHeight) {
    std::unique_lock<std::mutex> lock(m_requestMutex);

    m_currentRequest = UIRequest{};
    m_currentRequest.type = UIRequestType::Choice;
    m_currentRequest.title = title.empty() ? "Script Menu" : title;
    m_currentRequest.items = items;
    m_currentRequest.defaultChoice = defaultChoice;
    m_currentRequest.customWidth = (customWidth > 0.0f) ? customWidth : m_defaultWidth;
    m_currentRequest.customHeight = (customHeight > 0.0f) ? customHeight : m_defaultHeight;
    m_currentRequest.isCompleted = false;
    m_currentRequest.isCancelled = false;
    m_hasActiveRequest = true;

    // Block Lua thread until user interacts in ImGui
    m_requestCv.wait(lock, [this] { return m_currentRequest.isCompleted; });

    m_hasActiveRequest = false;
    if (m_currentRequest.isCancelled) {
        return 0; // Cancelled / nil
    }
    return m_currentRequest.selectedIndex;
}

bool LuaUIBridge::showMultiChoice(const std::string& title, const std::vector<std::string>& items,
                                  const std::map<int, bool>& defaultSelections, std::map<int, bool>& outResults,
                                  float customWidth, float customHeight) {
    std::unique_lock<std::mutex> lock(m_requestMutex);

    m_currentRequest = UIRequest{};
    m_currentRequest.type = UIRequestType::MultiChoice;
    m_currentRequest.title = title.empty() ? "Select Options" : title;
    m_currentRequest.items = items;
    m_currentRequest.multiSelections = defaultSelections;
    m_currentRequest.customWidth = (customWidth > 0.0f) ? customWidth : m_defaultWidth;
    m_currentRequest.customHeight = (customHeight > 0.0f) ? customHeight : m_defaultHeight;
    m_currentRequest.isCompleted = false;
    m_currentRequest.isCancelled = false;
    m_hasActiveRequest = true;

    m_requestCv.wait(lock, [this] { return m_currentRequest.isCompleted; });

    m_hasActiveRequest = false;
    if (m_currentRequest.isCancelled) {
        return false;
    }
    outResults = m_currentRequest.multiResults;
    return true;
}

bool LuaUIBridge::showPrompt(const std::string& title, std::vector<PromptItem>& items, std::map<std::string, std::string>& outResults,
                            float customWidth, float customHeight) {
    std::unique_lock<std::mutex> lock(m_requestMutex);

    m_currentRequest = UIRequest{};
    m_currentRequest.type = UIRequestType::Prompt;
    m_currentRequest.title = title.empty() ? "Input Prompt" : title;
    m_currentRequest.promptItems = items;
    m_currentRequest.customWidth = (customWidth > 0.0f) ? customWidth : m_defaultWidth;
    m_currentRequest.customHeight = (customHeight > 0.0f) ? customHeight : m_defaultHeight;
    m_currentRequest.isCompleted = false;
    m_currentRequest.isCancelled = false;
    m_hasActiveRequest = true;

    m_requestCv.wait(lock, [this] { return m_currentRequest.isCompleted; });

    m_hasActiveRequest = false;
    if (m_currentRequest.isCancelled) {
        return false;
    }
    outResults = m_currentRequest.promptResults;
    return true;
}

int LuaUIBridge::showAlert(const std::string& message, const std::string& positive,
                           const std::string& negative, const std::string& neutral,
                           float customWidth, float customHeight) {
    std::unique_lock<std::mutex> lock(m_requestMutex);

    m_currentRequest = UIRequest{};
    m_currentRequest.type = UIRequestType::Alert;
    m_currentRequest.title = "Alert";
    m_currentRequest.message = message;
    m_currentRequest.positiveButton = positive.empty() ? "OK" : positive;
    m_currentRequest.negativeButton = negative;
    m_currentRequest.neutralButton = neutral;
    m_currentRequest.customWidth = (customWidth > 0.0f) ? customWidth : m_defaultWidth;
    m_currentRequest.customHeight = (customHeight > 0.0f) ? customHeight : m_defaultHeight;
    m_currentRequest.isCompleted = false;
    m_hasActiveRequest = true;

    m_requestCv.wait(lock, [this] { return m_currentRequest.isCompleted; });

    m_hasActiveRequest = false;
    return m_currentRequest.selectedIndex;
}

void LuaUIBridge::showToast(const std::string& message, bool isShort) {
    std::lock_guard<std::mutex> lock(m_toastMutex);
    ToastNotification toast;
    toast.message = message;
    toast.timeRemaining = isShort ? 2.5f : 4.5f;
    m_activeToasts.push_back(toast);
    LOGI("Toast: %s", message.c_str());
}

bool LuaUIBridge::isMenuVisible() const {
    return m_menuVisible;
}

void LuaUIBridge::setMenuVisible(bool visible) {
    m_menuVisible = visible;
}

void LuaUIBridge::cancelCurrentRequest() {
    std::unique_lock<std::mutex> lock(m_requestMutex);
    if (m_hasActiveRequest) {
        m_currentRequest.isCancelled = true;
        m_currentRequest.isCompleted = true;
        m_requestCv.notify_all();
    }
}

void LuaUIBridge::renderImGui() {
    float dt = ImGui::GetIO().DeltaTime;

    // 1. Render active toasts
    {
        std::lock_guard<std::mutex> lock(m_toastMutex);
        if (!m_activeToasts.empty()) {
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, 50.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
            ImGui::SetNextWindowBgAlpha(0.85f);
            if (ImGui::Begin("##LuaToasts", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
                for (auto it = m_activeToasts.begin(); it != m_activeToasts.end();) {
                    it->timeRemaining -= dt;
                    if (it->timeRemaining <= 0.0f) {
                        it = m_activeToasts.erase(it);
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "[GG] %s", it->message.c_str());
                        ++it;
                    }
                }
            }
            ImGui::End();
        }
    }

    // 2. Render script dialog requests
    std::unique_lock<std::mutex> lock(m_requestMutex);
    if (!m_hasActiveRequest || m_currentRequest.isCompleted || !m_uiEnabled) {
        return;
    }

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    float targetMinWidth = 320.0f;
    if (m_currentRequest.customWidth > 0.0f) {
        targetMinWidth = m_currentRequest.customWidth;
    } else {
        float maxItemWidth = 320.0f;
        for (const auto& item : m_currentRequest.items) {
            float w = ImGui::CalcTextSize(item.c_str()).x + 50.0f;
            if (w > maxItemWidth) maxItemWidth = w;
        }
        targetMinWidth = std::min(maxItemWidth, displaySize.x * 0.92f);
    }

    float targetMinHeight = (m_currentRequest.customHeight > 0.0f) ? m_currentRequest.customHeight : 60.0f;

    ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(targetMinWidth, targetMinHeight), ImVec2(displaySize.x * 0.95f, displaySize.y * 0.95f));
    std::string winName = m_currentRequest.title + "###LuaScriptDialog";

    bool dialogOpen = true;
    if (ImGui::Begin(winName.c_str(), &dialogOpen, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        switch (m_currentRequest.type) {
            case UIRequestType::Choice: {
                for (size_t i = 0; i < m_currentRequest.items.size(); ++i) {
                    int oneBasedIdx = static_cast<int>(i) + 1;
                    std::string label = m_currentRequest.items[i] + "##choice_" + std::to_string(i);

                    if (ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f))) {
                        m_currentRequest.selectedIndex = oneBasedIdx;
                        m_currentRequest.isCompleted = true;
                        m_requestCv.notify_all();
                    }
                }
                ImGui::Separator();
                if (ImGui::Button("Cancel", ImVec2(-1.0f, 0.0f))) {
                    m_currentRequest.isCancelled = true;
                    m_currentRequest.isCompleted = true;
                    m_requestCv.notify_all();
                }
                break;
            }

            case UIRequestType::MultiChoice: {
                for (size_t i = 0; i < m_currentRequest.items.size(); ++i) {
                    int oneBasedIdx = static_cast<int>(i) + 1;
                    bool isChecked = m_currentRequest.multiSelections[oneBasedIdx];
                    std::string label = m_currentRequest.items[i] + "##mc_" + std::to_string(i);
                    if (ImGui::Checkbox(label.c_str(), &isChecked)) {
                        m_currentRequest.multiSelections[oneBasedIdx] = isChecked;
                    }
                }
                ImGui::Separator();
                if (ImGui::Button("Confirm", ImVec2(120, 0))) {
                    m_currentRequest.multiResults = m_currentRequest.multiSelections;
                    m_currentRequest.isCompleted = true;
                    m_requestCv.notify_all();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    m_currentRequest.isCancelled = true;
                    m_currentRequest.isCompleted = true;
                    m_requestCv.notify_all();
                }
                break;
            }

            case UIRequestType::Prompt: {
                for (size_t i = 0; i < m_currentRequest.promptItems.size(); ++i) {
                    auto& item = m_currentRequest.promptItems[i];
                    ImGui::Text("%s", item.label.c_str());
                    char buf[256] = {0};
                    strncpy(buf, item.value.c_str(), sizeof(buf) - 1);
                    std::string inputId = "##prompt_input_" + std::to_string(i);
                    if (ImGui::InputText(inputId.c_str(), buf, sizeof(buf))) {
                        item.value = buf;
                    }
                }
                ImGui::Separator();
                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    for (size_t i = 0; i < m_currentRequest.promptItems.size(); ++i) {
                        const auto& item = m_currentRequest.promptItems[i];
                        m_currentRequest.promptResults[item.key] = item.value;
                    }
                    m_currentRequest.isCompleted = true;
                    m_requestCv.notify_all();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    m_currentRequest.isCancelled = true;
                    m_currentRequest.isCompleted = true;
                    m_requestCv.notify_all();
                }
                break;
            }

            case UIRequestType::Alert: {
                ImGui::TextWrapped("%s", m_currentRequest.message.c_str());
                ImGui::Separator();
                if (!m_currentRequest.positiveButton.empty()) {
                    if (ImGui::Button(m_currentRequest.positiveButton.c_str())) {
                        m_currentRequest.selectedIndex = 1;
                        m_currentRequest.isCompleted = true;
                        m_requestCv.notify_all();
                    }
                }
                if (!m_currentRequest.negativeButton.empty()) {
                    ImGui::SameLine();
                    if (ImGui::Button(m_currentRequest.negativeButton.c_str())) {
                        m_currentRequest.selectedIndex = 2;
                        m_currentRequest.isCompleted = true;
                        m_requestCv.notify_all();
                    }
                }
                if (!m_currentRequest.neutralButton.empty()) {
                    ImGui::SameLine();
                    if (ImGui::Button(m_currentRequest.neutralButton.c_str())) {
                        m_currentRequest.selectedIndex = 3;
                        m_currentRequest.isCompleted = true;
                        m_requestCv.notify_all();
                    }
                }
                break;
            }

            default:
                break;
        }
        ImGui::End();
    }

    if (!dialogOpen) {
        m_currentRequest.isCancelled = true;
        m_currentRequest.isCompleted = true;
        m_requestCv.notify_all();
    }
}

} // namespace Canvas::Lua
