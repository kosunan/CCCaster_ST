#pragma once
// ============================================================================
// MatchScene — 画面別業務ロジック（統合版）
//
// 【責務】
//   各画面でのネット対戦特有の業務処理を集約する。
//   SceneRunner::Step() の Scene別ディスパッチから呼ばれる。
//
// 【各画面の業務】
//   CharaSelect: CB書込み → ReadBufferAndWrite → ゲームメモリ反映
//   Loading:     CB書込みなし（needKeepalive=true で通信維持）
//   InGame:      Intro同期(0→1遷移) → CB書込み → ReadBufferAndWrite
//   Rematch:     DirectInputHook 直接読取り → メニュー選択検出 →
//                retryMenuIndex 同期 + 自動メニューナビゲーション
//
// 【委譲先】
//   - パケット送受信: NetplaySession
//   - フレーム同期:   FrameInputBuffer + Metronome
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
