// ============================================================================
// SceneInGame — 対戦画面（最複雑）
// 設計書: docs/design/core_dll/scene_business_logic.md §7
//
// 【層アーキテクチャにおける位置づけ】
//   Layer 3 (Scene ビジネスロジック)
//   ゲーム制御は GameControl:: ファサードを通じて行う。
//
// 【機能】
//   - ラウンド開始同期（introState=2 → 一時停止 → 同期 → RE初期化 → 再開）
//   - ロールバック入力処理（毎F: リモート入力受渡 → RE更新 → 巻き戻し判定）
//   - 相対補正の適用（Phase 2 ドリフト補正）
//
// 【フレームループ概要 (Update 内)】
//   1. HandleRoundStartSync()    — introState=2 での同期パイプライン
//   2. ProcessRollbackFrame()    — RE 更新 + リモート入力処理
//   3. ApplyRelativeCorrection() — 相対補正適用
// ============================================================================
#include <windows.h>
#include "core_dll/session_orchestrator/scene/SceneInGame.hpp"
#include "core_dll/session_orchestrator/session/DebugLog.hpp"
#include "core_dll/adapter_netplay/SyncCoordinator.hpp"
#include "core_dll/session_orchestrator/session/GameControl.hpp"
#include "core_dll/pure_sync_engine/RollbackEngine.hpp"
#include "core_dll/pure_sync_engine/CentralBuffer.hpp"
#include "core_dll/game_memory_accessor/dump/DumpEntryList.hpp"
#include "core_dll/pure_sync_engine/InputFilter.hpp"
#include "core_dll/pure_sync_engine/RemoteInputQueue.hpp"
#include "core_dll/game_memory_accessor/MbaaConstants.hpp"
#include <cstring>

