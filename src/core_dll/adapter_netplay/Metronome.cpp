// ============================================================================
// Metronome.cpp — フレームリズム生成器（実装）
// ============================================================================

#include "core_dll/adapter_netplay/Metronome.hpp"
#include "core_dll/adapter_netplay/timer/WasapiClock.hpp"
#include "core_dll/session_orchestrator/session/DebugLog.hpp"
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
    // MIN/MAX クランプ
    if (interval < MIN_TICK_US) interval = MIN_TICK_US;
    if (interval > MAX_TICK_US) interval = MAX_TICK_US;
    return interval;
}

// ============================================================================
// Start — メトロノームスレッド起動
// ============================================================================
void Metronome::Start(uint32_t initialFrame) {
    if (_running.load()) return;

    _frame.store(initialFrame, std::memory_order_release);
    _alpha1Us.store(0, std::memory_order_release);
    _alpha2Us.store(0, std::memory_order_release);

    _running.store(true, std::memory_order_release);
    _thread = std::thread(&Metronome::ThreadMain, this);

    cccaster::domain::session::DebugLog(
        "[Metronome] Started. initialFrame=%u", initialFrame);
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
    cccaster::domain::session::DebugLog(
        "[Metronome] Stopped. finalFrame=%u", _frame.load());
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
// α1 + α2 補正付き間隔でカウンタをカウントアップする。
// 通信やバッファ操作は一切行わない。
//
void Metronome::ThreadMain() {
    int64_t nextTickUs = timer::WasapiClock::GetTimeUs();

    while (_running.load(std::memory_order_acquire)) {
        int64_t intervalUs = GetCurrentIntervalUs();
        nextTickUs += intervalUs;

        SleepUntil(nextTickUs);

        // カウンタをインクリメント
        _frame.fetch_add(1, std::memory_order_release);

        // 60Fごとにログ出力
        uint32_t f = _frame.load(std::memory_order_relaxed);
        if (f % 60 == 0) {
            cccaster::domain::session::DebugLog(
                "[Metronome] F=%u interval=%lldus α1=%lld α2=%lld",
                f, intervalUs,
                _alpha1Us.load(std::memory_order_relaxed),
                _alpha2Us.load(std::memory_order_relaxed));
        }
    }
}

} // namespace netplay
} // namespace core
} // namespace cccaster
