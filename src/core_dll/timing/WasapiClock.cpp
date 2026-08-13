// ============================================================================
// WasapiClock.cpp — WASAPI IAudioClock ベースの高精度クロック（実装）
//
// 【初期化フロー】
//   1. COM 初期化 (COINIT_MULTITHREADED)
//   2. デフォルトオーディオ出力デバイスを取得
//   3. IAudioClient を共有モードで初期化（バッファ 1秒）
//   4. IAudioClock を取得し、frequency をキャッシュ
//   5. AudioClient を Start() し、クロックカウントの進行を開始
//
// 【フォールバック】
//   GetTimeUs() は WASAPI 優先、取得不可時は QPC にフォールバックする。
//   TimeHooks にフックされた QPC ではなく、RealQueryPerformanceCounter を
//   使用して実時間を取得する。
// ============================================================================

// 【Linux ビルドについて】
//   WASAPI は Windows のオーディオ API なので、Linux では初期化そのものを行わず
//   `_available = false` のまま単調時計にフォールバックする。フォールバック経路は
//   もともと「オーディオデバイスが無いヘッドレス環境」のために用意されていたもので、
//   Linux 用に新設したものではない。
//   ドリフト特性は Windows(WASAPI) と Linux(CLOCK_MONOTONIC) で当然異なるため、
//   **Linux の harness はクロック品質の検証には使えない**。同期ロジックの
//   決定性検証にのみ使うこと。

#include "core_dll/timing/WasapiClock.hpp"
#include "core_dll/common/Platform.hpp"

#ifdef _WIN32
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#endif

namespace cccaster {
namespace core {
namespace timer {

#ifdef _WIN32

// ─── MinGW-w64 用 GUID 手動定義 ────────────────────────
static constexpr CLSID CLSID_MMDeviceEnumerator_Local = {
    0xBCDE0395, 0xE52F, 0x467C,
    { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E }
};
static constexpr IID IID_IMMDeviceEnumerator_Local = {
    0xA95664D2, 0x9614, 0x4F35,
    { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 }
};
static constexpr IID IID_IAudioClient_Local = {
    0x1CB9AD4C, 0xDBFA, 0x4c32,
    { 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2 }
};
static constexpr IID IID_IAudioClock_Local = {
    0xCD63314F, 0x3FBA, 0x4a1b,
    { 0x81, 0x21, 0xC9, 0x2A, 0x46, 0xAD, 0xCA, 0x28 }
};

// ─── コンストラクタ: WASAPI 初期化 ─────────────────────
WasapiClock::WasapiClock() {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    IMMDeviceEnumerator* pEnumerator = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_MMDeviceEnumerator_Local, NULL,
        CLSCTX_ALL, IID_IMMDeviceEnumerator_Local,
        (void**)&pEnumerator);
    if (FAILED(hr)) return;

    IMMDevice* pDevice = nullptr;
    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    if (FAILED(hr)) { pEnumerator->Release(); return; }

    IAudioClient* pClient = nullptr;
    hr = pDevice->Activate(
        IID_IAudioClient_Local, CLSCTX_ALL,
        NULL, (void**)&pClient);
    if (FAILED(hr)) { pDevice->Release(); pEnumerator->Release(); return; }

    WAVEFORMATEX* pwfx = nullptr;
    hr = pClient->GetMixFormat(&pwfx);
    if (FAILED(hr)) { pClient->Release(); pDevice->Release(); pEnumerator->Release(); return; }

    // 共有モードで初期化。バッファ = 1秒 (10,000,000 × 100ns)。
    hr = pClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED, 0,
        10000000, 0, pwfx, NULL);
    if (FAILED(hr)) {
        CoTaskMemFree(pwfx);
        pClient->Release(); pDevice->Release(); pEnumerator->Release();
        return;
    }

    IAudioClock* pClock = nullptr;
    hr = pClient->GetService(IID_IAudioClock_Local, (void**)&pClock);
    if (SUCCEEDED(hr) && pClock) {
        pClock->GetFrequency(&_frequency);
        if (SUCCEEDED(pClient->Start())) {
            _isStarted = true;
            _available = true;
        }
    }

    _pAudioClient = pClient;
    _pAudioClock  = pClock;

    CoTaskMemFree(pwfx);
    pDevice->Release();
    pEnumerator->Release();
}

// ─── デストラクタ ──────────────────────────────────────
WasapiClock::~WasapiClock() {
    auto* pClient = static_cast<IAudioClient*>(_pAudioClient);
    auto* pClock  = static_cast<IAudioClock*>(_pAudioClock);
    if (_isStarted && pClient) pClient->Stop();
    if (pClock)  pClock->Release();
    if (pClient) pClient->Release();
}

// ─── WASAPI 時刻取得 ──────────────────────────────────
int64_t WasapiClock::GetWasapiTimeUs() const {
    auto* pClock = static_cast<IAudioClock*>(_pAudioClock);
    if (!pClock) return 0;

    uint64_t pos = 0;
    uint64_t qpc = 0;
    HRESULT hr = pClock->GetPosition(&pos, &qpc);
    if (FAILED(hr) || _frequency == 0) return 0;

    return static_cast<int64_t>((pos * 1000000LL) / _frequency);
}

#else  // !_WIN32 ────────────────────────────────────────────

// Linux: WASAPI は存在しない。_available は false のまま、
// GetTimeUs() は必ずフォールバック（単調時計）を通る。
WasapiClock::WasapiClock()  {}
WasapiClock::~WasapiClock() {}
int64_t WasapiClock::GetWasapiTimeUs() const { return 0; }

#endif // _WIN32

// ─── シングルトン ──────────────────────────────────────
WasapiClock& WasapiClock::GetInstance() {
    static WasapiClock instance;
    return instance;
}

// ─── フォールバック: 実時間の単調時計 ─────────────────
// Windows ではフック前の QPC、Linux では CLOCK_MONOTONIC。
// どちらも Platform 側に閉じ込めてある。
int64_t WasapiClock::GetQpcTimeUs() {
    return cccaster::platform::RealMonotonicUs();
}

// ─── 統合 API: WASAPI 優先、単調時計フォールバック ────
int64_t WasapiClock::GetTimeUs() {
    auto& inst = GetInstance();
    if (inst._available) {
        int64_t t = inst.GetWasapiTimeUs();
        if (t > 0) return t;
    }
    return GetQpcTimeUs();
}

} // namespace timer
} // namespace core
} // namespace cccaster
