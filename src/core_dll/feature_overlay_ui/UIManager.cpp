// ============================================================================
// UIManager.cpp — 画面切替エントリポイント実装
// ============================================================================

#include "core_dll/feature_overlay_ui/UIManager.hpp"
#include "core_dll/feature_overlay_ui/State_Ui_Logic.hpp"
#include "core_dll/feature_overlay_ui/State_Ui_View.hpp"
#include "core_dll/feature_overlay_ui/CharaSelect_Ui_View.hpp"
#include "core_dll/feature_overlay_ui/InGame_Ui_View.hpp"
#include "core_dll/feature_overlay_ui/Rematch_Ui_View.hpp"
#include "core_dll/feature_overlay_ui/Controller_Ui_View.hpp"

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
}

void UIManager::OnRollbackInput(int num) {
    StateUiLogic::SetRollback(num);
    StateUiLogic::NotifyRollbackChanged();
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
