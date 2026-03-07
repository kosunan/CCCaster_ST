#include "core_dll/mbaa_sync/input/InputHook.hpp"
#include "core_dll/mbaa_sync/overlay/UIManager.hpp"
#include "core_dll/mbaa_game/constants/MbaaConstants.hpp"
#include <imgui.h>
#include <cstdio>
#include <Dbt.h>
#include "core_dll/platform/hooks/DirectInputHook.hpp"

void HookLog(const char* msg);

using namespace cccaster::game_interface;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

WNDPROC InputHook::original_WndProc = nullptr;
HWND InputHook::hooked_hwnd = nullptr;

bool InputHook::Initialize(HWND hwnd) {
    if (original_WndProc) return true; // Already initialized

    HWND mbaaWnd = FindWindowA("MBAA", NULL);
    if (!mbaaWnd) mbaaWnd = FindWindowA("Character", NULL);
    if (mbaaWnd) hwnd = mbaaWnd; // Prefer the game window class explicitly

    hooked_hwnd = hwnd;
    original_WndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)HookedWindowProc);
    
    char buf[128];
    if (!original_WndProc) {
        snprintf(buf, sizeof(buf), "[InputHook] SetWindowLongPtr FAILED for HWND %p, error: %lu", hwnd, GetLastError());
    } else {
        snprintf(buf, sizeof(buf), "[InputHook] SetWindowLongPtr SUCCEEDED for HWND %p", hwnd);
    }
    HookLog(buf);

    return original_WndProc != nullptr;
}

void InputHook::Shutdown() {
    if (original_WndProc && hooked_hwnd) {
        SetWindowLongPtr(hooked_hwnd, GWLP_WNDPROC, (LONG_PTR)original_WndProc);
        original_WndProc = nullptr;
        hooked_hwnd = nullptr;
    }
}

LRESULT CALLBACK InputHook::HookedWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // 1. Pass to ImGui first
    if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) {
        return true;
    }

    // 2. Intercept specific hotkeys for OverlayUI
    if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN) {
        // ★ Windows のキーリピート除外: lParam のビット30 が 1 = 前回もキーダウン = リピート
        //    リピートを処理するとF4等のトグル操作が高速で Open/Close を繰り返す
        bool isRepeat = (lParam & (1 << 30)) != 0;
        if (isRepeat) {
            // マッピング中はリピートもゲームにブロック
            if (cccaster::domain::ui::UIManager::IsMappingWindowOpen()) return 0;
            // リピートはホットキー処理をスキップしてゲームに通す
            return CallWindowProc(original_WndProc, hWnd, uMsg, wParam, lParam);
        }

        bool isCtrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool isAltDown = (uMsg == WM_SYSKEYDOWN) || ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0);

        int key = static_cast<int>(wParam);

        // F4
        if (key == VK_F4 && !isAltDown) { // Alt+F4 is normally close, we intercept just F4
            // キャラセレ画面以外では F4 によるマッピングトグルを制限する
            // CC_GAME_MODE_ADDR / CC_GAME_MODE_CHARA_SELECT は MbaaConstants.hpp で定義
            if (!IsBadReadPtr(CC_GAME_MODE_ADDR, sizeof(uint32_t))
                && *CC_GAME_MODE_ADDR != CC_GAME_MODE_CHARA_SELECT) {
                return 0; // キャラセレ以外では F4 を無視（ゲームにも渡さない）
            }
            cccaster::domain::ui::UIManager::OnMappingInput();
            return 0; // Block from game
        }

        // ★ マッピングウィンドウが開いている間は全キー入力をゲームにブロック
        //    ImGui 側は既に ImGui_ImplWin32_WndProcHandler でキーを受信済み。
        //    ゲームに渡すと画面遷移やキャラ選択が高速動作しクラッシュする。
        if (cccaster::domain::ui::UIManager::IsMappingWindowOpen()) {
            return 0; // Block all keyboard input from game during mapping
        }

        // Numbers 0-9
        if (key >= '0' && key <= '9') {
            int num = key - '0';
            if (isCtrlDown) {
                cccaster::domain::ui::UIManager::OnDelayInput(num);
                return 0; // Block
            } else if (isAltDown) {
                cccaster::domain::ui::UIManager::OnRollbackInput(num);
                return 0; // Block
            }
        }
        
        // Numpad 0-9
        if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) {
            int num = key - VK_NUMPAD0;
            if (isCtrlDown) {
                cccaster::domain::ui::UIManager::OnDelayInput(num);
                return 0; // Block
            } else if (isAltDown) {
                cccaster::domain::ui::UIManager::OnRollbackInput(num);
                return 0; // Block
            }
        }
    }

    // ★ マッピング中は WM_KEYUP もブロック（押しっぱなし判定の不整合防止）
    if ((uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP) &&
        cccaster::domain::ui::UIManager::IsMappingWindowOpen()) {
        return 0;
    }

    // 2.5 Intercept USB Device Insertions / Removals for Hotplugging
    if (uMsg == WM_DEVICECHANGE) {
        if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
            HookLog("[InputHook] WM_DEVICECHANGE detected. Refreshing controllers.");
            cccaster::game_interface::DirectInputHook::RefreshDevices();
        }
    }

    // 3. ImGui wants capture?
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse && (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST)) {
        return 0; // Block mouse from game since ImGui is using it
    }
    if (io.WantCaptureKeyboard && (uMsg == WM_KEYDOWN || uMsg == WM_KEYUP || uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP || uMsg == WM_CHAR)) {
        return 0; // Block keyboard input
    }

    return CallWindowProc(original_WndProc, hWnd, uMsg, wParam, lParam);
}
