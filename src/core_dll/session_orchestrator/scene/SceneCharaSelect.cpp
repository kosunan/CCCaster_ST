// ============================================================================
// SceneCharaSelect — キャラクターセレクト画面
// 設計書: docs/design/core_dll/scene_business_logic.md §5
// 業務仕様: prototype_workspace/docs/business_spec_chara_select_sync.md
//
// 【3層アーキテクチャにおける位置づけ】
//   Layer 3 (Scene ビジネスロジック)
//   ゲーム制御は GameControl:: ファサードを通じて行う。
//
// 【責務】
//   - Phase 1:   120F 高速スキップ（描画OFF→ON）
//   - Phase 1.5: PCスペック交換・maxRollback 決定 → TODO: 未実装
//   - Phase 2:   SyncCoordinator 同期待ち
//   - Phase 3:   ディレイ入力バッファ + 3種フィルタ
//
// 【3種フィルタ】
//   A: 150F 確認ボタン無効化（ムーンセレクト Desync 防止）
//   B: メインメニュー前方移行防止（CC_SELECT_CHARA 時の B/Cancel 無効化）
//   C: 3F バッファガード（連続確定スキップ防止）← 実装済み
//
// 【送信パケット形式】CS_INPUT (0x20)
//   byte[0] = 0x20, byte[1..2] = uint16_t 入力値 (little-endian)
//   毎フレーム Phase 3 で送信する。
// ============================================================================

#include "core_dll/session_orchestrator/scene/SceneCharaSelect.hpp"
#include "core_dll/session_orchestrator/session/DebugLog.hpp"
#include "core_dll/session_orchestrator/session/GameControl.hpp"
#include "core_dll/adapter_netplay/SyncCoordinator.hpp"
#include "core_dll/game_memory_accessor/MbaaConstants.hpp"
#include <atomic>
#include <cstring>

