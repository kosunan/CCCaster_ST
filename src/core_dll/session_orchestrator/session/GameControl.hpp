#pragma once
/**
 * @file GameControl.hpp
 * @brief ゲーム制御ファサード — メモリ操作を階層化した統一API
 *
 * 【3層アーキテクチャ】
 *
 *   Layer 3 (Scene)     : SceneInGame, SceneCharaSelect 等の業務ロジック
 *                         → GameControl の束ねた関数を呼び出して業務を遂行
 *
 *   Layer 2 (GameControl): このファイル — 複数のメモリ操作を束ねた制御関数
 *                         例: PauseForSync() = 高速化OFF + ゲーム一時停止
 *                         Scene は「何をしたいか」だけを知り、メモリの詳細は知らない
 *
 *   Layer 1 (Primitive)  : 個別メモリ読み書き（このファイル下部の private セクション）
 *                         例: SetPauseFlag(1), SetSkipFrames(1)
 *                         MbaaConstants.hpp で定義された生アドレスへの直接操作
 *
 * 【設計思想】
 *   - Scene は GameControl:: の関数のみを呼ぶ（メモリアドレスを直接触らない）
 *   - GameControl は MbaaSpeedController を内部で連携
 *   - 同期制御は SyncCoordinator に完全委譲（DLLスレッドは Read-only）
 *   - 個別メモリ操作は private メソッドとして隠蔽
 *   - 全メソッドは static — シングルトンへの委譲で状態管理
 *
 * 【使用例】
 *   GameControl::SetModePause();       // 同期ポイントで一時停止
 *   GameControl::SetModeHighSpeedSkip(); // ロールバック巻き戻し開始
 *   GameControl::SleepFrame();        // フレーム待機（Sleep(1)）
 *
 * @see MbaaSpeedController  フレームスキップ制御の実装
 * @see SyncCoordinator      通信同期（θ推定・ティックマスター）
 * @see MbaaConstants.hpp     メモリアドレス定義
 */

#include "core_dll/game_memory_accessor/MbaaSpeedController.hpp"
#include "core_dll/adapter_netplay/SyncCoordinator.hpp"
#include "core_dll/game_memory_accessor/MbaaConstants.hpp"
#include <cstdint>
#include <windows.h>

namespace cccaster::domain::session {

// 前方宣言: DebugLog（循環include回避）
void DebugLog(const char* fmt, ...);

class GameControl {
public:
    // =====================================================================
    //  Layer 2: 束ねた制御関数（Scene から呼ばれる公開API）
    // =====================================================================

    // -------------------- 速度・進行状態制御 --------------------

    /**
     * @brief 高速スキップ_通常 (起動時, キャラセレ初期用)
     * @details 描画スキップ=ON(1), Sleep=0ms
     */
    static void SetModeHighSpeedSkip() {
        Speed().SetMode(cccaster::core::SpeedMode::HighSpeedSkip_Normal);
    }

    /**
     * @brief 高速スキップ_ロールアップ
     * @param frames ロールバックするフレーム数
     * @details 描画スキップ=frames, Sleep=0ms
     */
    static void SetModeRollupSkip(uint32_t frames) {
        Speed().SetMode(cccaster::core::SpeedMode::HighSpeedSkip_Rollup, frames);
    }

    /**
     * @brief 通常速度
     * @details 描画スキップ=ON(1)固定, SyncCoordinatorがティック管理
     */
    static void SetModeNormalSpeed() {
        Speed().SetMode(cccaster::core::SpeedMode::NormalSpeed);
    }

    /**
     * @brief 一時停止
     * @details 描画スキップ=ON(1)固定, ゲーム進行停止(PauseFlag=1)
     */
    static void SetModePause() {
        Speed().SetMode(cccaster::core::SpeedMode::Pause);
    }

    /**
     * @brief 毎フレーム状態を維持する
     * @details 各モードに応じて CC_SKIP_FRAMES や CC_PAUSE_FLAG を再設定
     */
    static void MaintainState() {
        Speed().MaintainState();
    }


