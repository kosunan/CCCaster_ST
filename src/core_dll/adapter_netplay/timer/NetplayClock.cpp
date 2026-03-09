// ============================================================================
// NetplayClock.cpp — ネットプレイ同期補正 計算エンジン（実装）
//
// 【純粋関数群】
//   スレッドなし、I/Oなし。
//   SyncCoordinator から呼ばれてデータを受け取り、計算結果を返す。
// ============================================================================

#include "core_dll/adapter_netplay/timer/NetplayClock.hpp"
#include "core_dll/adapter_netplay/timer/WasapiClock.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace cccaster {
namespace core {
namespace timer {

// ============================================================================
// GetTimeUs — WasapiClock への委譲
// ============================================================================
int64_t NetplayClock::GetTimeUs() {
    return WasapiClock::GetTimeUs();
}

// ============================================================================
// Reset — 全状態リセット
// ============================================================================
void NetplayClock::Reset() {
    std::memset(_thetaSamples, 0, sizeof(_thetaSamples));
    _thetaSampleIndex = 0;
    _thetaSampleCount = 0;
    _thetaUs = 0;
    _baseThetaUs = 0;
    _baseThetaSet = false;

    _driftRate = 0.0;
    _lastThetaUs = 0;
    _lastThetaTimeUs = 0;

    _localStartTimeUs = 0;
    _peerStartTimeUs  = 0;
}

// ============================================================================
// AddThetaSample — パケットのタイムスタンプからθを更新
// ============================================================================
//
// offset_raw = peerTimestamp - localReceiveTime = θ - D
// D(片道遅延)は常に正 → 最大 offset_raw が θ の最良推定値
//
void NetplayClock::AddThetaSample(int64_t peerTimestamp, int64_t localReceiveTime) {
    int64_t offsetRaw = peerTimestamp - localReceiveTime;

    // リングバッファに追加
    _thetaSamples[_thetaSampleIndex] = offsetRaw;
    _thetaSampleIndex = (_thetaSampleIndex + 1) % THETA_SAMPLE_COUNT;
    if (_thetaSampleCount < THETA_SAMPLE_COUNT) {
        _thetaSampleCount++;
    }

    if (_thetaSampleCount < 3) return;  // データ不足

    // 最大値を θ の推定値として採用
    int64_t maxOffset = _thetaSamples[0];
    for (int i = 1; i < _thetaSampleCount; i++) {
        if (_thetaSamples[i] > maxOffset) {
            maxOffset = _thetaSamples[i];
        }
    }

    int64_t prevTheta = _thetaUs;
    _thetaUs = maxOffset;

    // ── ドリフトレート更新（EMA）──
    int64_t now = GetTimeUs();
    if (_lastThetaTimeUs > 0 && prevTheta != 0) {
        int64_t dt = now - _lastThetaTimeUs;
        if (dt > 0) {
            double instantRate = static_cast<double>(_thetaUs - prevTheta) / static_cast<double>(dt);
            _driftRate = DRIFT_EMA_ALPHA * instantRate + (1.0 - DRIFT_EMA_ALPHA) * _driftRate;
        }
    }
    _lastThetaUs = _thetaUs;
    _lastThetaTimeUs = now;

    // ── 基準θの自動設定（初回安定時）──
    if (!_baseThetaSet && IsThetaStable()) {
        _baseThetaUs = _thetaUs;
        _baseThetaSet = true;
    }
}

// ============================================================================
// IsThetaStable — θ安定判定
// ============================================================================
// 直近10サンプルのσ < 1ms
bool NetplayClock::IsThetaStable() const {
    if (_thetaSampleCount < THETA_STABLE_MIN) return false;

    int start = (_thetaSampleIndex - THETA_STABLE_MIN + THETA_SAMPLE_COUNT) % THETA_SAMPLE_COUNT;
    double mean = 0;
    for (int i = 0; i < THETA_STABLE_MIN; i++) {
        mean += _thetaSamples[(start + i) % THETA_SAMPLE_COUNT];
    }
    mean /= THETA_STABLE_MIN;

    double variance = 0;
    for (int i = 0; i < THETA_STABLE_MIN; i++) {
        double diff = _thetaSamples[(start + i) % THETA_SAMPLE_COUNT] - mean;
        variance += diff * diff;
    }
    variance /= (THETA_STABLE_MIN - 1);
    double sigma = std::sqrt(variance);

    return sigma < THETA_STABLE_SIGMA;
}

// ============================================================================
// GetTickUs — 非線形θ直接補正込みの1F周期
// ============================================================================
//
// θ（クロックオフセット [μs]）の符号:
//   θ > 0 → 相手の時計が自分より進んでいる → 自分を速くする（周期を短く）
//   θ < 0 → 相手の時計が自分より遅れている → 自分を遅くする（周期を長く）
//
// ※ ここでのθは「追いつくべき方向」を示す。
//   θが正 = 相手が先に進んでいる = 自分が遅い = TickUsを短くして追いつく
//
// 3段階非線形補正:
//   1. |θ| ≤ DEAD_BAND_US (500μs): 補正なし（ジッター吸収）
//   2. DEAD_BAND < |θ| ≤ STRONG_THRESHOLD (5000μs): 二乗カーブ（穏やかに加速）
//   3. |θ| > STRONG_THRESHOLD: 線形で急速補正（最大 MAX_CORRECTION_US）
//
// ============================================================================
int64_t NetplayClock::GetTickUs() const {
    // ── 補正パラメータ ──
    // 4段階: デッドバンド → 三乗カーブ → 最大補正 → フレームスキップ
    static constexpr int64_t DEAD_BAND_US          = 500;     // ±500μs以内: 補正なし
    static constexpr int64_t STRONG_THRESHOLD_US    = 16666;   // 1F: 最大補正到達
    static constexpr int64_t FRAMESKIP_THRESHOLD_US = 36665;   // 2.2F: フレームスキップ相当
    static constexpr double  MAX_CORRECTION_US      = 2666.0;  // 最大補正量 ≈ 16% of 16666μs

    // θ偏差（基準θからのずれ）を使う。基準未設定なら補正なし
    if (!_baseThetaSet) return BASE_TICK_US;

    int64_t thetaDelta = _thetaUs - _baseThetaUs;
    int64_t absDelta = (thetaDelta >= 0) ? thetaDelta : -thetaDelta;

    double correction = 0.0;

    if (absDelta <= DEAD_BAND_US) {
        // ── デッドバンド: ジッター域、補正なし ──
        correction = 0.0;
    }
    else if (absDelta <= STRONG_THRESHOLD_US) {
        // ── 三乗カーブ: 攻撃的な非線形 ──
        double t = static_cast<double>(absDelta - DEAD_BAND_US)
                 / static_cast<double>(STRONG_THRESHOLD_US - DEAD_BAND_US);
        correction = t * t * t * MAX_CORRECTION_US;
    }
    else if (absDelta <= FRAMESKIP_THRESHOLD_US) {
        // ── 最大補正: 1F~2.2Fの範囲 ──
        correction = MAX_CORRECTION_US;
    }
    else {
        // ── フレームスキップ相当: 2.2F超 → 1F丸ごと補正で即座に追いつく ──
        correction = static_cast<double>(BASE_TICK_US);  // 16666μs = 1F
    }

    // thetaDelta > 0 → 相手の時計がさらに進んだ → 自分を速くする → 周期を短く
    // thetaDelta < 0 → 相手の時計がさらに遅れた → 自分を遅くする → 周期を長く
    int64_t adjusted = BASE_TICK_US;
    if (thetaDelta > 0) {
        adjusted -= static_cast<int64_t>(correction);
    } else {
        adjusted += static_cast<int64_t>(correction);
    }

    // クランプ (フレームスキップ時は0μsまで許容、スタール時は2F=33332μsまで)
    if (adjusted > 33332) adjusted = 33332;  // 2F上限
    if (adjusted < 0)     adjusted = 0;       // 即座に次フレーム
    return adjusted;
}

// ============================================================================
// スタート時刻管理
// ============================================================================

void NetplayClock::SetLocalStartTime(int64_t wasapiUs) {
    _localStartTimeUs = wasapiUs;
}

void NetplayClock::SetPeerStartTime(int64_t peerWasapiUs) {
    // 相手のWASAPI時刻をθ変換してローカル基準に
    // peerTime = localTime + θ → localTime = peerTime - θ
    _peerStartTimeUs = peerWasapiUs - _thetaUs;
}

/// 合意スタート時刻（遅い方を採用）
int64_t NetplayClock::GetAgreedStartTime() const {
    if (_localStartTimeUs <= 0 || _peerStartTimeUs <= 0) return 0;
    return std::max(_localStartTimeUs, _peerStartTimeUs);
}

} // namespace timer
} // namespace core
} // namespace cccaster
