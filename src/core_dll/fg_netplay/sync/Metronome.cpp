// ============================================================================
// Metronome.cpp — フレームリズム生成器（実装）
//
// α1 + α2 補正付き間隔でティック信号を発火する。
// フレーム番号管理は行わない（SyncCalculator に委譲）。
// ============================================================================

#include "core_dll/fg_netplay/sync/Metronome.hpp"
#include "core_dll/platform/clock/WasapiClock.hpp"
#include "core_dll/platform/common/DebugLog.hpp"
#include <windows.h>

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
    return interval;
}

// ============================================================================
// Start — メトロノームスレッド起動
// ============================================================================
void Metronome::Start() {
    if (_running.load()) return;

    _pendingTicks.store(0, std::memory_order_release);
    _alpha1Us.store(0, std::memory_order_release);
    _alpha2Us.store(0, std::memory_order_release);

    _running.store(true, std::memory_order_release);
    _thread = std::thread(&Metronome::ThreadMain, this);

    cccaster::domain::session::DebugLog("[Metronome] Started.");
}

// ============================================================================
// Stop — メトロノームスレッド停止
// ============================================================================
void Metronome::Stop() {
    if (!_running.load()) return;
    _running.store(false, std::memory_order_release);
    if (_thread.joinable()) {
        _thread.join();
    }
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
            Sleep(1);
        } else {
            YieldProcessor();
        }
    }
}

// ============================================================================
// ThreadMain — メトロノームスレッドのメインループ
// ============================================================================
//
// α1 + α2 補正付き間隔でティック信号を蓄積する。
// 通信やバッファ操作は一切行わない。
//
void Metronome::ThreadMain() {
    int64_t nextTickUs = timer::WasapiClock::GetTimeUs();

    while (_running.load(std::memory_order_acquire)) {
        int64_t intervalUs = GetCurrentIntervalUs();
        nextTickUs += intervalUs;

        SleepUntil(nextTickUs);

        // ティック信号を蓄積（通信スレッドが ConsumeTicks で消費する）
        _pendingTicks.fetch_add(1, std::memory_order_release);
    }
}

} // namespace netplay
} // namespace core
} // namespace cccaster
