#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>

namespace cccaster::game_interface {

struct JoyDeviceInfo {
    int id;
    char name[256];
};

class DirectInputHook {
public:
    static bool Initialize(HWND hwnd);
    static void Shutdown();
    static void RefreshDevices();
    static void ReloadConfigs();
    
    // Call every frame to read gamepad states
    static void Poll();
    
    // Fetch MBAA input bitmask constructed from INI bindings
    static uint32_t GetPlayer1Input();
    static uint32_t GetPlayer2Input();

    // Mapping flow APIs
    static std::vector<JoyDeviceInfo> GetConnectedDevices();
    
    // Returns 1 if Right was just pressed, -1 if Left, 0 if nothing. Also outputs the joyId.
    // Used for assigning controllers to P1 or P2 in the UI.
    static int GetActiveDeviceDirection(int& outJoyId);

    // Returns a string representation of the newly pressed input (e.g. "Button:1", "POV:0", "Axis:X+")
    // Returns empty string if no new input was detected. Used for the mapping wizard.
    static std::string GetAnyInputEdge(int joyId);

    static void StartMappingPlayer1(int inputIndex);
    static void StartMappingPlayer2(int inputIndex);

    // テストモード: コントローラ入力を無視してパケット値を使用
    static void SetTestModeEnabled(bool enabled);
    static void SetTestInputP1(uint32_t input); // (direction<<16)|buttons
    static void SetTestInputP2(uint32_t input);
};

} // namespace cccaster::game_interface
