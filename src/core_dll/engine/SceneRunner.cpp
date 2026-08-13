// ============================================================================
// SceneRunner — ゲームスレッド統合型フレームディスパッチャ
//
// 【アーキテクチャ】
//   Init()  : InitThread から呼ばれ、状態変数を初期化して即リターン。
//   Step()  : ゲームスレッド (DxHook::Hooked_Present) から毎フレーム呼ばれ、
//             1フレーム分の処理を実行する。
// ============================================================================

#include "core_dll/common/Platform.hpp"
#include "core_dll/engine/SceneRunner.hpp"
#include "core_dll/engine/MatchContext.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/engine/FrameControl.hpp"
#include "core_dll/engine/MatchScene.hpp"
#include "core_dll/engine/SceneFastBoot.hpp"
#include "core_dll/sync/NetplaySession.hpp"
#include "core_dll/mbaa_mem/GamePhaseDetector.hpp"
#include "core_dll/mbaa_mem/MbaaAddresses.hpp"
#include "core_dll/mbaa_mem/IGameMemory.hpp"
#include "core_dll/sync/MatchInputBuffer.hpp"
#include "core_dll/hook/DirectInputHook.hpp"
#include "core_dll/engine/SceneInputFilter.hpp"
#include "core_dll/common/ScriptedInput.hpp"
#include "core_dll/mbaa_mem/MbaaMemTrace.hpp"
#include "shared_contracts/IpcData.hpp"
#include <atomic>

namespace cccaster::domain::session {

using GamePhase = cccaster::game_interface::GamePhase;
using GC = FrameControl;

static std::atomic<uint64_t> s_lastPacketReceiveTimeMs{0};

/// @brief 送信関数（現在未使用 — パケット送信はNetplaySessionに完全委譲）。
static SceneRunner::SendFunc s_send = nullptr;

/// @brief 現在の時間をミリ秒で取得するヘルパー
/// フック後の QPC は 1000 倍速なので、必ず Platform 経由の実時間を使う。
static uint64_t GetCurrentTimeMs() {
    return static_cast<uint64_t>(cccaster::platform::RealMonotonicUs() / 1000);
}

// ================================================================
// 画面遷移ハンドラ
// ================================================================
static void OnPhaseChanged(GamePhase from, GamePhase to, MatchContext& ctx) {
    DebugLog("[SceneRunner] Phase change: %d -> %d", static_cast<int>(from), static_cast<int>(to));

    // 画面が変わったら入力フィルタの履歴を捨てる
    scene::SceneInputFilter::Reset();

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

static bool     s_haveDelivered = false;  // 一度でも配信できたか
static uint32_t s_stallFrames   = 0;  // 配信できず直前入力を保持した累計フレーム数
static uint32_t s_starvedFrames = 0;  // 背圧で書込みを止めた累計フレーム数

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
    s_haveDelivered = false;
    s_stallFrames = 0;
    s_starvedFrames = 0;
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
        // FastBoot 中はメトロノーム待機なし、needKeepalive=true で通信維持
        cccaster::core::netplay::NetplaySession::GetMutableState()
            .needKeepalive.store(true, std::memory_order_release);
        s_prev = phase;
        ctx.framesInPhase++;
        return;
    }

    // (D) メトロノーム精密待機
    //   CB依存の gap 計算を撤去 — 常にメトロノーム待機
    {
        auto& metronome = cccaster::core::netplay::NetplaySession::GetInstance().GetMetronome();
        GC::SetRenderSkipByGap(0);
        if (metronome.IsRunning()) {
            metronome.WaitForNextTick(false);
        }
    }

    // (E) ローカル入力 → 入力バッファ書込み
    //   書き込んだ writeHead を通信スレッドが監視し、パケットとして送出する。
    {
        auto& buf = cccaster::core::sync::MatchInputBuffer::GetInstance();

        // 予測は「相手の最後の確定入力を繰り返す」。ロールバック導入までは
        // 予測が外れても巻き戻せないため、確定するまでゲームには渡さない。
        uint32_t predicted = 0;
        if (buf.HasConfirmedRemote()) {
            buf.TryGetRemoteInput(buf.GetConfirmedRemoteFrame(), predicted);
        }

        // 背圧: 相手の確定フレームから delay+maxRollback 以上は先行しない。
        //   ここで止めないと、先行した側は相手がまだ生成していないフレームを
        //   読み続けることになり、永久に配信できない。ロールバック netplay で
        //   先行側を抑えるのは「入力の枯渇」であり、その入口がこの判定。
        const uint32_t head = buf.GetWriteHead();
        const uint32_t confirmed = buf.HasConfirmedRemote()
            ? buf.GetConfirmedRemoteFrame() : head;
        const int32_t lead = static_cast<int32_t>(head) - static_cast<int32_t>(confirmed);
        const int32_t maxLead = static_cast<int32_t>(ctx.delay) + static_cast<int32_t>(ctx.maxRollback);

        if (lead <= maxLead) {
            // 自動テスト時はフレーム番号だけから決まる入力列を使う。
            // 人の操作では両者の入力を再現できず決定性を判定できないため。
            const uint32_t localInput = cccaster::testing::IsScriptedInputEnabled()
                ? cccaster::testing::ScriptedInput(head + 1, ctx.isHost)
                : (ctx.isHost
                    ? cccaster::game_interface::DirectInputHook::GetPlayer1Input()
                    : cccaster::game_interface::DirectInputHook::GetPlayer2Input());

            // フィルタは送信前に適用する。フィルタ済みの値が回線を通るので、
            // 相手のフェーズ認識とずれても両者が受け取る値は必ず一致する。
            const uint32_t filtered = scene::SceneInputFilter::Apply(phase, localInput);
            buf.WriteLocal(head + 1, filtered, predicted,
                           cccaster::game_interface::PhaseMonitor::IsRoundActive());
        } else {
            ++s_starvedFrames;   // 相手待ちで先行を止めたフレーム数
        }
    }

