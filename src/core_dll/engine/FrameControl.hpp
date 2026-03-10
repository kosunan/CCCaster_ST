#pragma once
/**
 * @file FrameControl.hpp
 * @brief ゲーム制御ファサード — メモリ操作を階層化した統一API
 *
 * 【2層構成】
 *
 *   制御層 (public)   : 速度モード切替・入力書込み・終了要求
 *                       Scene は「何をしたいか」だけを指示する
 *                       例: SetModeHighSpeedSkip(), WriteInput(p1, p2)
 *
 *   プリミティブ層 (private) : MbaaAddresses.hpp 定義の生アドレスへの直接操作
 *                       例: GetInputBasePtr(), WriteP1Input(), WriteP2Input()
 *
 * 【設計思想】
 *   - Scene は FrameControl:: の関数のみを呼ぶ（メモリアドレスを直接触らない）
 *   - 速度制御は SpeedFlags（RenderSkip + TickBypass）で直接管理
 *   - フレーム待機は Metronome に委譲（本クラスは関与しない）
 *   - 同期制御は NetplaySession に完全委譲
 *   - 全メソッドは static
 *
 * 【使用例】
 *   FrameControl::SetModeHighSpeedSkip(); // FastBoot / ロールアップ用高速化
 *   FrameControl::SetModeNormalSpeed();   // 通常速度復帰
 *   FrameControl::WriteInput(p1, p2);     // ゲームメモリに入力書込み
 *
 * @see SpeedFlags         描画スキップ + ティックバイパスの2フラグ
 * @see Metronome          フレーム精密待機
 * @see MbaaAddresses.hpp  メモリアドレス定義
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
     * @param frames 現在未使用（将来的にロールアップ深度制御用を想定）
     */
    static void SetModeRollupSkip(uint32_t /*frames*/) {
        cccaster::core::SpeedFlags::SetHighSpeed();
    }

    /**
     * @brief 通常速度
     * @details RenderSkip=OFF, TickBypass=OFF
     */
    static void SetModeNormalSpeed() {
        cccaster::core::SpeedFlags::SetNormalSpeed();
    }

    /**
     * @brief 一時停止（通常速度と同一）
     * @details Metronome がフレーム進行を制御するため、
     *          フレームが進まない状態では自然に待機状態になる。
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
