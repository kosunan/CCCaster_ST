// ============================================================================
// SceneInputFilter.cpp — 統合入力フィルタ（実装）
//
// 現時点ではパススルー。シーン別フィルタ条件は後日この関数に追加する。
// ============================================================================

#include "core_dll/engine/SceneInputFilter.hpp"
#include "core_dll/mbaa_mem/GamePhaseDetector.hpp"

namespace cccaster::domain::scene {

uint32_t SceneInputFilter::Apply(
    cccaster::game_interface::GamePhase /*phase*/, uint32_t input) {
    // TODO: シーン別フィルタ条件を実装
    //   - InGame: START/FN1/FN2 除去
    //   - CharaSelect: 150F A封印, B封印
    //   - Rematch: メニュー制限
    //   - オーバーレイ: 消費ボタンマスク
    return input;
}

} // namespace cccaster::domain::scene
