#pragma once
// ============================================================================
// Metronome — フレームリズム生成器（独立スレッド）
//
// 【責務】
//   α1 + α2 補正付き間隔でフレームカウンタをカウントアップする。
//   通信やCentralBuffer操作は一切行わない。純粋なリズム生成のみ。
//
// 【α補正】
//   α1: パケットディレイ不足補正 — D+R で吸収しきれない遅延分
//   α2: 相手メトロノームとのズレ補正 — Θ変化量ベースのドリフト追従
//
// 【スレッド間ルール】
//   - カウンタは atomic で公開。通信スレッド・ゲームスレッドからリード可。
//   - α1/α2 の設定は SyncCalculator が行う（atomic 書込み）。
// ============================================================================

#include <atomic>
#include <cstdint>
#include <thread>

namespace cccaster {
namespace core {
namespace netplay {

class Metronome {
public:
    // ─── ライフサイクル ─────────────────────────────────
    void Start(uint32_t initialFrame = 0);
    void Stop();
    bool IsRunning() const { return _running.load(std::memory_order_acquire); }

    // ─── カウンタ読取り（他スレッドから） ────────────────
    uint32_t GetFrame() const { return _frame.load(std::memory_order_acquire); }

    // ─── α補正設定（SyncCalculator から呼ばれる） ───────
    void SetAlpha1(int64_t alpha1Us) { _alpha1Us.store(alpha1Us, std::memory_order_release); }
    void SetAlpha2(int64_t alpha2Us) { _alpha2Us.store(alpha2Us, std::memory_order_release); }

    int64_t GetAlpha1() const { return _alpha1Us.load(std::memory_order_acquire); }
    int64_t GetAlpha2() const { return _alpha2Us.load(std::memory_order_acquire); }

    // ─── 現在のフレーム間隔 ─────────────────────────────
    int64_t GetCurrentIntervalUs() const;

    // ─── 定数 ──────────────────────────────────────────
    static constexpr int64_t BASE_TICK_US = 16666;   // 60fps 基本間隔
    static constexpr int64_t MAX_TICK_US  = 19332;   // 最大（減速上限）
    static constexpr int64_t MIN_TICK_US  = 14000;   // 最小（加速下限）

private:
    void ThreadMain();
    static void SleepUntil(int64_t targetUs);

    // ─── カウンタ ──────────────────────────────────────
    std::atomic<uint32_t> _frame{0};

    // ─── α補正（μs） ─────────────────────────────────
    std::atomic<int64_t> _alpha1Us{0};  // パケットディレイ不足補正
    std::atomic<int64_t> _alpha2Us{0};  // 相手ドリフト補正

    // ─── スレッド制御 ──────────────────────────────────
    std::atomic<bool> _running{false};
    std::thread _thread;
};

} // namespace netplay
} // namespace core
} // namespace cccaster
