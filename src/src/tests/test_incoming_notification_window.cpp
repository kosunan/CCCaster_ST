#include "test_support.hpp"
#include "launcher/RequestNotification.hpp"
#include <string>

int main() {
    using namespace cccaster::notification;
    // 自分で作った試験窓だけを操作する。通知音は試験中に鳴らさない。
    const auto mainWindow = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Notification test",
        WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    CC_CHECK(mainWindow != nullptr);
    const auto foreground = GetForegroundWindow();

    CC_CASE("通知はフォーカスを奪わず、GUI最小化と連動して隠れない");
    ShowIncomingToast(mainWindow, L"notification window test");
    CC_CHECK(IsWindowVisible(incomingToastWindow));
    CC_CHECK(GetForegroundWindow() == foreground);
    ShowWindow(mainWindow, SW_SHOWMINNOACTIVE);
    CC_CHECK(IsIconic(mainWindow));
    CC_CHECK(IsWindowVisible(incomingToastWindow));
    CC_CHECK(GetForegroundWindow() == foreground);

    CC_CASE("最小化したまま次の通知へ置き換えられる");
    ShowIncomingToast(mainWindow, L"second notification");
    CC_CHECK(IsWindowVisible(incomingToastWindow));
    CC_CHECK(IsIconic(mainWindow));
    CC_CHECK(GetForegroundWindow() == foreground);
    wchar_t title[64]{};
    GetWindowTextW(incomingToastWindow, title, 64);
    CC_CHECK(std::wstring(title) == L"second notification");

    CC_CASE("期限切れと明示的な消去で通知窓を残さない");
    SendMessageW(incomingToastWindow, WM_TIMER, 1, 0);
    CC_CHECK(incomingToastWindow == nullptr);
    ShowIncomingToast(mainWindow, L"cleanup test");
    CloseIncomingToast();
    CloseIncomingToast();
    CC_CHECK(incomingToastWindow == nullptr);
    DestroyWindow(mainWindow);
    return cccaster::test::Summarize("incoming_notification_window");
}
