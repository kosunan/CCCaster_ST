// ============================================================================
// SceneRunner — ゲームスレッド統合型フレームディスパッチャ
//
// 【アーキテクチャ】
//   Init()  : InitThread から呼ばれ、状態変数を初期化して即リターン。
//   Step()  : ゲームスレッド (DxHook::Hooked_Present) から毎フレーム呼ばれ、
//             1フレーム分の処理を実行する。
//
// 【Step() 処理フロー】
//   (A) Phase検出 (GamePhaseDetector)
//   (B) 画面遷移検出 → OnPhaseChanged
//   (C) FastBoot処理 (phase < CharaSelect)
//   (D) メトロノーム精密待機 (Metronome)
//   (E) CB書込み (CharaSelect/InGame: DirectInputHook → FrameInputBuffer)
//   (F) Scene別ディスパッチ (MatchScene::OnXxx)
//   (G) 相手入力待機 + CB→ゲームメモリ書込み (Rematch以外)
//   (H) 統合フロー確認ログ (60Fごと)
//   (I) 同期状態チェック + Peer切断検出
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
static bool    s_inBarrier      = false; // 遷移バリア待機中フラグ

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

    // (B) 画面遷移検出 → transitionId++ バリア開始
    if (phase != s_prev) {
        OnPhaseChanged(s_prev, phase, ctx);
        ctx.framesInPhase = 0;
        // Phase 変化 → transitionId++ + バリア開始
        if (phase >= GamePhase::CharaSelect) {
            auto& ms = cccaster::core::netplay::NetplaySession::GetMutableState();
            uint32_t newId = ms.localTransitionId.load(std::memory_order_relaxed) + 1;
            ms.localTransitionId.store(newId, std::memory_order_release);
            s_inBarrier = true;
            DebugLog("[Barrier] Phase %d->%d → transitionId=%u (barrier ON)",
                     static_cast<int>(s_prev), static_cast<int>(phase), newId);
        }
    }

    // (B2) InGame 中の intro 遷移検出 → transitionId++ バリア開始
    if (phase == GamePhase::InGame) {
        uint8_t curIntro = *CC_INTRO_STATE_ADDR;
        if (curIntro != s_prevIntroState) {
            DebugLog("[IntroTrack] intro %u->%u fip=%u WT=%u RT=%u",
                     s_prevIntroState, curIntro, ctx.framesInPhase,
                     *CC_WORLD_TIMER_ADDR, *CC_REAL_TIMER_ADDR);
            auto& ms = cccaster::core::netplay::NetplaySession::GetMutableState();
            uint32_t newId = ms.localTransitionId.load(std::memory_order_relaxed) + 1;
            ms.localTransitionId.store(newId, std::memory_order_release);
            s_inBarrier = true;
            DebugLog("[Barrier] intro %u->%u → transitionId=%u (barrier ON)",
                     s_prevIntroState, curIntro, newId);
            s_prevIntroState = curIntro;
        }
    }

    // (B3) 遷移バリア待機: peer の transitionId が追いつくまで CB書込みスキップ
    if (s_inBarrier) {
        auto& ss = cccaster::core::netplay::NetplaySession::GetState();
        uint32_t local = ss.localTransitionId.load(std::memory_order_acquire);
        uint32_t peer  = ss.peerTransitionId.load(std::memory_order_acquire);
        if (peer >= local) {
            s_inBarrier = false;
            DebugLog("[Barrier] Cleared! local=%u peer=%u (GO)", local, peer);
        } else {
            // バリア中 → keepalive 送信、CB書込みなし、ゲームスレッドを返す
            cccaster::core::netplay::NetplaySession::GetMutableState()
                .needKeepalive.store(true, std::memory_order_release);
            s_prev = phase;
            ctx.framesInPhase++;
            return;
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
    //   入力が意味を持つ Phase のみ書込む:
    //     CharaSelect: キャラ選択入力
    //     InGame かつ intro=0: 実際の対戦入力
    //   intro≠0 (イントロ再生中) は書込まない → wh が進まず両者一致を保つ
    {
        auto& buf = cccaster::core::sync::FrameInputBuffer::GetInstance();

        // CB 書込み判定
        bool shouldWrite = false;
        bool rollbackable = false;
        if (phase == GamePhase::CharaSelect) {
            shouldWrite = true;
        } else if (phase == GamePhase::InGame) {
            uint8_t introNow = *CC_INTRO_STATE_ADDR;
            if (introNow == 0) {
                shouldWrite = true;
                rollbackable = true;
            }
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

    // (G) CB → ゲームメモリ書込み（Rematch は OnRematch が独自処理）
    if (phase != GamePhase::Rematch) {
        auto& buf = cccaster::core::sync::FrameInputBuffer::GetInstance();
        uint32_t p1 = 0, p2 = 0;
        if (buf.ReadFrameForGame(ctx.isHost, p1, p2)) {
            GC::WriteInput(p1, p2);
        }
    }

    // (H) 統合フロー確認ログ（60フレームごと）
    if (ctx.framesInPhase % 60 == 0 && phase >= GamePhase::CharaSelect) {
        auto& buf = cccaster::core::sync::FrameInputBuffer::GetInstance();
        auto& syncState = cccaster::core::netplay::NetplaySession::GetState();
        uint32_t wt = *CC_WORLD_TIMER_ADDR;
        uint32_t rt = *CC_REAL_TIMER_ADDR;
        uint8_t intro = *CC_INTRO_STATE_ADDR;
        uint32_t ltid = syncState.localTransitionId.load(std::memory_order_relaxed);
        uint32_t ptid = syncState.peerTransitionId.load(std::memory_order_relaxed);
        DebugLog("[SceneRunner] phase=%d fip=%u wh=%u rp=%u ef=%u crf=%u WT=%u RT=%u intro=%u tid=%u/%u synced=%d",
                 static_cast<int>(phase), ctx.framesInPhase,
                 buf.GetWriteHead(), buf.GetReadPos(), buf.GetEffectiveHead(),
                 buf.GetConfirmedRemoteFrame(),
                 wt, rt, intro, ltid, ptid,
                 syncState.isSynced.load() ? 1 : 0);
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
