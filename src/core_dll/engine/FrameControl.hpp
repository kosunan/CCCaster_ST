#pragma once
/**
 * @file FrameControl.hpp
 * @brief ゲーム制御ファサード — メモリ操作を階層化した統一API
 *
 * 【3層アーキテクチャ】
 *
 *   Layer 3 (Scene)     : MatchScene 等の業務ロジック
 *                         → FrameControl の束ねた関数を呼び出して業務を遂行
 *
 *   Layer 2 (FrameControl): このファイル — 複数の操作を束ねた制御関数
 *                         例: SetModePause() = SpeedFlags::SetNormalSpeed()
 *                         Scene は「何をしたいか」だけを知り、制御の詳細は知らない
 *
 *   Layer 1 (Primitive)  : 個別メモリ読み書き（このファイル下部の private セクション）
 *                         例: GetInputBasePtr(), WriteP1Input(), WriteP2Input()
 *                         MbaaAddresses.hpp で定義された生アドレスへの直接操作
 *
 * 【設計思想】
 *   - Scene は FrameControl:: の関数のみを呼ぶ（メモリアドレスを直接触らない）
 *   - 速度制御は SpeedFlags（RenderSkip + TickBypass）で直接管理
 *   - 同期制御は NetplaySession に完全委譲（DLLスレッドは Read-only）
 *   - 個別メモリ操作は private メソッドとして隠蔽
 *   - 全メソッドは static — シングルトンへの委譲で状態管理
 *
 * 【使用例】
 *   FrameControl::SetModePause();         // 同期ポイントで一時停止
 *   FrameControl::SetModeHighSpeedSkip(); // 起動時・FastBoot 用高速化
 *   FrameControl::SleepFrame();           // gap ベースのフレーム待機
 *
 * @see SpeedFlags         描画スキップ + ティックバイパスの2フラグ
 * @see NetplaySession      通信同期（θ推定・ティックマスター）
 * @see MbaaAddresses.hpp     メモリアドレス定義
 */

#include "core_dll/timing/SpeedFlags.hpp"
#include "core_dll/mbaa_mem/MbaaInputDefs.hpp"
#include <cstdint>
#include <windows.h>

namespace cccaster::domain::session {

// 前方宣言: DebugLog（循環include回避）
void DebugLog(const char* fmt, ...);

class FrameControl {
public:
    // =====================================================================
    //  Layer 2: 束ねた制御関数（Scene から呼ばれる公開API）
    // =====================================================================

    // -------------------- 速度・進行状態制御 --------------------

    /**
     * @brief 高速スキップ (起動時, FastBoot用, ロールアップ用)
     * @details RenderSkip=ON, TickBypass=ON
     */
    static void SetModeHighSpeedSkip() {
        cccaster::core::SpeedFlags::SetHighSpeed();
    }

    /**
     * @brief 高速スキップ_ロールアップ
     * @details RenderSkip=ON, TickBypass=ON（HighSpeedSkip と同一動作）
     */
    static void SetModeRollupSkip(uint32_t /*frames*/) {
        cccaster::core::SpeedFlags::SetHighSpeed();
    }

    /**
     * @brief 通常速度
     * @details RenderSkip=OFF（SleepFrame が gap に応じて動的に ON/OFF 制御）
     */
    static void SetModeNormalSpeed() {
        cccaster::core::SpeedFlags::SetNormalSpeed();
    }

    /**
     * @brief 一時停止
     * @details RenderSkip=OFF, currentFrame が進まないので SleepFrame で自然待機
     */
    static void SetModePause() {
        cccaster::core::SpeedFlags::SetNormalSpeed();
    }


    /**
     * @brief gap に応じて RenderSkip を制御する
     * @param gap   peerLatestFrame - localWriteHead（相手との差分）
     * @details
     *   gap >= 2: 相手が先行 → RenderSkip=ON（描画スキップで高速キャッチアップ）
     *   gap <  2: 通常 → RenderSkip=OFF（描画ON）
     */
    static void SetRenderSkipByGap(int32_t gap) {
        using SF = cccaster::core::SpeedFlags;
        SF::RenderSkip().store(gap >= 2, std::memory_order_release);
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
            DebugLog("[FrameControl] Input base pointer is NULL (count=%u)", s_nullCount);
        }
    }
};

} // namespace cccaster::domain::session
