#pragma once
/**
 * @file GameControl.hpp
 * @brief ゲーム制御ファサード — メモリ操作を階層化した統一API
 *
 * 【3層アーキテクチャ】
 *
 *   Layer 3 (Scene)     : SceneBusiness 等の業務ロジック
 *                         → GameControl の束ねた関数を呼び出して業務を遂行
 *
 *   Layer 2 (GameControl): このファイル — 複数の操作を束ねた制御関数
 *                         例: SetModePause() = RenderSkip=OFF + 自然待機
 *                         Scene は「何をしたいか」だけを知り、制御の詳細は知らない
 *
 *   Layer 1 (Primitive)  : 個別メモリ読み書き（このファイル下部の private セクション）
 *                         例: GetInputBasePtr(), WriteP1Input(), WriteP2Input()
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
 *   GameControl::SetModePause();         // 同期ポイントで一時停止
 *   GameControl::SetModeHighSpeedSkip(); // 起動時・FastBoot 用高速化
 *   GameControl::SleepFrame();           // gap ベースのフレーム待機
 *
 * @see MbaaSpeedController  フレームスキップ制御の実装
 * @see SyncCoordinator      通信同期（θ推定・ティックマスター）
 * @see MbaaConstants.hpp     メモリアドレス定義
 */

#include "core_dll/mbaa_game/speed/MbaaSpeedController.hpp"
#include "core_dll/fg_netplay/buffer/CentralBuffer.hpp"
#include "core_dll/mbaa_game/constants/MbaaConstants.hpp"
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
     * @brief 高速スキップ_通常 (起動時, FastBoot用)
     * @details RenderSkip=ON, TickBypass=ON
     */
    static void SetModeHighSpeedSkip() {
        Speed().SetMode(cccaster::core::SpeedMode::HighSpeedSkip_Normal);
    }

    /**
     * @brief 高速スキップ_ロールアップ
     * @param frames ロールバックするフレーム数
     * @details RenderSkip=ON, TickBypass=ON
     */
    static void SetModeRollupSkip(uint32_t frames) {
        Speed().SetMode(cccaster::core::SpeedMode::HighSpeedSkip_Rollup, frames);
    }

    /**
     * @brief 通常速度
     * @details RenderSkip=OFF（SleepFrame が gap に応じて動的に ON/OFF 制御）
     */
    static void SetModeNormalSpeed() {
        Speed().SetMode(cccaster::core::SpeedMode::NormalSpeed);
    }

    /**
     * @brief 一時停止
     * @details RenderSkip=OFF, currentFrame が進まないので SleepFrame で自然待機
     */
    static void SetModePause() {
        Speed().SetMode(cccaster::core::SpeedMode::Pause);
    }

    /**
     * @brief 毎フレーム状態を維持する（現在は空実装）
     * @details 描画制御は SleepFrame が gap に基づいて RenderSkip を動的制御するため不要
     */
    static void MaintainState() {
        Speed().MaintainState();
    }


    /**
     * @brief 次フレームまで待機する
     * @details
     *   worldTimer（ゲームエンジン側フレームカウンタ）が
     *   effectiveHead（ディレイ+ロールバック補正+リモート確定）に追従する。
     *
     *   effectiveHead = min(writeHead - (delay+maxRollback), confirmedRemoteFrame)
     *
     *   gap = effectiveHead - worldTimer として:
     *     gap <= 0: worldTimer が追いついた → effectiveHead 変化を待機
     *     gap == 1: 通常速度で 1F 進める
     *     gap >= 2: 描画OFF で高速に追いつかせる
     *
     *   【注意】CC_SKIP_FRAMES_ADDR は使用禁止。
     *   描画の ON/OFF は API hook (RenderSkip → OnPresentSkip) で制御する。
     */
    static void SleepFrame() {
        auto& buf = cccaster::core::sync::CentralBuffer::GetInstance();
        uint32_t ef = buf.GetEffectiveHead();
        uint32_t wt = *CC_WORLD_TIMER_ADDR;

        // gap = effectiveHead - worldTimer
        int32_t gap = static_cast<int32_t>(ef) - static_cast<int32_t>(wt);

        if (gap <= 0) {
            // worldTimer が effectiveHead に追いついている → 変化を待つ
            MbaaSpeedController::RenderSkip().store(false, std::memory_order_release);
            MbaaSpeedController::TickBypass().store(false, std::memory_order_release);
            for (;;) {
                uint32_t now = buf.GetEffectiveHead();
                if (now != ef) break;
                Sleep(1);
            }
        } else if (gap == 1) {
            // 1F 遅れ → 通常速度で進行
            MbaaSpeedController::RenderSkip().store(false, std::memory_order_release);
            MbaaSpeedController::TickBypass().store(false, std::memory_order_release);
        } else {
            // 2F 以上遅れ → 描画OFF (API hook経由: RenderSkip→OnPresentSkip)
            MbaaSpeedController::RenderSkip().store(true, std::memory_order_release);
            MbaaSpeedController::TickBypass().store(true, std::memory_order_release);
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
