// ============================================================================
// MatchScene.cpp — 画面別業務ロジック（統合版 実装）
//
// 【設計】
//   各画面の業務処理を1ファイルに集約。
//   共通の FrameInputBuffer → SceneInputFilter → WriteInput フローは
//   ReadBufferAndWrite() で共用する。
//
// 【削除された処理】
//   - パケット作成/送信 (NetplaySession に完全委譲)
//   - RollbackEngine 管理 (FrameInputBuffer が自動処理)
//   - GAME_INPUT パケット構築
//   - 11F 入力履歴バッファ
// ============================================================================

#include "core_dll/engine/MatchScene.hpp"
#include "core_dll/engine/SceneInputFilter.hpp"
#include "core_dll/engine/FrameControl.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/sync/FrameInputBuffer.hpp"
#include "core_dll/sync/NetplaySession.hpp"
#include "core_dll/mbaa_mem/MbaaAddresses.hpp"
#include "core_dll/mbaa_mem/MbaaInputDefs.hpp"
#include <atomic>

namespace cccaster::domain::scene {

using GC = cccaster::domain::session::FrameControl;
using cccaster::domain::session::DebugLog;
using cccaster::game_interface::GamePhase;

// ============================================================================
// 共通: FrameInputBuffer → SceneInputFilter → WriteInput
// ============================================================================
static void ReadBufferAndWrite(GamePhase phase, bool isHost) {
    auto& buf = cccaster::core::sync::FrameInputBuffer::GetInstance();

    uint32_t p1, p2;
    if (!buf.ReadFrameForGame(isHost, p1, p2)) {
        GC::ClearInput();
        return;
    }

    // SceneInputFilter でフィルタ適用
    p1 = SceneInputFilter::Apply(phase, p1);
    p2 = SceneInputFilter::Apply(phase, p2);

    GC::WriteInput(p1, p2);
}

// ============================================================================
// CharaSelect — FrameInputBuffer 読取 → WriteInput
// ============================================================================
void MatchScene::ResetCharaSelect() {
    // 状態なし — FrameInputBuffer が全管理
}

void MatchScene::OnCharaSelect(session::MatchContext& ctx) {
    ReadBufferAndWrite(GamePhase::CharaSelect, ctx.isHost);
}

// ============================================================================
// Loading — CB書込みなし（バリアは SceneRunner が管理）
// ============================================================================
void MatchScene::ResetLoading() {
    // 状態なし
}

void MatchScene::OnLoading(session::MatchContext& ctx) {
    // Loading 中は CB 書込みなし（SceneRunner が shouldWrite=false）
    // 遷移同期は SceneRunner の transitionId バリアが管理
}

// ============================================================================
// InGame — FrameInputBuffer 読取 + 入力書込み
// ============================================================================
void MatchScene::ResetInGame() {
    // transitionId バリアで同期するため、追加のリセットは不要
}

void MatchScene::OnInGame(session::MatchContext& ctx) {
    uint8_t introNow = *CC_INTRO_STATE_ADDR;
    bool noInputFlag = *CC_P1_NO_INPUT_FLAG_ADDR != 0;
    if (introNow != 0 || noInputFlag) return;

    ReadBufferAndWrite(GamePhase::InGame, ctx.isHost);
}

// ============================================================================
// Rematch — 自動リトライ（Phase 1 実装）
// ============================================================================
// Phase 1: 双方が自動的に「もう1回」(menuIndex=0) で合意し、
//   確定入力をゲームメモリに書込む。
// Phase 2（将来）: MBAA メニューカーソルアドレス特定後に正式ナビ。
// ============================================================================

static std::atomic<int8_t> s_remoteRetryMenuIndex{-1};
static constexpr int8_t MENU_INDEX_RETRY = 0;
static constexpr int8_t MENU_INDEX_NONE  = -1;

// Rematch 状態
static bool    s_localRetryReady    = false;
static bool    s_rematchResolved    = false;
static uint32_t s_rematchConfirmFrames = 0;

void MatchScene::ResetRematch() {
    s_localRetryReady     = false;
    s_rematchResolved     = false;
    s_rematchConfirmFrames = 0;
    s_remoteRetryMenuIndex.store(MENU_INDEX_NONE, std::memory_order_relaxed);
    // SharedSyncState もリセット
    cccaster::core::netplay::NetplaySession::GetMutableState()
        .localRetryMenuIndex.store(MENU_INDEX_RETRY, std::memory_order_release);
    DebugLog("[Rematch] Reset. Auto-retry mode (menuIndex=0).");
}

void MatchScene::SetRemoteRetryMenuIndex(int8_t menuIndex) {
    int8_t prev = s_remoteRetryMenuIndex.load(std::memory_order_relaxed);
    if (prev != menuIndex) {
        s_remoteRetryMenuIndex.store(menuIndex, std::memory_order_relaxed);
        DebugLog("[Rematch] Remote selected: menuIndex=%d", menuIndex);
    }
}

void MatchScene::OnRematch(session::MatchContext& ctx) {
    // ステップ 1: ローカル側は即座に「もう1回」を宣言
    if (!s_localRetryReady) {
        s_localRetryReady = true;
        cccaster::core::netplay::NetplaySession::GetMutableState()
            .localRetryMenuIndex.store(MENU_INDEX_RETRY, std::memory_order_release);
        DebugLog("[Rematch] Local auto-retry ready (menuIndex=0).");
    }

    // ステップ 2: 双方合意チェック
    if (!s_rematchResolved) {
        int8_t remoteIndex = s_remoteRetryMenuIndex.load(std::memory_order_relaxed);
        if (remoteIndex >= 0) {
            s_rematchResolved = true;
            DebugLog("[Rematch] Resolved! local=0 remote=%d → Confirming retry.", remoteIndex);
        } else {
            return;  // 相手未到着 → 待機
        }
    }

    // ステップ 3: 確定入力送出（数フレーム A ボタンを押す）
    s_rematchConfirmFrames++;
    if (s_rematchConfirmFrames <= 30) {
        uint32_t confirmInput = static_cast<uint32_t>(CC_BUTTON_A | CC_BUTTON_CONFIRM);
        if (ctx.isHost) {
            GC::WriteInput(confirmInput, 0);
        } else {
            GC::WriteInput(0, confirmInput);
        }
    }
    // 30F経過後は入力なし → ゲーム側が遷移するのを待つ
}

} // namespace cccaster::domain::scene
