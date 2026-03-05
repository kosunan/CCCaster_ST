// ============================================================================
// SceneRunner — ゲームスレッド統合型フレームディスパッチャ
//
// 【アーキテクチャ】
//   Init()  : InitThread から呼ばれ、状態変数を初期化して即リターン。
//   Step()  : ゲームスレッド (DxHook::Hooked_EndScene) から毎フレーム呼ばれ、
//             1フレーム分の処理を実行する。
//
// 【Step() 処理フロー】
//   (A) Phase検出
//   (B) 画面遷移検出 → OnPhaseChanged
//   (C) FastBoot処理
//   (D) SceneBusiness ディスパッチ（Phase別）
//   (E) SleepFrame + MaintainState
//   (F) 同期状態チェック + 疎通チェック
//   (G) 中断チェック (F12)
// ============================================================================

#include <windows.h>
#include "core_dll/session_orchestrator/session/SceneRunner.hpp"
#include "core_dll/session_orchestrator/session/SessionContext.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/session_orchestrator/session/GameControl.hpp"
#include "core_dll/session_orchestrator/scene/SceneBusiness.hpp"
#include "core_dll/session_orchestrator/scene/SceneFastBoot.hpp"
#include "core_dll/adapter_netplay/SyncCoordinator.hpp"
#include "core_dll/game_memory_accessor/monitor/GamePhaseDetector.hpp"
#include "core_dll/game_memory_accessor/MbaaConstants.hpp"
#include "core_dll/adapter_os_hooks/api_hook/TimeHooks.hpp"
#include "shared_contracts/IpcData.hpp"
#include <atomic>

namespace cccaster::domain::session {

using GamePhase = cccaster::game_interface::GamePhase;
using GC = GameControl;

static std::atomic<uint64_t> s_lastPacketReceiveTimeMs{0};

/// @brief 送信関数（現在未使用 — パケット送信はSyncCoordinatorに完全委譲）。
static SceneRunner::SendFunc s_send = nullptr;

/// @brief 現在の時間をミリ秒で取得するヘルパー
static uint64_t GetCurrentTimeMs() {
    static LARGE_INTEGER s_freq = {0};
    if (s_freq.QuadPart == 0) QueryPerformanceFrequency(&s_freq);
    LARGE_INTEGER nowQpc;
    cccaster::core::hooks::TimeHooks::RealQueryPerformanceCounter(&nowQpc);
    return (nowQpc.QuadPart * 1000) / s_freq.QuadPart;
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
        scene::SceneBusiness::ResetLoading();
        DebugLog("[SceneRunner] Loading entered.");
    }
    if (to == GamePhase::CharaSelect) {
        GC::SetModeNormalSpeed();
        scene::SceneBusiness::ResetCharaSelect();
        DebugLog("[SceneRunner] CharaSelect entered.");
    }
    if (to == GamePhase::InGame) {
        scene::SceneBusiness::ResetInGame();
    }
    if (to == GamePhase::Rematch) {
        scene::SceneBusiness::ResetRematch();
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
    s_lastPacketReceiveTimeMs.store(GetCurrentTimeMs(), std::memory_order_relaxed);

    // FastBoot 初期化
    auto targetMode = static_cast<cccaster::public_api::IpcGameMode>(ctx.appMode);
    scene::SceneFastBoot::Start(targetMode);
    DebugLog("[SceneRunner] SceneFastBoot initialized.");

    s_ready = true;
    DebugLog("[SceneRunner] Init complete. Ready for Step() calls.");
}

// ================================================================
// Step — 1フレーム分の処理（ゲームスレッドから毎フレーム呼ばれる）
// ================================================================
void SceneRunner::Step() {
    if (!s_ready || !s_running || !s_ctx) return;

    SessionContext& ctx = *s_ctx;

    // (A) Phase検出
    GamePhase phase = cccaster::game_interface::GameMonitor::GetCurrentPhase();

    // (B) 画面遷移検出
    if (phase != s_prev) {
        OnPhaseChanged(s_prev, phase, ctx);
        ctx.framesInPhase = 0;
    }

    // (C) FastBoot
    if (phase < GamePhase::CharaSelect && !scene::SceneFastBoot::IsComplete()) {
        scene::SceneFastBoot::ProcessFrame(ctx.isHost);
        GC::SleepFrame();
        GC::MaintainState();
        s_prev = phase;
        ctx.framesInPhase++;
        return;
    }

    // (D) SleepFrame + MaintainState
    GC::SleepFrame();
    GC::MaintainState();

    // (E) SceneBusiness ディスパッチ
    switch (phase) {
        case GamePhase::CharaSelect:
            scene::SceneBusiness::OnCharaSelect(ctx);
            break;
        case GamePhase::Loading:
            scene::SceneBusiness::OnLoading(ctx);
            break;
        case GamePhase::InGame:
            scene::SceneBusiness::OnInGame(ctx);
            break;
        case GamePhase::Rematch:
            scene::SceneBusiness::OnRematch(ctx);
            break;
        default:
            break;
    }

    // (F) 同期状態チェック + 疎通チェック
    if (phase >= GamePhase::CharaSelect) {
        auto& syncState = cccaster::core::netplay::SyncCoordinator::GetState();

        // 同期完了をIPCに通知（初回のみ）
        if (!s_syncReported && syncState.isSynced.load(std::memory_order_acquire)) {
            cccaster::public_api::IpcManager::UpdateOrReadState([](cccaster::public_api::SharedState& s) {
                s.syncCompleted = true;
            });
            DebugLog("[SceneRunner] Sync completed! θ=%lldus",
                     syncState.clockOffsetUs.load());
            s_syncReported = true;
        }

        // 疎通チェック
        if (s_syncReported) {
            bool peerAlive = syncState.isPeerAlive.load(std::memory_order_acquire);
            if (!peerAlive) {
                DebugLog("[SceneRunner] Peer Disconnected!");
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

    // (G) 中断チェック
    if (GetAsyncKeyState(VK_F12) & 0x8000) {
        DebugLog("[SceneRunner] Aborted by F12.");
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
