#pragma once
#include <windows.h>

namespace cccaster::game_interface {

class InputHook {
public:
    static bool Initialize(HWND hwnd);
    static void Shutdown();

private:
    static LRESULT CALLBACK HookedWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static WNDPROC original_WndProc;
    static HWND hooked_hwnd;
};

} // namespace cccaster::game_interface
