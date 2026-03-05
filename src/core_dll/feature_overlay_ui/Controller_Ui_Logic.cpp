// ============================================================================
// Controller_Ui_Logic.cpp — コントローラーマッピング ロジック実装
// ============================================================================

#include "core_dll/feature_overlay_ui/Controller_Ui_Logic.hpp"
#include "core_dll/adapter_os_hooks/input/DirectInputHook.hpp"
#include "cli_launcher/ConfigManager.hpp"
#include <imgui.h>
#include <cmath>

namespace cccaster::domain::ui {

// ============================================================================
// 共通ヘルパー
// ============================================================================

static std::string SanitizeDeviceName(const std::string& name) {
    std::string s = name;
    for (char& c : s) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '\"' || c == '<' || c == '>' || c == '|') c = '_';
    }
    return s;
}

static std::string GetDeviceNameById(int joyId) {
    if (joyId == -2) return "Keyboard";
    if (joyId >= 0) {
        auto devices = cccaster::game_interface::DirectInputHook::GetConnectedDevices();
        for (const auto& dev : devices) {
            if (dev.id == joyId) return dev.name;
        }
    }
    return "";
}

// ============================================================================
// static メンバー定義
// ============================================================================

int         ControllerUiLogic::s_p1JoyId = -1;
int         ControllerUiLogic::s_p2JoyId = -1;
int         ControllerUiLogic::s_p1Position = 0;
int         ControllerUiLogic::s_p2Position = 0;
std::string ControllerUiLogic::s_p1Binds[NUM_GAME_INPUTS];
std::string ControllerUiLogic::s_p2Binds[NUM_GAME_INPUTS];
double      ControllerUiLogic::s_p1BindStartTime = 0.0;
double      ControllerUiLogic::s_p2BindStartTime = 0.0;
std::string ControllerUiLogic::s_p1CachedEdge;
std::string ControllerUiLogic::s_p2CachedEdge;

static const char* const s_gameInputNames[] = {
    "Up", "Down", "Left", "Right",
    "A (confirm)", "B (cancel)", "C", "D", "E",
    "Start", "FN1", "FN2", "A+B"
};

const char* const* ControllerUiLogic::GetGameInputNames() { return s_gameInputNames; }

// ============================================================================
// Getter 実装
// ============================================================================

int ControllerUiLogic::GetP1JoyId()  { return s_p1JoyId; }
int ControllerUiLogic::GetP2JoyId()  { return s_p2JoyId; }
int ControllerUiLogic::GetP1Position() { return s_p1Position; }
int ControllerUiLogic::GetP2Position() { return s_p2Position; }
const std::string* ControllerUiLogic::GetP1Binds() { return s_p1Binds; }
const std::string* ControllerUiLogic::GetP2Binds() { return s_p2Binds; }
const std::string& ControllerUiLogic::GetP1CachedEdge() { return s_p1CachedEdge; }
const std::string& ControllerUiLogic::GetP2CachedEdge() { return s_p2CachedEdge; }

// ============================================================================
// デバイス割当保存
// ============================================================================

void ControllerUiLogic::SaveDeviceAllocations() {
    auto saveAssignedDev = [](int joyId, const std::string& prefix) {
        std::string deviceName = GetDeviceNameById(joyId);
        if (!deviceName.empty()) {
            cccaster::main_app::ConfigManager::SetString("Settings", prefix + "Device", SanitizeDeviceName(deviceName));
        } else {
            cccaster::main_app::ConfigManager::SetString("Settings", prefix + "Device", "");
        }
    };
    saveAssignedDev(s_p1JoyId, "P1");
    saveAssignedDev(s_p2JoyId, "P2");
    cccaster::main_app::ConfigManager::Save("cccaster\\cccaster_v10.ini");
    cccaster::game_interface::DirectInputHook::ReloadConfigs();
}

// ============================================================================
// バインド保存
// ============================================================================

