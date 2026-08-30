#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <map>
#include <variant>

namespace Canvas::Lua {

enum class UIRequestType {
    None,
    Choice,
    MultiChoice,
    Prompt,
    Alert
};

struct PromptItem {
    std::string key;
    std::string type; // "text", "number", "checkbox", "file"
    std::string label;
    std::string value;
};

struct UIRequest {
    UIRequestType type = UIRequestType::None;
    std::string title;
    std::string message;

    // Custom dimensions (0 = automatic)
    float customWidth = 0.0f;
    float customHeight = 0.0f;

    // Choice / MultiChoice
    std::vector<std::string> items;
    int defaultChoice = 1; // 1-based index for Lua
    std::map<int, bool> multiSelections; // 1-based index -> bool

    // Prompt
    std::vector<PromptItem> promptItems;

    // Alert
    std::string positiveButton = "OK";
    std::string negativeButton = "Cancel";
    std::string neutralButton = "";

    // Response data
    bool isCompleted = false;
    bool isCancelled = false;
    int selectedIndex = -1; // for choice & alert (1: pos, 2: neg, 3: neu)
    std::map<int, bool> multiResults;
    std::map<std::string, std::string> promptResults;
};

struct ToastNotification {
    std::string message;
    float timeRemaining = 3.0f; // seconds
};

class LuaUIBridge {
public:
    static LuaUIBridge& getInstance();

    // Dialog requests
    int showChoice(const std::string& title, const std::vector<std::string>& items, int defaultChoice = 1, float customWidth = 0.0f, float customHeight = 0.0f);
    bool showMultiChoice(const std::string& title, const std::vector<std::string>& items,
                         const std::map<int, bool>& defaultSelections, std::map<int, bool>& outResults,
                         float customWidth = 0.0f, float customHeight = 0.0f);
    bool showPrompt(const std::string& title, std::vector<PromptItem>& items, std::map<std::string, std::string>& outResults,
                    float customWidth = 0.0f, float customHeight = 0.0f);
    int showAlert(const std::string& message, const std::string& positive = "OK",
                  const std::string& negative = "", const std::string& neutral = "",
                  float customWidth = 0.0f, float customHeight = 0.0f);

    // Global default dialog size configuration
    void setDefaultDialogSize(float width, float height);
    void getDefaultDialogSize(float& width, float& height) const;

    // Notifications & ImGui controls
    void showToast(const std::string& message, bool isShort = true);
    bool isMenuVisible() const;
    void setMenuVisible(bool visible);
    bool isUIEnabled() const { return m_uiEnabled; }
    void setUIEnabled(bool enabled) { m_uiEnabled = enabled; }
    void cancelCurrentRequest();

    // Called on ImGui render thread
    void renderImGui();

private:
    LuaUIBridge() = default;
    ~LuaUIBridge() = default;

    std::mutex m_requestMutex;
    std::condition_variable m_requestCv;
    UIRequest m_currentRequest;
    bool m_hasActiveRequest = false;

    float m_defaultWidth = 0.0f;
    float m_defaultHeight = 0.0f;

    std::mutex m_toastMutex;
    std::vector<ToastNotification> m_activeToasts;

    bool m_menuVisible = false;
    bool m_uiEnabled = true;
};

} // namespace Canvas::Lua
