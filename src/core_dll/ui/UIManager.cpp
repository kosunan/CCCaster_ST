// ============================================================================
// UIManager.cpp — 画面切替エントリポイント実装
// ============================================================================

#include "core_dll/ui/UIManager.hpp"
#include "core_dll/ui/State_Ui_Logic.hpp"
#include "core_dll/ui/CharaSelect_Ui_View.hpp"
#include "core_dll/ui/InGame_Ui_View.hpp"
#include "core_dll/ui/Rematch_Ui_View.hpp"
#include "core_dll/ui/Controller_Ui_View.hpp"
#include "core_dll/sync/MenuInputBuffer.hpp"
#include "core_dll/sync/MatchInputBuffer.hpp"
#include "core_dll/sync/NetplaySession.hpp"
#include "core_dll/mbaa_mem/MbaaAddresses.hpp"
#include "core_dll/mbaa_mem/IGameMemory.hpp"
#include "core_dll/hook/DirectInputHook.hpp"
#include <imgui.h>
#include <Dbt.h>

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace cccaster::domain::ui {

void UIManager::Render(UiPhase phase) {
    switch (phase) {
        case UiPhase::CharaSelect:
            CharaSelectUiView::Draw();
            break;
        case UiPhase::InGame:
            InGameUiView::Draw();
            break;
        case UiPhase::Rematch:
            RematchUiView::Draw();
            break;
        case UiPhase::None:
        default:
            break;
    }
}

// --- 入力イベント委譲 ---
void UIManager::OnDelayInput(int num) {
    StateUiLogic::SetDelay(num);
    StateUiLogic::NotifyDelayChanged();
    // ★ 実処理にも即反映
    cccaster::core::sync::MenuInputBuffer::GetInstance().SetDelay(static_cast<int16_t>(num));
    cccaster::core::sync::MatchInputBuffer::GetInstance().SetSyncParams(
        static_cast<int16_t>(num),
        cccaster::core::sync::MatchInputBuffer::GetInstance().GetMaxRollback());
    if (cccaster::core::netplay::NetplaySession::GetInstance().IsRunning())
        cccaster::core::netplay::NetplaySession::GetInstance().SetDelayFrames(num);
}

void UIManager::OnRollbackInput(int num) {
    StateUiLogic::SetRollback(num);
    StateUiLogic::NotifyRollbackChanged();
    // ★ 実処理にも即反映 (Menuはロールバックを持たないため設定不要)
    cccaster::core::sync::MatchInputBuffer::GetInstance().SetSyncParams(
        cccaster::core::sync::MatchInputBuffer::GetInstance().GetDelay(),
        static_cast<int16_t>(num));
    if (cccaster::core::netplay::NetplaySession::GetInstance().IsRunning())
        cccaster::core::netplay::NetplaySession::GetInstance().SetMaxRollback(num);
}

void UIManager::OnMappingInput() {
    if (StateUiLogic::IsMappingWindowOpen()) {
        StateUiLogic::CloseMappingWindow();
        ControllerUiView::OnClose();
    } else {
        StateUiLogic::ToggleMappingWindow();
    }
}

void UIManager::UpdateNetworkMetrics(float pingMs, float jitterMs) {
    StateUiLogic::UpdateNetworkMetrics(pingMs, jitterMs);
}

bool UIManager::IsMappingWindowOpen() {
    return StateUiLogic::IsMappingWindowOpen();
}

// ============================================================================
// HandleWndProcMessage — WndProc メッセージの UI 側処理
//   WndProcHook から呼ばれる。フック基盤（SetWindowLongPtr）は hook/ に残し、
//   UI 固有ロジック（ImGui, ホットキー, マッピング, デバイス検出）をここに集約。
//
//   @return >0: ゲームにブロック, 0: ゲームに通す, -1: 判定なし(元 WndProc に委譲)
// ============================================================================

int UIManager::HandleWndProcMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // (1) ImGui に渡す
    if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) {
        return 1; // ImGui が消費
    }

    // (2) ホットキー処理
    if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN) {
        // リピート除外: lParam bit30=1 → 前回もキーダウン
        bool isRepeat = (lParam & (1 << 30)) != 0;
        if (isRepeat) {
            if (IsMappingWindowOpen()) return 1; // マッピング中はブロック
            return -1; // リピートはゲームに通す
        }

        bool isCtrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool isAltDown = (uMsg == WM_SYSKEYDOWN) || ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0);
        int key = static_cast<int>(wParam);

        // F4: マッピングトグル（キャラセレ画面のみ）
        if (key == VK_F4 && !isAltDown) {
            auto& mem = cccaster::game_interface::GameMem();
            if (mem.IsAvailable() && mem.GameMode() != CC_GAME_MODE_CHARA_SELECT) {
                return 1; // キャラセレ以外では無視
            }
            OnMappingInput();
            return 1;
        }

        // マッピング中は全キーブロック
        if (IsMappingWindowOpen()) return 1;

        // Ctrl+数字: Delay / Alt+数字: Rollback
        if (key >= '0' && key <= '9') {
            int num = key - '0';
            if (isCtrlDown)  { OnDelayInput(num); return 1; }
            if (isAltDown)   { OnRollbackInput(num); return 1; }
        }
        if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) {
            int num = key - VK_NUMPAD0;
            if (isCtrlDown)  { OnDelayInput(num); return 1; }
            if (isAltDown)   { OnRollbackInput(num); return 1; }
        }
    }

    // (3) マッピング中は KEYUP もブロック
    if ((uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP) && IsMappingWindowOpen()) {
        return 1;
    }

    // (4) USB デバイス挿抜 → コントローラ再検出
    if (uMsg == WM_DEVICECHANGE) {
        if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
            cccaster::game_interface::DirectInputHook::RefreshDevices();
        }
    }

    // (5) ImGui WantCapture
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse && (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST)) return 1;
    if (io.WantCaptureKeyboard && (uMsg == WM_KEYDOWN || uMsg == WM_KEYUP ||
                                    uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP || uMsg == WM_CHAR)) return 1;

    return -1; // 判定なし → 元 WndProc に委譲
}

} // namespace cccaster::domain::ui
