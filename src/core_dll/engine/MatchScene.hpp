#pragma once
// ============================================================================
// MatchScene — 画面別業務ロジック（統合版）
//
// 【責務】
//   各画面でのネット対戦特有の業務処理を集約する。
//   FrameInputBuffer からの入力読取 → SceneInputFilter → WriteInput の
//   共通フローを内部で呼び出す。
//
// 【各画面の業務】
//   CharaSelect: FrameInputBuffer 読取 → 入力フィルタ → WriteInput
//   Loading:     FrameInputBuffer 読取 → 入力フィルタ → WriteInput
//   InGame:      ラウンド開始同期 → FrameInputBuffer 読取 → 入力フィルタ → WriteInput
//   Rematch:     メニュー選択同期 + 自動ナビ + FrameInputBuffer 読取
//
// 【削除された処理】
//   - パケット作成/送信 (NetplaySession に完全委譲)
//   - RollbackEngine 管理 (FrameInputBuffer が自動的に処理)
// ============================================================================

#include "core_dll/engine/MatchContext.hpp"

namespace cccaster::domain::scene {

class MatchScene {
public:
    // ─── 画面別業務 ─────────────────────────────────
    static void OnCharaSelect(session::MatchContext& ctx);
    static void OnLoading(session::MatchContext& ctx);
    static void OnInGame(session::MatchContext& ctx);
    static void OnRematch(session::MatchContext& ctx);

    // ─── リセット ────────────────────────────────────
    static void ResetCharaSelect();
    static void ResetLoading();
    static void ResetInGame();
    static void ResetRematch();

    // ─── リマッチ外部API ─────────────────────────────
    static void SetRemoteRetryMenuIndex(int8_t menuIndex);
};

} // namespace cccaster::domain::scene
