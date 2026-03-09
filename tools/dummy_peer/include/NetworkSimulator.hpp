#pragma once
#include <cstdint>
#include <random>
#include <vector>
#include <queue>
#include <functional>
#include <chrono>

namespace dummy_peer {

// 遅延付きパケットキュー（送信を意図的に遅延させる）
struct DelayedPacket {
    std::vector<uint8_t> data;
    std::string destIp;
    uint16_t destPort;
    int64_t releaseTimeUs; // この時刻以降に実際に送信する
};

class NetworkSimulator {
public:
    NetworkSimulator(int64_t minDelayUs, int64_t maxDelayUs,
                     double packetLossRate,
                     int64_t spikeDelayUs, double spikeChance);

    // パケットを遅延キューに投入する。ロスの場合はドロップされる。
    // 戻り値: true=キューに入った, false=ロスでドロップされた
    bool Enqueue(const std::vector<uint8_t>& data, const std::string& destIp, uint16_t destPort);

    // 現在時刻で送信可能になったパケットを全て取り出す
    std::vector<DelayedPacket> PopReady(int64_t nowUs);

    // 統計
    uint64_t totalSent = 0;
    uint64_t totalDropped = 0;
    uint64_t totalSpiked = 0;

private:
    int64_t _minDelayUs;
    int64_t _maxDelayUs;
    double _packetLossRate;
    int64_t _spikeDelayUs;
    double _spikeChance;

    std::mt19937 _rng;
    std::uniform_int_distribution<int64_t> _delayDist;
    std::uniform_real_distribution<double> _lossDist;
    std::uniform_real_distribution<double> _spikeDist;

    // priority_queue: releaseTimeUsが早いものを先に取り出す → パケット順序逆転が自然に発生
    struct CompareByReleaseTime {
        bool operator()(const DelayedPacket& a, const DelayedPacket& b) const {
            return a.releaseTimeUs > b.releaseTimeUs;
        }
    };
    std::priority_queue<DelayedPacket, std::vector<DelayedPacket>, CompareByReleaseTime> _queue;

    int64_t GetCurrentTimeUs() const;
};

} // namespace dummy_peer
