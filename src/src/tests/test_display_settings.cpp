#include "core_dll/hook/BorderlessDisplay.hpp"
#include <cstdlib>
#include <iostream>
using namespace cccaster::game_interface::borderless;
void HookLog(const char*) {}
void check(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
bool sizeIs(Resolution size, int w, int h) { return size.width == w && size.height == h; }
int main() {
    check(sizeIs(NextResolution({800,600},{1000,800},1),960,720),"領域内の次のサイズ");
    check(sizeIs(NextResolution({960,720},{1000,800},1),640,480),"収まらない候補を除いて循環");
    check(sizeIs(NextResolution({640,480},{1000,800},-1),960,720),"逆順に循環");
    check(!NextResolution({640,480},{500,400},1).width,"収まる候補がなければ変更しない");
    WNDCLASSW type{}; type.lpfnWndProc = DefWindowProcW;
    type.hInstance = GetModuleHandle(nullptr); type.lpszClassName = L"CCCasterDisplaySettingsTest";
    check(RegisterClassW(&type) != 0,"テスト窓の登録");
    RECT outer{0,0,640,480};
    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectEx(&outer,style,FALSE,0);
    const auto hwnd = CreateWindowW(type.lpszClassName,L"display settings test",style,100,100,
        outer.right-outer.left,outer.bottom-outer.top,nullptr,nullptr,type.hInstance,nullptr);
    check(hwnd != nullptr,"非表示のテスト窓を作成");
    Attach(hwnd);
    check(sizeIs(GetDisplaySettings().windowSize,640,480),"実クライアント寸法を取得");
    check(ApplyRenderResolution({800,600}) && sizeIs(GetDisplaySettings().windowSize,800,600),"再設定後の実窓を800x600へ");
    check(SetFullscreen(true) && Active(),"共通入口で全画面へ");
    MONITORINFO info{sizeof(info)}; GetMonitorInfo(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&info);
    RECT actual{}; GetWindowRect(hwnd,&actual);
    check(EqualRect(&actual,&info.rcMonitor),"モニター全域へ配置");
    RECT rendering{};
    check(RenderingClientRect(hwnd,rendering) && rendering.right == 800 && rendering.bottom == 600,"全画面の描画比率を保持");
    check(ApplyRenderResolution({1280,720}) && sizeIs(GetDisplaySettings().windowSize,1280,720),"全画面中も描画解像度を変更");
    GetWindowRect(hwnd,&actual);
    check(EqualRect(&actual,&info.rcMonitor),"全画面中の窓寸法法は不変");
    check(RenderingClientRect(hwnd,rendering) && rendering.right == 1280 && rendering.bottom == 720,"全画面の比率も変更後の描画解像度へ追従");
    const auto content = FitContent(1920,1080);
    check(content.right-content.left == 1920 && content.bottom-content.top == 1080,"4対3から16対9へ変えたら黒帯を除く");
    check(HandleMessage(hwnd,WM_SYSKEYDOWN,VK_RETURN,1u<<29) && !Active(),"Alt Enterでも同じ解除経路");
    check(sizeIs(GetDisplaySettings().windowSize,1280,720),"選んだ解像度に復元");
    check(HandleMessage(hwnd,WM_SYSKEYDOWN,VK_RETURN,(1u<<29)|(1u<<30)) && !Active(),"長押しは再切替しない");
    HandleMessage(hwnd,WM_SYSKEYUP,VK_RETURN,1u<<29);
    check(HandleMessage(hwnd,WM_SYSKEYDOWN,VK_RETURN,1u<<29) && Active(),"Alt Enterで再び全画面へ");
    check(SetFullscreen(false) && !Active(),"メニュー側から同じ通常窓へ");
    check(sizeIs(GetDisplaySettings().windowSize,1280,720),"通常窓の寸法を保持");
    Detach(); DestroyWindow(hwnd); UnregisterClassW(type.lpszClassName,type.hInstance);
    check(!GetDisplaySettings().available,"窓が無いと操作不可");
}
