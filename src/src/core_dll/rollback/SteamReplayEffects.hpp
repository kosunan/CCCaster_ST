#pragma once

#include "shared_contracts/GameBuild.hpp"
#include "shared_contracts/GameImageAddress.hpp"
#include <windows.h>
#include <MinHook.h>
#include <array>
#include <cstdint>
#include <cstring>

namespace cccaster::sync::steam_effects {
using game_build::LoadedImage;

// 呼出元がゲーム全体の版/コード照合を済ませていること。この部品は局所署名も照合する。
// 初期化中、ゲームスレッドでフック対象が動く前に使用する。
inline bool ReadableCode(const LoadedImage &image, uintptr_t address, size_t bytes) {
    const auto end = address + bytes;
    if (!address || end < address) return false;
    while (address < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<const void *>(address), &mbi, sizeof(mbi)) ||
            mbi.State != MEM_COMMIT || uintptr_t(mbi.AllocationBase) != image.base ||
            (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const auto protection = mbi.Protect & 0xff;
        if (protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE &&
            protection != PAGE_EXECUTE_WRITECOPY) return false;
        const auto next = uintptr_t(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= address) return false;
        address = next;
    }
    return true;
}

template<size_t N, size_t R>
inline bool Match(const LoadedImage &image, uint32_t rva,
                  std::array<uint8_t, N> expected, const std::array<size_t, R> &relocations) {
    if (image.base > UINT32_MAX || image.size != 0xf3e000) return false;
    const auto address = image.Resolve(rva, N);
    if (!ReadableCode(image, address, N)) return false;
    // PE HIGHLOW と同じ32bit加算。相対 CALL/Jcc は差分を加えない。
    const uint32_t delta = uint32_t(image.base) - 0x400000u;
    for (const auto offset : relocations) {
        if (offset > N || N - offset < sizeof(uint32_t)) return false;
        uint32_t value;
        std::memcpy(&value, expected.data() + offset, sizeof(value));
        value += delta;
        std::memcpy(expected.data() + offset, &value, sizeof(value));
    }
    return !std::memcmp(reinterpret_cast<const void *>(address), expected.data(), N);
}

inline bool ValidateUpdate(const LoadedImage &image) {
    // 0x521950..0x5219a4 全体。抑止時にも通常の末尾クリア処理を通す。
    constexpr std::array<uint8_t, 85> code = {
        0x56,0x57,0x33,0xff,0x33,0xf6,0x80,0xbe,0xb0,0x47,0x7d,0x00,0x01,0x75,0x11,
        0x8b,0x0c,0xb5,0x90,0x2d,0x7d,0x00,0x85,0xc9,0x74,0x05,0xe8,0xf1,0x52,0xf4,
        0xff,0x47,0x46,0x81,0xfe,0xdc,0x05,0x00,0x00,0x7c,0xdd,0x85,0xff,0x74,0x11,
        0xb9,0x77,0x01,0x00,0x00,0xbe,0xb0,0x47,0x7d,0x00,0xbf,0x50,0x27,0x7d,0x00,
        0xf3,0xa5,0x68,0xdc,0x05,0x00,0x00,0x6a,0x00,0x68,0xb0,0x47,0x7d,0x00,0xe8,
        0x11,0x47,0x01,0x00,0x83,0xc4,0x0c,0x5f,0x5e,0xc3};
    return Match(image, 0x121950, code, std::array<size_t, 5>{8,18,51,56,70});
}

inline bool ValidateSoundObject(const LoadedImage &image) {
    // thiscall再生入口: ECX=this、+4=バッファ配列。停止処理も+0x10個を同配列で走査。
    constexpr std::array<uint8_t, 84> play = {
        0x56,0x57,0x8b,0xf9,0x8b,0x47,0x04,0x8b,0x30,0x85,0xf6,0x75,0x08,0x5f,
        0xb8,0xf0,0x01,0x04,0x80,0x5e,0xc3,0x8b,0xce,0xe8,0xf4,0x00,0x00,0x00,
        0x8b,0x07,0x8b,0xcf,0xff,0x50,0x04,0xc7,0x47,0x08,0x00,0x00,0x00,0x00,
        0x8b,0x06,0x6a,0x00,0x56,0xff,0x50,0x34,0xff,0x77,0x18,0x8b,0x06,0x56,
        0xff,0x50,0x3c,0xff,0x77,0x14,0x8b,0x06,0x56,0xff,0x50,0x40,0xff,0x77,
        0x1c,0x8b,0x06,0x6a,0x00,0x6a,0x00,0x56,0xff,0x50,0x30,0x5f,0x5e,0xc3};
    constexpr std::array<uint8_t, 55> stop = {
        0x55,0x8b,0xec,0x51,0x53,0x56,0x8b,0xd9,0x33,0xf6,0x57,0x33,0xff,0x39,
        0x73,0x10,0x7e,0x25,0x8b,0x43,0x04,0x8d,0x55,0xfc,0xc7,0x45,0xfc,0x00,
        0x00,0x00,0x00,0x52,0x8b,0x04,0xb0,0x50,0x8b,0x08,0xff,0x51,0x24,0x8b,
        0x4d,0xfc,0x46,0x83,0xe1,0x01,0x0b,0xf9,0x3b,0x73,0x10,0x7c,0xdb};
    return Match(image, 0x66c60, play, std::array<size_t, 0>{}) &&
           Match(image, 0x66ce0, stop, std::array<size_t, 0>{});
}

struct Sites {
    void *update = nullptr;
    void *dispatch = nullptr;
    uintptr_t skip = 0;
    uintptr_t objects = 0;
    uintptr_t flags = 0;
};

inline bool Resolve(game_build::Edition edition, const LoadedImage &image, Sites &out) {
    out = {};
    if (edition != game_build::Edition::Steam20170105 || !ValidateUpdate(image) ||
        !ValidateSoundObject(image)) return false;
    out = {reinterpret_cast<void *>(image.Resolve(0x121950)),
           reinterpret_cast<void *>(image.Resolve(0x12195f)), image.Resolve(0x121970),
           image.Resolve(0x3d2d90, 1500 * 4), image.Resolve(0x3d47b0, 1500)};
    return out.update && out.dispatch && out.skip && out.objects && out.flags;
}

// 既存 cccaster_sfx_hook を渡せる。ESI=sound、pushalでEDI=件数を保存し、
// 元トランポリンの mov ecx,[esi*4+objects] を実行する。skip は件数加算の後へ。
// original/skip はフック稼働期間中有効な保存先。再入/再インストールは呼出元で防ぐ。
inline bool Install(game_build::Edition edition, const LoadedImage &image, void *detour,
                    void **original, uintptr_t *skip) {
    Sites sites;
    if (!detour || !original || !skip || !Resolve(edition, image, sites)) return false;
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return false;
    if (MH_CreateHook(sites.dispatch, detour, original) != MH_OK) return false;
    const auto previousSkip = *skip;
    *skip = sites.skip;
    if (MH_EnableHook(sites.dispatch) == MH_OK) return true;
    MH_RemoveHook(sites.dispatch);
    *original = nullptr;
    *skip = previousSkip;
    return false;
}

// 診断用。この入口だけでは他のインライン乱数更新関数を数えられない。
inline void *ResolveRngTrace(game_build::Edition edition, const LoadedImage &image) {
    constexpr std::array<uint8_t, 75> code = {
        0x8b,0x0d,0xb8,0xb2,0x5c,0x00,0xb8,0x01,0x00,0x00,0x00,0x41,0x83,0xf9,
        0x38,0x0f,0x4d,0xc8,0x89,0x0d,0xb8,0xb2,0x5c,0x00,0x8d,0x51,0x15,0x83,
        0xf9,0x22,0x7e,0x03,0x8d,0x51,0xde,0x8b,0x04,0x8d,0xbc,0xb2,0x5c,0x00,
        0x2b,0x04,0x95,0xbc,0xb2,0x5c,0x00,0x79,0x05,0x05,0xff,0xff,0xff,0x7f,
        0xff,0x05,0xc0,0xa9,0x5c,0x00,0x89,0x04,0x8d,0xbc,0xb2,0x5c,0x00,0xa3,
        0xbc,0xa9,0x5c,0x00,0xc3};
    if (edition != game_build::Edition::Steam20170105 ||
        !Match(image, 0x79990, code, std::array<size_t, 7>{2,20,38,45,58,65,70})) return nullptr;
    return reinterpret_cast<void *>(image.Resolve(0x79990));
}
} // namespace cccaster::sync::steam_effects
