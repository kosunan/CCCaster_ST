// ============================================================================
// SceneRematch — リマッチ画面（menuConfirmState ゲート方式）
// 設計書: docs/design/core_dll/scene_business_logic.md §8
// 業務仕様: prototype_workspace/docs/business_spec_retry_menu.md
//
// 【3層アーキテクチャにおける位置づけ】
//   Layer 3 (Scene ビジネスロジック)
//   ゲーム制御は GameControl:: ファサードを通じて行う。
//
// 【責務】
//   - プレイヤーの自由なメニュー操作を許可
//   - menuConfirmState でゲームの確認入力をインターセプト
//   - 双方の選択が揃ったら max(local, remote) で最終決定
//   - 最終決定後は自動ナビでカーソル移動 → 確定
//
// 【menuConfirmState ゲート】
//   0: ブロック（確認入力を無効化）
//   1: 検出通知（DLLが読み取り、ローカル選択として記録）
//   ≥2: 通過許可（ゲームに確認入力を通す）
//
// 【最終選択ロジック】
//   max(local, remote) を採用:
//   → 再戦=0, キャラセレ=1 → 片方でもキャラセレなら1
//
// 【タイムアウト保護】
//   ローカル選択後 REMOTE_TIMEOUT_FRAMES フレーム待っても
//   リモートが届かない場合は切断扱いとしてセッションを終了する。
//
// 【送信パケット形式】REMATCH_MENU (0x22)
//   byte[0] = 0x22, byte[1] = int8_t 選択インデックス, byte[2] = 0
//   選択確定時に1度だけ送信する。
// ============================================================================

#include "core_dll/session_orchestrator/scene/SceneRematch.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/session_orchestrator/session/GameControl.hpp"
#include "core_dll/game_memory_accessor/MbaaConstants.hpp"
#include "core_dll/adapter_os_hooks/input/DirectInputHook.hpp"
#include <atomic>
#include <cstring>

namespace cccaster::domain::scene {

using GC = cccaster::domain::session::GameControl;
using cccaster::domain::session::DebugLog;
using cccaster::domain::session::SceneRunner;

// ===== AsmHacks グローバル変数（hijackMenu で更新される）=====
// TODO: AsmHacks モジュールを v10 に統合後、正式な配置に変更
namespace AsmHacks {
    /// @brief メニューカーソル位置（ゲーム側 hijackMenu から更新）
    uint32_t currentMenuIndex = 0;

