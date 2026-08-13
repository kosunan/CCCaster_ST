#include "core_dll/hook/DirectInputHook.hpp"
#include "cli_launcher/ConfigManager.hpp"
#include "core_dll/common/DataPaths.hpp"
#include "core_dll/common/DebugLog.hpp"
#include <windows.h>
#include <dinput.h>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <imgui.h>
#include <fstream>

using namespace cccaster::game_interface;
using namespace cccaster::main_app;

static IDirectInput8* g_pDI = nullptr;

struct ControllerData {
    IDirectInputDevice8* device;
    DIJOYSTATE2 state;
    DIJOYSTATE2 prevState; // For edge detection
    int id;
    char name[256];
};

static std::vector<ControllerData> g_Controllers;
static HWND g_hwnd = nullptr;

// テストモード用: パケット値で入力をオーバーライド
static bool g_testModeEnabled = false;
static uint32_t g_testInputP1 = 0; // (direction<<16)|buttons
static uint32_t g_testInputP2 = 0;

BOOL CALLBACK EnumObjectsCallback(const DIDEVICEOBJECTINSTANCE* pdidoi, VOID* pContext) {
    if (pdidoi->dwType & DIDFT_AXIS) {
        DIPROPRANGE diprg; 
        diprg.diph.dwSize       = sizeof(DIPROPRANGE); 
        diprg.diph.dwHeaderSize = sizeof(DIPROPHEADER); 
        diprg.diph.dwHow        = DIPH_BYID; 
        diprg.diph.dwObj        = pdidoi->dwType;
        diprg.lMin              = -32768; 
        diprg.lMax              = 32767; 
        ((IDirectInputDevice8*)pContext)->SetProperty(DIPROP_RANGE, &diprg.diph);
        
        // Disable internal deadzone so we can handle it manually with our THRESHOLD / DEADZONE logic
        DIPROPDWORD dipdw;
        dipdw.diph.dwSize       = sizeof(DIPROPDWORD);
        dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        dipdw.diph.dwHow        = DIPH_BYID;
        dipdw.diph.dwObj        = pdidoi->dwType;
        dipdw.dwData            = 0;
        ((IDirectInputDevice8*)pContext)->SetProperty(DIPROP_DEADZONE, &dipdw.diph);
    }
    return DIENUM_CONTINUE;
}

BOOL CALLBACK EnumJoysticksCallback(const DIDEVICEINSTANCE* pdidInstance, VOID* pContext) {
    IDirectInputDevice8* joystick;
    if (FAILED(g_pDI->CreateDevice(pdidInstance->guidInstance, &joystick, nullptr))) {
        return DIENUM_CONTINUE;
    }

    joystick->SetDataFormat(&c_dfDIJoystick2);
    joystick->SetCooperativeLevel(g_hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
    joystick->EnumObjects(EnumObjectsCallback, joystick, DIDFT_AXIS);

    ControllerData data;
    data.device = joystick;
    data.id = g_Controllers.size();
    memset(&data.state, 0, sizeof(DIJOYSTATE2));
    strncpy(data.name, pdidInstance->tszInstanceName, sizeof(data.name) - 1);
    data.name[sizeof(data.name) - 1] = '\0';
    
    g_Controllers.push_back(data);

    return DIENUM_CONTINUE;
}

bool DirectInputHook::Initialize(HWND hwnd) {
    if (g_pDI) return true;
    
    g_hwnd = hwnd;
    if (FAILED(DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8, (VOID**)&g_pDI, nullptr))) {
        return false;
    }

    g_pDI->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumJoysticksCallback, nullptr, DIEDFL_ATTACHEDONLY);
    ReloadConfigs();
    return true;
}

void DirectInputHook::Shutdown() {
    for (auto& ctrl : g_Controllers) {
        if (ctrl.device) {
            ctrl.device->Unacquire();
            ctrl.device->Release();
        }
    }
    g_Controllers.clear();

    if (g_pDI) {
        g_pDI->Release();
        g_pDI = nullptr;
    }
}

// ポーリング中の再入（COMメッセージポンプ経由のWM_DEVICECHANGE等）による vector クリア（クラッシュ）を防ぐフラグ
static bool g_isPolling = false;
static bool g_refreshPending = false;

