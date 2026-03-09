#pragma once
// ============================================================================
// NetplayClock — ネットプレイ同期補正 計算エンジン（純粋関数群）
//
// 【責務】
//   θ推定、ドリフトレート学習、1F周期算出、スタート時刻管理。
//   スレッドを持たない。I/Oをしない。値を渡されて計算して返すだけ。
//   SyncCoordinator（通信スレッド）が呼び出す。
//
// 【θ推定方式】
//   パケットに載った相手のWASAPIタイムスタンプと
//   自分のWASAPI受信時刻の差分からオフセットを推定する。
//   offset_raw = peerTimestamp - localReceiveTime = θ - D
//   D(片道遅延)は常に正なので、最大 offset_raw が θ の最良推定値。
//
// 【ドリフト補正】
//   θの変化率（ドリフトレート）をEMAで追跡し、
//   1F周期を微調整して相手のクロックに追従する。
// ============================================================================

#include <cstdint>

namespace cccaster {
namespace core {
namespace timer {

class NetplayClock {
public:
    // ── WASAPIクロック（WasapiClock委譲）──────────
    /// WasapiClock::GetTimeUs() のラッパー
    static int64_t GetTimeUs();

    // ── θ推定 ───────────────────────────────────
    /// パケット受信時に呼ぶ。θ内部更新。
    void AddThetaSample(int64_t peerTimestamp, int64_t localReceiveTime);

    /// 現在のθ推定値 [μs]
    int64_t GetThetaUs() const { return _thetaUs; }

    /// θの初期基準値からの偏差 [μs]（ドリフト補正に使用）
    int64_t GetThetaDeltaUs() const { return _thetaUs - _baseThetaUs; }

    /// θが安定しているか（σ < 1ms && 10サンプル以上）
    bool IsThetaStable() const;

    // ── ドリフトレート ───────────────────────────
    /// θの変化速度 [μs/μs]。正=相手が遅れている方向。
    double GetDriftRate() const { return _driftRate; }

    // ── 1F周期算出 ──────────────────────────────
    /// ドリフト補正込みのティック周期 [μs]
    int64_t GetTickUs() const;

    // ── スタート時刻管理 ─────────────────────────
    /// 自分のスタート希望時刻を設定 [WASAPI μs]
    void SetLocalStartTime(int64_t wasapiUs);

    /// 相手のスタート希望時刻を受信（θ変換してローカル基準に保存）
    void SetPeerStartTime(int64_t peerWasapiUs);

    /// 合意されたスタート時刻（遅い方を採用）[ローカルWASAPI μs]
    /// 双方未設定なら 0。
    int64_t GetAgreedStartTime() const;

    /// 全状態リセット
    void Reset();

    // ── 定数 ────────────────────────────────────
    static constexpr int64_t BASE_TICK_US = 16666;   // 60fps
    static constexpr int64_t MAX_TICK_US  = 18000;   // スローダウン上限
    static constexpr int64_t MIN_TICK_US  = 14000;   // 加速下限
    static constexpr int     THETA_SAMPLE_COUNT = 30; // リングバッファサイズ
    static constexpr int     THETA_STABLE_MIN   = 10; // 安定判定の最小サンプル数
    static constexpr double  THETA_STABLE_SIGMA = 1000.0; // 安定判定σ閾値 [μs]
    static constexpr double  DRIFT_EMA_ALPHA    = 0.1;    // EMA 学習率

private:
    // ── θ推定状態 ────────────────────────────────
    int64_t _thetaSamples[THETA_SAMPLE_COUNT] = {};
    int     _thetaSampleIndex = 0;
    int     _thetaSampleCount = 0;
    int64_t _thetaUs = 0;
    int64_t _baseThetaUs = 0;     // θの初期基準値（Counting遷移時に設定）
    bool    _baseThetaSet = false; // 基準θが設定済みか

    // ── ドリフトレート ───────────────────────────
    double  _driftRate = 0.0;       // [μs/μs]
    int64_t _lastThetaUs = 0;       // 前回のθ
    int64_t _lastThetaTimeUs = 0;   // 前回のθ測定時刻

    // ── スタート時刻 ─────────────────────────────
    int64_t _localStartTimeUs = 0;  // 自分の希望（ローカルWASAPI基準）
    int64_t _peerStartTimeUs  = 0;  // 相手の希望（θ変換済み、ローカル基準）
};

} // namespace timer
} // namespace core
} // namespace cccaster
