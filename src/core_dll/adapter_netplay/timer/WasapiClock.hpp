#pragma once
// ============================================================================
// WasapiClock — WASAPI IAudioClock ベースの高精度クロック
//
// 【設計思想】
//   Windows のオーディオ再生デバイス (DAC) のハードウェアクロックを利用する。
//   QPC (QueryPerformanceCounter) は CPU の TSC に基づくため、
//   温度変化による周波数ドリフト (数十PPM) の影響を受ける。
//   一方、WASAPI IAudioClock はオーディオDAC固有の水晶発振器に基づくため、
//   よりドリフトが小さく安定している（数PPM程度）。
//
// 【フォールバック】
//   オーディオデバイスが存在しない（ヘッドレスサーバ等）場合、
//   QPC にフォールバックする。
//
// 【スレッド安全性】
//   GetTimeUs() は read-only であり、マルチスレッドから安全に呼び出し可能。
// ============================================================================

#include <cstdint>

namespace cccaster {
namespace core {
namespace timer {

class WasapiClock {
public:
    static WasapiClock& GetInstance();

    /// WASAPI / QPC の高精度時刻を取得する [μs]。
    /// WASAPI が利用可能な場合はそちらを優先し、
    /// 利用不可の場合は QPC にフォールバックする。
    static int64_t GetTimeUs();

    /// WASAPI IAudioClock が利用可能かどうか。
    bool IsAvailable() const { return _available; }

private:
    WasapiClock();
    ~WasapiClock();
    WasapiClock(const WasapiClock&) = delete;
    WasapiClock& operator=(const WasapiClock&) = delete;

    int64_t GetWasapiTimeUs() const;
    static int64_t GetQpcTimeUs();

    bool _available = false;

    // COMオブジェクト（前方宣言のため void* で保持、.cpp で具象型にキャスト）
    void* _pAudioClient = nullptr;
    void* _pAudioClock  = nullptr;
    uint64_t _frequency  = 0;
    bool _isStarted      = false;
};

} // namespace timer
} // namespace core
} // namespace cccaster
