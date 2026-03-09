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
//   (E) SleepFrame
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
#include "core_dll/sync/FrameInputBuffer.hpp"
#include "core_dll/mbaa_mem/GamePhaseDetector.hpp"
#include "core_dll/mbaa_mem/MbaaAddresses.hpp"
#include "core_dll/hook/DirectInputHook.hpp"
#include "core_dll/hook/TimeHooks.hpp"
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
static bool    s_introStarted  = false; // intro 0→1 遷移が発生したか（InGame CB書込みゲート）

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
    s_introStarted = false;
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
        // InGame に入ったら intro 遷移フラグをリセット
        if (phase == GamePhase::InGame) {
            s_introStarted = false;
        }
    }

    // (C) FastBoot
    if (phase < GamePhase::CharaSelect && !scene::SceneFastBoot::IsComplete()) {
        scene::SceneFastBoot::ProcessFrame(ctx.isHost);
        // FastBoot 中はメトロノーム待機なし、needKeepalive=true で通信維持
        cccaster::core::netplay::NetplaySession::GetMutableState()
            .needKeepalive.store(true, std::memory_order_release);
        s_prev = phase;
        ctx.framesInPhase++;
        return;
    }

    // (D) メトロノーム精密待機 or キャッチアップ
    //   gap = peerLatestFrame - localWriteHead
    //   gap >= 2: 相手が先行 → メトロノーム待ち不要（即座に処理）
    //   gap <  2: 通常 → メトロノームの次ティックまで Sleep+CPUスピン
    {
        auto& metronome = cccaster::core::netplay::NetplaySession::GetInstance().GetMetronome();
        auto& syncState = cccaster::core::netplay::NetplaySession::GetState();
        auto& buf = cccaster::core::sync::FrameInputBuffer::GetInstance();

        uint32_t peerFrame = syncState.isSynced.load(std::memory_order_acquire)
            ? cccaster::core::netplay::NetplaySession::GetInstance().GetLatestPeerFrame()
            : 0;
        uint32_t localHead = buf.GetWriteHead();
        int32_t gap = static_cast<int32_t>(peerFrame) - static_cast<int32_t>(localHead);

        bool skipWait = (gap >= 2);
        GC::SetRenderSkipByGap(gap);

        if (metronome.IsRunning()) {
            metronome.WaitForNextTick(skipWait);
        }
    }

    // (E) 入力取得 → CB書込み
    //   「いつ・何を書くか」は SceneRunner が判断する。
    //   FrameInputBuffer は純粋なデータ格納のみ。
    {
        auto& buf = cccaster::core::sync::FrameInputBuffer::GetInstance();

        // InGame の intro 0→1 遷移検出（CB書込みのゲート）
        if (phase == GamePhase::InGame && !s_introStarted) {
            uint8_t introNow = *CC_INTRO_STATE_ADDR;
            if (introNow >= 1) {
                s_introStarted = true;
                DebugLog("[SceneRunner] intro 0->%u detected. InGame CB writing enabled.", introNow);
            }
        }

        // CB 書込み判定
        bool shouldWrite = false;
        bool rollbackable = false;
        if (phase == GamePhase::CharaSelect) {
            shouldWrite = true;
        } else if (phase == GamePhase::InGame && s_introStarted) {
            shouldWrite = true;
            rollbackable = true;
        }

        if (shouldWrite) {
            cccaster::game_interface::DirectInputHook::Poll();
            uint32_t localInput = ctx.isHost
                ? cccaster::game_interface::DirectInputHook::GetPlayer1Input()
                : cccaster::game_interface::DirectInputHook::GetPlayer2Input();
            uint8_t phaseU8 = static_cast<uint8_t>(phase);

            uint32_t frame = buf.GetWriteHead() + 1;
            buf.WriteSlot(frame, phaseU8, rollbackable, localInput, 0, false);
            buf.SetWriteHead(frame);

            // CB書込み中 → keepalive 不要
            cccaster::core::netplay::NetplaySession::GetMutableState()
                .needKeepalive.store(false, std::memory_order_release);
        } else {
            // CB書込みなし → keepalive 要求
            cccaster::core::netplay::NetplaySession::GetMutableState()
                .needKeepalive.store(true, std::memory_order_release);
        }
    }

    // (F) MatchScene ディスパッチ（Phase固有ロジック — 入力以外の処理）
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

    // (G) 相手入力待機 + CB → ゲームメモリ書込み
    //   Rematch では OnRematch が独自に入力を処理するためスキップ。
    //   readPos が confirmedRemoteFrame を超えている場合、
    //   相手の入力パケット到着を待ってからゲームメモリに書込む。
    if (phase != GamePhase::Rematch) {
        auto& buf = cccaster::core::sync::FrameInputBuffer::GetInstance();
        uint32_t readPos = buf.GetReadPos();

        if (readPos > 0) {
            uint32_t crf = buf.GetConfirmedRemoteFrame();
            if (readPos > crf) {
                // 相手入力未到着 → crf が readPos 以上になるまで待機
                static constexpr int WAIT_TIMEOUT_MS = 3000;
                auto startWait = GetCurrentTimeMs();
                while (buf.GetConfirmedRemoteFrame() < readPos) {
                    if ((GetCurrentTimeMs() - startWait) > WAIT_TIMEOUT_MS) {
                        DebugLog("[SceneRunner] Remote input wait TIMEOUT at readPos=%u crf=%u (waited %dms)",
                                 readPos, buf.GetConfirmedRemoteFrame(), WAIT_TIMEOUT_MS);
                        break;
                    }
                    Sleep(0);
                }
            }
        }

        uint32_t p1 = 0, p2 = 0;
        if (buf.ReadFrameForGame(ctx.isHost, p1, p2)) {
            GC::WriteInput(p1, p2);
        }
    }

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
