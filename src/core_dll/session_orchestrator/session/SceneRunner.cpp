// ============================================================================
// SceneRunner — ゲームスレッド統合型フレームディスパッチャ
// 設計書: docs/design/core_dll/scene_business_logic.md §3
//
// 【アーキテクチャ】
//   Init()  : InitThread から呼ばれ、状態変数を初期化して即リターン。
//   Step()  : ゲームスレッド (DxHook::Hooked_EndScene) から毎フレーム呼ばれ、
//             1フレーム分の処理を実行する。
//
// 【Step() 処理フロー】
//   Gate 1: ロールバック巻き戻し中 → ProcessRollbackGate()
//   Gate 2: 高速起動中            → return (即リターン)
//   通常処理:
//     (A) MaintainState
//     (B) 画面フェーズ監視
//     (C) 遷移検出 → OnPhaseChanged
//     (D) SyncCoordinator ベースの同期状態チェック
//     (E) Scene ディスパッチ
//     (F) 中断チェック (F12)
// ============================================================================

#include <windows.h>
#include "core_dll/session_orchestrator/session/SceneRunner.hpp"
#include "core_dll/session_orchestrator/session/SessionContext.hpp"
#include "core_dll/session_orchestrator/session/DebugLog.hpp"
#include "core_dll/session_orchestrator/session/GameControl.hpp"
#include "core_dll/session_orchestrator/scene/SceneCharaSelect.hpp"
#include "core_dll/session_orchestrator/scene/SceneLoading.hpp"
#include "core_dll/adapter_netplay/SyncCoordinator.hpp"
#include "core_dll/session_orchestrator/scene/SceneInGame.hpp"
#include "core_dll/session_orchestrator/scene/SceneRematch.hpp"
#include "core_dll/game_memory_accessor/monitor/GamePhaseDetector.hpp"
#include "core_dll/pure_sync_engine/RollbackEngine.hpp"
#include "core_dll/pure_sync_engine/RemoteInputQueue.hpp"
#include "core_dll/game_memory_accessor/MbaaConstants.hpp"
#include "core_dll/adapter_os_hooks/api_hook/TimeHooks.hpp"
#include "shared_contracts/IpcData.hpp"
#include "core_dll/session_orchestrator/scene/SceneFastBoot.hpp"
#include <atomic>

namespace cccaster::domain::session {

using GamePhase = cccaster::game_interface::GamePhase;
using GC = GameControl;

// ===== 重いオブジェクト: SessionContext外に static 配置 =====
static cccaster::sync::RollbackEngine    s_rollbackEngine;
static cccaster::sync::RemoteInputQueue   s_remoteInputQueue;     // E-12: SPSCキュー
static std::atomic<uint64_t>              s_lastPacketReceiveTimeMs{0}; // タイムアウト監視用

/// @brief 送信関数（GameHooks から渡された UdpSocket::Send ラムダ）。
static SceneRunner::SendFunc           s_send = nullptr;

/// @brief 現在の時間をミリ秒で取得するヘルパー
static uint64_t GetCurrentTimeMs() {
    static LARGE_INTEGER s_freq = {0};
    if (s_freq.QuadPart == 0) QueryPerformanceFrequency(&s_freq);
    LARGE_INTEGER nowQpc;
    cccaster::core::hooks::TimeHooks::RealQueryPerformanceCounter(&nowQpc);
    return (nowQpc.QuadPart * 1000) / s_freq.QuadPart;
}

} // namespace cccaster::domain::session

// ================================================================
// PacketRouter用コールバック（名前空間: cccaster::core_dll）
// ================================================================
namespace cccaster::core_dll {

// PacketRouter.cpp の GameInputEntry と同一レイアウト
struct GameInputEntryCompat {
    uint16_t direction;
    uint16_t buttons;
};

void OnRemoteInputPacket(uint32_t latestFrame,
                         const void* historyRaw, int count) {
    using namespace cccaster::domain::session;
    // パケットを正常受信した時刻を記録
    s_lastPacketReceiveTimeMs.store(GetCurrentTimeMs(), std::memory_order_relaxed);

    auto* entries = static_cast<const GameInputEntryCompat*>(historyRaw);
    for (int i = 0; i < count; ++i) {
        uint16_t buttons = entries[i].buttons;
        s_remoteInputQueue.Push(latestFrame - (count - 1 - i), buttons);
    }
}

} // namespace cccaster::core_dll