    /// @brief 確認入力ゲート（0=ブロック, 1=検出通知, ≥2=通過許可）
    uint32_t menuConfirmState = 0;
}

// ===== メニュー選択肢定数 =====

/// @brief 再戦（もう一度プレイ）
static constexpr int8_t MENU_INDEX_RETRY       = 0;

/// @brief キャラセレに戻る
static constexpr int8_t MENU_INDEX_CHARA_SEL   = 1;

/// @brief リプレイ保存
static constexpr int8_t MENU_INDEX_REPLAY_SAVE = 2;

/// @brief 未選択状態を示すセンチネル値
static constexpr int8_t MENU_INDEX_NONE        = -1;

/// @brief リモート選択待ちタイムアウト（フレーム数: 30秒 = 1800F）
static constexpr uint32_t REMOTE_TIMEOUT_FRAMES = 1800;

// ===== Scene固有のstatic変数 =====

/// @brief ローカルプレイヤーの選択（MENU_INDEX_NONE=未選択）
static int8_t  s_localRetryMenuIndex  = MENU_INDEX_NONE;

/// @brief リモートプレイヤーの選択（PacketRouter の REMATCH_MENU で更新）
/// スレッドセーフ: PacketRouter(受信スレッド)とSceneRunner(ゲームスレッド)が共有する
static std::atomic<int8_t> s_remoteRetryMenuIndex{MENU_INDEX_NONE};

/// @brief ローカル選択のネットワーク送信済みフラグ
static bool    s_localIndexSent       = false;

/// @brief 自動ナビゲーション状態 (-1=非アクティブ, 0〜=進行中)
static int32_t s_targetMenuState      = -1;

/// @brief 自動ナビゲーション目標メニュー位置
static int8_t  s_targetMenuIndex      = MENU_INDEX_NONE;

/// @brief サブメニュー判定用カウンタ（Reset 時にスナップショット取得）
static uint32_t s_retryMenuStateCounter = 0;

/// @brief ローカル選択後のリモート待機フレームカウンタ（タイムアウト検出）
static uint32_t s_remoteWaitFrames    = 0;

void SceneRematch::Reset() {
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

void SceneRematch::SetRemoteRetryMenuIndex(int8_t menuIndex) {
    s_remoteRetryMenuIndex.store(menuIndex, std::memory_order_relaxed);
    DebugLog("[Rematch] Remote selected: menuIndex=%d", menuIndex);
}

// ================================================================
// HandleAutoNavigation — カーソル自動移動 + 確定操作
//
// 【処理内容】
//   最終選択が確定した後、カーソルを目標位置へ自動移動し確定する。
//
// 【ステートマシン】
//   State 0:  ターゲット確定ログ
//   State 1:  上下入力発行（方向計算）
//   State 2-4: ゲーム側の currentMenuIndex 更新待機（3F）
//   State 5+: 到達チェック → 未到達なら State 1 へ戻る
//   State 39: Confirm 連打（menuConfirmState=2 で通過許可）
//
// 【出力】
//   @return 自動ナビ用の入力値（0 の場合もある）
// ================================================================
static uint16_t HandleAutoNavigation() {
    if (s_targetMenuState == -1 || s_targetMenuIndex == MENU_INDEX_NONE)
        return 0;

    // State 0: ターゲット確定
    if (s_targetMenuState == 0) {
        DebugLog("[Rematch] Nav: targetMenuIndex=%d", s_targetMenuIndex);
        s_targetMenuState = 1;
        return 0;
    }

    // State 1: 方向入力発行
    if (s_targetMenuState == 1) {
        s_targetMenuState = 2;
        int8_t current = static_cast<int8_t>(AsmHacks::currentMenuIndex);
        if (s_targetMenuIndex != current) {
            uint16_t dir = (s_targetMenuIndex < current) ? 8 : 2;
            return dir << 8;
        }
        return 0;
    }

    // State 2-4: 更新待機
    if (s_targetMenuState >= 2 && s_targetMenuState <= 4) {
        s_targetMenuState++;
        return 0;
    }

    // State 39: Confirm 連打
    if (s_targetMenuState == 39) {
        AsmHacks::menuConfirmState = 2;
        static int mashCount = 0;
        mashCount++;
        return (mashCount % 2) ? (CC_BUTTON_A | CC_BUTTON_CONFIRM) : 0;
    }

    // State 5+: 到達チェック
    int8_t current = static_cast<int8_t>(AsmHacks::currentMenuIndex);
    if (s_targetMenuIndex != current) {
        s_targetMenuState = 1;
    } else {
        DebugLog("[Rematch] Nav: reached target=%d current=%u",
                 s_targetMenuIndex, AsmHacks::currentMenuIndex);
        s_targetMenuState = 39;
    }
    return 0;
}

// ================================================================
// ResolveMenuSelection — 双方の選択から最終決定を行う
//
// 【選択ロジック】
//   max(local, remote) を採用:
//   - 再戦=0, キャラセレ=1
//   - 片方でもキャラセレ(1)を選んだら1になる
//   - MENU_INDEX_CHARA_SEL を超える場合は安全制限で CHARA_SEL にクランプ
//
// 【入力変数】
//   @param[in,out] input  現在のローカル入力（決定後は 0 にクリア）
//
// 【出力】
//   @return true: 決定完了（自動ナビ開始）
//           false: まだ双方揃っていない
// ================================================================
static bool ResolveMenuSelection(uint16_t& input) {
    int8_t remote = s_remoteRetryMenuIndex.load(std::memory_order_relaxed);

    // 双方揃った → 最終決定
    if (remote != MENU_INDEX_NONE && s_localRetryMenuIndex != MENU_INDEX_NONE) {
        int8_t finalIndex = (s_localRetryMenuIndex > remote)
                          ? s_localRetryMenuIndex : remote;
        if (finalIndex > MENU_INDEX_CHARA_SEL) finalIndex = MENU_INDEX_CHARA_SEL;

        s_targetMenuState = 0;
        s_targetMenuIndex = finalIndex;
        input = 0;

        DebugLog("[Rematch] Both selected! local=%d remote=%d → final=%d",
                 s_localRetryMenuIndex, remote, finalIndex);
        return true;
    }

    // ローカル選択済み → 入力クリア（相手待ち）
    if (s_localRetryMenuIndex != MENU_INDEX_NONE) {
        input = 0;
        return false;
    }

    // Confirm 検出 → ローカル選択記録
    if (AsmHacks::menuConfirmState == 1) {
        s_localRetryMenuIndex = static_cast<int8_t>(AsmHacks::currentMenuIndex);
        input = 0;
        DebugLog("[Rematch] Local selected: menuIndex=%d", s_localRetryMenuIndex);
    }

    return false;
}

// ================================================================
// HandleMenuGate — menuConfirmState ゲート制御
//
// 【処理内容】
//   入力フィルタ + リプレイ保存サブメニュー判定 +
//   ネットワーク同期ゲート処理を一括で行う。
//
// 【入力変数】
//   @param[in,out] ctx   セッションコンテキスト
//   @param[in,out] input 現在のローカル入力（フィルタ適用後に上書き）
//
// 【出力】
//   @return true: サブメニュー処理中（呼出元は SleepFrame + return）
//           false: 通常ゲート処理を続行
// ================================================================
static bool HandleMenuGate(session::SessionContext& ctx, uint16_t& input) {
    // メニュー選択肢制限
    if (AsmHacks::currentMenuIndex > static_cast<uint32_t>(MAX_RETRY_MENU_INDEX)) {
        input &= ~(CC_BUTTON_A | CC_BUTTON_CONFIRM);
    }

    // リプレイ保存サブメニュー
    if (AsmHacks::currentMenuIndex == MENU_INDEX_REPLAY_SAVE
        || *CC_MENU_STATE_COUNTER_ADDR > s_retryMenuStateCounter) {
        AsmHacks::menuConfirmState = 2;
        return true;
    }

    return false;
}

// ================================================================
// ReadAndSend — Phase A: ローカル入力読取 + REMATCH_MENU 送信
//
// 【処理フロー】（SleepFrame 前に呼ばれる）
//   1. 自動ナビ中 → 入力書込み → return
//   2. ローカル入力取得
//   3. HandleMenuGate() — サブメニュー判定 + 入力フィルタ
//   4. ResolveMenuSelection() — 最終決定判定
//   5. menuConfirmState リセット
//   6. ネットワーク送信（選択確定時の一度のみ）
// ================================================================
void SceneRematch::ReadAndSend(session::SessionContext& ctx,
                               const SceneRunner::SendFunc& send) {
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

    // ステップ 2: 入力取得 (DirectInputHook から直接)
    cccaster::game_interface::DirectInputHook::Poll();
    uint32_t rawInput = ctx.isHost
        ? cccaster::game_interface::DirectInputHook::GetPlayer1Input()
        : cccaster::game_interface::DirectInputHook::GetPlayer2Input();
    uint16_t input = static_cast<uint16_t>(rawInput & 0xFFFF);

    // ステップ 3: メニューゲート
    if (HandleMenuGate(ctx, input)) {
        return;
    }

    // ステップ 4: 最終決定判定
    ResolveMenuSelection(input);

    // ステップ 5: ゲートリセット
    AsmHacks::menuConfirmState = 0;

    // ステップ 6: ネットワーク送信（選択確定時の一度のみ ★低レイテンシ）
    if (s_localRetryMenuIndex != MENU_INDEX_NONE && !s_localIndexSent) {
        if (send) {
            uint8_t pkt[3];
            pkt[0] = 0x22; // REMATCH_MENU
            pkt[1] = static_cast<uint8_t>(s_localRetryMenuIndex);
            pkt[2] = 0;
            send(std::vector<uint8_t>(pkt, pkt + 3));
        }
        s_localIndexSent = true;
        s_remoteWaitFrames = 0;
        DebugLog("[Rematch] Sent MenuIndex: %d", s_localRetryMenuIndex);
    }
}

// ================================================================
// ProcessFrame — Phase B: 受信処理 + ゲームロジック更新
//
// 【処理フロー】（SleepFrame 後に呼ばれる）
//   1. タイムアウト検出（ローカル選択済みかつリモート未着）
// ================================================================
void SceneRematch::ProcessFrame(session::SessionContext& ctx) {
    // タイムアウト検出（ローカル選択済みかつリモート未着）
    if (s_localRetryMenuIndex != MENU_INDEX_NONE
        && s_remoteRetryMenuIndex.load(std::memory_order_relaxed) == MENU_INDEX_NONE) {
        s_remoteWaitFrames++;
        if (s_remoteWaitFrames >= REMOTE_TIMEOUT_FRAMES) {
            DebugLog("[Rematch] TIMEOUT: Remote did not select within %u frames. Disconnecting.",
                     REMOTE_TIMEOUT_FRAMES);
            // TODO: 断線通知 API を実装する
        }
    }
}

} // namespace cccaster::domain::scene

