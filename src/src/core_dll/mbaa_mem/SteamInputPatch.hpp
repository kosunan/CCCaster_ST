#pragma once
#include "shared_contracts/GameBuild.hpp"
#include "shared_contracts/GameImageAddress.hpp"
#include <windows.h>
#include <array>
#include <cstring>

namespace cccaster::game_memory::steam_input {
// Steam 2017-01-05専用。呼出側で全コードの版照合を済ませ、ゲームスレッドが
// 実行を始める前に適用する。実行中のポーリングと並行して変更してはいけない。
// 本ヘッダー単体ではSteamランタイムの有効化・入力注入は行わない。
inline constexpr uint32_t SourcePointerRva = 0x1ca9b8;
inline constexpr uint32_t DirectionOffset = 0x18;
inline constexpr uint32_t ButtonsOffset = 0x24;
inline constexpr uint32_t PlayerStride = 0x14;
inline constexpr std::array<uint32_t, 2> PatchRvas{{0x76216, 0x8f9a9}};
inline constexpr std::array<uint8_t, 21> Original{{
    0x0f,0x57,0xc0, 0xf3,0x0f,0x7f,0x06,
    0xc7,0x46,0x10,0,0,0,0, 0x56,0x8b,0x01,0x57,0xff,0x50,0x14}};
// ESIは入力元の各プレイヤー先頭。方向[ESI]とボタン[ESI+0xc]を残す。
// 物理入力コピーのcallとその引数pushをまとめて取り除き、スタックを維持する。
inline constexpr std::array<uint8_t, 21> Replacement{{
    0x0f,0x57,0xc0, 0x33,0xc0, 0x89,0x46,0x04,
    0x89,0x46,0x08, 0x89,0x46,0x10,
    0x90,0x90,0x90,0x90,0x90,0x90,0x90}};
enum class Result { Applied, Rejected, FatalProtectionFailure };

inline bool Readable(const game_build::LoadedImage& image, uintptr_t address, size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
        info.AllocationBase != reinterpret_cast<void*>(image.base)) return false;
    const auto start = reinterpret_cast<uintptr_t>(info.BaseAddress);
    return address >= start && size <= info.RegionSize &&
        address - start <= info.RegionSize - size;
}

inline Result Apply(const game_build::LoadedImage& image, game_build::Edition verifiedEdition) {
    if (verifiedEdition != game_build::Edition::Steam20170105 ||
        image.base != reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))) return Result::Rejected;
    std::array<uint8_t*, 2> sites{};
    std::array<DWORD, 2> protection{};
    int already = 0;
    for (size_t i = 0; i < sites.size(); ++i) {
        const auto context = image.Resolve(PatchRvas[i] - 6, 36);
        if (!context || !Readable(image, context, 36)) return Result::Rejected;
        auto* code = reinterpret_cast<uint8_t*>(context);
        uint32_t pointer{};
        std::memcpy(&pointer, code + 2, sizeof(pointer));
        constexpr uint8_t suffix[] = {0x47,0x83,0xc6,0x14,0x83,0xff,0x04,0x7c,0xdc};
        if (code[0] != 0x8b || code[1] != 0x0d ||
            pointer != image.Resolve(0x3d4df4, 4) ||
            std::memcmp(code + 27, suffix, sizeof(suffix))) return Result::Rejected;
        sites[i] = code + 6;
        if (!std::memcmp(sites[i], Replacement.data(), Replacement.size())) ++already;
        else if (std::memcmp(sites[i], Original.data(), Original.size())) return Result::Rejected;
    }
    if (already == 2) return Result::Applied;
    if (already) return Result::Rejected; // 部分適用された状態からの継続はしない。
    for (size_t i = 0; i < sites.size(); ++i) {
        if (!VirtualProtect(sites[i], Original.size(), PAGE_EXECUTE_READWRITE, &protection[i])) {
            bool restored = true;
            for (size_t j = 0; j < i; ++j) {
                DWORD ignored{};
                if (!VirtualProtect(sites[j], Original.size(), protection[j], &ignored)) restored = false;
            }
            return restored ? Result::Rejected : Result::FatalProtectionFailure;
        }
    }
    for (auto* site : sites) std::memcpy(site, Replacement.data(), Replacement.size());
    bool flushed = true;
    for (auto* site : sites)
        if (!FlushInstructionCache(GetCurrentProcess(), site, Replacement.size())) flushed = false;
    if (!flushed) {
        for (auto* site : sites) std::memcpy(site, Original.data(), Original.size());
        for (auto* site : sites)
            if (!FlushInstructionCache(GetCurrentProcess(), site, Original.size()))
                return Result::FatalProtectionFailure;
    }
    bool restored = true;
    for (size_t i = 0; i < sites.size(); ++i) {
        DWORD ignored{};
        if (!VirtualProtect(sites[i], Original.size(), protection[i], &ignored)) restored = false;
    }
    // Fatalではゲームを再開してはいけない。呼出側の起動失敗処理へ伝える。
    if (!restored) return Result::FatalProtectionFailure;
    return flushed ? Result::Applied : Result::Rejected;
}
}
