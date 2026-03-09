#pragma once
/**
 * @file MbaaSpeedController.hpp
 * @brief MBAACC固有のゲーム速度制御
 *
 * 【モード一覧とその挙動】
 *   - HighSpeedSkip_Normal (起動時、キャラセレ画面の最初50F)
 *       → 描画スキップ ON + 通信スレッド周期バイパス
 *   - HighSpeedSkip_Rollup (ロールバック巻き戻し時)
 *       → 描画スキップ ON + 通信スレッド周期バイパス
 *   - NormalSpeed (通常、NetplaySession がティック管理)
 *       → 描画スキップ OFF + 通信スレッドの currentFrame をポーリング
 *   - Pause (一時停止)
 *       → 描画スキップ OFF + 通信スレッドの currentFrame が進まないので自然に停止
 *
 * 【高速モードの設計原則】
 *   1. DxHook::EndScene → ImGui描画をスキップ（s_renderSkip=true）
 *   2. 通信スレッドの1F周期を待たず、DLLの処理とゲームの処理を即実行
 *   3. Sleep(0) / Sleep(1) 等のスリープは一切行わない
 *
 * 【一時停止の実現方式】
 *   通信スレッドの currentFrame が進まないことで DLL スレッドが
 *   SleepFrame() のポーリングで自然に待機する。
 *   ゲームメモリへのポーズフラグ書き込み (CC_PAUSE_FLAG_ADDR) は行わない。
 *
 * 【メモリ書き込みの隔離】
 *   CC_SKIP_FRAMES_ADDR は不使用 (0固定)。
 */

#include "core_dll/timing/ISpeedController.hpp"
#include "core_dll/detect/MbaaAddresses.hpp"
#include "core_dll/timing/TimeHooks.hpp"
#include <cstdio>
#include <atomic>

extern void HookLog(const char* msg);

namespace cccaster::domain {

class MbaaSpeedController : public cccaster::core::ISpeedController {
public:
    static MbaaSpeedController& GetInstance() {
        static MbaaSpeedController instance;
        return instance;
    }

    // ================================================================
    // 描画スキップフラグ（DxHook から参照）
    // ================================================================

    /// @brief 高速モード中はtrue → DxHookがEndScene内のImGui描画をスキップ
    /// @note DxHookスレッドとゲームスレッドで共有するため atomic
    static std::atomic<bool>& RenderSkip() {
        static std::atomic<bool> s_renderSkip{false};
        return s_renderSkip;
    }

    // ================================================================
    // 通信スレッド周期バイパスフラグ
    // ================================================================

    /// @brief 高速モード中はtrue → DLLスレッドが通信スレッドのcurrentFrameポーリングをスキップ
    static std::atomic<bool>& TickBypass() {
        static std::atomic<bool> s_tickBypass{false};
        return s_tickBypass;
    }

    void SetMode(cccaster::core::SpeedMode mode, uint32_t rollupFrames = 0) override {
        // 同じ状態の再設定防止（Rollup時のみ値が変わる可能性があるため許可）
        if (m_mode == mode && mode != cccaster::core::SpeedMode::HighSpeedSkip_Rollup) return;
        m_mode = mode;
        m_rollupFrames = rollupFrames;

        // ゲーム内蔵のSleepとVSyncを常に無効化し、TimeHooksを通じて1000倍速で回す。
        cccaster::core::hooks::TimeHooks::SetTimeMultiplier(1000);
        cccaster::core::hooks::TimeHooks::SetSleepBypass(true);

        // CC_SKIP_FRAMES は不使用 (0固定)
        // *CC_SKIP_FRAMES_ADDR = 0;  // 使用禁止: 描画制御は API hook (RenderSkip) で行う

        switch (mode) {
            case cccaster::core::SpeedMode::HighSpeedSkip_Rollup:
            case cccaster::core::SpeedMode::HighSpeedSkip_Normal:
                // 高速モード: 描画スキップ ON + 周期バイパス ON
                RenderSkip().store(true, std::memory_order_release);
                TickBypass().store(true, std::memory_order_release);
                break;

            case cccaster::core::SpeedMode::NormalSpeed:
                // 通常速度: 描画スキップ OFF + 周期バイパス OFF
                RenderSkip().store(false, std::memory_order_release);
                TickBypass().store(false, std::memory_order_release);
                break;

            case cccaster::core::SpeedMode::Pause:
                // 一時停止: 描画スキップ OFF + 周期バイパス OFF
                // currentFrame が進まないので自然に DLL スレッドが待機する
                RenderSkip().store(false, std::memory_order_release);
                TickBypass().store(false, std::memory_order_release);
                break;
        }

        // ログ出力
        const char* modeName = "";
        switch (mode) {
            case cccaster::core::SpeedMode::HighSpeedSkip_Normal: modeName = "HighSpeedSkip_Normal"; break;
            case cccaster::core::SpeedMode::HighSpeedSkip_Rollup: modeName = "HighSpeedSkip_Rollup"; break;
            case cccaster::core::SpeedMode::NormalSpeed:          modeName = "NormalSpeed"; break;
            case cccaster::core::SpeedMode::Pause:                modeName = "Pause"; break;
        }

        char buf[128];
        if (mode == cccaster::core::SpeedMode::HighSpeedSkip_Rollup) {
            snprintf(buf, sizeof(buf), "[GameSpeed] Mode: %s (frames=%u)", modeName, rollupFrames);
        } else {
            snprintf(buf, sizeof(buf), "[GameSpeed] Mode: %s", modeName);
        }
        HookLog(buf);
    }

    void MaintainState() override {
        // CC_SKIP_FRAMES は SleepFrame が gap に基づいて制御するため、ここでは触らない
    }

    cccaster::core::SpeedMode GetMode() const override { return m_mode; }

    /// @brief 現在のモードを取得
    cccaster::core::SpeedMode GetCurrentMode() const { return m_mode; }

    bool IsSkipMode() const override {
        return m_mode == cccaster::core::SpeedMode::HighSpeedSkip_Normal ||
               m_mode == cccaster::core::SpeedMode::HighSpeedSkip_Rollup;
    }

private:
    MbaaSpeedController() = default;
    cccaster::core::SpeedMode m_mode = cccaster::core::SpeedMode::NormalSpeed;
    uint32_t m_rollupFrames = 0;
};

} // namespace cccaster::domain
