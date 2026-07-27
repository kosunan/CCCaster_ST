// ============================================================================
// MatchScene.cpp — 画面別業務ロジック（統合版 実装）
//
// 【設計】
//   各画面の業務処理を1ファイルに集約。
//   共通の FrameInputBuffer → SceneInputFilter → WriteInput フローは
//   ReadBufferAndWrite() で共用する。
//
// 【削除された処理】
//   - パケット作成/送信 (NetplaySession に完全委譲)
//   - RollbackEngine 管理 (FrameInputBuffer が自動処理)
//   - GAME_INPUT パケット構築
//   - 11F 入力履歴バッファ
// ============================================================================

#include "core_dll/engine/MatchScene.hpp"
#include "core_dll/engine/FrameControl.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/sync/NetplaySession.hpp"
#include "core_dll/mbaa_mem/MbaaAddresses.hpp"
#include "core_dll/mbaa_mem/IGameMemory.hpp"
#include "core_dll/mbaa_mem/MbaaInputDefs.hpp"
#include "core_dll/mbaa_mem/GamePhaseDetector.hpp"
#include "core_dll/hook/DirectInputHook.hpp"
#include <atomic>

namespace cccaster::domain::scene {

using GC = cccaster::domain::session::FrameControl;
using cccaster::domain::session::DebugLog;
using cccaster::game_interface::GamePhase;
using cccaster::game_interface::GameInput;
namespace Dir = cccaster::game_interface::Dir;

// [撤去] ReadBufferAndWrite はリセットにより削除。
//   CB読取→SceneInputFilter→WriteInput パイプラインは再構築フェーズで実装する。

// ============================================================================
// CharaSelect — FrameInputBuffer 読取 → WriteInput
// ============================================================================
void MatchScene::ResetCharaSelect() {
    // 状態なし — FrameInputBuffer が全管理
}

void MatchScene::OnCharaSelect(session::MatchContext& ctx) {
    // [撤去] CB読取→WriteInput は再構築フェーズで実装
}

// ============================================================================
// Loading — FrameInputBuffer 読取 → WriteInput (CharaSelectと同一処理)
// ============================================================================
void MatchScene::ResetLoading() {
    // 次のラウンド開始同期に向けてバリアを白紙に戻す。
    // 片方だけ残っていると、前の対戦の通知で次のバリアが素通りする。
    auto& ms = cccaster::core::netplay::NetplaySession::GetMutableState();
    ms.localPhaseReady.store(false, std::memory_order_relaxed);
    ms.peerPhaseReady.store(false, std::memory_order_relaxed);
}

void MatchScene::OnLoading(session::MatchContext& ctx) {
    // ここで localPhaseReady を立ててはいけない。
    //   以前は「InGame 到達時のバリア待機をゼロにする」目的で Loading 突入時に
    //   事前通知していたが、localPhaseReady は HandleRoundStartSync 側で
    //   「intro=2 に到達した」の意味で待たれている。ロード時間が左右で違うと、
    //   まだ Loading 中の相手からの通知でバリアが解除され、先行側がそのまま
    //   進んでしまう（ロード 60F/240F で 180F ずれることを harness で確認）。
    // [撤去] CB読取→WriteInput は再構築フェーズで実装
}

// ============================================================================
// InGame — ラウンド開始同期 + FrameInputBuffer読取
// ============================================================================
static bool s_syncInitiated  = false;
static bool s_reachedIntro2  = false;  ///< このラウンドで intro=2 を観測したか

static bool HandleRoundStartSync(session::MatchContext& ctx) {
    if (ctx.roundStartSynced) return false;

    auto& ms = cccaster::core::netplay::NetplaySession::GetMutableState();

    // ステップ1: intro=2 への「到達」をラッチする
    //   瞬間値で待ってはいけない。GC::SetModePause() は実際にはゲームを止めない
    //   （SetNormalSpeed と同じ）ため、相手を待っている間に自分の intro は
    //   2→1→0 と進んでしまう。瞬間値で判定すると、先に到達した側が
    //   次のラウンドまで条件を満たせなくなる。
    if (!s_reachedIntro2 && cccaster::game_interface::GameMem().IntroState() == 2) {
        s_reachedIntro2 = true;
    }
    if (!s_reachedIntro2) {
        return true;  // まだ intro=2 に到達していない → 待機
    }

    // ステップ2: 到達を peer に通知（isSynced 待ち中もパケットに乗る）
    if (!s_syncInitiated) {
        ms.localPhaseReady.store(true, std::memory_order_release);
        GC::SetModePause();
        s_syncInitiated = true;
        DebugLog("[IntroBarrier] Local reached intro=2. Waiting for peer...");
    }

    // ステップ3: NetplaySession 同期待ち
    auto& syncState = cccaster::core::netplay::NetplaySession::GetState();
    if (!syncState.isSynced.load(std::memory_order_acquire)) {
        return true;
    }

    // ステップ4: IntroBarrier — peer も intro=2 に到達するまで待機
    //   return でゲームスレッドを EndScene に戻し、
    //   通信スレッドの GAME_TICK 送受信を妨げない。
    if (!ms.peerPhaseReady.load(std::memory_order_acquire)) {
        return true;  // peer 未到達 → 次フレームで再チェック
    }

    // ステップ5: 双方到達 → ラウンド開始基準を確定
    ctx.phaseBaseWorldTimer = cccaster::game_interface::GameMem().WorldTimer();

    // [撤去] CB依存の phaseBaseFrame 算出
    ms.phaseBaseFrame.store(0, std::memory_order_release);
    DebugLog("[IntroBarrier] Both peers reached intro=2! phaseBaseFrame=0 (WT=%u BaseWT=%u) Go!",
             cccaster::game_interface::GameMem().WorldTimer(), ctx.phaseBaseWorldTimer);

    GC::SetModeNormalSpeed();
    ctx.roundStartSynced = true;
    s_syncInitiated = false;
    DebugLog("[InGame] Round ready! Game is live.");

    return true;  // 同期完了フレームは待機
}

void MatchScene::ResetInGame() {
    s_syncInitiated = false;
    s_reachedIntro2 = false;

    // 双方リセットする。localPhaseReady を残すと、前の対戦での到達通知が
    // そのまま次のバリアを解除してしまう。
    // 通知はレベル駆動（到達している間ずっと送り続ける）なので、
    // 片側が先にリセットしても相手の次のパケットで復帰する。
    auto& syncState = cccaster::core::netplay::NetplaySession::GetMutableState();
    syncState.localPhaseReady.store(false, std::memory_order_relaxed);
    syncState.peerPhaseReady.store(false, std::memory_order_relaxed);
}

void MatchScene::OnInGame(session::MatchContext& ctx) {
    // ラウンド開始同期 + IntroBarrier（intro=2 で双方ブロック）
    if (HandleRoundStartSync(ctx)) return;

    // [撤去] CB読取→WriteInput は再構築フェーズで実装
}

// ============================================================================
// Rematch — メニュー選択同期 + 自動ナビ + FrameInputBuffer読取
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

void MatchScene::ResetRematch() {
    s_localRetryMenuIndex  = MENU_INDEX_NONE;
    s_remoteRetryMenuIndex.store(MENU_INDEX_NONE, std::memory_order_relaxed);
    s_localIndexSent       = false;
    s_targetMenuState      = -1;
    s_targetMenuIndex      = MENU_INDEX_NONE;
    s_retryMenuStateCounter = cccaster::game_interface::GameMem().MenuStateCounter() + 1;
    s_remoteWaitFrames     = 0;
    AsmHacks::currentMenuIndex = 0;
    AsmHacks::menuConfirmState = 0;
    // SharedSyncState もリセット
    cccaster::core::netplay::NetplaySession::GetMutableState()
        .localRetryMenuIndex.store(-1, std::memory_order_release);
}

void MatchScene::SetRemoteRetryMenuIndex(int8_t menuIndex) {
    s_remoteRetryMenuIndex.store(menuIndex, std::memory_order_relaxed);
    DebugLog("[Rematch] Remote selected: menuIndex=%d", menuIndex);
}

/// 自動ナビゲーション — カーソル移動 + 確定操作
static GameInput HandleAutoNavigation() {
    if (s_targetMenuState == -1 || s_targetMenuIndex == MENU_INDEX_NONE) return {};

    int currentState = static_cast<int>(AsmHacks::menuConfirmState);

    if (currentState >= s_targetMenuState) {
        s_targetMenuState = -1;
        s_targetMenuIndex = MENU_INDEX_NONE;
        DebugLog("[Rematch] AutoNav complete.");
        return {};
    }

    int currentIndex = static_cast<int>(AsmHacks::currentMenuIndex);
    int targetIndex  = static_cast<int>(s_targetMenuIndex);

    if (currentIndex < targetIndex) {
        return { Dir::Down, 0 };
    } else if (currentIndex > targetIndex) {
        return { Dir::Up, 0 };
    } else {
        return { Dir::Neutral, CC_BUTTON_A | CC_BUTTON_CONFIRM };
    }
}

/// 双方の選択からメニュー決定
static bool ResolveMenuSelection(GameInput& input) {
    int8_t remoteIndex = s_remoteRetryMenuIndex.load(std::memory_order_relaxed);

    // ローカル選択検出
    if (s_localRetryMenuIndex == MENU_INDEX_NONE) {
        if (input.buttons & (CC_BUTTON_A | CC_BUTTON_CONFIRM)) {
            s_localRetryMenuIndex = static_cast<int8_t>(AsmHacks::currentMenuIndex);
            // SharedSyncState に書込み → GAME_TICK パケットで相手に送信される
            cccaster::core::netplay::NetplaySession::GetMutableState()
                .localRetryMenuIndex.store(s_localRetryMenuIndex, std::memory_order_release);
            DebugLog("[Rematch] Local selected: menuIndex=%d", s_localRetryMenuIndex);
            input.buttons &= ~(CC_BUTTON_A | CC_BUTTON_CONFIRM); // 即確定を防止
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

/// メニューゲート制御（入力は参照しない — メニュー階層カウンタのみで判定する）
static bool HandleMenuGate() {
    // メニュー選択肢制限 (シーン別フィルタとして後日実装)
    if (AsmHacks::currentMenuIndex > static_cast<uint32_t>(kMaxRetryMenuIndex)) {
        // TODO: SceneInputFilter 経由に移行
    }

    // リプレイ保存サブメニュー
    if (AsmHacks::currentMenuIndex == static_cast<uint32_t>(MENU_INDEX_REPLAY_SAVE)
        || cccaster::game_interface::GameMem().MenuStateCounter() > s_retryMenuStateCounter) {
        AsmHacks::menuConfirmState = 2;
        return true;
    }

    return false;
}

void MatchScene::OnRematch(session::MatchContext& ctx) {
    // ステップ 1: 自動ナビ中
    if (s_targetMenuState != -1 && s_targetMenuIndex != MENU_INDEX_NONE) {
        GameInput navInput = HandleAutoNavigation();
        if (!navInput.IsNeutral()) {
            if (ctx.isHost) {
                GC::WriteInput(navInput, {});
            } else {
                GC::WriteInput({}, navInput);
            }
        }
        return;
    }

    // ステップ 2: 入力取得 (DirectInputHook から)
    cccaster::game_interface::DirectInputHook::Poll();
    GameInput input = GameInput::Unpack(ctx.isHost
        ? cccaster::game_interface::DirectInputHook::GetPlayer1Input()
        : cccaster::game_interface::DirectInputHook::GetPlayer2Input());

    // ステップ 3: メニューゲート
    if (HandleMenuGate()) {
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