void DirectInputHook::RefreshDevices() {
    if (g_isPolling) {
        g_refreshPending = true;
        return;
    }

    // Release existing devices
    for (auto& ctrl : g_Controllers) {
        if (ctrl.device) {
            ctrl.device->Unacquire();
            ctrl.device->Release();
        }
    }
    g_Controllers.clear();

    // Re-enumerate
    if (g_pDI) {
        g_pDI->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumJoysticksCallback, nullptr, DIEDFL_ATTACHEDONLY);
    }
}

void DirectInputHook::Poll() {
    if (g_isPolling) return;
    g_isPolling = true;

    // 範囲for文ではなくインデックスベースで安全に回す（念のため）
    size_t count = g_Controllers.size();
    for (size_t i = 0; i < count; ++i) {
        auto& ctrl = g_Controllers[i];
        if (!ctrl.device) continue;
        
        ctrl.device->Poll();
        
        DIJOYSTATE2 newState;
        HRESULT hr = ctrl.device->GetDeviceState(sizeof(DIJOYSTATE2), &newState);
        if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
            ctrl.device->Acquire();
            hr = ctrl.device->GetDeviceState(sizeof(DIJOYSTATE2), &newState);
        }
        
        if (SUCCEEDED(hr)) {
            ctrl.prevState = ctrl.state;
            ctrl.state = newState;
        }
    }

    g_isPolling = false;

    // もしポーリング中に Refresh リクエストが来ていた場合は、安全なここで実行する
    if (g_refreshPending) {
        g_refreshPending = false;
        RefreshDevices();
    }
}

// Constants for MBAA Inputs
#define CC_BUTTON_A                 0x0010
#define CC_BUTTON_B                 0x0020
#define CC_BUTTON_C                 0x0008
#define CC_BUTTON_D                 0x0004
#define CC_BUTTON_E                 0x0080
#define CC_BUTTON_AB                0x0040
#define CC_BUTTON_START             0x0001
#define CC_BUTTON_FN1               0x0100
#define CC_BUTTON_FN2               0x0200
#define CC_BUTTON_CONFIRM           0x0400
#define CC_BUTTON_CANCEL            0x0800

#define AXIS_CENTERED 0
#define AXIS_POSITIVE 1
#define AXIS_NEGATIVE 2

static inline uint8_t mapAxisValue(LONG value, uint32_t deadzone) {
    LONG absValue = value < 0 ? -value : value;
    if (absValue > (LONG)deadzone)
        return (value > 0 ? AXIS_POSITIVE : AXIS_NEGATIVE);
    return AXIS_CENTERED;
}

static inline uint8_t mapHatValue(uint32_t value) {
    static const uint8_t values[] = { 8, 9, 6, 3, 2, 1, 4, 7 }; // U, UR, R, DR, D, DL, L, UL
    if (LOWORD(value) == 0xFFFF) return 5;
    value %= 36000;
    value /= 4500;
    return values[value];
}

static bool IsJoyButtonPressed(const DIJOYSTATE2& state, int btn) {
    if (btn < 0 || btn >= 128) return false;
    return (state.rgbButtons[btn] & 0x80) != 0;
}

static bool IsJoyHatPressed(const DIJOYSTATE2& state, int pov, int dir) {
    if (pov < 0 || pov >= 4) return false;
    uint8_t mapped = mapHatValue(state.rgdwPOV[pov]);
    if (mapped == 5) return false; // Centered
    // Strict cardinal checking including diagonals
    if (dir == 8 && (mapped == 7 || mapped == 8 || mapped == 9)) return true; // Up
    if (dir == 6 && (mapped == 9 || mapped == 6 || mapped == 3)) return true; // Right
    if (dir == 2 && (mapped == 3 || mapped == 2 || mapped == 1)) return true; // Down
    if (dir == 4 && (mapped == 1 || mapped == 4 || mapped == 7)) return true; // Left
    return false;
}

static bool IsJoyAxisPressed(const DIJOYSTATE2& state, int axis, int sign) {
    LONG val = 0;
    switch(axis) {
        case 0: val = state.lX; break;
        case 1: val = -state.lY; break; // Invert Y
        case 2: val = state.lZ; break;
        case 3: val = state.lRx; break;
        case 4: val = -state.lRy; break; // Invert Y
        case 5: val = state.lRz; break;
        case 6: val = state.rglSlider[0]; break;
        case 7: val = state.rglSlider[1]; break;
        default: return false;
    }
    const LONG THRESHOLD = 16383; // Half of 32767
    uint8_t mapped = mapAxisValue(val, THRESHOLD);
    if (sign < 0 && mapped == AXIS_NEGATIVE) return true;
    if (sign > 0 && mapped == AXIS_POSITIVE) return true;
    return false;
}