void ControllerUiLogic::SaveBinds(int joyId, const std::string& prefix, std::string* binds) {
    std::string deviceName = GetDeviceNameById(joyId);
    if (deviceName.empty()) deviceName = "UnknownDevice_" + std::to_string(joyId);

    std::string sanitizedName = SanitizeDeviceName(deviceName);
    std::string filename = "cccaster\\" + sanitizedName + ".ini";
    cccaster::main_app::Config deviceConfig;
    deviceConfig.Load(filename);

    deviceConfig.SetString("Mapping", "Up",    binds[0]);
    deviceConfig.SetString("Mapping", "Down",  binds[1]);
    deviceConfig.SetString("Mapping", "Left",  binds[2]);
    deviceConfig.SetString("Mapping", "Right", binds[3]);
    deviceConfig.SetString("Mapping", "A",     binds[4]);
    deviceConfig.SetString("Mapping", "B",     binds[5]);
    deviceConfig.SetString("Mapping", "C",     binds[6]);
    deviceConfig.SetString("Mapping", "D",     binds[7]);
    deviceConfig.SetString("Mapping", "E",     binds[8]);
    deviceConfig.SetString("Mapping", "Start", binds[9]);
    deviceConfig.SetString("Mapping", "FN1",   binds[10]);
    deviceConfig.SetString("Mapping", "FN2",   binds[11]);
    deviceConfig.SetString("Mapping", "A+B",   binds[12]);

    deviceConfig.Save(filename);
    cccaster::main_app::ConfigManager::SetString("Settings", prefix + "Device", sanitizedName);
}

// ============================================================================
// デバイス選択入力処理
// ============================================================================

void ControllerUiLogic::ProcessDeviceSelectionInput() {
    int activeJoyId = -1;
    int dir = cccaster::game_interface::DirectInputHook::GetActiveDeviceDirection(activeJoyId);

    if (s_p1Position == 0 && dir == -1 && activeJoyId >= 0) {
        if (s_p2JoyId == activeJoyId) { if (s_p2Position == 0) s_p2JoyId = -1; }
        else if (s_p1JoyId != activeJoyId) s_p1JoyId = activeJoyId;
    }
    else if (s_p2Position == 0 && dir == 1 && activeJoyId >= 0) {
        if (s_p1JoyId == activeJoyId) { if (s_p1Position == 0) s_p1JoyId = -1; }
        else if (s_p2JoyId != activeJoyId) s_p2JoyId = activeJoyId;
    }

    if (s_p1Position == 0 && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
        if (s_p2JoyId == -2) { if (s_p2Position == 0) s_p2JoyId = -1; }
        else if (s_p1JoyId != -2) s_p1JoyId = -2;
    }
    if (s_p2Position == 0 && ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
        if (s_p1JoyId == -2) { if (s_p1Position == 0) s_p1JoyId = -1; }
        else if (s_p2JoyId != -2) s_p2JoyId = -2;
    }
}

// ============================================================================
// バインド入力処理
// ============================================================================

void ControllerUiLogic::ProcessBindingInput(int joyId, int playerIndex, int& pos, std::string* binds) {
    if (pos == 0) return;

    double bindStartTime = (playerIndex == 0) ? s_p1BindStartTime : s_p2BindStartTime;
    if (ImGui::GetTime() - bindStartTime < MAPPING_START_DEAD_TIME) return;

    int maxPos = NUM_GAME_INPUTS + 1;
    int bindIndex = pos - 1;

    bool deleteBind = ImGui::IsKeyPressed(ImGuiKey_Delete, false);
    std::string newBind = "";

    if (joyId == -2) {
        if (!deleteBind && !ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
                if (ImGui::IsKeyPressed((ImGuiKey)key, false)) {
                    if (pos == maxPos && (key == ImGuiKey_Enter || key == ImGuiKey_Space)) {
                        SaveBinds(joyId, playerIndex == 0 ? "P1" : "P2", binds);
                        cccaster::main_app::ConfigManager::Save("cccaster\\cccaster_v10.ini");
                        cccaster::game_interface::DirectInputHook::ReloadConfigs();
                        pos = 0; return;
                    }
                    if (key != ImGuiKey_Enter && key != ImGuiKey_F4 && key != ImGuiKey_Escape) {
                        std::string kName = ImGui::GetKeyName((ImGuiKey)key);
                        if (kName.find("Gamepad") == std::string::npos && kName.find("Mouse") == std::string::npos) {
                            newBind = kName; break;
                        }
                    }
                }
            }
        }
    } else if (joyId >= 0) {
        const std::string& edge = (playerIndex == 0) ? s_p1CachedEdge : s_p2CachedEdge;
        if (!edge.empty()) {
            if (pos == maxPos && edge.find("H") == std::string::npos && edge.find("A") == std::string::npos) {
                SaveBinds(joyId, playerIndex == 0 ? "P1" : "P2", binds);
                cccaster::main_app::ConfigManager::Save("cccaster\\cccaster_v10.ini");
                cccaster::game_interface::DirectInputHook::ReloadConfigs();
                pos = 0; return;
            }
            if (pos < maxPos) newBind = edge;
        }
    }

    if (deleteBind && pos < maxPos) binds[bindIndex] = "";
    if (!newBind.empty() && pos < maxPos) { binds[bindIndex] = newBind; pos++; }
}

