// ============================================================================
// SceneLoading — ロード画面
// 設計書: docs/design/core_dll/scene_business_logic.md §6
//
// 【3層アーキテクチャにおける位置づけ】
//   Layer 3 (Scene ビジネスロジック)
//   ゲーム制御は GameControl:: ファサードを通じて行う。
//
// 【責務】
//   - Phase 1: 安定待ち（30F、入力クリア、通常速度）
//   - Phase 2: 時刻同期待ち（SyncCoordinator.isSynced）
//   - Phase 3: ディレイ付き入力交換（スキップタイミングを同期）
//
// 【ディレイ算出方式】
//   Phase 3 突入時に一度だけ算出:
//   delay = ceil((RTT/2 + 1Fマージン) / 16.667ms)
//   最小 2F, 最大 MAX_DELAY(15F)
//
// 【送信パケット形式】LOADING_INPUT (0x21)
//   byte[0] = 0x21, byte[1..2] = uint16_t 入力値 (little-endian)
//   Phase 3 で毎フレーム送信する。
// ============================================================================

#include "core_dll/session_orchestrator/scene/SceneLoading.hpp"
#include "core_dll/session_orchestrator/session/DebugLog.hpp"
#include "core_dll/session_orchestrator/session/GameControl.hpp"
#include "core_dll/adapter_netplay/SyncCoordinator.hpp"
#include "core_dll/pure_sync_engine/InputFilter.hpp"
#include "core_dll/game_memory_accessor/MbaaConstants.hpp"
#include <atomic>
#include <cstring>

namespace cccaster::domain::scene {

using GC = cccaster::domain::session::GameControl;
using cccaster::domain::session::DebugLog;
using cccaster::domain::session::SceneRunner;
using Filter = cccaster::sync::InputFilter;

// ===== Scene固有のstatic変数 =====

/// @brief Phase 2 時刻同期完了フラグ
static bool s_timeSyncDone = false;

// ===== ディレイ入力バッファ（Phase 3 用）=====

/// @brief ディレイフレーム上限
static constexpr int MAX_DELAY = 15;

/// @brief ローカル入力リングバッファ（delay フレーム分遅延させる）
static uint16_t s_localInputRing[MAX_DELAY] = {};

/// @brief 最新リモート入力（PacketRouter の LOADING_INPUT で更新）
/// スレッドセーフ: PacketRouter(受信スレッド)とSceneRunner(ゲームスレッド)が共有する
static std::atomic<uint16_t> s_remoteInput{0};

/// @brief リングバッファ書き込み位置
static int      s_ringHead = 0;

/// @brief Phase 3 に入ってからのフレーム数
static int      s_delaySyncFrames = 0;

/// @brief Phase 3 突入時に一度だけ算出されるディレイ値
static int      s_calculatedDelay = 0;

void SceneLoading::Reset() {
    s_timeSyncDone = false;
    s_remoteInput.store(0, std::memory_order_relaxed);
    s_ringHead = 0;
    s_delaySyncFrames = 0;
    s_calculatedDelay = 0;
    for (int i = 0; i < MAX_DELAY; i++) s_localInputRing[i] = 0;
}

void SceneLoading::SetRemoteLoadingInput(uint16_t input) {
    s_remoteInput.store(input, std::memory_order_relaxed);
}

// ================================================================
// ReadAndSend — Phase A: ローカル入力読取 + LOADING_INPUT 送信
//
// 【処理フロー】（SleepFrame 前に呼ばれる）
//   Phase 1: 安定待ち → ClearInput
//   Phase 2: 時刻同期待ち → ApplySyncOffset
//   Phase 3: ローカル入力読取 + LOADING_INPUT 送信
// ================================================================
void SceneLoading::ReadAndSend(session::SessionContext& ctx,
                                const SceneRunner::SendFunc& send) {
    // Phase 1: 安定待ち
    if (ctx.framesInPhase < 30) {
        GC::ClearInput();
        return;
    }

    // Phase 2: 時刻同期待ち
    if (!s_timeSyncDone) {
        auto& syncState = cccaster::core::netplay::SyncCoordinator::GetState();
        if (!syncState.isSynced.load(std::memory_order_acquire)) {
            GC::ClearInput();
            return;
        }
        DebugLog("[Loading] TimeSync done! θ=%lldus",
                 syncState.clockOffsetUs.load());
        s_timeSyncDone = true;
        DebugLog("[Loading] Phase 3: delay-sync input exchange (delay=%d)", ctx.delay);
    }

    // Phase 3: ディレイ算出（初回のみ）
    if (s_calculatedDelay == 0) {
        static constexpr int64_t FRAME_DURATION_US = 16667;
        // RTTはSyncCoordinatorのθ推定から得られるが、現時点ではctx.delayを使う
        s_calculatedDelay = ctx.delay;
        if (s_calculatedDelay < 2) s_calculatedDelay = 2;
        if (s_calculatedDelay > MAX_DELAY) s_calculatedDelay = MAX_DELAY;
        DebugLog("[Loading] Phase3 delay fixed: %d frames", s_calculatedDelay);
    }

    // ローカル入力取得 + フィルタ + バッファ格納
    uint16_t rawLocal = GC::ReadLocal(ctx.isHost);
    uint16_t filteredLocal = Filter::FilterBlockedButtons(rawLocal);
    s_localInputRing[s_ringHead % MAX_DELAY] = filteredLocal;

    // LOADING_INPUT パケット送信（入力読取直後に即送信 ★低レイテンシ）
    if (send) {
        uint8_t pkt[3];
        pkt[0] = 0x21; // LOADING_INPUT
        std::memcpy(pkt + 1, &filteredLocal, 2);
        send(std::vector<uint8_t>(pkt, pkt + 3));
    }
}

// ================================================================
// ProcessFrame — Phase B: 受信処理 + ゲームロジック更新
//
// 【処理フロー】（SleepFrame 後に呼ばれる）
//   Phase 1/2: 処理不要
//   Phase 3: リモート入力取得 + P1/P2 書込み
// ================================================================
void SceneLoading::ProcessFrame(session::SessionContext& ctx) {
    // Phase 1/2 中は処理不要
    if (ctx.framesInPhase < 30 || !s_timeSyncDone || s_calculatedDelay == 0) return;

    int delay = s_calculatedDelay;

    // リモート入力取得（atomic 読み取り）
    uint16_t remoteInput = s_remoteInput.load(std::memory_order_relaxed);

    // バッファ溜め期間
    if (s_delaySyncFrames < delay) {
        GC::ClearInput();
        s_ringHead++;
        s_delaySyncFrames++;
        return;
    }

    // ディレイ遅延ローカル入力取得
    int readPos = (s_ringHead - delay + MAX_DELAY) % MAX_DELAY;
    uint16_t delayedLocal = s_localInputRing[readPos];

    // P1/P2 振り分け + 書き込み
    uint32_t p1, p2;
    if (ctx.isHost) {
        p1 = static_cast<uint32_t>(delayedLocal);
        p2 = static_cast<uint32_t>(remoteInput);
    } else {
        p1 = static_cast<uint32_t>(remoteInput);
        p2 = static_cast<uint32_t>(delayedLocal);
    }
    GC::WriteInput(p1, p2);

    s_ringHead++;
    s_delaySyncFrames++;
}

} // namespace cccaster::domain::scene