static bool CheckInputBind(int joyId, const std::string& bindStr) {
    if (bindStr.empty()) return false;

    if (joyId == -2) {
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
            const char* name = ImGui::GetKeyName((ImGuiKey)key);
            if (name && bindStr == name) {
                return ImGui::IsKeyDown((ImGuiKey)key);
            }
        }
        return false;
    }

    if (joyId < 0 || joyId >= (int)g_Controllers.size()) return false;
    
    try {
        // Binding strings example: "B0", "A0+", "H0_6"
        char type = bindStr[0];
        if (type == 'B') {
            int btn = std::stoi(bindStr.substr(1));
            return IsJoyButtonPressed(g_Controllers[joyId].state, btn);
        } else if (type == 'A') {
            int axis = std::stoi(bindStr.substr(1, bindStr.length() - 2));
            int sign = bindStr.back() == '+' ? 1 : -1;
            return IsJoyAxisPressed(g_Controllers[joyId].state, axis, sign);
        } else if (type == 'H') {
            size_t underscore = bindStr.find('_');
            if (underscore != std::string::npos) {
                int hat = std::stoi(bindStr.substr(1, underscore - 1));
                int dir = std::stoi(bindStr.substr(underscore + 1));
                return IsJoyHatPressed(g_Controllers[joyId].state, hat, dir);
            }
        }
    } catch (...) {
        // 不正なバインド文字列 — クラッシュ防止
    }
    return false;
}

static std::string GetDeviceFileName(int joyId) {
    std::string deviceName = (joyId == -2) ? "Keyboard" : "";
    if (joyId >= 0 && joyId < (int)g_Controllers.size()) {
        deviceName = g_Controllers[joyId].name;
    }
    if (deviceName.empty()) deviceName = "UnknownDevice_" + std::to_string(joyId);

    std::string sanitizedName = deviceName;
    for (char& c : sanitizedName) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '\"' || c == '<' || c == '>' || c == '|') c = '_';
    }
    // 相対パス（"cccaster\\xxx.ini"）はゲームのカレントディレクトリ基準になり、
    // 書き手と読み手が別の場所を指しうる。DLL 基準の絶対パスに解決する。
    return cccaster::core::paths::Resolve(sanitizedName + ".ini");
}

static cccaster::main_app::Config s_p1Config;
static cccaster::main_app::Config s_p2Config;

static int GetJoyIdFromDeviceName(const std::string& sanitizedDeviceName);

static void LoadOrCreateDeviceConfig(cccaster::main_app::Config& cfg, const std::string& filename) {
    std::ifstream testFile(filename);
    if (!testFile.is_open()) {
        std::ofstream create(filename);
        if (create.is_open()) {
            create << "[Mapping]\n";
            create << "Up = H0_8\n";
            create << "Down = H0_2\n";
            create << "Left = H0_4\n";
            create << "Right = H0_6\n";
            create << "A = B0\n";
            create << "B = B1\n";
            create << "C = B2\n";
            create << "D = B3\n";
            create << "E = B4\n";
            create << "Start = B7\n";
            create << "FN1 = B8\n";
            create << "FN2 = B9\n";
            create << "A+B = \n";
            create.close();
        }
    }
    cfg.Load(filename);
}

void DirectInputHook::ReloadConfigs() {
    std::string p1Dev = cccaster::main_app::ConfigManager::GetString("Settings", "P1Device", "");
    int p1Idx = GetJoyIdFromDeviceName(p1Dev);
    s_p1Config.Clear();
    if (p1Idx != -1) LoadOrCreateDeviceConfig(s_p1Config, GetDeviceFileName(p1Idx));

    std::string p2Dev = cccaster::main_app::ConfigManager::GetString("Settings", "P2Device", "");
    int p2Idx = GetJoyIdFromDeviceName(p2Dev);
    s_p2Config.Clear();
    if (p2Idx != -1) LoadOrCreateDeviceConfig(s_p2Config, GetDeviceFileName(p2Idx));
}

