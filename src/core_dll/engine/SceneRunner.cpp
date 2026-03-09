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
//   (D) MatchScene ディスパッチ（Phase別）
//   (E) SleepFrame + MaintainState
//   (F) 同期状態チェック + 疎通チェック
//   (G) 中断チェック (F12)
// ============================================================================

#include <windows.h>
#include "core_dll/engine/SceneRunner.hpp"
#include "core_dll/engine/MatchContext.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/engine/FrameControl.hpp"
#include "core_dll/engine/MatchScene.hpp"
#include "core_dll/engine/SceneFastBoot.hpp"
#include "core_dll/sync/NetplaySession.hpp"
#include "core_dll/detect/GamePhaseDetector.hpp"
#include "core_dll/detect/MbaaAddresses.hpp"
#include "core_dll/timing/TimeHooks.hpp"
#include "shared_contracts/IpcData.hpp"
#include <atomic>

namespace cccaster::domain::session {

using GamePhase = cccaster::game_interface::GamePhase;
using GC = FrameControl;

static std::atomic<uint64_t> s_lastPacketReceiveTimeMs{0};

/// @brief 送信関数（現在未使用 — パケット送信はNetplaySessionに完全委譲）。
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
static void OnPhaseChanged(GamePhase from, GamePhase to, MatchContext& ctx) {
    DebugLog("[SceneRunner] Phase change: %d -> %d", static_cast<int>(from), static_cast<int>(to));

    if (to == GamePhase::Loading) {
        GC::SetModeNormalSpeed();
        ctx.roundStartSynced = false;
        ctx.rollbackReady = false;
        scene::MatchScene::ResetLoading();
        DebugLog("[SceneRunner] Loading entered.");
    }
    if (to == GamePhase::CharaSelect) {
        GC::SetModeNormalSpeed();
        scene::MatchScene::ResetCharaSelect();
        DebugLog("[SceneRunner] CharaSelect entered.");
    }
    if (to == GamePhase::InGame) {
        scene::MatchScene::ResetInGame();
    }
    if (to == GamePhase::Rematch) {
        scene::MatchScene::ResetRematch();
    }
}

// ================================================================
// Step() 用の状態変数（static — Init で初期化、Step で毎F更新）
// ================================================================
static MatchContext* s_ctx = nullptr;
static GamePhase s_prev = GamePhase::Unknown;
static bool s_running = false;
static bool s_syncReported = false;
static bool s_ready = false;
static uint8_t s_prevIntroState = 255;  // introState 変化追跡用

// ================================================================
// Init — 初期化（InitThread から1回だけ呼ばれる）
// ================================================================
void SceneRunner::Init(MatchContext& ctx, SendFunc send) {
    s_send = std::move(send);
    s_ctx = &ctx;

    DebugLog("[SceneRunner] Init... appMode=%u isHost=%s",
             ctx.appMode, ctx.isHost ? "true" : "false");

    // NetplaySession 通信スレッド起動
    cccaster::core::netplay::NetplaySession::GetInstance().Start(
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

    MatchContext& ctx = *s_ctx;

    // (A) Phase検出
    GamePhase phase = cccaster::game_interface::PhaseMonitor::GetCurrentPhase();

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

    // (D) メトロノーム駆動: ConsumeTicks → CommitFrame (引数なし)
    //     CommitFrame() 内で Phase取得・入力Poll・rollbackable判定を自前収集
    {
        auto& metronome = cccaster::core::netplay::NetplaySession::GetInstance().GetMetronome();
        uint32_t ticks = metronome.ConsumeTicks();
        auto& buf = cccaster::core::sync::FrameInputBuffer::GetInstance();
        for (uint32_t t = 0; t < ticks; t++) {
            buf.CommitFrame();
        }
    }

    // (D2) MatchScene ディスパッチ（Phase固有ロジック — 入力以外の処理）
    switch (phase) {
        case GamePhase::CharaSelect:
            scene::MatchScene::OnCharaSelect(ctx);
            break;
        case GamePhase::Loading:
            scene::MatchScene::OnLoading(ctx);
            break;
        case GamePhase::InGame:
            scene::MatchScene::OnInGame(ctx);
            break;
        case GamePhase::Rematch:
            scene::MatchScene::OnRematch(ctx);
            break;
        default:
            break;
    }

    // (E) CB → ゲームメモリ書込み + SleepFrame
    {
        auto& buf = cccaster::core::sync::FrameInputBuffer::GetInstance();
        uint32_t p1 = 0, p2 = 0;
        if (buf.ReadFrameForGame(ctx.isHost, p1, p2)) {
            GC::WriteInput(p1, p2);
        }
    }

    // InGame バリア待機中は SleepFrame をスキップ
    if (phase == GamePhase::InGame && !ctx.roundStartSynced) {
        Sleep(1);
    } else {
        GC::SleepFrame();
    }
    GC::MaintainState();

    // (F) 統合フロー確認ログ（60フレームごと）
    if (ctx.framesInPhase % 60 == 0 && phase >= GamePhase::CharaSelect) {
        auto& buf = cccaster::core::sync::FrameInputBuffer::GetInstance();
        auto& syncState = cccaster::core::netplay::NetplaySession::GetState();
        uint32_t wt = *CC_WORLD_TIMER_ADDR;
        uint32_t rt = *CC_REAL_TIMER_ADDR;
        uint8_t intro = *CC_INTRO_STATE_ADDR;
        DebugLog("[SceneRunner] phase=%d fip=%u wh=%u rp=%u ef=%u crf=%u WT=%u RT=%u intro=%u synced=%d alive=%d",
                 static_cast<int>(phase), ctx.framesInPhase,
                 buf.GetWriteHead(), buf.GetReadPos(), buf.GetEffectiveHead(),
                 buf.GetConfirmedRemoteFrame(),
                 wt, rt, intro,
                 syncState.isSynced.load() ? 1 : 0,
                 syncState.isPeerAlive.load() ? 1 : 0);
    }

    // (F2) introState 変化検出（InGame 中のみ）
    if (phase == GamePhase::InGame) {
        uint8_t curIntro = *CC_INTRO_STATE_ADDR;
        if (curIntro != s_prevIntroState) {
            uint32_t wt = *CC_WORLD_TIMER_ADDR;
            uint32_t rt = *CC_REAL_TIMER_ADDR;
            DebugLog("[IntroTrack] intro %u->%u fip=%u WT=%u RT=%u",
                     s_prevIntroState, curIntro, ctx.framesInPhase, wt, rt);
            s_prevIntroState = curIntro;
        }
    }

    // (G) 同期状態チェック + 疎通チェック
    if (phase >= GamePhase::CharaSelect) {
        auto& syncState = cccaster::core::netplay::NetplaySession::GetState();

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

    // (H) 中断チェック
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