    // 入力を毎フレーム送るので keepalive は本来不要だが、
    // 送信が止まった場合の保険として要求は立てたままにする。
    cccaster::core::netplay::NetplaySession::GetMutableState()
        .needKeepalive.store(true, std::memory_order_release);

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

    // (F2) 確定フレーム → フィルタ → ゲームメモリ書込み
    //   readPos = writeHead - (delay + maxRollback)。相手入力が届いていない
    //   フレームは書き込まず、直前の入力を保持する（ゲームスレッドは絶対に
    //   ブロックしない — ブロックすると keepalive が途絶えて切断扱いになる）。
    {
        auto& buf = cccaster::core::sync::MatchInputBuffer::GetInstance();

        uint32_t p1 = 0, p2 = 0;
        // Rematch の自動ナビが入力を握っている間は書かない。
        // 書くと同一フレームで後勝ちになり自動ナビが効かなくなる。
        if (!scene::MatchScene::IsDrivingInput() &&
            buf.TryReadForGame(ctx.isHost, p1, p2)) {
            // ここではフィルタを掛けない。掛けるとローカルのフェーズが引数に
            // なり、ロード時間差でフェーズがずれたときに両者の結果が食い違う。
            using cccaster::game_interface::GameInput;
            const GameInput g1 = GameInput::Unpack(p1);
            const GameInput g2 = GameInput::Unpack(p2);
            GC::WriteInput(g1, g2);
            s_haveDelivered = true;

            // 自動テスト時のみ、配信したフレームを記録する。
            // 両プロセスのログを突き合わせて決定性を判定するため。
            if (cccaster::testing::IsScriptedInputEnabled()) {
                DebugLog("[REC] %u %u %u %u %u", buf.GetReadPos(),
                         g1.direction, g1.buttons, g2.direction, g2.buttons);
            }
        } else {
            // 未確定 → 何も書かない。ゲームメモリには直前フレームの値が
            // そのまま残るため「保持」と同じ挙動になる。古い入力を新しい
            // フレームの分として書き直さないので、記録も実態とずれない。
            ++s_stallFrames;
        }
    }

    // (G) 統合フロー確認ログ（60フレームごと）
    if (ctx.framesInPhase % 60 == 0 && phase >= GamePhase::CharaSelect) {
        auto& syncState = cccaster::core::netplay::NetplaySession::GetState();
        auto& mem = cccaster::game_interface::GameMem();
        uint32_t wt = mem.WorldTimer();
        uint32_t rt = mem.RealTimer();
        uint8_t intro = mem.IntroState();
        auto& buf = cccaster::core::sync::MatchInputBuffer::GetInstance();
        DebugLog("[SceneRunner] phase=%d fip=%u wh=%u rp=%u WT=%u RT=%u intro=%u "
                 "synced=%d alive=%d stall=%u starve=%u conflict=%u",
                 static_cast<int>(phase), ctx.framesInPhase,
                 buf.GetWriteHead(), buf.GetReadPos(),
                 wt, rt, intro,
                 syncState.isSynced.load() ? 1 : 0,
                 syncState.isPeerAlive.load() ? 1 : 0,
                 s_stallFrames, s_starvedFrames, buf.ConfirmConflicts());
    }

    // (F2) introState 変化検出（InGame 中のみ）
    if (phase == GamePhase::InGame) {
        uint8_t curIntro = cccaster::game_interface::GameMem().IntroState();
        if (curIntro != s_prevIntroState) {
            uint32_t wt = cccaster::game_interface::GameMem().WorldTimer();
            uint32_t rt = cccaster::game_interface::GameMem().RealTimer();
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

    // 実機メモリの毎フレーム記録（CCCASTER_MEM_TRACE=1 のときのみ）
    cccaster::game_memory::MbaaMemTrace::Sample(
        cccaster::core::sync::MatchInputBuffer::GetInstance().GetWriteHead());

    // (H) 中断チェック（Windows: F12 / Linux: SIGINT・SIGTERM）
    if (cccaster::platform::IsAbortRequested()) {
        DebugLog("[SceneRunner] Aborted by user.");
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
