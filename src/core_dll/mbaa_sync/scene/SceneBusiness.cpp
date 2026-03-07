// ============================================================================
// SceneBusiness.cpp — 画面別業務ロジック（統合版 実装）
//
// 【設計】
//   各画面の業務処理を1ファイルに集約。
//   共通の CentralBuffer → SceneInputFilter → WriteInput フローは
//   ReadBufferAndWrite() で共用する。
//
// 【削除された処理】
//   - パケット作成/送信 (SyncCoordinator に完全委譲)
//   - RollbackEngine 管理 (CentralBuffer が自動処理)
//   - GAME_INPUT パケット構築
//   - 11F 入力履歴バッファ
// ============================================================================

#include "core_dll/mbaa_sync/scene/SceneBusiness.hpp"
#include "core_dll/mbaa_sync/scene/SceneInputFilter.hpp"
#include "core_dll/mbaa_sync/orchestrator/GameControl.hpp"
#include "core_dll/platform/common/DebugLog.hpp"
#include "core_dll/fg_netplay/buffer/CentralBuffer.hpp"
#include "core_dll/fg_netplay/sync/SyncCoordinator.hpp"
#include "core_dll/mbaa_game/constants/MbaaConstants.hpp"
#include "core_dll/mbaa_game/monitor/GamePhaseDetector.hpp"
#include "core_dll/platform/hooks/DirectInputHook.hpp"
#include <atomic>

