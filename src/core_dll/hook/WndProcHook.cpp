#include "core_dll/hook/WndProcHook.hpp"
#include "core_dll/ui/UIManager.hpp"
#include <cstdio>

void HookLog(const char* msg);

using namespace cccaster::game_interface;

WNDPROC WndProcHook::original_WndProc = nullptr;
HWND WndProcHook::hooked_hwnd = nullptr;

bool WndProcHook::Initialize(HWND hwnd) {
    if (original_WndProc) return true;

    HWND mbaaWnd = FindWindowA("MBAA", NULL);
    if (!mbaaWnd) mbaaWnd = FindWindowA("Character", NULL);
    if (mbaaWnd) hwnd = mbaaWnd;

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

void WndProcHook::Shutdown() {
    if (original_WndProc && hooked_hwnd) {
        SetWindowLongPtr(hooked_hwnd, GWLP_WNDPROC, (LONG_PTR)original_WndProc);
        original_WndProc = nullptr;
        hooked_hwnd = nullptr;
    }
}

LRESULT CALLBACK WndProcHook::HookedWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // UI 側に処理を委譲
    int result = cccaster::domain::ui::UIManager::HandleWndProcMessage(hWnd, uMsg, wParam, lParam);
    if (result > 0) return 0;    // ブロック
    if (result == 0) {}          // ゲームに通す（フォールスルー）

    // 元の WndProc に委譲
    return CallWindowProc(original_WndProc, hWnd, uMsg, wParam, lParam);
}