static uint32_t BuildPlayerInput(int joyId, const cccaster::main_app::Config& deviceConfig) {
    if (joyId == -1) return 0;  // デバイス未接続 → ニュートラル
    uint16_t buttons = 0;

    // Evaluate buttons
    if (CheckInputBind(joyId, deviceConfig.GetString("Mapping", "A", "B0"))) buttons |= (CC_BUTTON_A | CC_BUTTON_CONFIRM);
    if (CheckInputBind(joyId, deviceConfig.GetString("Mapping", "B", "B1"))) buttons |= (CC_BUTTON_B | CC_BUTTON_CANCEL);
    if (CheckInputBind(joyId, deviceConfig.GetString("Mapping", "C", "B2"))) buttons |= CC_BUTTON_C;
    if (CheckInputBind(joyId, deviceConfig.GetString("Mapping", "D", "B3"))) buttons |= CC_BUTTON_D;
    if (CheckInputBind(joyId, deviceConfig.GetString("Mapping", "E", "B4"))) buttons |= CC_BUTTON_E;
    if (CheckInputBind(joyId, deviceConfig.GetString("Mapping", "Start", "B7"))) buttons |= CC_BUTTON_START;
    if (CheckInputBind(joyId, deviceConfig.GetString("Mapping", "FN1", "B8"))) buttons |= CC_BUTTON_FN1;
    if (CheckInputBind(joyId, deviceConfig.GetString("Mapping", "FN2", "B9"))) buttons |= CC_BUTTON_FN2;
    if (CheckInputBind(joyId, deviceConfig.GetString("Mapping", "A+B", ""))) buttons |= CC_BUTTON_AB;

    // Evaluate directions
    bool up    = CheckInputBind(joyId, deviceConfig.GetString("Mapping", "Up",    "H0_8")) || CheckInputBind(joyId, deviceConfig.GetString("Mapping", "Up_Alt",    "A1-"));
    bool down  = CheckInputBind(joyId, deviceConfig.GetString("Mapping", "Down",  "H0_2")) || CheckInputBind(joyId, deviceConfig.GetString("Mapping", "Down_Alt",  "A1+"));
    bool left  = CheckInputBind(joyId, deviceConfig.GetString("Mapping", "Left",  "H0_4")) || CheckInputBind(joyId, deviceConfig.GetString("Mapping", "Left_Alt",  "A0-"));
    bool right = CheckInputBind(joyId, deviceConfig.GetString("Mapping", "Right", "H0_6")) || CheckInputBind(joyId, deviceConfig.GetString("Mapping", "Right_Alt", "A0+"));

    // Pre-clean SOCD
    if (up && down) { up = false; down = false; }
    if (left && right) { left = false; right = false; }

    uint16_t direction = 5; // Neutral
    if (up && left)         direction = 7;
    else if (up && right)   direction = 9;
    else if (down && left)  direction = 1;
    else if (down && right) direction = 3;
    else if (up)            direction = 8;
    else if (down)          direction = 2;
    else if (left)          direction = 4;
    else if (right)         direction = 6;
    else                    direction = 0; // 0 is neutral in MBAA memory format normally when writing to state? Note: DllProcessManager says "if dir == 5 -> dir = 0" 
    
    if (direction == 5) direction = 0;

    return ((uint32_t)direction << 16) | buttons;
}


static int GetJoyIdFromDeviceName(const std::string& sanitizedDeviceName) {
    if (sanitizedDeviceName.empty()) return -1;
    if (sanitizedDeviceName == "Keyboard") return -2;

    for (size_t i = 0; i < g_Controllers.size(); ++i) {
        std::string rawName = g_Controllers[i].name;
        for (char& c : rawName) {
            if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '\"' || c == '<' || c == '>' || c == '|') c = '_';
        }
        if (rawName == sanitizedDeviceName) return (int)i;
    }
    return -1;
}

uint32_t DirectInputHook::GetPlayer1Input() {
    if (g_testModeEnabled) return g_testInputP1;
    std::string devName = ConfigManager::GetString("Settings", "P1Device", "");
    int joyId = GetJoyIdFromDeviceName(devName);
    return BuildPlayerInput(joyId, s_p1Config);
}

uint32_t DirectInputHook::GetPlayer2Input() {
    if (g_testModeEnabled) return g_testInputP2;
    std::string devName = ConfigManager::GetString("Settings", "P2Device", "");
    int joyId = GetJoyIdFromDeviceName(devName);
    return BuildPlayerInput(joyId, s_p2Config);
}

