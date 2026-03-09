#pragma once
// ============================================================================
// Metronome — フレームリズム生成器（ゲームスレッド直接呼出し型）
//
// 【責務】
//   α1 + α2 補正付き間隔で精密待機を提供する。
//   フレーム番号の管理は行わない（GameTickCodec に委譲）。
//   通信やFrameInputBuffer操作は一切行わない。
//
// 【使い方】
//   ゲームスレッドが毎フレーム WaitForNextTick() を呼ぶ。
//   WaitForNextTick は次ティック時刻まで Sleep+CPUスピン で精密待機する。
//   skipWait=true の場合は待機せず次ティック時刻のみ進める（キャッチアップ用）。
//
// 【α補正】
//   α1: パケットディレイ不足補正 — D+R で吸収しきれない遅延分
//   α2: 相手メトロノームとのズレ補正 — Θ変化量ベースのドリフト追従
//
// 【スレッド安全性】
//   WaitForNextTick() はゲームスレッドから呼ばれる。
//   α1/α2 の設定は GameTickCodec が行う（atomic 書込み）。
// ============================================================================

#include <atomic>
#include <cstdint>

namespace cccaster {
namespace core {
namespace netplay {

class Metronome {
public:
    // ─── ライフサイクル ─────────────────────────────────
    void Start();
    void Stop();
    bool IsRunning() const { return _running.load(std::memory_order_acquire); }

    // ─── ゲームスレッド精密待機 ──────────────────────────
    /// 次ティックまで Sleep+CPUスピン で精密待機する。
    /// @param skipWait true: 待機せず次ティック時刻のみ進める（キャッチアップ用）
    void WaitForNextTick(bool skipWait = false);

    // ─── α補正設定（GameTickCodec から呼ばれる） ───────
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
    static void SleepUntil(int64_t targetUs);

    // ─── 次ティック時刻 ────────────────────────────────
    int64_t _nextTickUs = 0;

    // ─── α補正（μs） ─────────────────────────────────
    std::atomic<int64_t> _alpha1Us{0};  // パケットディレイ不足補正
    std::atomic<int64_t> _alpha2Us{0};  // 相手ドリフト補正

    // ─── 状態 ──────────────────────────────────────────
    std::atomic<bool> _running{false};
};

} // namespace netplay
} // namespace core
} // namespace cccaster