namespace cccaster::domain::scene {

using GC = cccaster::domain::session::GameControl;
using cccaster::domain::session::DebugLog;
using cccaster::game_interface::GamePhase;

// ============================================================================
// 共通: CentralBuffer → SceneInputFilter → WriteInput
// ============================================================================
static void ReadBufferAndWrite(GamePhase phase, bool isHost) {
    auto& buf = cccaster::core::sync::CentralBuffer::GetInstance();

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
// CharaSelect — CentralBuffer 読取 → WriteInput
// ============================================================================
void SceneBusiness::ResetCharaSelect() {
    // 状態なし — CentralBuffer が全管理
}

void SceneBusiness::OnCharaSelect(session::SessionContext& ctx) {
    ReadBufferAndWrite(GamePhase::CharaSelect, ctx.isHost);
}

// ============================================================================
// Loading — CentralBuffer 読取 → WriteInput (CharaSelectと同一処理)
// ============================================================================
void SceneBusiness::ResetLoading() {
    // 状態なし
}

void SceneBusiness::OnLoading(session::SessionContext& ctx) {
    // IntroBarrier 事前通知: Loading 中に localIntroComplete=true を設定し
    // GAME_TICK に乗せて peer に通知。InGame 到達時にはバリア待機ゼロを実現。
    auto& ms = cccaster::core::netplay::SyncCoordinator::GetMutableState();
    if (!ms.localIntroComplete.load(std::memory_order_relaxed)) {
        ms.localIntroComplete.store(true, std::memory_order_release);
        DebugLog("[IntroBarrier] Pre-signaling during Loading phase.");
    }
    ReadBufferAndWrite(GamePhase::Loading, ctx.isHost);
}

// ============================================================================
// InGame — ラウンド開始同期 + CentralBuffer読取
// ============================================================================
static bool s_syncInitiated = false;

static bool HandleRoundStartSync(session::SessionContext& ctx) {
    if (ctx.roundStartSynced) return false;

    uint8_t introState = *CC_INTRO_STATE_ADDR;
    if (introState != 2) {
        return true;  // まだ intro=2 に到達していない → 待機
    }

    // ステップ1: intro=2 到達を即座に通知（isSynced 待ち中もパケットに乗る）
    auto& ms = cccaster::core::netplay::SyncCoordinator::GetMutableState();
    if (!s_syncInitiated) {
        ms.localIntroComplete.store(true, std::memory_order_release);
        GC::SetModePause();
        s_syncInitiated = true;
        DebugLog("[InGame] introState=2 reached. localIntroComplete=true. Checking SyncCoordinator...");
    }

    // ステップ2: SyncCoordinator 同期待ち
    auto& syncState = cccaster::core::netplay::SyncCoordinator::GetState();
    if (!syncState.isSynced.load(std::memory_order_acquire)) {
        return true;
    }

    // ステップ3: IntroBarrier — peer も intro=2 に到達するまで待機
    //   SetModePause 中なのでフレーム進行は停止。return でゲームスレッドを
    //   EndScene に戻し、通信スレッドの GAME_TICK 送受信を妨げない。
    if (!ms.peerIntroComplete.load(std::memory_order_acquire)) {
        return true;  // peer 未到達 → 次フレームで再チェック
    }

    // ステップ4: 双方揃い → 通常速度でフレーム進行開始
    DebugLog("[IntroBarrier] Both peers at intro=2! (WT=%u RT=%u) Go!",
             *CC_WORLD_TIMER_ADDR, *CC_REAL_TIMER_ADDR);
    GC::SetModeNormalSpeed();
    ctx.roundStartSynced = true;
    s_syncInitiated = false;
    DebugLog("[InGame] Round ready! Game is live.");

    return true;  // 同期完了フレームは待機
}

void SceneBusiness::ResetInGame() {
    s_syncInitiated = false;
    // IntroBarrier: peerIntroComplete のみリセット（peer の次の intro=2 到達を待つため）
    // localIntroComplete は true のまま維持 → GAME_TICK で常に flags=0x01 を送信
    auto& syncState = cccaster::core::netplay::SyncCoordinator::GetMutableState();
    syncState.peerIntroComplete.store(false, std::memory_order_relaxed);
}

void SceneBusiness::OnInGame(session::SessionContext& ctx) {
    // ラウンド開始同期 + IntroBarrier（intro=2 で双方ブロック）
    if (HandleRoundStartSync(ctx)) return;

    uint8_t introNow = *CC_INTRO_STATE_ADDR;
    bool noInputFlag = *CC_P1_NO_INPUT_FLAG_ADDR != 0;
    if (introNow != 0 || noInputFlag) return;

    ReadBufferAndWrite(GamePhase::InGame, ctx.isHost);
}

// ============================================================================
// Rematch — メニュー選択同期 + 自動ナビ + CentralBuffer読取
// ============================================================================
// TODO: AsmHacks モジュールを v10 に統合後、正式な配置に変更
namespace AsmHacks {
    uint32_t currentMenuIndex = 0;
    uint32_t menuConfirmState = 0;
}

static constexpr int8_t MENU_INDEX_RETRY       = 0;
static constexpr int8_t MENU_INDEX_CHARA_SEL   = 1;
static constexpr int8_t MENU_INDEX_REPLAY_SAVE = 2;
static constexpr int8_t MENU_INDEX_NONE        = -1;
static constexpr uint32_t REMOTE_TIMEOUT_FRAMES = 1800;
// MAX_RETRY_MENU_INDEX は MbaaAddresses.hpp のマクロを使用
static constexpr int8_t kMaxRetryMenuIndex = MAX_RETRY_MENU_INDEX;

// Rematch static 変数
static int8_t  s_localRetryMenuIndex  = MENU_INDEX_NONE;
static std::atomic<int8_t> s_remoteRetryMenuIndex{MENU_INDEX_NONE};
static bool    s_localIndexSent       = false;
static int8_t  s_targetMenuState      = -1;
static int8_t  s_targetMenuIndex      = MENU_INDEX_NONE;
static uint32_t s_retryMenuStateCounter = 0;
static uint32_t s_remoteWaitFrames    = 0;

void SceneBusiness::ResetRematch() {
    s_localRetryMenuIndex  = MENU_INDEX_NONE;
    s_remoteRetryMenuIndex.store(MENU_INDEX_NONE, std::memory_order_relaxed);
    s_localIndexSent       = false;
    s_targetMenuState      = -1;
    s_targetMenuIndex      = MENU_INDEX_NONE;
    s_retryMenuStateCounter = *CC_MENU_STATE_COUNTER_ADDR + 1;
    s_remoteWaitFrames     = 0;
    AsmHacks::currentMenuIndex = 0;
    AsmHacks::menuConfirmState = 0;
}

void SceneBusiness::SetRemoteRetryMenuIndex(int8_t menuIndex) {
    s_remoteRetryMenuIndex.store(menuIndex, std::memory_order_relaxed);
    DebugLog("[Rematch] Remote selected: menuIndex=%d", menuIndex);
}

/// 自動ナビゲーション — カーソル移動 + 確定操作
static uint16_t HandleAutoNavigation() {
    if (s_targetMenuState == -1 || s_targetMenuIndex == MENU_INDEX_NONE) return 0;

    int currentState = static_cast<int>(AsmHacks::menuConfirmState);

    if (currentState >= s_targetMenuState) {
        s_targetMenuState = -1;
        s_targetMenuIndex = MENU_INDEX_NONE;
        DebugLog("[Rematch] AutoNav complete.");
        return 0;
    }

    int currentIndex = static_cast<int>(AsmHacks::currentMenuIndex);
    int targetIndex  = static_cast<int>(s_targetMenuIndex);

    if (currentIndex < targetIndex) {
        return 0x0002; // 下
    } else if (currentIndex > targetIndex) {
        return 0x0001; // 上
    } else {
        return CC_BUTTON_A | CC_BUTTON_CONFIRM;
    }
}

/// 双方の選択からメニュー決定
static bool ResolveMenuSelection(uint16_t& input) {
    int8_t remoteIndex = s_remoteRetryMenuIndex.load(std::memory_order_relaxed);

    // ローカル選択検出
    if (s_localRetryMenuIndex == MENU_INDEX_NONE) {
        if (input & (CC_BUTTON_A | CC_BUTTON_CONFIRM)) {
            s_localRetryMenuIndex = static_cast<int8_t>(AsmHacks::currentMenuIndex);
            DebugLog("[Rematch] Local selected: menuIndex=%d", s_localRetryMenuIndex);
            input &= ~(CC_BUTTON_A | CC_BUTTON_CONFIRM); // 即確定を防止
        }
    }

    // 双方揃い
    if (s_localRetryMenuIndex != MENU_INDEX_NONE && remoteIndex != MENU_INDEX_NONE) {
        int8_t finalIndex = (s_localRetryMenuIndex > remoteIndex)
                           ? s_localRetryMenuIndex : remoteIndex;
        s_targetMenuIndex = finalIndex;
        s_targetMenuState = 2;
        DebugLog("[Rematch] Resolved: local=%d remote=%d → final=%d",
                 s_localRetryMenuIndex, remoteIndex, finalIndex);
        return true;
    }

    return false;
}

/// メニューゲート制御
static bool HandleMenuGate(uint16_t& input) {
    // メニュー選択肢制限 (シーン別フィルタとして後日実装)
    if (AsmHacks::currentMenuIndex > static_cast<uint32_t>(kMaxRetryMenuIndex)) {
        // TODO: SceneInputFilter 経由に移行
    }

    // リプレイ保存サブメニュー
    if (AsmHacks::currentMenuIndex == static_cast<uint32_t>(MENU_INDEX_REPLAY_SAVE)
        || *CC_MENU_STATE_COUNTER_ADDR > s_retryMenuStateCounter) {
        AsmHacks::menuConfirmState = 2;
        return true;
    }

    return false;
}

void SceneBusiness::OnRematch(session::SessionContext& ctx) {
    // ステップ 1: 自動ナビ中
    if (s_targetMenuState != -1 && s_targetMenuIndex != MENU_INDEX_NONE) {
        uint16_t navInput = HandleAutoNavigation();
        if (navInput != 0) {
            uint32_t input32 = static_cast<uint32_t>(navInput);
            if (ctx.isHost) {
                GC::WriteInput(input32, 0);
            } else {
                GC::WriteInput(0, input32);
            }
        }
        return;
    }

    // ステップ 2: 入力取得 (DirectInputHook から)
    cccaster::game_interface::DirectInputHook::Poll();
    uint32_t rawInput = ctx.isHost
        ? cccaster::game_interface::DirectInputHook::GetPlayer1Input()
        : cccaster::game_interface::DirectInputHook::GetPlayer2Input();
    uint16_t input = static_cast<uint16_t>(rawInput & 0xFFFF);

    // ステップ 3: メニューゲート
    if (HandleMenuGate(input)) {
        return;
    }

    // ステップ 4: 最終決定判定
    ResolveMenuSelection(input);

    // ステップ 5: ゲートリセット
    AsmHacks::menuConfirmState = 0;

    // タイムアウト検出
    if (s_localRetryMenuIndex != MENU_INDEX_NONE
        && s_remoteRetryMenuIndex.load(std::memory_order_relaxed) == MENU_INDEX_NONE) {
        s_remoteWaitFrames++;
        if (s_remoteWaitFrames >= REMOTE_TIMEOUT_FRAMES) {
            DebugLog("[Rematch] TIMEOUT: Remote did not select within %u frames.",
                     REMOTE_TIMEOUT_FRAMES);
        }
    }
}

} // namespace cccaster::domain::scene
