#include "InputGenerator.hpp"
#include "RealClock.hpp"

namespace dummy_peer {

InputGenerator::InputGenerator()
    : _rng(std::random_device{}()),
      _dist(0, 0x0FFF) {} // 12bit: 方向4bit + ボタン8bit

uint16_t InputGenerator::Generate() {
    return _dist(_rng);
}

} // namespace dummy_peer