// ============================================================================
// GetLocalPlayerInput — この機体で操作している人の入力を取る
// ============================================================================
//
// 【なぜ P1/P2 を直接呼ばないか】
//   `P1Device` / `P2Device` の意味が UI と読み手で食い違っていた。
//     UI（Controller_Ui_*）  … 画面左が P1、右が P2 という**ローカルの座席**
//     読み手（SceneRunner）  … isHost ? P1 : P2 ＝ **ホスト機/クライアント機**
//   クライアント機の人は自分を「1P」と認識して左（P1）に割り当てるが、
//   読み手は P2Device を見るため永久に無反応になる。実際に
//   `_TEST_MBAACC` の ini は両機とも「読むほうのキーが空」になっていた。
//
// 【解決】
//   ネットプレイでは**この機体のローカルプレイヤーは1人しかいない**。
//   ならば「どちらのスロットに入っていても、それがローカルデバイス」で正しい。
//   本来のスロットが空のときだけ、もう一方にフォールバックする。
//   オフライン（Training 等）は2人がローカルなので、この読み替えはしない。
//
//   UI 側の意味づけを変える案もあるが、既存の ini を壊すうえ、
//   「どちらに割り当てても動く」ほうが利用者にとって事故が起きない。
//
uint32_t DirectInputHook::GetLocalPlayerInput(bool isHost, bool soloLocal) {
    if (g_testModeEnabled) return isHost ? g_testInputP1 : g_testInputP2;

    const char* primaryKey = isHost ? "P1Device" : "P2Device";
    const char* otherKey   = isHost ? "P2Device" : "P1Device";

    std::string devName = ConfigManager::GetString("Settings", primaryKey, "");
    const cccaster::main_app::Config* cfg = isHost ? &s_p1Config : &s_p2Config;

    if (devName.empty() && soloLocal) {
        const std::string alt = ConfigManager::GetString("Settings", otherKey, "");
        if (!alt.empty()) {
            static bool s_warned = false;
            if (!s_warned) {
                s_warned = true;
                cccaster::domain::session::DebugLog(
                    "[DirectInputHook] %s が空のため %s のデバイス '%s' を"
                    "ローカル入力として使う（ネットプレイのローカルは1人）",
                    primaryKey, otherKey, alt.c_str());
            }
            devName = alt;
            cfg     = isHost ? &s_p2Config : &s_p1Config;
        }
    }

    const int joyId = GetJoyIdFromDeviceName(devName);

    // 解決に失敗した状態は「入力が一切効かない」と等価だが、これまで
    // ログにも UI にも出ていなかった。同じ状態が続く間は1回だけ出す。
    static int s_lastReportedJoyId = -12345;
    if (joyId == -1 && s_lastReportedJoyId != joyId) {
        cccaster::domain::session::DebugLog(
            "[DirectInputHook] ローカルデバイスを解決できない（%s='%s'）。"
            "入力は常にニュートラルになる。接続デバイス数=%d",
            primaryKey, devName.c_str(), static_cast<int>(g_Controllers.size()));
    }
    s_lastReportedJoyId = joyId;

    return BuildPlayerInput(joyId, *cfg);
}

std::vector<JoyDeviceInfo> DirectInputHook::GetConnectedDevices() {
    std::vector<JoyDeviceInfo> list;
    for (const auto& ctrl : g_Controllers) {
        JoyDeviceInfo info;
        info.id = ctrl.id;
        strncpy(info.name, ctrl.name, sizeof(info.name));
        list.push_back(info);
    }
    return list;
}

int DirectInputHook::GetActiveDeviceDirection(int& outJoyId) {
    // Check all controllers for a left or right hat/axis press
    for (const auto& ctrl : g_Controllers) {
        // Only trigger on newly pressed to avoid rapid firing
        bool edgeLeft = !IsJoyHatPressed(ctrl.prevState, 0, 4) && IsJoyHatPressed(ctrl.state, 0, 4);
        bool edgeRight = !IsJoyHatPressed(ctrl.prevState, 0, 6) && IsJoyHatPressed(ctrl.state, 0, 6);
        bool edgeAxisLeft = !IsJoyAxisPressed(ctrl.prevState, 0, -1) && IsJoyAxisPressed(ctrl.state, 0, -1);
        bool edgeAxisRight = !IsJoyAxisPressed(ctrl.prevState, 0, 1) && IsJoyAxisPressed(ctrl.state, 0, 1);

        if (edgeLeft || edgeAxisLeft) {
            outJoyId = ctrl.id;
            return -1;
        }
        if (edgeRight || edgeAxisRight) {
            outJoyId = ctrl.id;
            return 1;
        }
    }
    return 0;
}