// ============================================================================
// メインロジック更新（毎フレーム）
// ============================================================================

void ControllerUiLogic::Update() {
    ProcessDeviceSelectionInput();

    // バインド開始共通ヘルパー
    auto startBinding = [](int& pos, double& startTime, std::string* binds) {
        if (pos == 0) {
            pos = 1;
            startTime = ImGui::GetTime();
            for (int i = 0; i < NUM_GAME_INPUTS; ++i) binds[i] = "";
        }
    };

    bool kbdStart = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false);
    bool bindingJustStarted = false;
    if (kbdStart) {
        if (s_p1JoyId == -2 && s_p1Position == 0) { startBinding(s_p1Position, s_p1BindStartTime, s_p1Binds); bindingJustStarted = true; }
        if (s_p2JoyId == -2 && s_p2Position == 0) { startBinding(s_p2Position, s_p2BindStartTime, s_p2Binds); bindingJustStarted = true; }
    }

    // Edge 取得 + バインド開始 + 切断検知
    auto devices = cccaster::game_interface::DirectInputHook::GetConnectedDevices();
    s_p1CachedEdge.clear();
    s_p2CachedEdge.clear();
    bool p1DeviceStillExists = (s_p1JoyId == -2);
    bool p2DeviceStillExists = (s_p2JoyId == -2);
    for (const auto& dev : devices) {
        if (dev.id == s_p1JoyId) p1DeviceStillExists = true;
        if (dev.id == s_p2JoyId) p2DeviceStillExists = true;

        std::string edge = cccaster::game_interface::DirectInputHook::GetAnyInputEdge(dev.id);
        if (!edge.empty()) {
            if (dev.id == s_p1JoyId) s_p1CachedEdge = edge;
            if (dev.id == s_p2JoyId) s_p2CachedEdge = edge;

            if (edge.find("H") == std::string::npos && edge.find("A") == std::string::npos) {
                if (s_p1JoyId == dev.id) startBinding(s_p1Position, s_p1BindStartTime, s_p1Binds);
                if (s_p2JoyId == dev.id) startBinding(s_p2Position, s_p2BindStartTime, s_p2Binds);
            }
        }
    }
    if (!p1DeviceStillExists && s_p1Position > 0) { s_p1Position = 0; s_p1JoyId = -1; }
    if (!p2DeviceStillExists && s_p2Position > 0) { s_p2Position = 0; s_p2JoyId = -1; }

    // バインド入力処理（開始フレームはスキップ — デッドタイムで保護）
    if (!bindingJustStarted) {
        ProcessBindingInput(s_p1JoyId, 0, s_p1Position, s_p1Binds);
        ProcessBindingInput(s_p2JoyId, 1, s_p2Position, s_p2Binds);
    }
}

// ============================================================================
// バインド状態リセット
// ============================================================================

void ControllerUiLogic::ResetBindingState() {
    if (s_p1Position > 0) {
        s_p1Position = 0;
        for (int i = 0; i < NUM_GAME_INPUTS; ++i) s_p1Binds[i] = "";
    }
    if (s_p2Position > 0) {
        s_p2Position = 0;
        for (int i = 0; i < NUM_GAME_INPUTS; ++i) s_p2Binds[i] = "";
    }
}

} // namespace cccaster::domain::ui