namespace cccaster::domain::scene {

using GC = cccaster::domain::session::GameControl;
using cccaster::domain::session::DebugLog;
using cccaster::domain::session::SceneRunner;

// ===== Scene固有のstatic変数 =====

/// @brief Phase 1 スキップ中フラグ（true: 120F 高速スキップ進行中）
static bool     s_skipping = true;

/// @brief Phase 3 開始からのフレーム数（Filter A の 150F カウンタ）
static uint32_t s_frameCount = 0;

/// @brief RNG 同期フラグ（CharaSelect 進入時に true、Host が送信後 false）
static bool     s_shouldSyncRng = false;

/// @brief リモート側入力（PacketRouter の CS_INPUT パケット受信で更新）
/// スレッドセーフ: PacketRouter(受信スレッド)とSceneRunner(ゲームスレッド)が共有する
static std::atomic<uint16_t> s_remoteCharaInput{0};

// ===== Filter C 用変数 =====

/// @brief 直前フレームの方向入力値（上位16bit）
static uint16_t s_lastDirection = 0;

/// @brief 最後に方向変化が起きたフレーム番号（Filter C 判定用）
static uint32_t s_lastDirChangedFrame = 0;

/// @brief ReadAndSend → ProcessFrame 間のデータ受け渡し用
static uint32_t s_lastFinalInput = 0;

/// @brief Filter C の封印フレーム数
static constexpr uint32_t FILTER_C_GUARD_FRAMES = 3;

/// @brief Phase 1 の高速スキップフレーム数
static constexpr uint32_t FAST_BOOT_SKIP_FRAMES = 120;

void SceneCharaSelect::Reset() {
    s_skipping = true;
    s_frameCount = 0;
    s_shouldSyncRng = true;
    s_remoteCharaInput.store(0, std::memory_order_relaxed);
    s_lastDirection = 0;
    s_lastDirChangedFrame = 0;
}

void SceneCharaSelect::SetRemoteInput(uint16_t input) {
    s_remoteCharaInput.store(input, std::memory_order_relaxed);
}

// ================================================================
// FilterCharaSelectInput — キャラセレ専用3種フィルタ
//
// 【入力変数】
//   @param[in] rawInput  生ボタン入力（上位16bit=方向、下位16bit）
//   @param[in] isHost    ホスト側かどうか
//   @param[in] currentFrame 現在のフレームカウンタ（Filter C 用）
//
// 【出力】
//   @return フィルタ適用後のボタン入力
//
// 【Filter A】: 150F 確認ボタン無効化
//   Frame < 150 の間は + Confirm を無効化。
//   理由: ムーンセレクトでの Desync 防止
//
// 【Filter B】: メインメニュー前方移行防止
//   セレクターモードが CC_SELECT_CHARA のとき、B/Cancel を無効化。
//   理由: キャラセレからメインメニューに戻る操作を防止。
//
// 【Filter C】: 3F バッファガード（连続確定スキップ防止）
//   方向変化後3F以内は A/B/Confirm/Cancel を無効化して連続スキップを防ぐ。
// ================================================================
static uint16_t FilterCharaSelectInput(uint16_t rawButtons, uint16_t rawDir,
                                        bool isHost, uint32_t currentFrame) {
    uint16_t input = rawButtons;

    // Filter A
    if (currentFrame < 150) {
        input &= ~static_cast<uint16_t>(CC_BUTTON_A | CC_BUTTON_CONFIRM);
    }

    // Filter B
    uint32_t selectorMode = isHost
        ? *CC_P1_SELECTOR_MODE_ADDR
        : *CC_P2_SELECTOR_MODE_ADDR;
    if (selectorMode == CC_SELECT_CHARA) {
        input &= ~static_cast<uint16_t>(CC_BUTTON_B | CC_BUTTON_CANCEL);
    }

    // Filter C: 3F バッファガード（方向変化後に決定・キャンセルを封印）
    if (rawDir != s_lastDirection) {
        s_lastDirChangedFrame = currentFrame;
        s_lastDirection = rawDir;
    }
    if (currentFrame - s_lastDirChangedFrame < FILTER_C_GUARD_FRAMES) {
        input &= ~static_cast<uint16_t>(
            CC_BUTTON_A | CC_BUTTON_B | CC_BUTTON_CONFIRM | CC_BUTTON_CANCEL);
    }

    return input;
}

// ================================================================
// HandleFastBootSkip — Phase 1: 120F高速スキップ
//
// 【処理の概要】
//   120F 経過するまで高速スキップ（描画OFF）を継続し、
//   完了後に NormalSpeed（描画ON）に移行する。
//   時刻同期は通信スレッドで並行して進行中。
//
// 【出力】
//   @return true: スキップ中（呼出元は return すること）
//           false: スキップ完了
// ================================================================
static bool HandleFastBootSkip(session::SessionContext& ctx, uint32_t finalInput) {
    if (!s_skipping) return false;

    uint32_t p1 = ctx.isHost ? finalInput : 0;
    uint32_t p2 = ctx.isHost ? 0 : finalInput;
    GC::WriteInput(p1, p2);

    if (ctx.framesInPhase >= FAST_BOOT_SKIP_FRAMES) {
        DebugLog("[CharaSelect] %uF skip complete. Rendering ON.", FAST_BOOT_SKIP_FRAMES);
        GC::SetModeNormalSpeed();
        s_skipping = false;
    }
    // SleepFrame は SceneRunner メインループ先頭に集約 (E-8)
    return true;
}

// ================================================================
// HandleTimeSyncWait — Phase 2: 時刻同期待ち
//
// 【処理の概要】
//   SyncCoordinator の同期が完了するまで待機する。
//   完了後にゲームを開放する。
// ================================================================
static bool HandleTimeSyncWait(session::SessionContext& ctx, uint32_t finalInput) {
    if (ctx.charaSelectSyncDone) return false;

    uint32_t p1 = ctx.isHost ? finalInput : 0;
    uint32_t p2 = ctx.isHost ? 0 : finalInput;
    GC::WriteInput(p1, p2);

    auto& syncState = cccaster::core::netplay::SyncCoordinator::GetState();
    if (!syncState.isSynced.load(std::memory_order_acquire)) {
        return true;
    }

    DebugLog("[CharaSelect] Sync done! θ=%lldus",
             syncState.clockOffsetUs.load());
    ctx.charaSelectSyncDone = true;
    GC::SetModeNormalSpeed();
    return false;
}

// ================================================================
// ProcessDelayInput — Phase 3: ディレイ入力バッファ処理
//
// 【処理の概要】
//   フィルタ済みのローカル入力とリモート入力をゲームに書き込む。
//   RNG 同期送信（Host 側のみ）を初回に実施する。
//   send が有効な場合は CS_INPUT パケット（3バイト）を毎フレーム送信する。
//
// 【入力変数】
//   @param[in] ctx        セッションコンテキスト
//   @param[in] finalInput フィルタ済みローカル入力
//   @param[in] send       送信ラムダ（nullptr の場合は送信スキップ）
// ================================================================
static void ProcessDelayInput(session::SessionContext& ctx, uint32_t finalInput,
                               const SceneRunner::SendFunc& send) {
    // RNG 同期（Host 側のみ）
    if (s_shouldSyncRng && ctx.isHost) {
        s_shouldSyncRng = false;
        DebugLog("[CharaSelect] RNG sync sent (host)");
    }

    // リモート入力取得（atomic 読み取り）
    uint16_t remoteInputBtn = s_remoteCharaInput.load(std::memory_order_relaxed);
    uint32_t finalRemoteInput = remoteInputBtn;

    // P1/P2 振り分け + 書き込み
    uint32_t p1, p2;
    if (ctx.isHost) {
        p1 = finalInput;
        p2 = finalRemoteInput;
    } else {
        p1 = finalRemoteInput;
        p2 = finalInput;
    }
    GC::WriteInput(p1, p2);

    // CS_INPUT パケット送信（0x20 + 2バイト入力）
    if (send) {
        uint16_t localBtn = static_cast<uint16_t>(finalInput & 0xFFFF);
        uint8_t pkt[3];
        pkt[0] = 0x20; // CS_INPUT
        std::memcpy(pkt + 1, &localBtn, 2);
        send(std::vector<uint8_t>(pkt, pkt + 3));
    }

    // レイヤー1 RTTベース基礎時間延長はSyncCoordinatorの通信スレッドが担当

    s_frameCount++;
    // SleepFrame は SceneRunner メインループ先頭に集約 (E-8)
}

// ================================================================
// ReadAndSend — Phase A: ローカル入力読取 + パケット送信
//
// 【処理フロー】
//   1. ローカル入力読取 + フィルタ A/B/C
//   2. Phase 1: HandleFastBootSkip() — 120F 高速スキップ（描画OFF→ON）
//   3. Phase 2: HandleTimeSyncWait() — 時刻同期待ち
//   4. Phase 3: CS_INPUT パケット送信
// ================================================================
void SceneCharaSelect::ReadAndSend(session::SessionContext& ctx,
                                    const SceneRunner::SendFunc& send) {
    // 入力取得 + フィルタ A/B/C
    uint32_t rawInput = GC::ReadLocal(ctx.isHost);
    uint16_t buttons  = static_cast<uint16_t>(rawInput & 0xFFFF);
    uint16_t dir      = static_cast<uint16_t>((rawInput >> 16) & 0xFFFF);
    uint16_t filteredBtn = FilterCharaSelectInput(buttons, dir, ctx.isHost, s_frameCount);
    uint32_t finalInput = (rawInput & 0xFFFF0000) | filteredBtn;

    // Phase A 用に保存（ProcessFrame で使用）
    s_lastFinalInput = finalInput;

    // Phase 1: 高速スキップ中は書込みのみ（送信不要）
    if (s_skipping) {
        uint32_t p1 = ctx.isHost ? finalInput : 0;
        uint32_t p2 = ctx.isHost ? 0 : finalInput;
        GC::WriteInput(p1, p2);
        if (ctx.framesInPhase >= FAST_BOOT_SKIP_FRAMES) {
            DebugLog("[CharaSelect] %uF skip complete. Rendering ON.", FAST_BOOT_SKIP_FRAMES);
            GC::SetModeNormalSpeed();
            s_skipping = false;
        }
        return;
    }

    // Phase 2: 時刻同期待ち中は送信不要
    if (!ctx.charaSelectSyncDone) {
        uint32_t p1 = ctx.isHost ? finalInput : 0;
        uint32_t p2 = ctx.isHost ? 0 : finalInput;
        GC::WriteInput(p1, p2);

        auto& syncState = cccaster::core::netplay::SyncCoordinator::GetState();
        if (syncState.isSynced.load(std::memory_order_acquire)) {
            DebugLog("[CharaSelect] Sync done! θ=%lldus",
                     syncState.clockOffsetUs.load());
            ctx.charaSelectSyncDone = true;
            GC::SetModeNormalSpeed();
        }
        return;
    }

    // Phase 3: CS_INPUT パケット送信（入力読取直後に即送信 ★低レイテンシ）
    if (send) {
        uint16_t localBtn = static_cast<uint16_t>(finalInput & 0xFFFF);
        uint8_t pkt[3];
        pkt[0] = 0x20; // CS_INPUT
        std::memcpy(pkt + 1, &localBtn, 2);
        send(std::vector<uint8_t>(pkt, pkt + 3));
    }
}

// ================================================================
// ProcessFrame — Phase B: 受信処理 + ゲームロジック更新
//
// 【処理フロー】（SleepFrame 後に呼ばれる）
//   1. リモート入力取得（atomic 読取り）
//   2. P1/P2 振り分け + ゲームメモリ書込み
//   3. フレームカウンタ更新
// ================================================================
void SceneCharaSelect::ProcessFrame(session::SessionContext& ctx) {
    // Phase 1/2 中は ProcessFrame の処理不要
    if (s_skipping || !ctx.charaSelectSyncDone) return;

    uint32_t finalInput = s_lastFinalInput;

    // リモート入力取得（atomic 読み取り）
    uint16_t remoteInputBtn = s_remoteCharaInput.load(std::memory_order_relaxed);
    uint32_t finalRemoteInput = remoteInputBtn;

    // P1/P2 振り分け + 書き込み
    uint32_t p1, p2;
    if (ctx.isHost) {
        p1 = finalInput;
        p2 = finalRemoteInput;
    } else {
        p1 = finalRemoteInput;
        p2 = finalInput;
    }
    GC::WriteInput(p1, p2);

    // RNG 同期（Host 側のみ）
    if (s_shouldSyncRng && ctx.isHost) {
        s_shouldSyncRng = false;
        DebugLog("[CharaSelect] RNG sync sent (host)");
    }

    // レイヤー1 RTTベース基礎時間延長はSyncCoordinatorの通信スレッドが担当

    s_frameCount++;
}

} // namespace cccaster::domain::scene