namespace cccaster::domain::scene {

using GC = cccaster::domain::session::GameControl;

using Filter = cccaster::sync::InputFilter;
using cccaster::domain::session::DebugLog;

// QPCベースの高精度時刻取得（VClock::QPCNowUsの代替）
static int64_t QPCNowUs() {
    static LARGE_INTEGER s_freq = {0};
    if (s_freq.QuadPart == 0) QueryPerformanceFrequency(&s_freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (now.QuadPart * 1000000LL) / s_freq.QuadPart;
}

// ===== Scene固有の static 変数 =====

/// @brief 同期開始済みフラグ（introState=2 検知後に true、同期完了後に false）
static bool s_syncInitiated = false;

/// @brief フレーム処理開始時刻(µs、QPCベース)
static int64_t s_frameProcessStartUs = 0;

/// @brief 直前フレームの処理時間(µs)—フレーム計測用
static int64_t s_lastFrameProcessTimeUs = 0;

// ===== 11F入力履歴バッファ (E-11: GAME_INPUT 冗長化) =====
static constexpr int INPUT_HISTORY_SIZE = 11;
struct InputEntry { uint16_t direction; uint16_t buttons; };
static InputEntry s_inputHistory[INPUT_HISTORY_SIZE] = {};
static uint32_t   s_inputHistoryCount = 0;  // 蓄積フレーム数（最大 INPUT_HISTORY_SIZE）

/// @brief ReadAndSend → ProcessFrame 間のデータ受け渡し用
static uint16_t s_lastLocalInput = 0;

void SceneInGame::Reset() {
    s_syncInitiated = false;
    s_frameProcessStartUs = 0;
    s_lastFrameProcessTimeUs = 0;
    std::memset(s_inputHistory, 0, sizeof(s_inputHistory));
    s_inputHistoryCount = 0;
}

// ================================================================
// HandleRoundStartSync — ラウンド開始同期パイプライン
//
// 【処理の概要】
//   introState が 2 になった時点で以下を順次実行:
//   1. PauseForSync()   — 高速化OFF + 一時停止
//   2. IsSynced() 待ち  — SyncCoordinator 同期完了を待機
//   3. 同期完了時のログ出力
//   4. WaitUntilStartTime() — 同時スタート（Phase 1）
//   5. RE 初期化 + 始動
//   6. ResumeGame()     — 一時停止解除
//
// 【入出力変数】
//   @param[in]  introState  ゲームのイントロ状態 (0=アクティブ, 2=イントロ演出完了)
//   @param[in,out] ctx      セッションコンテキスト
//   @param[in,out] re       RollbackEngine
//
// 【出力】
//   @return true:  同期処理中（呼出元は SleepFrame + return すること）
//           false: 同期完了済み（通常処理に移行可能）
// ================================================================
static bool HandleRoundStartSync(uint8_t introState,
                                  session::SessionContext& ctx,
                                  cccaster::sync::RollbackEngine& re) {
    if (ctx.roundStartSynced) return false;

    if (introState != 2) {
        // SleepFrame は SceneRunner メインループ先頭に集約 (E-8)
        return true;
    }

    // ステップ1: 一時停止開始（初回のみ）
    if (!s_syncInitiated) {
        GC::SetModePause();
        s_syncInitiated = true;
        DebugLog("[InGame] introState=2 reached. Checking SyncCoordinator...");
    }

    // ステップ2: 同期待ち（SyncCoordinatorベース）
    auto& syncState = cccaster::core::netplay::SyncCoordinator::GetState();
    if (!syncState.isSynced.load(std::memory_order_acquire)) {
        return true;
    }

    // ステップ3: 同期完了ログ
    DebugLog("[InGame] Round sync done! θ=%lldus",
             syncState.clockOffsetUs.load());

    // ステップ4: RE初期化 + モード切替ollbackEngine 初期化 + 始動
    re.Initialize(ctx.delay, ctx.maxRollback);
    auto entries = cccaster::sync::BuildGameDumpEntries();
    re.SetupDumpEntries(entries);
    ctx.rollbackReady = true;
    DebugLog("[InGame] RollbackEngine initialized+started: delay=%d maxRB=%d",
             ctx.delay, ctx.maxRollback);

    // ステップ6: 一時停止解除、状態リセット
    GC::SetModeNormalSpeed();
    ctx.roundStartSynced = true;
    s_syncInitiated = false;
    DebugLog("[InGame] Round ready! Game is live.");

    // SleepFrame は SceneRunner メインループ先頭に集約 (E-8)
    return true;
}

// ================================================================
// ProcessRollbackFrame — 毎フレームのロールバック入力処理
//
// 【処理の概要】
//   1. フレーム処理時間計測開始
//   2. リモート入力を atomic から読取り（メインスレッドのみ）
//   3. ローカル入力取得 + ボタンフィルタ
//   4. GAME_INPUT パケット送信（RE.UpdateFrame 前 = 低レイテンシ）
//   5. 時間補正を一時停止 → RE.UpdateFrame → 解除
//   6. ロールバック発生時: 高速化ON → SceneRunner Gate 1 に委譲
//   7. フレーム処理時間に基づくフレーム計測動的調整
//
// 【入出力変数】
//   @param[in,out] ctx         セッションコンテキスト
//   @param[in,out] re          RollbackEngine
//   @param[in]     send        送信関数
// ================================================================
static void ProcessRollbackFrame(session::SessionContext& ctx,
                                  cccaster::sync::RollbackEngine& re,
                                  const session::SceneRunner::SendFunc& send) {
    // 計測開始
    s_frameProcessStartUs = QPCNowUs();

    // リモート入力キュー drain — 全蓄積エントリを RE に渡す (E-12)
    {
        cccaster::sync::RemoteInputEntry entry;
        while (session::GetRemoteInputQueue().Pop(entry)) {
            uint16_t filtered = Filter::FilterBlockedButtons(entry.input);
            re.OnRemoteInputReceived(entry.frameId, filtered);
        }
    }

    // ローカル入力取得 + フィルタ
    uint16_t localInput = GC::ReadLocal(ctx.isHost);
    localInput = Filter::FilterBlockedButtons(localInput);

    // ★ 11F入力履歴バッファにシフト挿入 (E-11)
    //   DummyPeer の PushInputHistory と同一パターン
    //   direction: 上位16bit, buttons: 下位16bit
    for (int i = INPUT_HISTORY_SIZE - 1; i > 0; i--) {
        s_inputHistory[i] = s_inputHistory[i-1];
    }
    s_inputHistory[0].direction = static_cast<uint16_t>((localInput >> 16) & 0xFFFF);
    s_inputHistory[0].buttons   = static_cast<uint16_t>(localInput & 0xFFFF);
    if (s_inputHistoryCount < INPUT_HISTORY_SIZE) s_inputHistoryCount++;

    // ★ GAME_INPUT パケット送信 (E-11: 61B冗長化版)
    //   RE.UpdateFrame 前に送信することでレイテンシ~1ms削減
    //   ヘッダ(20B) + GameInputPayload(61B) = 81B
    if (send) {
        uint32_t currentFrame = re.GetCurrentFrame();

        // ヘッダ(20B)構築
        std::vector<uint8_t> pkt(20 + 61, 0);  // 81B
        uint32_t magic = 0x30314343u;  // 'CC10'
        std::memcpy(pkt.data(), &magic, 4);
        pkt[4] = 0x04;  // phase = IN_GAME
        pkt[5] = 0x40;  // type  = GAME_INPUT
        // sequence, timestamp, peerState は簡易版では省略(0)

        // GameInputPayload(61B)構築
        uint8_t* pl = pkt.data() + 20;  // ペイロード先頭
        std::memcpy(pl, &currentFrame, 4);           // latestFrame(4B)
        pl[4] = static_cast<uint8_t>(cccaster::core::sync::CentralBuffer::GetInstance().GetDelay());
        uint32_t roundTimer = currentFrame;            // roundTimer(4B)
        std::memcpy(pl + 5, &roundTimer, 4);
        uint64_t wasapiClock = static_cast<uint64_t>(QPCNowUs());
        std::memcpy(pl + 9, &wasapiClock, 8);         // wasapiClock(8B)
        // history[11] (44B): direction(2B) + buttons(2B) * 11
        for (int i = 0; i < INPUT_HISTORY_SIZE; i++) {
            std::memcpy(pl + 17 + i * 4, &s_inputHistory[i].direction, 2);
            std::memcpy(pl + 17 + i * 4 + 2, &s_inputHistory[i].buttons, 2);
        }
        send(pkt);
    }

    // 時間補正一時停止 → RE更新 → 解除
    // SyncCoordinatorがティック管理するため、時間補正の一時停止は不要
    bool shouldRender = re.UpdateFrame(localInput);

    // ロールバック発生 → 高速化ON（SceneRunner Gate 1 に委譲）
    if (re.IsRollingBack()) {
        GC::SetModeHighSpeedSkip();
        // SleepFrame は SceneRunner メインループ先頭に集約 (E-8)
        return;
    }

    // フレーム処理時間計測終了
    s_lastFrameProcessTimeUs = QPCNowUs() - s_frameProcessStartUs;
    s_frameProcessStartUs = 0;

    // フレーム計測動的調整はSyncCoordinatorの通信スレッドが担当

    // ログ出力
    if (!shouldRender && ctx.framesInPhase % 30 == 0) {
        DebugLog("[InGame] Stall: waiting for remote input (frame=%u)",
                 re.GetCurrentFrame());
    }
    if (ctx.framesInPhase % 120 == 0) {
        DebugLog("[InGame] RB frame=%u confirmed=%u depth=%d procTimeUs=%lld",
                 re.GetCurrentFrame(), re.GetLastConfirmedFrame(),
                 re.GetLastRollbackDepth(), s_lastFrameProcessTimeUs);
    }
}

// SyncCoordinator が通信スレッド側でCalcTickDuration()を通じて補正するため、
// DLLスレッド側での相対補正は不要。
static void ApplyRelativeCorrection() {
    // 通信スレッドのCalcTickDuration()が担当
}

// ================================================================
// ReadAndSend — Phase A: ローカル入力読取 + GAME_INPUT 送信
//
// 【処理フロー】（SleepFrame 前に呼ばれる）
//   1. introState 読取 + ラウンド開始同期判定
//   2. ローカル入力取得 + ボタンフィルタ
//   3. 11F入力履歴バッファにシフト挿入
//   4. GAME_INPUT パケット送信（RE.UpdateFrame 前 = 低レイテンシ）
// ================================================================
void SceneInGame::ReadAndSend(session::SessionContext& ctx,
                              cccaster::sync::RollbackEngine& re,
                              const session::SceneRunner::SendFunc& send) {

    uint8_t introState = *CC_INTRO_STATE_ADDR;

    // ラウンド開始同期中は入力送信不要
    if (!ctx.roundStartSynced) {
        // HandleRoundStartSync 相当の処理
        if (introState != 2) return;

        if (!s_syncInitiated) {
            GC::SetModePause();
            s_syncInitiated = true;
            DebugLog("[InGame] introState=2 reached. Checking SyncCoordinator...");
        }

        auto& syncState = cccaster::core::netplay::SyncCoordinator::GetState();
        if (!syncState.isSynced.load(std::memory_order_acquire)) return;

        // 同期完了
        DebugLog("[InGame] Round sync done! θ=%lldus",
                 syncState.clockOffsetUs.load());

        re.Initialize(ctx.delay, ctx.maxRollback);
        auto entries = cccaster::sync::BuildGameDumpEntries();
        re.SetupDumpEntries(entries);
        ctx.rollbackReady = true;
        DebugLog("[InGame] RollbackEngine initialized+started: delay=%d maxRB=%d",
                 ctx.delay, ctx.maxRollback);

        GC::SetModeNormalSpeed();
        ctx.roundStartSynced = true;
        s_syncInitiated = false;
        DebugLog("[InGame] Round ready! Game is live.");
        return;
    }

    // ロールバック可能判定
    bool noInputFlag = *CC_P1_NO_INPUT_FLAG_ADDR != 0;
    bool canRollback = ctx.rollbackReady
                    && (introState == 0)
                    && !noInputFlag;
    if (!canRollback) return;

    // 計測開始
    s_frameProcessStartUs = QPCNowUs();

    // ローカル入力取得 + フィルタ
    uint16_t localInput = GC::ReadLocal(ctx.isHost);
    localInput = Filter::FilterBlockedButtons(localInput);
    s_lastLocalInput = localInput;

    // 11F入力履歴バッファにシフト挿入 (E-11)
    for (int i = INPUT_HISTORY_SIZE - 1; i > 0; i--) {
        s_inputHistory[i] = s_inputHistory[i-1];
    }
    s_inputHistory[0].direction = static_cast<uint16_t>((localInput >> 16) & 0xFFFF);
    s_inputHistory[0].buttons   = static_cast<uint16_t>(localInput & 0xFFFF);
    if (s_inputHistoryCount < INPUT_HISTORY_SIZE) s_inputHistoryCount++;

    // ★ GAME_INPUT パケット送信 (入力読取直後 = 最低レイテンシ)
    if (send) {
        uint32_t currentFrame = re.GetCurrentFrame();

        std::vector<uint8_t> pkt(20 + 61, 0);  // 81B
        uint32_t magic = 0x30314343u;  // 'CC10'
        std::memcpy(pkt.data(), &magic, 4);
        pkt[4] = 0x04;  // phase = IN_GAME
        pkt[5] = 0x40;  // type  = GAME_INPUT

        uint8_t* pl = pkt.data() + 20;
        std::memcpy(pl, &currentFrame, 4);
        pl[4] = 0;
        uint32_t roundTimer = currentFrame;
        std::memcpy(pl + 5, &roundTimer, 4);
        uint64_t wasapiClock = static_cast<uint64_t>(QPCNowUs());
        std::memcpy(pl + 9, &wasapiClock, 8);
        for (int i = 0; i < INPUT_HISTORY_SIZE; i++) {
            std::memcpy(pl + 17 + i * 4, &s_inputHistory[i].direction, 2);
            std::memcpy(pl + 17 + i * 4 + 2, &s_inputHistory[i].buttons, 2);
        }
        send(pkt);
    }
}

// ================================================================
// ProcessFrame — Phase B: 受信処理 + ゲームロジック更新
//
// 【処理フロー】（SleepFrame 後に呼ばれる）
//   1. リモート入力キュー ドレイン（Sleep中に届いたパケットを処理）
//   2. 時間補正一時停止 → RE.UpdateFrame → 解除
//   3. ロールバック発生時: 高速化ON
//   4. 相対補正の適用
// ================================================================
void SceneInGame::ProcessFrame(session::SessionContext& ctx,
                               cccaster::sync::RollbackEngine& re) {
    // ラウンド開始同期中/ロールバック不可時は処理不要
    if (!ctx.roundStartSynced || !ctx.rollbackReady) return;

    uint8_t introState = *CC_INTRO_STATE_ADDR;
    bool noInputFlag = *CC_P1_NO_INPUT_FLAG_ADDR != 0;
    if (introState != 0 || noInputFlag) return;

    // リモート入力キュー drain — Sleep 中に溜まったエントリを RE に渡す (E-12)
    {
        cccaster::sync::RemoteInputEntry entry;
        while (session::GetRemoteInputQueue().Pop(entry)) {
            uint16_t filtered = Filter::FilterBlockedButtons(entry.input);
            re.OnRemoteInputReceived(entry.frameId, filtered);
        }
    }

    // SyncCoordinatorがティック管理するため、時間補正一時停止は不要
    bool shouldRender = re.UpdateFrame(s_lastLocalInput);

    // ロールバック発生 → 高速化ON
    if (re.IsRollingBack()) {
        GC::SetModeHighSpeedSkip();
        return;
    }

    // フレーム処理時間計測終了
    s_lastFrameProcessTimeUs = QPCNowUs() - s_frameProcessStartUs;
    s_frameProcessStartUs = 0;

    // フレーム計測動的調整と相対補正はSyncCoordinatorの通信スレッドが担当

    // ログ出力
    if (!shouldRender && ctx.framesInPhase % 30 == 0) {
        DebugLog("[InGame] Stall: waiting for remote input (frame=%u)",
                 re.GetCurrentFrame());
    }
    if (ctx.framesInPhase % 120 == 0) {
        DebugLog("[InGame] RB frame=%u confirmed=%u depth=%d procTimeUs=%lld",
                 re.GetCurrentFrame(), re.GetLastConfirmedFrame(),
                 re.GetLastRollbackDepth(), s_lastFrameProcessTimeUs);
    }
}

} // namespace cccaster::domain::scene

