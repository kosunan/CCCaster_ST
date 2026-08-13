// ============================================================================
// Metronome.cpp — フレームリズム生成器（実装）
//
// ゲームスレッドが WaitForNextTick() を直接呼ぶ精密待機型。
// α1 + α2 補正付き間隔で待機する。
// ============================================================================

#include "core_dll/timing/Metronome.hpp"
#include "core_dll/common/TimeScale.hpp"
#include "core_dll/timing/WasapiClock.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/common/Platform.hpp"

namespace cccaster {
namespace core {
namespace netplay {

// ============================================================================
// GetCurrentIntervalUs — 現在のフレーム間隔を返す
// ============================================================================
int64_t Metronome::GetCurrentIntervalUs() const {
    int64_t interval = BASE_TICK_US
                     + _alpha1Us.load(std::memory_order_acquire)
                     + _alpha2Us.load(std::memory_order_acquire);
    if (interval < MIN_TICK_US) interval = MIN_TICK_US;
    if (interval > MAX_TICK_US) interval = MAX_TICK_US;
    // クランプは論理単位で行い、最後に時間圧縮をかける（既定は等倍）
    return cccaster::testing::ScaleTickUs(interval);
}

// ============================================================================
// Start — 次ティック時刻を初期化
// ============================================================================
void Metronome::Start() {
    if (_running.load()) return;

    _alpha1Us.store(0, std::memory_order_release);
    _alpha2Us.store(0, std::memory_order_release);
    _nextTickUs = timer::WasapiClock::GetTimeUs();

    _running.store(true, std::memory_order_release);

    cccaster::domain::session::DebugLog("[Metronome] Started.");
}

// ============================================================================
// Stop
// ============================================================================
void Metronome::Stop() {
    if (!_running.load()) return;
    _running.store(false, std::memory_order_release);
    cccaster::domain::session::DebugLog("[Metronome] Stopped.");
}

// ============================================================================
// SleepUntil — 精密スリープ（Sleep + スピンウェイトのハイブリッド）
// ============================================================================
void Metronome::SleepUntil(int64_t targetUs) {
    while (true) {
        int64_t remain = targetUs - timer::WasapiClock::GetTimeUs();
        if (remain <= 0) break;
        if (remain > 2000) {
            // 実機では TimeHooks により 0ms 化される（AUDIT_2026-08-13 A-4）
            cccaster::platform::SleepMs(1);
        } else {
            cccaster::platform::CpuRelax();
        }
    }
}

// ============================================================================
// WaitForNextTick — ゲームスレッドから呼ばれる精密待機
// ============================================================================
//
// 次ティック時刻を α補正付きで計算し、その時刻まで Sleep+CPUスピン で待機する。
// skipWait=true の場合は待機せず、次ティック時刻のみ進める（キャッチアップ用）。
//
void Metronome::WaitForNextTick(bool skipWait) {
    int64_t intervalUs = GetCurrentIntervalUs();
    _nextTickUs += intervalUs;

    if (!skipWait) {
        SleepUntil(_nextTickUs);
    }
}

} // namespace netplay
} // namespace core
} // namespace cccaster
