#pragma once
#include <array>
#include <cstdint>
namespace cccaster::spectator {
// TCP観戦版1、MBAACC Steam 2017-01-05、対戦通信拡張5。
inline constexpr std::array<uint32_t, 4> Hello{0x53504343, 1, 0x20170105, 5};
}
