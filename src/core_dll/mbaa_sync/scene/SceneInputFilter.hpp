#pragma once
// ============================================================================
// SceneInputFilter — 統合入力フィルタ（全シーン・全モード対応）
//
// 【責務】
//   FrameInputBuffer から読取った入力に対し、現在のシーンと状態に応じた
//   フィルタを適用してゲームメモリに書き込む。
//
// 【フィルタ条件（後日詳細実装）】
//   - 画面 (GamePhase):   InGame→START/FN1/FN2ブロック等
//   - オーバーレイ表示中:  消費ボタンマスク
//   - キーコンフィグ中:    全入力横取り
//   - キャラ選択中:        150F A封印 / B封印
//   - スタイル選択中:      特定ボタン制限
//   - メニュー制限:        A/CONFIRM封印
//
// 【呼び出しタイミング】
//   各 SceneBusiness::OnXxx() 内の GC::WriteInput() 直前。
// ============================================================================

#include <cstdint>
#include "core_dll/mbaa_game/monitor/GamePhaseDetector.hpp"

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