std::string DirectInputHook::GetAnyInputEdge(int joyId) {
    auto it = std::find_if(g_Controllers.begin(), g_Controllers.end(), [&](const ControllerData& c) { return c.id == joyId; });
    if (it == g_Controllers.end()) return "";

    const auto& ctrl = *it;
    
    // Check Buttons (0 to 127)
    for (int i = 0; i < 128; ++i) {
        if ((ctrl.state.rgbButtons[i] & 0x80) && !(ctrl.prevState.rgbButtons[i] & 0x80)) {
            return "B" + std::to_string(i);
        }
    }
    
    // Check POV Hats (0 to 3)
    for (int i = 0; i < 4; ++i) {
        uint8_t curMap = mapHatValue(ctrl.state.rgdwPOV[i]);
        uint8_t prevMap = mapHatValue(ctrl.prevState.rgdwPOV[i]);
        if (curMap != 5 && prevMap == 5) { // Edge from neutral
            // For binding, we snap to nearest cardinal
            if (curMap == 7 || curMap == 8 || curMap == 9) return "H" + std::to_string(i) + "_8"; // Up
            if (curMap == 3 || curMap == 2 || curMap == 1) return "H" + std::to_string(i) + "_2"; // Down
            if (curMap == 1 || curMap == 4 || curMap == 7) return "H" + std::to_string(i) + "_4"; // Left
            if (curMap == 9 || curMap == 6 || curMap == 3) return "H" + std::to_string(i) + "_6"; // Right
        }
    }
    
    // Check Axes (0 to 7)
    const long DEADZONE = 16383; 
    auto checkAxisEdge = [&](long current, long prev, int axisId, bool invert) -> std::string {
        long c = invert ? -current : current;
        long p = invert ? -prev : prev;
        uint8_t curMap = mapAxisValue(c, DEADZONE);
        uint8_t prevMap = mapAxisValue(p, DEADZONE);
        if (curMap == AXIS_POSITIVE && prevMap != AXIS_POSITIVE) return "A" + std::to_string(axisId) + "+";
        if (curMap == AXIS_NEGATIVE && prevMap != AXIS_NEGATIVE) return "A" + std::to_string(axisId) + "-";
        return "";
    };

    std::string res;
    if (!(res = checkAxisEdge(ctrl.state.lX, ctrl.prevState.lX, 0, false)).empty()) return res;
    if (!(res = checkAxisEdge(ctrl.state.lY, ctrl.prevState.lY, 1, true)).empty()) return res;
    if (!(res = checkAxisEdge(ctrl.state.lZ, ctrl.prevState.lZ, 2, false)).empty()) return res;
    if (!(res = checkAxisEdge(ctrl.state.lRx, ctrl.prevState.lRx, 3, false)).empty()) return res;
    if (!(res = checkAxisEdge(ctrl.state.lRy, ctrl.prevState.lRy, 4, true)).empty()) return res;
    if (!(res = checkAxisEdge(ctrl.state.lRz, ctrl.prevState.lRz, 5, false)).empty()) return res;
    if (!(res = checkAxisEdge(ctrl.state.rglSlider[0], ctrl.prevState.rglSlider[0], 6, false)).empty()) return res;
    if (!(res = checkAxisEdge(ctrl.state.rglSlider[1], ctrl.prevState.rglSlider[1], 7, false)).empty()) return res;

    return "";
}

void DirectInputHook::StartMappingPlayer1(int inputIndex) {
}

void DirectInputHook::StartMappingPlayer2(int inputIndex) {
}

void DirectInputHook::SetTestModeEnabled(bool enabled) {
    g_testModeEnabled = enabled;
}

void DirectInputHook::SetTestInputP1(uint32_t input) {
    g_testInputP1 = input;
}

void DirectInputHook::SetTestInputP2(uint32_t input) {
    g_testInputP2 = input;
}