// アクセサ: SceneInGame.cpp からキューにアクセスするため (E-12)
namespace cccaster::domain::session {
    cccaster::sync::RemoteInputQueue& GetRemoteInputQueue() {
        return s_remoteInputQueue;
    }
}

// ================================================================
// 以下は cccaster::domain::session 名前空間に戻る
// ================================================================
namespace cccaster::domain::session {


// ================================================================
// Gate 1: ロールバック巻き戻しゲート
// ================================================================
static bool ProcessRollbackGate() {
    // RollbackEngine が rerun フェーズの場合、
    // 保存済み入力で 1F 巻き込み直す → true を返して通常処理をスキップ
    return false; // TODO: ロールバック実装後に有効化
}

// ================================================================
// 画面遷移ハンドラ
// ================================================================
static void OnPhaseChanged(GamePhase from, GamePhase to, SessionContext& ctx) {
    DebugLog("[SceneRunner] Phase change: %d -> %d", static_cast<int>(from), static_cast<int>(to));

    if (to == GamePhase::Loading) {
        GC::SetModeNormalSpeed();
        ctx.roundStartSynced = false;
        ctx.rollbackReady = false;
        scene::SceneLoading::Reset();
        DebugLog("[SceneRunner] Loading entered. FastForward OFF.");
    }
    if (to == GamePhase::CharaSelect) {
        GC::SetModeNormalSpeed();  // FastBoot 後は SkipMode OFF
        scene::SceneCharaSelect::Reset();
        DebugLog("[SceneRunner] CharaSelect entered. FastForward OFF.");
    }
    if (to == GamePhase::InGame) {
        scene::SceneInGame::Reset();
    }
}

// ================================================================
// Step() 用の状態変数（static — Init で初期化、Step で毎F更新）
// ================================================================
static SessionContext* s_ctx = nullptr;
static GamePhase s_prev = GamePhase::Unknown;
static bool s_running = false;
static bool s_syncReported = false;
static bool s_ready = false;

// ================================================================
// Init — 初期化（InitThread から1回だけ呼ばれる）
// ================================================================
void SceneRunner::Init(SessionContext& ctx, SendFunc send) {
    s_send = std::move(send);
    s_ctx = &ctx;

    DebugLog("[SceneRunner] Init... appMode=%u isHost=%s",
             ctx.appMode, ctx.isHost ? "true" : "false");

    // SyncCoordinator 通信スレッド起動
    cccaster::core::netplay::SyncCoordinator::GetInstance().Start(
        ctx.isHost,
        std::string(ctx.peerIp),
        ctx.peerPort,
        ctx.localPort,
        ctx.delay,
        ctx.maxRollback);

    GC::SetModeHighSpeedSkip();  // 起動直後は高速化ON

    s_prev = GamePhase::Unknown;
    s_running = true;
    s_syncReported = false;
    // 初回Init時に受信時刻も初期化しておく
    s_lastPacketReceiveTimeMs.store(GetCurrentTimeMs(), std::memory_order_relaxed);

    // FastBoot 初期化（ゲームスレッドでメニュー自動遷移）
    auto targetMode = static_cast<cccaster::public_api::IpcGameMode>(ctx.appMode);
    scene::SceneFastBoot::Start(targetMode);
    DebugLog("[SceneRunner] SceneFastBoot initialized.");

    s_ready = true;  // EndScene からの Step() 呼び出しを許可

    DebugLog("[SceneRunner] Init complete. Ready for Step() calls from game thread.");
}

// ================================================================
// Step — 1フレーム分の処理（ゲームスレッドから毎フレーム呼ばれる）
// ================================================================
void SceneRunner::Step() {
    if (!s_ready || !s_running || !s_ctx) return;

    SessionContext& ctx = *s_ctx;

    // ============================================================
    // ★★★ 最適フレーム処理順序 ★★★
    //
    //   1. ローカル入力読取 + パケット送信  (Scene::ReadAndSend)
    //   2. フレーム待機                     (SleepFrame)
    //   3. 受信パケット処理 + ゲームロジック  (Scene::ProcessFrame)
    //   4. 状態監視・補正
    //   5. 中断チェック
    //
    // 【設計原則】
    //   - 入力→送信の間にゼロ遅延（最低レイテンシ）
    //   - Sleepの~16msが受信バッファとして機能
    //   - ゲームロジック実行時点で相手入力の到着確率が最大
    // ============================================================

    // (A) Phase検出 — 軽量なメモリ読取り（タイミングに影響なし）
    GamePhase phase = cccaster::game_interface::GameMonitor::GetCurrentPhase();

    // (B) 画面遷移検出
    if (phase != s_prev) {
        OnPhaseChanged(s_prev, phase, ctx);
        ctx.framesInPhase = 0;
    }

    // ============================================================
    // FastBoot: phase < CharaSelect のとき高速メニュー遷移
    // ============================================================
    if (phase < GamePhase::CharaSelect && !scene::SceneFastBoot::IsComplete()) {
        // SyncCoordinator がPINGで疎通維持するため、旧Sync駆動は不要
        scene::SceneFastBoot::ProcessFrame(ctx.isHost);
        GC::SleepFrame();
        GC::MaintainState();
        s_prev = phase;
        ctx.framesInPhase++;
        return;
    }

    // ============================================================
    // Phase A: ローカル入力読取 + パケット送信（SleepFrame 前）
    // ============================================================
    // 同期駆動はSyncCoordinatorの通信スレッドが担当

    // Scene ディスパッチ Phase A: 入力読取 → 即送信
    switch (phase) {
        case GamePhase::CharaSelect:
            scene::SceneCharaSelect::ReadAndSend(ctx, s_send);
            break;
        case GamePhase::Loading:
            scene::SceneLoading::ReadAndSend(ctx, s_send);
            break;
        case GamePhase::InGame:
            scene::SceneInGame::ReadAndSend(ctx, s_rollbackEngine, s_send);
            break;
        case GamePhase::Rematch:
            scene::SceneRematch::ReadAndSend(ctx, s_send);
            break;
        default:
            break;
    }

    // ============================================================
    // フレーム待機 — ~16ms（この間に相手のパケットが届く）
    // ============================================================
    GC::SleepFrame();

    // ============================================================
    // Phase B: 受信パケット処理 + ゲームロジック更新（SleepFrame 後）
    // ============================================================
    GC::MaintainState();

    // Scene ディスパッチ Phase B: 受信ドレイン → ゲームロジック
    switch (phase) {
        case GamePhase::CharaSelect:
            scene::SceneCharaSelect::ProcessFrame(ctx);
            break;
        case GamePhase::Loading:
            scene::SceneLoading::ProcessFrame(ctx);
            break;
        case GamePhase::InGame:
            scene::SceneInGame::ProcessFrame(ctx, s_rollbackEngine);
            break;
        case GamePhase::Rematch:
            scene::SceneRematch::ProcessFrame(ctx);
            break;
        default:
            break;
    }

    // ============================================================
    // 状態監視・補正
    // ============================================================
    if (phase >= GamePhase::CharaSelect) {
        // ── SyncCoordinator ベースの同期状態チェック（Read-only）──
        auto& syncState = cccaster::core::netplay::SyncCoordinator::GetState();

        // 同期完了をIPCに通知（初回のみ）
        if (!s_syncReported && syncState.isSynced.load(std::memory_order_acquire)) {
            cccaster::public_api::IpcManager::UpdateOrReadState([](cccaster::public_api::SharedState& s) {
                s.syncCompleted = true;
            });
            DebugLog("[SceneRunner] Sync completed (SyncCoordinator)! IPC flag set. θ=%lldus",
                     syncState.clockOffsetUs.load());
            s_syncReported = true;
        }

        // 疎通チェック: SyncCoordinator.isPeerAlive
        if (s_syncReported) {
            bool peerAlive = syncState.isPeerAlive.load(std::memory_order_acquire);
            if (!peerAlive) {
                DebugLog("[SceneRunner] Peer Disconnected! isPeerAlive=false");
                cccaster::public_api::IpcManager::UpdateOrReadState([](cccaster::public_api::SharedState& s) {
                    s.lastErrorCode = static_cast<uint32_t>(cccaster::public_api::SessionErrorType::PeerDisconnected);
                });
                s_running = false;
                GC::ExitGame();
                return;
            }
        }
    }


    s_prev = phase;
    ctx.framesInPhase++;

    // ============================================================
    // 中断チェック
    // ============================================================
    if (GetAsyncKeyState(VK_F12) & 0x8000) {
        DebugLog("[SceneRunner] Aborted by F12. Requesting process exit.");
        cccaster::public_api::IpcManager::UpdateOrReadState([](cccaster::public_api::SharedState& s) {
            s.lastErrorCode = static_cast<uint32_t>(cccaster::public_api::SessionErrorType::AbortedByUser);
        });
        s_running = false;
        GC::ExitGame();
    }
}

// ================================================================
// IsReady — Init完了判定
// ================================================================
bool SceneRunner::IsReady() {
    return s_ready;
}

} // namespace cccaster::domain::session
