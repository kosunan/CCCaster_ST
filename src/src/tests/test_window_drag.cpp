#include "tests/test_support.hpp"
#include "core_dll/hook/WndProcHook.hpp"
#include "core_dll/hook/BorderlessDisplay.hpp"
#include "core_dll/ui/UIManager.hpp"
#include "core_dll/sync/InputTimeline.hpp"
#include "core_dll/sync/NetplaySession.hpp"
#include "core_dll/common/Platform.hpp"
#include <windowsx.h>
#include <vector>

// 非表示の自プロセス窓で本物のWndProcを検査。画面・実マウスは移動しない。
void HookLog(const char *) {}
namespace cccaster::platform {
int64_t RealMonotonicTicks() { return 1; }
}
namespace cccaster::domain::ui {
int UIManager::HandleWndProcMessage(HWND, UINT, WPARAM, LPARAM) { return 0; }
bool UIManager::IsMappingWindowOpen() { return false; }
}
namespace cccaster::core::sync {
InputTimeline &InputTimeline::GetInstance() { static InputTimeline s; return s; }
}
namespace cccaster::core::netplay {
NetplaySession &NetplaySession::GetInstance() { static NetplaySession s; return s; }
void NetplaySession::Stop() {}
}
namespace {
unsigned nativeCaption = 0, nativeModal = 0, updates = 0, escapeMessages = 0;
unsigned nativeMenu = 0, closeCommands = 0, nativeEnter = 0;
LRESULT CALLBACK Original(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP || message == WM_SYSCHAR) && w == VK_RETURN) {
        ++nativeEnter; return 0;
    }
    if (message == WM_NCRBUTTONDOWN || message == WM_NCRBUTTONUP || message == WM_CONTEXTMENU ||
        (message == WM_SYSCOMMAND && ((w & 0xfff0) == SC_MOUSEMENU || (w & 0xfff0) == SC_KEYMENU))) {
        ++nativeMenu; return 0;
    }
    if (message == WM_SYSCOMMAND && (w & 0xfff0) == SC_CLOSE) { ++closeCommands; return 0; }
    if (message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONUP || message == WM_NCLBUTTONDBLCLK) {
        ++nativeCaption; return 0;
    }
    if (message == WM_ENTERSIZEMOVE) ++nativeModal;
    if (message == WM_APP+1) { ++updates; return 0; }
    if (message == WM_APP+2) { cccaster::game_interface::WndProcHook::PumpMessages(); return 0; }
    if ((message == WM_KEYDOWN || message == WM_KEYUP) && w == VK_ESCAPE) ++escapeMessages;
    return DefWindowProc(hwnd, message, w, l);
}
RECT Position(HWND hwnd) { RECT r{}; GetWindowRect(hwnd,&r); return r; }
BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    MONITORINFO info{sizeof(info)};
    if (GetMonitorInfo(monitor, &info)) reinterpret_cast<std::vector<RECT>*>(data)->push_back(info.rcMonitor);
    return TRUE;
}
void Begin(HWND hwnd) {
    const auto r = Position(hwnd);
    SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(r.left+100,r.top+10));
}
void MoveTo(HWND hwnd, int screenX, int screenY, UINT message = WM_MOUSEMOVE, WPARAM buttons = MK_LBUTTON) {
    POINT p{screenX,screenY}; ScreenToClient(hwnd,&p);
    SendMessage(hwnd,message,buttons,MAKELPARAM(p.x,p.y));
}
}
int main() {
    using cccaster::game_interface::WndProcHook;
    WNDCLASS wc{};
    wc.lpfnWndProc = Original;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "CCCasterWindowDragTest";
    CC_CHECK(RegisterClass(&wc));
    const auto window = CreateWindow(wc.lpszClassName, "", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                     -300,40,640,480,nullptr,nullptr,wc.hInstance,nullptr);
    const auto other = CreateWindow(wc.lpszClassName, "", WS_OVERLAPPED,0,0,10,10,
                                    nullptr,nullptr,wc.hInstance,nullptr);
    CC_CHECK(window && other);
    if (!window || !other) return cccaster::test::Summarize("window_drag");
    CC_CHECK(WndProcHook::Initialize(window));
    CC_CASE("Alt+Enterは配置先モニターを覆い、リピートせず元の矩形と枠へ戻す");
    const auto normalRect = Position(window);
    RECT normalClient{}; GetClientRect(window, &normalClient);
    const auto normalStyle = GetWindowLongPtr(window, GWL_STYLE);
    MONITORINFO monitor{sizeof(monitor)};
    CC_CHECK(GetMonitorInfo(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor));
    SendMessage(window, WM_SYSKEYDOWN, VK_RETURN, 1u << 29);
    CC_CHECK(cccaster::game_interface::borderless::Active());
    auto fullscreenRect = Position(window);
    CC_CHECK(EqualRect(&monitor.rcMonitor, &fullscreenRect));
    CC_CHECK(!(GetWindowLongPtr(window, GWL_STYLE) & WS_CAPTION));
    RECT renderingClient{};
    CC_CHECK(cccaster::game_interface::borderless::RenderingClientRect(window, renderingClient));
    CC_CHECK(EqualRect(&normalClient, &renderingClient));
    CC_CHECK(!cccaster::game_interface::borderless::RenderingClientRect(other, renderingClient));
    SendMessage(window, WM_SYSKEYDOWN, VK_RETURN, (1u << 29) | (1u << 30));
    SendMessage(window, WM_SYSCHAR, VK_RETURN, 1u << 29);
    CC_CHECK(cccaster::game_interface::borderless::Active());
    SendMessage(window, WM_SYSKEYUP, VK_RETURN, 1u << 29);
    SendMessage(window, WM_ACTIVATE, WA_INACTIVE, 0);
    SendMessage(window, WM_KILLFOCUS, 0, 0);
    CC_CHECK(cccaster::game_interface::borderless::Active());
    SendMessage(window, WM_SYSKEYDOWN, VK_RETURN, 1u << 29);
    SendMessage(window, WM_SYSKEYUP, VK_RETURN, 1u << 29);
    CC_CHECK(!cccaster::game_interface::borderless::Active());
    CC_CHECK(!cccaster::game_interface::borderless::RenderingClientRect(window, renderingClient));
    auto restoredRect = Position(window);
    CC_CHECK(EqualRect(&normalRect, &restoredRect));
    CC_CHECK_EQ(GetWindowLongPtr(window, GWL_STYLE), normalStyle);
    CC_CHECK_EQ(nativeEnter, 0);
    CC_CASE("元の画面の縦横比を保持し、16:9窓の内部黒帯も二重に圧縮しない");
    const auto wide = cccaster::game_interface::borderless::FitAspect(1920, 1080, 640, 480);
    CC_CHECK_EQ(wide.left, 240); CC_CHECK_EQ(wide.right, 1680);
    CC_CHECK_EQ(wide.top, 0); CC_CHECK_EQ(wide.bottom, 1080);
    const auto tall = cccaster::game_interface::borderless::FitAspect(1080, 1920, 640, 480);
    CC_CHECK_EQ(tall.left, 0); CC_CHECK_EQ(tall.right, 1080);
    CC_CHECK_EQ(tall.bottom - tall.top, 810);
    const auto originalWide = cccaster::game_interface::borderless::FitAspect(2560, 1440, 1280, 720);
    CC_CHECK_EQ(originalWide.left, 0); CC_CHECK_EQ(originalWide.right, 2560);
    CC_CHECK_EQ(originalWide.top, 0); CC_CHECK_EQ(originalWide.bottom, 1440);
    for (const auto size : {SIZE{1920,1080}, SIZE{2560,1440}, SIZE{1080,1920}, SIZE{1366,768}}) {
        const auto content = cccaster::game_interface::borderless::FitAspect(size.cx, size.cy, 640, 480);
        // 丸め1px以内でX/Yの倍率が同じ。片方だけ75%へ縮む旧不具合を許さない。
        CC_CHECK(std::abs((content.right - content.left) * 480 - (content.bottom - content.top) * 640) <= 640);
    }
    CC_CASE("接続された各モニターへの移動後に配置先を選び直す");
    std::vector<RECT> monitors;
    CC_CHECK(EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors)));
    for (const auto& bounds : monitors) {
        SetWindowPos(window, nullptr, bounds.left + 30, bounds.top + 30, 320, 240, SWP_NOZORDER | SWP_NOACTIVATE);
        const auto before = Position(window);
        SendMessage(window, WM_SYSKEYDOWN, VK_RETURN, 1u << 29);
        SendMessage(window, WM_SYSKEYUP, VK_RETURN, 1u << 29);
        auto fullscreen = Position(window);
        CC_CHECK(EqualRect(&bounds, &fullscreen));
        SendMessage(window, WM_SYSKEYDOWN, VK_RETURN, 1u << 29);
        SendMessage(window, WM_SYSKEYUP, VK_RETURN, 1u << 29);
        auto after = Position(window);
        CC_CHECK(EqualRect(&before, &after));
    }
    SetWindowPos(window, nullptr, normalRect.left, normalRect.top, normalRect.right - normalRect.left,
                 normalRect.bottom - normalRect.top, SWP_NOZORDER | SWP_NOACTIVATE);
    CC_CASE("通常フレーム境界のキュー処理は自窓だけを扱い再入と投稿過多を制限する");
    PostMessage(other,WM_APP+1,0,0);
    PostMessage(window,WM_APP+2,0,0);
    for (int i=0;i<100;++i) PostMessage(window,WM_APP+1,0,0);
    WndProcHook::PumpMessages();
    CC_CHECK(updates > 0 && updates <= 63); // 初回のOSメッセージも64件枠に含まれる。
    WndProcHook::PumpMessages();
    CC_CHECK_EQ(updates,100);
    MSG pending{};
    CC_CHECK(PeekMessage(&pending,other,WM_APP+1,WM_APP+1,PM_REMOVE));
    updates=0;
    while (PeekMessage(&pending,nullptr,0,0,PM_REMOVE)) {
        TranslateMessage(&pending);
        DispatchMessage(&pending);
    }
    PostQuitMessage(17);
    WndProcHook::PumpMessages();
    CC_CHECK(PeekMessage(&pending,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE));
    CC_CHECK_EQ(pending.wParam,17);
    CC_CASE("観戦の更新停止中もキュー経由のタイトルバー操作だけで移動を完了できる");
    const auto waitOrigin = Position(window);
    PostMessage(window, WM_NCLBUTTONDOWN, HTCAPTION,
                MAKELPARAM(waitOrigin.left + 100, waitOrigin.top + 10));
    WndProcHook::PumpMessages();
    CC_CHECK(GetCapture() == window);
    POINT waitCursor{waitOrigin.left + 140, waitOrigin.top + 30};
    ScreenToClient(window, &waitCursor);
    PostMessage(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(waitCursor.x, waitCursor.y));
    WndProcHook::PumpMessages();
    CC_CHECK_EQ(Position(window).left, waitOrigin.left + 40);
    CC_CHECK_EQ(Position(window).top, waitOrigin.top + 20);
    // 移動後のクライアント座標で解放する。ゲーム更新は一度も呼んでいない。
    waitCursor = {waitOrigin.left + 140, waitOrigin.top + 30};
    ScreenToClient(window, &waitCursor);
    PostMessage(window, WM_LBUTTONUP, 0, MAKELPARAM(waitCursor.x, waitCursor.y));
    WndProcHook::PumpMessages();
    CC_CHECK(GetCapture() != window);
    CC_CHECK(!WndProcHook::BlocksEscapeExit());
    CC_CHECK_EQ(updates, 0);
    CC_CHECK_EQ(nativeModal, 0);
    CC_CASE("標準メニューのマウス/キーボード入口を抑止し閉じる命令を維持する");
    SendMessage(window,WM_NCLBUTTONDOWN,HTSYSMENU,0);
    SendMessage(window,WM_NCLBUTTONUP,HTSYSMENU,0);
    SendMessage(window,WM_NCLBUTTONDBLCLK,HTSYSMENU,0);
    for (auto hit : {HTCAPTION, HTSYSMENU}) {
        SendMessage(window,WM_NCRBUTTONDOWN,hit,0);
        SendMessage(window,WM_NCRBUTTONUP,hit,0);
    }
    SendMessage(window,WM_SYSCOMMAND,SC_MOUSEMENU | 3,0);
    SendMessage(window,WM_SYSCOMMAND,SC_KEYMENU,' ');
    SendMessage(window,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(window),-1);
    CC_CHECK_EQ(nativeCaption,0);
    CC_CHECK_EQ(nativeMenu,0);
    SendMessage(window,WM_SYSCOMMAND,SC_CLOSE,0);
    CC_CHECK_EQ(closeCommands,1);
    CC_CASE("タイトルバー押下で非アクティブ窓へフォーカスが移り、保持中も通常処理へ戻る");
    // 非表示窓ではOSの前面化制限と切り離してアクティブ窓・入力先を検査する。
    // 解放も移動もしていない押下直後に切り替わることが必要。
    SetFocus(other);
    CC_CHECK(GetActiveWindow() == other);
    CC_CHECK(GetFocus() == other);
    const auto original = Position(window);
    Begin(window);
    CC_CHECK(GetActiveWindow() == window);
    CC_CHECK(GetFocus() == window);
    CC_CHECK(GetCapture() == window);
    CC_CHECK(WndProcHook::BlocksEscapeExit());
    for (int i=0;i<180;++i) SendMessage(window,WM_APP+1,0,0);
    CC_CHECK_EQ(updates,180);
    CC_CHECK_EQ(nativeCaption,0);
    CC_CHECK_EQ(nativeModal,0);
    CC_CASE("負の画面座標と移動後のクライアント座標を正しく扱う");
    MoveTo(window,original.left+145,original.top-15);
    auto moved = Position(window);
    CC_CHECK_EQ(moved.left,original.left+45);
    CC_CHECK_EQ(moved.top,original.top-25);
    MoveTo(window,original.left+165,original.top+30,WM_LBUTTONUP,0);
    moved = Position(window);
    CC_CHECK_EQ(moved.left,original.left+65);
    CC_CHECK_EQ(moved.top,original.top+20);
    CC_CHECK(GetCapture() != window);
    CC_CHECK(!WndProcHook::BlocksEscapeExit());
    CC_CHECK(GetActiveWindow() == window);
    CC_CHECK(GetFocus() == window);
    CC_CASE("Escで元位置に戻しリピートとキーアップをゲームへ漏らさない");
    const auto beforeCancel = Position(window);
    Begin(window);
    MoveTo(window,beforeCancel.left+180,beforeCancel.top+30);
    SendMessage(window,WM_KEYDOWN,VK_ESCAPE,0);
    CC_CHECK(WndProcHook::BlocksEscapeExit());
    SendMessage(window,WM_KEYDOWN,VK_ESCAPE,1LL<<30);
    SendMessage(window,WM_KEYUP,VK_ESCAPE,0);
    CC_CHECK(!WndProcHook::BlocksEscapeExit());
    CC_CHECK_EQ(escapeMessages,0);
    CC_CHECK_EQ(Position(window).left,beforeCancel.left);
    CC_CHECK_EQ(Position(window).top,beforeCancel.top);
    CC_CHECK(GetCapture() != window);
    CC_CASE("捕捉を別窓に渡した後は相手の捕捉を解除せず移動を終了する");
    Begin(window);
    SetCapture(other);
    CC_CHECK(GetCapture() == other);
    CC_CHECK(!WndProcHook::BlocksEscapeExit());
    MoveTo(window,0,0);
    CC_CHECK_EQ(Position(window).left,beforeCancel.left);
    ReleaseCapture();
    CC_CASE("ボタン解放通知の欠落とキャンセルからも捕捉を解放する");
    Begin(window);
    MoveTo(window,0,0,WM_MOUSEMOVE,0);
    CC_CHECK(GetCapture() != window);
    Begin(window);
    SendMessage(window,WM_CANCELMODE,0,0);
    CC_CHECK(GetCapture() != window);
    Begin(window);
    SendMessage(window,WM_ACTIVATE,WA_INACTIVE,0);
    CC_CHECK(GetCapture() != window);
    CC_CASE("解除時に捕捉とフックを残さずタイトルバー以外を横取りしない");
    Begin(window);
    SendMessage(window,WM_KEYDOWN,VK_ESCAPE,0);
    SendMessage(window,WM_KILLFOCUS,0,0);
    CC_CHECK(!WndProcHook::BlocksEscapeExit());
    SendMessage(window,WM_NCLBUTTONDOWN,HTCLOSE,0);
    CC_CHECK_EQ(nativeCaption,1);
    Begin(window);
    WndProcHook::Shutdown();
    CC_CHECK(GetCapture() != window);
    CC_CHECK(reinterpret_cast<WNDPROC>(GetWindowLongPtr(window,GWLP_WNDPROC)) == Original);
    DestroyWindow(window);
    DestroyWindow(other);
    UnregisterClass(wc.lpszClassName,wc.hInstance);
    return cccaster::test::Summarize("window_drag");
}
