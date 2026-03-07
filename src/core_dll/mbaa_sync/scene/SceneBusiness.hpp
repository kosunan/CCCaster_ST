#pragma once
// ============================================================================
// SceneBusiness — 画面別業務ロジック（統合版）
//
// 【責務】
//   各画面でのネット対戦特有の業務処理を集約する。
//   CentralBuffer からの入力読取 → SceneInputFilter → WriteInput の
//   共通フローを内部で呼び出す。
//
// 【各画面の業務】
//   CharaSelect: CentralBuffer 読取 → 入力フィルタ → WriteInput
//   Loading:     CentralBuffer 読取 → 入力フィルタ → WriteInput
//   InGame:      ラウンド開始同期 → CentralBuffer 読取 → 入力フィルタ → WriteInput
//   Rematch:     メニュー選択同期 + 自動ナビ + CentralBuffer 読取
//
// 【削除された処理】
//   - パケット作成/送信 (SyncCoordinator に完全委譲)
//   - RollbackEngine 管理 (CentralBuffer が自動的に処理)
// ============================================================================

#include "core_dll/mbaa_sync/orchestrator/SessionContext.hpp"

namespace cccaster::domain::scene {

class SceneBusiness {
public:
    // ─── 画面別業務 ─────────────────────────────────
    static void OnCharaSelect(session::SessionContext& ctx);
    static void OnLoading(session::SessionContext& ctx);
    static void OnInGame(session::SessionContext& ctx);
    static void OnRematch(session::SessionContext& ctx);

    // ─── リセット ────────────────────────────────────
    static void ResetCharaSelect();
    static void ResetLoading();
    static void ResetInGame();
    static void ResetRematch();

    // ─── リマッチ外部API ─────────────────────────────
    static void SetRemoteRetryMenuIndex(int8_t menuIndex);
};

} // namespace cccaster::domain::scene
