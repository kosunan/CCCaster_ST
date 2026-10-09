#pragma once
#include "SteamCodeSignature.hpp"
// Steam 2017-01-05。PE HIGHLOWを正規化し、関数の文脈と呼出規約を照合する。
namespace cccaster::game_memory::steam_v154 {
inline constexpr uint16_t FpsUpdateRelocations[]{0x4,0xf,0x15,0x1c,0x24,0x2c,0x34,0x3c,0x47,0x4f,0x5b,0x63,0x69,0x73,0x7f};
inline constexpr steam_code::Signature FpsUpdate{0x48b143,0x83,0x114c625606ff8c5dull,FpsUpdateRelocations};
inline constexpr uint16_t InputBeforeRelocations[]{0x4,0x24,0x36};
inline constexpr steam_code::Signature InputBefore{0x4c2f10,0x4c,0xd5016acb12d6689bull,InputBeforeRelocations};
inline constexpr uint16_t InputAfterRelocations[]{0x18,0x2b};
inline constexpr steam_code::Signature InputAfter{0x4c5737,0x3b,0x0323b5a95701b659ull,InputAfterRelocations};
inline constexpr uint16_t ResolutionWindowRelocations[]{0x7,0x1c,0x2c,0x3c,0x44,0x5c,0x64};
inline constexpr steam_code::Signature ResolutionWindow{0x4f3100,0x7f,0x4ff57f7a656b9693ull,ResolutionWindowRelocations};
inline constexpr uint16_t ResolutionRequestRelocations[]{0x20,0x2b,0x33,0x58,0x9c,0xa4,0x112,0x11a,0x148};
inline constexpr steam_code::Signature ResolutionRequest{0x439cc0,0x150,0x163741fbc5aeca71ull,ResolutionRequestRelocations};
inline constexpr uint16_t ResolutionResetRelocations[]{0x1e,0x2a,0x4a,0x50,0x59,0x66,0x77,0x82,0x8f,0x97,0x9c,0xb1,0x100,0x105,0x116,0x11d};
inline constexpr steam_code::Signature ResolutionReset{0x439e60,0x128,0x906e4122ddb62e3full,ResolutionResetRelocations};
inline constexpr uint16_t AspectSelectionRelocations[]{0xd,0x21,0x86};
inline constexpr steam_code::Signature AspectSelection{0x48aa06,0x8a,0xebfabbc8dfbcab8cull,AspectSelectionRelocations};
}
