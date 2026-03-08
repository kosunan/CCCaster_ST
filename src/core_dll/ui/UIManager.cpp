// ============================================================================
// UIManager.cpp — 画面切替エントリポイント実装
// ============================================================================

#include "core_dll/ui/UIManager.hpp"
#include "core_dll/ui/State_Ui_Logic.hpp"
#include "core_dll/ui/State_Ui_View.hpp"
#include "core_dll/ui/CharaSelect_Ui_View.hpp"
#include "core_dll/ui/InGame_Ui_View.hpp"
#include "core_dll/ui/Rematch_Ui_View.hpp"
#include "core_dll/ui/Controller_Ui_View.hpp"
#include "core_dll/fg_netplay/buffer/FrameInputBuffer.hpp"
#include "core_dll/fg_netplay/frame_sync/NetplaySession.hpp"

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
    cccaster::core::sync::FrameInputBuffer::GetInstance().SetSyncParams(
        static_cast<int16_t>(num),
        cccaster::core::sync::FrameInputBuffer::GetInstance().GetMaxRollback());
    if (cccaster::core::netplay::NetplaySession::GetInstance().IsRunning())
        cccaster::core::netplay::NetplaySession::GetInstance().SetDelayFrames(num);
}

void UIManager::OnRollbackInput(int num) {
    StateUiLogic::SetRollback(num);
    StateUiLogic::NotifyRollbackChanged();
    // ★ 実処理にも即反映
    cccaster::core::sync::FrameInputBuffer::GetInstance().SetSyncParams(
        cccaster::core::sync::FrameInputBuffer::GetInstance().GetDelay(),
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

} // namespace cccaster::domain::ui
