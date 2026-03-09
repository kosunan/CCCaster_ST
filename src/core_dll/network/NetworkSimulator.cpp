#include "core_dll/network/NetworkSimulator.hpp"
#include <algorithm>

namespace cccaster::network {

NetworkSimulator& NetworkSimulator::Instance() {
    static NetworkSimulator instance;
    return instance;
}

void NetworkSimulator::Enable(uint32_t minDelayMs, uint32_t maxDelayMs, uint32_t lossPercent) {
    std::lock_guard<std::mutex> lock(_mtx);
    _minDelayMs  = minDelayMs;
    _maxDelayMs  = std::max(minDelayMs, maxDelayMs); // min <= max を保証
    _lossPercent = std::min(lossPercent, uint32_t(100));
    _enabled.store(true, std::memory_order_release);
}

bool NetworkSimulator::IsEnabled() const {
    return _enabled.load(std::memory_order_acquire);
}

uint32_t NetworkSimulator::GetRandomDelayMs() {
    std::lock_guard<std::mutex> lock(_mtx);
    if (_minDelayMs == _maxDelayMs) return _minDelayMs;
    std::uniform_int_distribution<uint32_t> dist(_minDelayMs, _maxDelayMs);
    return dist(_rng);
}

bool NetworkSimulator::ShouldDrop() {
    if (_lossPercent == 0) return false;
    std::lock_guard<std::mutex> lock(_mtx);
    std::uniform_int_distribution<uint32_t> dist(0, 99);
    return dist(_rng) < _lossPercent;
}

} // namespace cccaster::network
