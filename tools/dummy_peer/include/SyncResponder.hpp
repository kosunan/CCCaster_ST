#pragma once
#include <cstdint>
#include <vector>
#include <atomic>

// Forward declarations for WASAPI (avoid including heavy headers)
struct IAudioClient;
struct IAudioClock;

namespace dummy_peer {

// TimeSynchronizer の SYNC_REQ / SYNC_RES プロトコルに応答するモジュール
// ダミークライアントを「ホスト側」として振る舞わせ、本体DLL(クライアント)と擬似同期する
class SyncResponder {
public:
    // 意図的な時計のズレ（マイクロ秒）。正の値 = ダミーの時計が進んでいる
    explicit SyncResponder(int64_t clockOffsetUs, double driftPpm);
    ~SyncResponder();

    // 受信パケットを解析し、SYNC_REQ なら SYNC_RES を生成して返す
    // 戻り: 応答パケット（空なら応答なし）
    std::vector<uint8_t> HandlePacket(const std::vector<uint8_t>& data);

    // WASAPI優先のローカル時刻 (μs) を返す。WASAPI失敗時はQPCフォールバック
    int64_t GetLocalTimeUs() const;

    // 後方互換: GetLocalTimeUs()と同じ
    int64_t GetFakeLocalTimeUs() const;

    // QPC高精度時刻取得 (フォールバック用)
    int64_t GetRealQpcTimeUs() const;

    // θ(クロックオフセット): SYNC_REQ/RES交換で算出した相手との時計差 (μs)
    // 正の値 = 自分の時計が相手より進んでいる
    int64_t GetClockOffset() const { return _measuredOffset.load(); }
    void SetClockOffset(int64_t offsetUs) { _measuredOffset.store(offsetUs); }

    // 相手のリモート時刻をローカル時刻に変換
    // localTime = remoteTime + θ
    int64_t ConvertRemoteToLocal(int64_t remoteTimeUs) const;

    // パケットタイプ定義（本体側と同じ値）
    static constexpr uint8_t SYNC_REQ = 0x10;
    static constexpr uint8_t SYNC_RES = 0x11;
    static constexpr uint8_t SYNC_DONE = 0x12;

private:
    int64_t _clockOffsetUs;     // 意図的な時計ズレ (テスト用)
    double _driftPpm;
    int64_t _startTimeUs;       // 起動時のQPC時刻（ドリフト計算の基準）
    std::atomic<int64_t> _measuredOffset{0}; // SYNC_REQ/RES で測定したθ

    // WASAPI
    IAudioClient* _pAudioClient = nullptr;
    IAudioClock* _pAudioClock = nullptr;
    uint64_t _wasapiFrequency = 0;
    bool _wasapiReady = false;

    void InitWasapi();
    int64_t GetWasapiTimeUs() const;
};

} // namespace dummy_peer
