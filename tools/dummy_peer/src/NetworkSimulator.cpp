#include "NetworkSimulator.hpp"
#include "RealClock.hpp"

namespace dummy_peer {

NetworkSimulator::NetworkSimulator(int64_t minDelayUs, int64_t maxDelayUs,
                                   double packetLossRate,
                                   int64_t spikeDelayUs, double spikeChance)
    : _minDelayUs(minDelayUs), _maxDelayUs(maxDelayUs),
      _packetLossRate(packetLossRate),
      _spikeDelayUs(spikeDelayUs), _spikeChance(spikeChance),
      _rng(std::random_device{}()),
      _delayDist(minDelayUs, maxDelayUs),
      _lossDist(0.0, 1.0),
      _spikeDist(0.0, 1.0) {}

int64_t NetworkSimulator::GetCurrentTimeUs() const {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
}

bool NetworkSimulator::Enqueue(const std::vector<uint8_t>& data, const std::string& destIp, uint16_t destPort) {
    // パケットロス判定
    if (_packetLossRate > 0.0 && _lossDist(_rng) < _packetLossRate) {
        totalDropped++;
        return false;
    }

    int64_t delay = _delayDist(_rng);

    // スパイク判定
    if (_spikeDelayUs > 0 && _spikeChance > 0.0 && _spikeDist(_rng) < _spikeChance) {
        delay = _spikeDelayUs;
        totalSpiked++;
    }

    int64_t now = GetCurrentTimeUs();
    _queue.push({data, destIp, destPort, now + delay});
    totalSent++;
    return true;
}

std::vector<DelayedPacket> NetworkSimulator::PopReady(int64_t nowUs) {
    std::vector<DelayedPacket> ready;
    while (!_queue.empty() && _queue.top().releaseTimeUs <= nowUs) {
        // const_cast は priority_queue の top() が const 参照を返すための回避策
        ready.push_back(std::move(const_cast<DelayedPacket&>(_queue.top())));
        _queue.pop();
    }
    return ready;
}

} // namespace dummy_peer
