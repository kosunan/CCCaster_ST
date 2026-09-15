#pragma once
#include "GameRuntime.hpp"
#include "SteamMenuPatch.hpp"
#include "core_dll/common/DebugLog.hpp"

namespace cccaster::game_memory::startup {
inline bool replayPatched = false;
inline bool MatchesMenuCode() { return steam_menu::MatchesStartup(GameRuntime::Image()); }
inline bool Apply(uint8_t displacement) {
    if (displacement != 0x22 && displacement != 0x3f) return false;
    return steam_menu::Apply(GameRuntime::Image(), displacement == 0x22 ?
        steam_menu::BootMode::Training : steam_menu::BootMode::Versus);
}
// Steam 48542C: native ReplayVS初期化、リプレイ一覧生成。ECX/EAX/スタックを維持。
inline bool SetReplayEntry(bool enable) {
    if (replayPatched == enable) return true;
    const auto image = GameRuntime::Image();
    auto *site = reinterpret_cast<uint8_t *>(image.Resolve(0x852f5, 5));
    auto *target = reinterpret_cast<const uint8_t *>(image.Resolve(0x8542c, 29));
    uint8_t destination[] = {0x0f,0xb6,0x05,0,0,0,0,0x50,0xe8,0xa7,0xec,0xff,0xff,
        0xe8,0x02,0x63,0,0,0x5f,0xb8,1,0,0,0,0x5e,0x8b,0xe5,0x5d,0xc3};
    const auto player = uint32_t(image.Resolve(0x1c9c97));
    std::memcpy(destination + 3, &player, 4);
    constexpr uint8_t original[] = {0x85,0xc0,0x75,0x29,0x0f};
    constexpr uint8_t replacement[] = {0xe9,0x32,0x01,0,0};
    if (!steam_menu::Readable(uintptr_t(site),5) || !steam_menu::Readable(uintptr_t(target),29) ||
        std::memcmp(site,enable ? original : replacement,5) ||
        std::memcmp(target,destination,29)) return false;
    DWORD protection{}, ignored{};
    if (!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&protection)) return false;
    std::memcpy(site,enable ? replacement : original,5);
    if (!FlushInstructionCache(GetCurrentProcess(),site,5) ||
        !VirtualProtect(site,5,protection,&ignored)) ExitProcess(ERROR_WRITE_FAULT);
    replayPatched = enable;
    return true;
}
// Steam 4851B8: state3の暗転加算だけを1.0 (XMM2)へ。状態遷移・解放は維持。
inline bool fadePatched = false;
inline bool SetBootFade(bool enable) {
    if (fadePatched == enable) return true;
    auto *site = reinterpret_cast<uint8_t *>(GameRuntime::Preferred(0x4851b8,23));
    uint8_t expected[] = {0xf3,0x0f,0x10,0x86,0xbc,0,0,0,0xf3,0x0f,0x58,0xc1,
        0x0f,0x2f,0xc2,0xf3,0x0f,0x11,0x86,0xbc,0,0,0};
    if (!enable) expected[11] = 0xc2;
    if (!site || std::memcmp(site,expected,sizeof(expected))) return false;
    DWORD protection{}, ignored{};
    if (!VirtualProtect(site+11,1,PAGE_EXECUTE_READWRITE,&protection)) return false;
    site[11] = enable ? 0xc2 : 0xc1;
    if (!FlushInstructionCache(GetCurrentProcess(),site+11,1) ||
        !VirtualProtect(site+11,1,protection,&ignored)) ExitProcess(ERROR_WRITE_FAULT);
    fadePatched = enable;
    return true;
}
}
