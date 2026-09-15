#pragma once
#include "shared_contracts/GameImageAddress.hpp"
#include <windows.h>
#include <cstdint>
#include <cstring>

// Steam 2017-01-05版限定。呼出側はGameBuildによる版・コード全体の照合を先に行う。
// 適用はゲームスレッド限定。ASLRを維持し、状態番号の直書きや初期化関数の直呼びをしない。
namespace cccaster::game_memory::steam_menu {
enum class BootMode { Training, Versus };
inline constexpr uint32_t DispatchPatchRva = 0x852F5;
inline constexpr uint32_t TrainingInitRva = 0x83ED0;
inline constexpr uint32_t VersusInitRva = 0x83F60;
inline constexpr uint32_t ModeKindRva = 0x1CA794;
inline constexpr uint32_t VersusFlagRva = 0x3E9B94;

inline bool Readable(uintptr_t address, size_t size) {
    if (!address || !size || address + size < address) return false;
    while (size) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        const auto end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (end <= address) return false;
        const size_t count = size < end - address ? size : end - address;
        address += count;
        size -= count;
    }
    return true;
}

// 絶対オペランドはワイルドカードにせず、ロード先から算出して照合する。
inline bool MatchBranch(const game_build::LoadedImage& image, uint32_t rva,
                        int32_t relativeCall) {
    const auto address = image.Resolve(rva, 0x29);
    if (!Readable(address, 0x29)) return false;
    uint8_t expected[] = {
        0x0F,0xB6,0x05,0,0,0,0,0x50,0xE8,0,0,0,0,0x5F,
        0xC7,0x05,0,0,0,0,0x14,0,0,0,0xB8,1,0,0,0,
        0xC6,0x05,0,0,0,0,1,0x5E,0x8B,0xE5,0x5D,0xC3};
    const auto player = static_cast<uint32_t>(image.Resolve(0x1C9C97));
    const auto next = static_cast<uint32_t>(image.Resolve(0x1CA9B4, 4));
    const auto reset = static_cast<uint32_t>(image.Resolve(0x1C9C96));
    if (!player || !next || !reset) return false;
    std::memcpy(expected + 3, &player, 4);
    std::memcpy(expected + 9, &relativeCall, 4);
    std::memcpy(expected + 16, &next, 4);
    std::memcpy(expected + 31, &reset, 4);
    return std::memcmp(reinterpret_cast<void*>(address), expected, sizeof(expected)) == 0;
}

inline bool MatchesStartup(const game_build::LoadedImage& image) {
    const auto address = image.Resolve(DispatchPatchRva, 4);
    if (!Readable(address, 4)) return false;
    const auto* code = reinterpret_cast<const uint8_t*>(address);
    // 続くjneを維持し、適用済みの場合も他の全署名を再照合する。
    if (code[2] != 0x75 || code[3] != 0x29) return false;
    if (!((code[0] == 0x85 && code[1] == 0xC0) ||
          (code[0] == 0xEB && (code[1] == 0x3B || code[1] == 0x74)))) return false;
    return MatchBranch(image, 0x85332, -0x146F) &&
           MatchBranch(image, 0x8536B, -0x1418);
}

inline bool Apply(const game_build::LoadedImage& image, BootMode mode) {
    if (image.base != reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) ||
        !MatchesStartup(image)) return false;
    auto* code = reinterpret_cast<uint8_t*>(image.Resolve(DispatchPatchRva, 2));
    const uint8_t patch[] = {0xEB, static_cast<uint8_t>(mode == BootMode::Training ? 0x3B : 0x74)};
    if (std::memcmp(code, patch, 2) == 0) return true;
    // 既に別モードへ設定した呼出しは拒否する。
    if (code[0] != 0x85 || code[1] != 0xC0) return false;
    DWORD protection{}, ignored{};
    if (!VirtualProtect(code, 2, PAGE_EXECUTE_READWRITE, &protection)) return false;
    std::memcpy(code, patch, 2);
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), code, 2) != 0;
    const bool restored = VirtualProtect(code, 2, protection, &ignored) != 0;
    if (flushed && restored && std::memcmp(code, patch, 2) == 0) return true;
    DWORD writable{};
    if (!VirtualProtect(code, 2, PAGE_EXECUTE_READWRITE, &writable)) ExitProcess(ERROR_WRITE_FAULT);
    code[0] = 0x85;
    code[1] = 0xC0;
    const bool rollbackFlushed = FlushInstructionCache(GetCurrentProcess(), code, 2) != 0;
    const bool rollbackProtected = VirtualProtect(code, 2, protection, &ignored) != 0;
    if (!rollbackFlushed || !rollbackProtected) ExitProcess(ERROR_WRITE_FAULT);
    return false;
}
} // namespace cccaster::game_memory::steam_menu
