// ============================================================================
// SceneFastBoot.cpp — ゲームスレッド上の高速起動の実装
//
// 【旧 FastBootRunner からの移植】
//   - 裏スレッドの 1ms ポーリング → ゲームスレッドの毎フレーム呼び出し
//   - CC_SKIP_FRAMES 直書き → MbaaSpeedController::HighSpeedSkip_Normal
//   - MemoryPatcher 直書き → GC::WriteInput() + 直接メモリ操作
// ============================================================================

#include "core_dll/mbaa_sync/scene/SceneFastBoot.hpp"
#include "core_dll/mbaa_sync/orchestrator/GameControl.hpp"
#include "core_dll/mbaa_game/constants/MbaaConstants.hpp"
#include "core_dll/common/DebugLog.hpp"

#include <cstring>

namespace cccaster::domain::scene {

using GC = session::GameControl;
using session::DebugLog;

// ================================================================
// Static 変数
// ================================================================

/// FastBoot の目標ゲームモード
static cccaster::public_api::IpcGameMode s_targetMode =
    cccaster::public_api::IpcGameMode::Versus;

/// FastBoot 完了フラグ
static bool s_complete = false;

/// ダイレクトジャンプ命令適用済み
static bool s_forceGotoApplied = false;

/// メインメニューのナビゲーション進捗
static int s_menuNavCount = 0;

/// 入力トグル（1F おきに押す / 離す）
static bool s_toggle = false;

/// フレームカウンタ（デバッグログ用）
static uint32_t s_frameCount = 0;

// ================================================================
// Start — 初期化
// ================================================================
void SceneFastBoot::Start(cccaster::public_api::IpcGameMode targetMode) {
    s_targetMode = targetMode;
    s_complete = false;
    s_forceGotoApplied = false;
    s_menuNavCount = 0;
    s_toggle = false;
    s_frameCount = 0;
    DebugLog("[FastBoot] Started (targetMode=%d)", static_cast<int>(targetMode));
}

// ================================================================
// Reset
// ================================================================
void SceneFastBoot::Reset() {
    s_complete = false;
    s_forceGotoApplied = false;
    s_menuNavCount = 0;
    s_toggle = false;
    s_frameCount = 0;
}

// ================================================================
// IsComplete
// ================================================================
bool SceneFastBoot::IsComplete() {
    return s_complete;
}

// ================================================================
// ProcessFrame — 毎フレーム呼ばれるメインロジック
//
//   phase < CharaSelect のときに SceneRunner::Step() から呼ばれる。
//   戻り値: true = キャラセレ到達（FastBoot 完了）
// ================================================================
bool SceneFastBoot::ProcessFrame(bool isHost) {
    if (s_complete) return true;

    s_frameCount++;

    // ゲームモード読取り
    uint32_t gameMode = *CC_GAME_MODE_ADDR;

    // キャラセレ到達判定
    if (gameMode == CC_GAME_MODE_CHARA_SELECT) {
        s_complete = true;
        DebugLog("[FastBoot] ★ CharaSelect reached! (frame=%u) Switching to NormalSpeed.",
                 s_frameCount);
        GC::SetModeNormalSpeed();
        return true;
    }

    // 無効なゲームモード（起動初期）
    if (gameMode == 65535 || gameMode == 0) {
        return false;
    }

    // 30F ごとに進行ログ出力
    if (s_frameCount % 30 == 0) {
        DebugLog("[FastBoot] frame=%u gameMode=%u", s_frameCount, gameMode);
    }

    // ================================================================
    // イントロスキップ: gameState == 1 or 99 → 101 に書き換え
    // ================================================================
    uint32_t gameState = *CC_GAME_STATE_ADDR;
    if (gameState == CC_GAME_STATE_CHARA_INTRO || gameState == CC_GAME_STATE_INTRO_DONE) {
        *CC_GAME_STATE_ADDR = CC_GAME_STATE_INTRO_SKIP;
    }

    // SFX バッファゼロクリア（起動中の不快な SE 連打防止）
    std::memset(CC_SFX_ARRAY_ADDR, 0, CC_SFX_ARRAY_LEN);

    // ================================================================
    // メインメニュー (gameMode == 25):
    //   方向キー下 + 決定ボタンを交互に偽造してメニューを遷移
    // ================================================================
    if (gameMode == 25) {
        // ナビ回数: Versus=1, Training=5
        int targetNav = (s_targetMode == cccaster::public_api::IpcGameMode::Training) ? 5 : 1;

        uint16_t dirBits = 0;
        uint16_t btnBits = 0;

        if (s_menuNavCount < targetNav) {
            // 方向キー下を交互に入力
            if (s_toggle) {
                dirBits = 0x0002; // 下
            }
            if (!s_toggle) {
                s_menuNavCount++;
            }
        } else {
            // 決定ボタンを交互に入力
            if (s_toggle) {
                btnBits = CC_BUTTON_CONFIRM;
            }
        }

        // P1 入力として書込み (direction << 16 | buttons)
        uint32_t input = (static_cast<uint32_t>(dirBits) << 16) | btnBits;
        if (isHost) {
            GC::WriteInput(input, 0);
        } else {
            GC::WriteInput(0, input);
        }

        if (s_toggle && s_frameCount % 10 == 0) {
            DebugLog("[FastBoot] MainMenu nav=%d/%d input=0x%08X",
                     s_menuNavCount, targetNav, input);
        }

        s_toggle = !s_toggle;
        return false;
    }

    // ================================================================
    // 遷移画面 (gameMode == 2 or 3):
    //   ダイレクトジャンプ命令を書き込み
    // ================================================================
    if (!s_forceGotoApplied && (gameMode == 2 || gameMode == 3)) {
        uint8_t forcePatch[2] = { 0xEB, 0x00 };

        switch (s_targetMode) {
            case cccaster::public_api::IpcGameMode::Training:  forcePatch[1] = 0x22; break;
            case cccaster::public_api::IpcGameMode::VersusCPU: forcePatch[1] = 0x5C; break;
            case cccaster::public_api::IpcGameMode::Versus:    forcePatch[1] = 0x3F; break;
            case cccaster::public_api::IpcGameMode::Replay:    forcePatch[1] = 0x22; break;
            default: forcePatch[1] = 0x3F; break;
        }

        // CC_FORCE_GOTO_ADDR にパッチ適用
        DWORD oldProtect;
        VirtualProtect(CC_FORCE_GOTO_ADDR, 2, PAGE_EXECUTE_READWRITE, &oldProtect);
        std::memcpy(CC_FORCE_GOTO_ADDR, forcePatch, 2);
        VirtualProtect(CC_FORCE_GOTO_ADDR, 2, oldProtect, &oldProtect);

        s_forceGotoApplied = true;
        DebugLog("[FastBoot] ForceGoto patch applied at gameMode=%u", gameMode);
        return false;
    }

    // ================================================================
    // その他の画面 (タイトル画面等):
    //   決定ボタンを偽造して進める
    // ================================================================
    {
        uint16_t btnBits = s_toggle ? CC_BUTTON_CONFIRM : 0;
        uint32_t input = btnBits;

        if (isHost) {
            GC::WriteInput(input, 0);
        } else {
            GC::WriteInput(0, input);
        }

        s_toggle = !s_toggle;
    }

    return false;
}

} // namespace cccaster::domain::scene