    /**
     * @brief 次フレームまで待機する（ワールドタイマー完全一致同期方式）
     * @details
     *   【高速モード（TickBypass=true）】
     *     SetModeHighSpeedSkip 等の永続高速モード中は即リターン。
     *     差分補正による一時的な早回しも同じパスを使う。
     *
     *   【通常モード（TickBypass=false）】
     *     ① worldTimer < netFrame（ゲームが遅れている）
     *          → TickBypass=true にして即リターン（ゲームを1F加速）
     *          　 次回呼び出し時に再評価し、一致したら ② へ移行
     *
     *     ② worldTimer > netFrame（ゲームが進みすぎ）
     *          → Sleep(1) ループで netFrame が worldTimer に追いつくまで停止
     *          　 追いついたら TickBypass=false を確認して通常ポーリングへ
     *
     *     ③ worldTimer == netFrame（一致）
     *          → TickBypass=false を確認し、次の netFrame 変化を待つ通常ポーリング
     *
     *   【設計原則】
     *     ワールドタイマーは CC_WORLD_TIMER_ADDR (0x55D1D4) を参照。
     *     FastBoot 中（SetModeHighSpeedSkip 永続中）は差分チェックをスキップ。
     */
    static void SleepFrame() {
        // ── 永続高速モード（FastBoot/Rollup）: 即リターン ──────────────
        if (MbaaSpeedController::TickBypass().load(std::memory_order_acquire)) {
            return;
        }

        auto& syncState = cccaster::core::netplay::SyncCoordinator::GetState();

        // ── ワールドタイマー vs ネットワークフレーム 差分チェック ────
        uint32_t worldTimer = *CC_WORLD_TIMER_ADDR;
        uint32_t netFrame   = syncState.currentFrame.load(std::memory_order_acquire);

        if (worldTimer < netFrame) {
            // ① ゲームが遅れている → 1F 加速（TickBypass を一時的に true）
            // 次フレームの SleepFrame 先頭で再評価する
            MbaaSpeedController::TickBypass().store(true, std::memory_order_release);
            DebugLog("[GameControl] WorldTimer catch-up: wt=%u net=%u (+%u F ahead)",
                     worldTimer, netFrame, netFrame - worldTimer);
            return;
        }

        if (worldTimer > netFrame) {
            // ② ゲームが進みすぎ → netFrame が追いつくまで停止
            DebugLog("[GameControl] WorldTimer wait: wt=%u net=%u (+%u F behind)",
                     worldTimer, netFrame, worldTimer - netFrame);
            for (;;) {
                uint32_t curNet = syncState.currentFrame.load(std::memory_order_acquire);
                uint32_t curWorld = *CC_WORLD_TIMER_ADDR;
                if (curNet >= curWorld) break;   // 追いついた
                Sleep(1);
            }
            // 一時的な早回しフラグが残っていれば解除
            MbaaSpeedController::TickBypass().store(false, std::memory_order_release);
            return;
        }

        // ③ 完全一致: TickBypass 解除（一時的早回し後の復帰）を確実に行い、
        //    次の netFrame 変化（SyncCoordinator の 1F 進行）まで通常ポーリング
        MbaaSpeedController::TickBypass().store(false, std::memory_order_release);
        for (;;) {
            uint32_t now = syncState.currentFrame.load(std::memory_order_acquire);
            if (now != netFrame) break;
            Sleep(1);
        }
    }

    // -------------------- 入力操作 --------------------

    /**
     * @brief P1/P2 の入力をゲームメモリに書き込む
     */
    static void WriteInput(uint32_t p1Input, uint32_t p2Input) {
        char* base = GetInputBasePtr();
        if (!base) {
            LogNullInputBase();
            return;
        }
        WriteP1Input(base, p1Input);
        WriteP2Input(base, p2Input);
    }

    /**
     * @brief 入力をクリアする（P1=0, P2=0）
     */
    static void ClearInput() {
        WriteInput(0, 0);
    }

    /**
     * @brief ローカルプレイヤーの入力をゲームメモリから読み取る
     * @param[in] isHost  true: P1として読取, false: P2として読取
     * @return (direction << 16) | buttons 形式の入力値
     */
    static uint32_t ReadLocal(bool isHost) {
        char* base = GetInputBasePtr();
        if (!base) return 0;
        uint32_t offset = isHost ? CC_P1_OFFSET_DIRECTION : CC_P2_OFFSET_DIRECTION;
        uint32_t dir = *reinterpret_cast<uint32_t*>(base + offset) & 0xFFFF;
        uint16_t btn = *reinterpret_cast<uint16_t*>(base + offset + 4);
        return (dir << 16) | btn;
    }

    // -------------------- ゲーム終了 --------------------

    /**
     * @brief ゲームプロセス（MBAA全体）の終了を要求
     */
    static void ExitGame() {
        TerminateProcess(GetCurrentProcess(), 1);
    }

private:
    // =====================================================================
    //  Layer 1: 個別メモリ操作プリミティブ（外部には非公開）
    // =====================================================================

    static char* GetInputBasePtr() {
        return *reinterpret_cast<char**>(CC_PTR_TO_WRITE_INPUT_ADDR);
    }

    static void WriteP1Input(char* base, uint32_t input) {
        *reinterpret_cast<uint32_t*>(base + CC_P1_OFFSET_DIRECTION) = (input >> 16) & 0xFFFF;
        *reinterpret_cast<uint16_t*>(base + CC_P1_OFFSET_BUTTONS)   = static_cast<uint16_t>(input & 0xFFFF);
    }

    static void WriteP2Input(char* base, uint32_t input) {
        *reinterpret_cast<uint32_t*>(base + CC_P2_OFFSET_DIRECTION) = (input >> 16) & 0xFFFF;
        *reinterpret_cast<uint16_t*>(base + CC_P2_OFFSET_BUTTONS)   = static_cast<uint16_t>(input & 0xFFFF);
    }

    static void LogNullInputBase() {
        static uint32_t s_nullCount = 0;
        if (s_nullCount++ % 120 == 0) {
            DebugLog("[GameControl] Input base pointer is NULL (count=%u)", s_nullCount);
        }
    }

    // =====================================================================
    //  シングルトンアクセサ（内部用）
    // =====================================================================

    static MbaaSpeedController& Speed() {
        return MbaaSpeedController::GetInstance();
    }
};

} // namespace cccaster::domain::session
