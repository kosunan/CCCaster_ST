#include "tests/test_support.hpp"
#include "core_dll/hook/WndProcHook.hpp"
#include "core_dll/hook/BorderlessDisplay.hpp"
#include "core_dll/ui/UIManager.hpp"
#include "core_dll/sync/InputTimeline.hpp"
#include "core_dll/sync/NetplaySession.hpp"
#include "core_dll/common/Platform.hpp"
#include "core_dll/engine/SceneRunner.hpp"
#include "core_dll/mbaa_mem/GamePhaseDetector.hpp"
#include "core_dll/mbaa_mem/TrainingHitboxMenu.hpp"
#include "core_dll/mbaa_mem/TrainingPaletteMenu.hpp"
#include "core_dll/engine/SelectionOptions.hpp"
#include "shared_contracts/IpcData.hpp"
#include <windowsx.h>
#include <vector>

// 非表示の自プロセス窓で本物のWndProcを検査。画面・実マウスは移動しない。
void HookLog(const char *) {}
namespace {
uint8_t appMode = 255;
auto phase = cccaster::game_interface::GamePhase::Unknown;
bool hitboxOpen = false, mappingOpen = false;
struct LocalExit {};
unsigned localTerminations = 0;
}
namespace cccaster::domain::session {
uint8_t SceneRunner::AppMode() { return appMode; }
}
namespace cccaster::game_interface {
GamePhase PhaseMonitor::GetCurrentPhase() { return phase; }
}
namespace cccaster::training_hitbox {
bool Active() { return hitboxOpen; }
}
namespace cccaster::platform {
int64_t RealMonotonicTicks() { return 1; }
[[noreturn]] void TerminateSelf() { ++localTerminations; throw LocalExit{}; }
}
namespace cccaster::domain::ui {
int UIManager::HandleWndProcMessage(HWND, UINT, WPARAM, LPARAM) { return 0; }
bool UIManager::IsMappingWindowOpen() { return mappingOpen; }
}
namespace cccaster::core::sync {
InputTimeline &InputTimeline::GetInstance() { static InputTimeline s; return s; }
}
namespace cccaster::core::netplay {
NetplaySession &NetplaySession::GetInstance() { static NetplaySession s; return s; }
void NetplaySession::Stop() {}
}
namespace {
// 終了stubの例外をWinAPI内部へ投げない。設置済みの本物のWndProcを直接呼ぶ。
LRESULT ExitMessage(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    const auto procedure = reinterpret_cast<WNDPROC>(GetWindowLongPtr(hwnd, GWLP_WNDPROC));
    try { return procedure(hwnd, message, w, l); }
    catch (const LocalExit&) { return 0; }
}
unsigned nativeCaption = 0, nativeModal = 0, updates = 0, escapeMessages = 0;
unsigned nativeMenu = 0, closeCommands = 0, closeMessages = 0, nativeEnter = 0;
LRESULT CALLBACK Original(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP || message == WM_SYSCHAR) && w == VK_RETURN) {
        ++nativeEnter; return 0;
    }
    if (message == WM_NCRBUTTONDOWN || message == WM_NCRBUTTONUP || message == WM_CONTEXTMENU ||
        (message == WM_SYSCOMMAND && ((w & 0xfff0) == SC_MOUSEMENU || (w & 0xfff0) == SC_KEYMENU))) {
        ++nativeMenu; return 0;
    }
    if (message == WM_SYSCOMMAND && (w & 0xfff0) == SC_CLOSE) { ++closeCommands; return 0; }
    if (message == WM_CLOSE) { ++closeMessages; return 0; }
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
    ExitMessage(window,WM_SYSCOMMAND,SC_CLOSE,0);
    CC_CHECK_EQ(closeCommands,0);
    CC_CHECK_EQ(localTerminations,1);
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
    ExitMessage(window,WM_KEYDOWN,VK_ESCAPE,0);
    CC_CHECK(WndProcHook::BlocksEscapeExit());
    ExitMessage(window,WM_KEYDOWN,VK_ESCAPE,1LL<<30);
    ExitMessage(window,WM_KEYUP,VK_ESCAPE,0);
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
    ExitMessage(window,WM_KEYDOWN,VK_ESCAPE,0);
    SendMessage(window,WM_KILLFOCUS,0,0);
    CC_CHECK(!WndProcHook::BlocksEscapeExit());
    SendMessage(window,WM_NCLBUTTONDOWN,HTCLOSE,0);
    CC_CHECK_EQ(nativeCaption,1);
    CC_CHECK_EQ(localTerminations,1);
    CC_CASE("戦闘中だけEsc・閉じるボタン・直接終了要求を遮断しIPCへ通知しない");
    using namespace cccaster::public_api;
    using cccaster::game_interface::GamePhase;
    SharedState state{};
    state.magicVersion = IPC_VERSION_MAGIC;
    const auto ipc = IpcManager::CreateAndWrite(state);
    CC_CHECK(ipc != nullptr);
    for (const auto mode : {0, 1, 2, 4, 5, 255}) {
        appMode = mode;
        for (const auto scene : {GamePhase::Unknown, GamePhase::MainMenu, GamePhase::CharaSelect,
                                GamePhase::Loading, GamePhase::InGame, GamePhase::Rematch}) {
            phase = scene;
            const bool blocked = (mode == 0 || mode == 5) && scene == GamePhase::InGame;
            CC_CHECK_EQ(WndProcHook::BlocksCloseExit(), blocked);
            CC_CHECK_EQ(WndProcHook::BlocksEscapeExit(), blocked);
            IpcManager::UpdateOrReadState([mode](SharedState &s) {
                s.targetGameMode = mode; s.gameShutdownRequest = false; s.localExitReason = 0;
            });
            const auto beforeClose = closeMessages;
            const auto beforeCommand = closeCommands;
            const auto beforeCaption = nativeCaption;
            const auto beforeTerminate = localTerminations;
            for (auto message : {WM_NCLBUTTONDOWN, WM_NCLBUTTONUP, WM_NCLBUTTONDBLCLK})
                SendMessage(window,message,HTCLOSE,0);
            ExitMessage(window,WM_SYSCOMMAND,SC_CLOSE | 3,0);
            ExitMessage(window,WM_CLOSE,0,0);
            SharedState observed{};
            CC_CHECK(IpcManager::OpenAndRead(observed));
            CC_CHECK_EQ(observed.gameShutdownRequest, !blocked && mode == 0);
            CC_CHECK_EQ(observed.localExitReason, blocked ? 0u : unsigned(SessionExitReason::CloseButton));
            CC_CHECK_EQ(nativeCaption-beforeCaption, blocked ? 0u : 3u);
            CC_CHECK_EQ(closeMessages-beforeClose,0u);
            CC_CHECK_EQ(closeCommands-beforeCommand,0u);
            CC_CHECK_EQ(localTerminations-beforeTerminate, blocked || mode == 0 ? 0u : 2u);
            IpcManager::UpdateOrReadState([](SharedState &s) { s.gameShutdownRequest=false;s.localExitReason=0; });
            const auto beforeEscape = closeMessages;
            const auto beforeEscapeTerminate = localTerminations;
            for (auto message : {WM_KEYDOWN, WM_SYSKEYDOWN}) ExitMessage(window,message,VK_ESCAPE,0);
            ExitMessage(window,WM_KEYDOWN,VK_ESCAPE,1LL<<30);
            ExitMessage(window,WM_KEYUP,VK_ESCAPE,0);
            WndProcHook::PumpMessages();
            CC_CHECK(IpcManager::OpenAndRead(observed));
            CC_CHECK_EQ(observed.gameShutdownRequest, !blocked && mode == 0);
            CC_CHECK_EQ(observed.localExitReason, blocked ? 0u : unsigned(SessionExitReason::Escape));
            CC_CHECK_EQ(closeMessages-beforeEscape,0u);
            CC_CHECK_EQ(localTerminations-beforeEscapeTerminate, blocked || mode == 0 ? 0u : 3u);
        }
    }
    CC_CASE("戦闘からキャラ選択へ移っても押し続けたEscでは終了しない");
    const auto beforeHeldTerminate = localTerminations;
    appMode=5;phase=GamePhase::InGame;
    ExitMessage(window,WM_KEYDOWN,VK_ESCAPE,0);
    phase=GamePhase::CharaSelect;
    CC_CHECK(WndProcHook::BlocksEscapeExit());
    ExitMessage(window,WM_KEYDOWN,VK_ESCAPE,1LL<<30);
    ExitMessage(window,WM_KEYUP,VK_ESCAPE,0);
    CC_CHECK(!WndProcHook::BlocksEscapeExit());
    CC_CHECK_EQ(localTerminations,beforeHeldTerminate);
    CC_CASE("選択設定・パレット・HITBOX・F4のEscを終了要求にしない");
    appMode=1;phase=GamePhase::InGame;
    namespace options=cccaster::domain::scene::selection_options;
    for (unsigned menu=0;menu<4;++menu) {
        options::active = menu==0;
        cccaster::training_palette::editorOpen=menu==1;
        hitboxOpen=menu==2;mappingOpen=menu==3;
        IpcManager::UpdateOrReadState([](SharedState &s) { s.gameShutdownRequest=false;s.localExitReason=0; });
        const auto beforeClose=closeMessages;
        const auto beforeMenuTerminate=localTerminations;
        ExitMessage(window,WM_KEYDOWN,VK_ESCAPE,0);
        WndProcHook::PumpMessages();
        SharedState observed{}; CC_CHECK(IpcManager::OpenAndRead(observed));
        CC_CHECK_EQ(observed.localExitReason,0);
        CC_CHECK_EQ(closeMessages,beforeClose);
        CC_CHECK_EQ(localTerminations,beforeMenuTerminate);
    }
    options::active=false;cccaster::training_palette::editorOpen=false;hitboxOpen=mappingOpen=false;
    CloseHandle(ipc);
    Begin(window);
    WndProcHook::Shutdown();
    CC_CHECK(GetCapture() != window);
    CC_CHECK(reinterpret_cast<WNDPROC>(GetWindowLongPtr(window,GWLP_WNDPROC)) == Original);
    DestroyWindow(window);
    DestroyWindow(other);
    UnregisterClass(wc.lpszClassName,wc.hInstance);
    return cccaster::test::Summarize("window_drag");
}
