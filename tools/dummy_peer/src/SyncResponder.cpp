#include "SyncResponder.hpp"
#include "RealClock.hpp"
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <cstring>
#include <iostream>

namespace dummy_peer {

// MinGW-w64 compatibility for missing __uuidof and GUID definitions
static constexpr CLSID CLSID_MMDeviceEnumerator_Local = { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static constexpr IID IID_IMMDeviceEnumerator_Local = { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static constexpr IID IID_IAudioClient_Local = { 0x1CB9AD4C, 0xDBFA, 0x4c32, { 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2 } };
static constexpr IID IID_IAudioClock_Local = { 0xCD63314F, 0x3FBA, 0x4a1b, { 0x81, 0x21, 0xC9, 0x2A, 0x46, 0xAD, 0xCA, 0x28 } };

SyncResponder::SyncResponder(int64_t clockOffsetUs, double driftPpm)
    : _clockOffsetUs(clockOffsetUs), _driftPpm(driftPpm) {
    _startTimeUs = GetRealQpcTimeUs();
    InitWasapi();
    std::cout << "[SyncResponder] WASAPI: " << (_wasapiReady ? "OK" : "FAILED (QPC fallback)") << "\n";
}

SyncResponder::~SyncResponder() {
    if (_pAudioClock) _pAudioClock->Release();
    if (_pAudioClient) {
        _pAudioClient->Stop();
        _pAudioClient->Release();
    }
}

void SyncResponder::InitWasapi() {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    IMMDeviceEnumerator* pEnumerator = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_MMDeviceEnumerator_Local, NULL,
        CLSCTX_ALL, IID_IMMDeviceEnumerator_Local,
        (void**)&pEnumerator);
    if (FAILED(hr)) return;

    IMMDevice* pDevice = nullptr;
    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    if (FAILED(hr)) { pEnumerator->Release(); return; }

    hr = pDevice->Activate(
        IID_IAudioClient_Local, CLSCTX_ALL,
        NULL, (void**)&_pAudioClient);
    if (FAILED(hr)) { pDevice->Release(); pEnumerator->Release(); return; }

    WAVEFORMATEX* pwfx = nullptr;
    hr = _pAudioClient->GetMixFormat(&pwfx);
    if (FAILED(hr)) { _pAudioClient->Release(); _pAudioClient = nullptr; pDevice->Release(); pEnumerator->Release(); return; }

    hr = _pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0,
        10000000,  // 1秒バッファ
        0,
        pwfx,
        NULL);
    if (FAILED(hr)) { CoTaskMemFree(pwfx); _pAudioClient->Release(); _pAudioClient = nullptr; pDevice->Release(); pEnumerator->Release(); return; }

    hr = _pAudioClient->GetService(
        IID_IAudioClock_Local,
        (void**)&_pAudioClock);

    if (SUCCEEDED(hr) && _pAudioClock) {
        _pAudioClock->GetFrequency(&_wasapiFrequency);
        if (SUCCEEDED(_pAudioClient->Start())) {
            _wasapiReady = true;
        }
    }

    CoTaskMemFree(pwfx);
    pDevice->Release();
    pEnumerator->Release();
}

int64_t SyncResponder::GetWasapiTimeUs() const {
    if (!_pAudioClock || _wasapiFrequency == 0) return 0;

    uint64_t pos = 0;
    uint64_t qpc = 0;
    HRESULT hr = _pAudioClock->GetPosition(&pos, &qpc);
    if (FAILED(hr)) return 0;

    return (pos * 1000000LL) / _wasapiFrequency;
}

int64_t SyncResponder::GetRealQpcTimeUs() const {
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (t.QuadPart * 1000000LL) / freq.QuadPart;
}

int64_t SyncResponder::GetLocalTimeUs() const {
    // WASAPI優先、失敗時はQPCフォールバック
    int64_t wasapi = GetWasapiTimeUs();
    if (wasapi > 0) return wasapi;
    return GetRealQpcTimeUs();
}

int64_t SyncResponder::GetFakeLocalTimeUs() const {
    // 後方互換: GetLocalTimeUs()と同じ
    return GetLocalTimeUs();
}

int64_t SyncResponder::ConvertRemoteToLocal(int64_t remoteTimeUs) const {
    // θ = (自分の時計 - 相手の時計)
    // ローカル時刻 = リモート時刻 + θ
    return remoteTimeUs + _measuredOffset.load();
}

std::vector<uint8_t> SyncResponder::HandlePacket(const std::vector<uint8_t>& data) {
    if (data.empty()) return {};

    uint8_t type = data[0];

    if (type == SYNC_REQ && data.size() >= 9) {
        // SYNC_REQ: [type(1)] [T1(8)]
        int64_t t1 = 0;
        std::memcpy(&t1, data.data() + 1, 8);

        int64_t t2 = GetLocalTimeUs(); // 受信時刻（WASAPI優先）
        int64_t t3 = GetLocalTimeUs(); // 送信時刻（WASAPI優先）

        // SYNC_RES: [type(1)] [T1(8)] [T2(8)] [T3(8)] = 25 bytes
        std::vector<uint8_t> response(25);
        response[0] = SYNC_RES;
        std::memcpy(response.data() + 1, &t1, 8);
        std::memcpy(response.data() + 9, &t2, 8);
        std::memcpy(response.data() + 17, &t3, 8);
        return response;
    }

    return {};
}

} // namespace dummy_peer
