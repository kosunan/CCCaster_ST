#pragma once
// ============================================================================
// SceneInputFilter — 統合入力フィルタ（全シーン・全モード対応）
//
// 【責務】
//   FrameInputBuffer から読取った入力に対し、現在のシーンと状態に応じた
//   フィルタを適用してゲームメモリに書き込む。
//
// 【フィルタ条件】
//   - InGame: START/FN1/FN2ブロック（メニュー誤操作防止）
//   - その他フェーズ: 現在はパススルー（将来的に拡張予定）
//
// 【呼び出しタイミング】
//   各 MatchScene::OnXxx() 内の GC::WriteInput() 直前。
// ============================================================================

#include <cstdint>
#include "core_dll/mbaa_mem/GamePhaseDetector.hpp"

namespace cccaster::domain::scene {

class SceneInputFilter {
public:
    /// 入力にシーン別フィルタを適用する
    /// @param phase   現在のゲーム画面
    /// @param input   生入力値 (direction << 16 | buttons)
    /// @return        フィルタ適用後の入力値
    static uint32_t Apply(cccaster::game_interface::GamePhase phase, uint32_t input);
};

} // namespace cccaster::domain::scene
