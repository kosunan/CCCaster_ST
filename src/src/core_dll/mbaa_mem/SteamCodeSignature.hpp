#pragma once
#include "GameRuntime.hpp"
#include <cstdint>
#include <cstring>
#include <span>
namespace cccaster::game_memory::steam_code {
struct Signature { uint32_t address, size; uint64_t hash; std::span<const uint16_t> relocations; };
// PE HIGHLOW再配置だけをpreferred VAへ正規化し、他の命令差異は拒否する。
inline uint64_t NormalizedHash(std::span<const uint8_t> bytes, uint32_t delta, std::span<const uint16_t> relocations) {
    uint64_t hash=14695981039346656037ull;
    size_t relocation=0;
    for(size_t i=0;i<bytes.size();) {
        if(relocation<relocations.size() && i==relocations[relocation]) {
            if(bytes.size()-i<4) return 0;
            uint32_t value; std::memcpy(&value,bytes.data()+i,4); value-=delta;
            for(unsigned n=0;n<4;++n) hash=(hash^uint8_t(value>>(8*n)))*1099511628211ull;
            i+=4; ++relocation;
        } else hash=(hash^bytes[i++])*1099511628211ull;
    }
    return relocation==relocations.size()?hash:0;
}
inline bool Matches(const Signature& signature) {
    if(!GameRuntime::IsSteam()) return false;
    const auto address=GameRuntime::Preferred(signature.address,signature.size);
    return address && NormalizedHash({reinterpret_cast<const uint8_t*>(address),signature.size},
        uint32_t(GameRuntime::Image().base-0x400000),signature.relocations)==signature.hash;
}
}
