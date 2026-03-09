#pragma once
#include <cstdint>
#include <vector>
#include <chrono>

namespace dummy_peer {

/**
 * SessionNegotiator の 26バイト Ping-Pong パケットに応答するモジュール。
 * 
 * パケットフォーマット (26 bytes):
 *   [0..1]   seq           (uint16_t)  シーケンス番号
 *   [2..9]   timestamp     (uint64_t)  送信時刻 (system_clock, us)
 *   [10..17] echoedTime    (uint64_t)  相手のtimestampをエコーバック
 *   [18..25] processingDelay (uint64_t)  エコー対象受信時刻からの処理遅延(us)
 */
class NegotiationResponder {
public:
    static constexpr int PACKET_SIZE = 26;

    NegotiationResponder();

    /// 受信した26バイトパケットを解析し、応答パケットを生成して返す。
    /// サイズ不足の場合は空vectorを返す。
    std::vector<uint8_t> HandlePacket(const uint8_t* data, int len);

    /// 能動的にパケットを生成する（Client側の初回送信用）。
    std::vector<uint8_t> CreatePacket();

    /// 接続が確立されたか (少なくとも1パケット受信済み)
    bool IsConnected() const { return _connected; }

    /// 統計
    uint64_t GetPacketsReceived() const { return _packetsReceived; }
    uint64_t GetPacketsSent() const { return _packetsSent; }
    double GetCurrentPingMs() const { return _currentPingMs; }
    double GetCurrentJitterMs() const { return _currentJitterMs; }

    /// レポート出力
    void PrintReport() const;

private:
    bool _connected = false;
    uint16_t _seqNum = 0;
    uint64_t _packetsReceived = 0;
    uint64_t _packetsSent = 0;

    // エコーバック用に保持する受信データ
    uint64_t _lastReceivedRemoteTime = 0;
    uint64_t _lastReceiveLocalTime = 0;

    // Ping/Jitter 計測
    double _currentPingMs = 0.0;
    double _currentJitterMs = 0.0;
    double _lastPingMs = 0.0;

    uint64_t GetNowUs() const;
};

} // namespace dummy_peer
