#pragma once
#include "shared_contracts/GameImageAddress.hpp"
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>

// 版全体の照合後、ゲーム入口を解放する前に適用する。
// WinMain自身の既存mutex判定だけを変更し、OS/Steam APIには触れない。
namespace cccaster::game_memory::steam_multi_instance {
inline constexpr uint32_t SignatureRva = 0x38c22;
inline constexpr uint32_t PatchRva = 0x38c43;
inline constexpr std::array<uint8_t, 84> Original = {
    0x68,0x90,0x39,0x58,0x00,0x6a,0x01,0x6a,0x00,0xa3,0xbc,0xb5,0x5b,0x00,
    0xff,0x15,0x08,0xb1,0x56,0x00,0x8b,0xf0,0xff,0x15,0xd8,0xb0,0x56,0x00,
    0x3d,0xb7,0x00,0x00,0x00,0x75,0x2a,0x6a,0x00,0x68,0x90,0x39,0x58,0x00,
    0xff,0x15,0x4c,0xb2,0x56,0x00,0x85,0xc0,0x74,0x0e,0x50,0xff,0x15,0xfc,
    0xb2,0x56,0x00,0x50,0xff,0x15,0x90,0xb2,0x56,0x00,0x33,0xc0,0x5f,0x5e,
    0x5b,0x8b,0xe5,0x5d,0xc2,0x10,0x00,0x56,0xff,0x15,0xf0,0xb1,0x56,0x00};
inline constexpr std::array<size_t, 9> Relocations = {1,10,16,24,38,44,55,62,80};

inline bool ValidateBytes(const game_build::LoadedImage& image, std::span<const uint8_t> bytes) {
    if (image.base > UINT32_MAX || image.size != 0xf3e000 || bytes.size() != Original.size() ||
        !image.Resolve(SignatureRva, Original.size())) return false;
    auto expected = Original;
    const uint32_t delta = uint32_t(image.base) - 0x400000u;
    for (auto offset : Relocations) {
        uint32_t value;
        std::memcpy(&value, expected.data() + offset, 4);
        value += delta;
        std::memcpy(expected.data() + offset, &value, 4);
    }
    constexpr size_t branch = PatchRva - SignatureRva;
    if (bytes[branch] == 0xeb) expected[branch] = 0xeb;
    return std::memcmp(bytes.data(), expected.data(), expected.size()) == 0;
}

inline bool Validate(const game_build::LoadedImage& image) {
    const auto address = image.Resolve(SignatureRva, Original.size());
    if (!address) return false;
    auto cursor = address;
    const auto end = address + Original.size();
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || uintptr_t(info.AllocationBase) != image.base ||
            (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        const auto protection = info.Protect & 0xff;
        if (protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE &&
            protection != PAGE_EXECUTE_WRITECOPY) return false;
        const auto next = uintptr_t(info.BaseAddress) + info.RegionSize;
        if (next <= cursor) return false;
        cursor = next;
    }
    return ValidateBytes(image, {reinterpret_cast<const uint8_t*>(address), Original.size()});
}

inline bool Apply(const game_build::LoadedImage& image) {
    if (image.base != reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) || !Validate(image))
        return false;
    auto* code = reinterpret_cast<uint8_t*>(image.Resolve(PatchRva));
    if (*code == 0xeb) return true;
    DWORD protection{}, ignored{};
    if (!VirtualProtect(code, 1, PAGE_EXECUTE_READWRITE, &protection)) return false;
    *code = 0xeb; // JNE +0x2a -> JMP +0x2a; ReleaseMutexから通常初期化へ進む。
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), code, 1) != 0;
    const bool restored = VirtualProtect(code, 1, protection, &ignored) != 0;
    if (flushed && restored && Validate(image)) return true;
    DWORD writable{};
    if (!VirtualProtect(code, 1, PAGE_EXECUTE_READWRITE, &writable)) ExitProcess(ERROR_WRITE_FAULT);
    *code = 0x75;
    const bool rollbackFlushed = FlushInstructionCache(GetCurrentProcess(), code, 1) != 0;
    const bool rollbackProtected = VirtualProtect(code, 1, protection, &ignored) != 0;
    if (!rollbackFlushed || !rollbackProtected || !Validate(image)) ExitProcess(ERROR_WRITE_FAULT);
    return false;
}
} // namespace cccaster::game_memory::steam_multi_instance
