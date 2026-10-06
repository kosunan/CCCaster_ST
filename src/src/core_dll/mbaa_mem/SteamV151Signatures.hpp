#pragma once
#include "SteamCodeSignature.hpp"
// Steam 2017-01-05: PE HIGHLOW正規化。検証済みEXEの命令から生成。
namespace cccaster::game_memory::steam_v151 {
inline constexpr uint16_t ReplayInitRelocations[]{0x5,0xf,0x1e,0x2f,0x38,0x41,0x47,0x51};
inline constexpr steam_code::Signature ReplayInit{0x4840e0,0x5d,0x4176e83cfd8472f8ull,ReplayInitRelocations};
inline constexpr uint16_t ReplayEntryRelocations[]{0x3};
inline constexpr steam_code::Signature ReplayEntry{0x48542c,0x1d,0x16f488c4429e4940ull,ReplayEntryRelocations};
inline constexpr uint16_t ReplayReserveRelocations[]{0x2,0x9,0x13};
inline constexpr steam_code::Signature ReplayReserve{0x48b740,0x1c,0x61ea51f43d3e3e76ull,ReplayReserveRelocations};
inline constexpr uint16_t SweepStepRelocations[]{0x2};
inline constexpr steam_code::Signature SweepStep{0x4b8ed0,0x32,0xc23aaebd91e94ab3ull,SweepStepRelocations};
inline constexpr uint16_t VerifyCollisionRelocations[]{0xa,0xc0,0xc5};
inline constexpr steam_code::Signature VerifyCollision{0x4c7360,0x227,0xf69afd736995e9c1ull,VerifyCollisionRelocations};
}
