#pragma once
#include <windows.h>
#include <d3d9.h>
#include <cstring>
#include <cstdlib>
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/mbaa_mem/GameRuntime.hpp"

namespace cccaster::game_interface::scene_pair_merge {
// このゲーム版の連続EndScene/BeginSceneだけを対象にする。
// ドロー・転送・状態設定やシーン外の区間は移動しない。
inline uintptr_t endCaller = 0, beginCaller = 0;
inline uintptr_t endAfterDraw = 0, beginAfterDraw = 0;
inline thread_local uintptr_t expectedBegin = 0;
inline thread_local IDirect3DDevice9 *active = nullptr, *pending = nullptr;
inline thread_local unsigned merged = 0;
inline void Initialize() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    if (std::getenv("CCCASTER_DISABLE_SCENE_MERGE")) {
        domain::session::DebugLog("[SceneMerge] disabled by environment");
        return;
    }
    if (game_memory::GameRuntime::Edition() != game_build::Edition::Steam20170105) return;
    const auto image = game_memory::GameRuntime::Image();
    const auto match = [&](uint32_t rva, const auto &bytes) {
        const auto address = image.Resolve(rva, sizeof(bytes));
        return address && !std::memcmp(reinterpret_cast<const void *>(address), bytes, sizeof(bytes));
    };
    // Steam 50F96B..50F985: 同じEDIのEnd/Begin間はvtable読取りだけ。
    constexpr unsigned char adjacent[] = {0x8b,0x06,0x8b,0x78,0x04,0x57,0x89,0x7d,0xfc,0x8b,0x07,0xff,0x90,0xa8,0x00,0x00,0x00,0x8b,0x07,0x57,0xff,0x90,0xa4,0x00,0x00,0x00};
    if (!match(0x10f96b, adjacent)) {
        domain::session::DebugLog("[SceneMerge] Steam signature mismatch; normal End/Begin retained");
        return;
    }
    endCaller = image.Resolve(0x10f97c);
    beginCaller = image.Resolve(0x10f985);
    // 50FA42..50FAC8: CPUバッファの循環・コピーのみ。途中に描画/API/分岐なし。
    constexpr unsigned char afterDraw[] = {0x8b,0x07,0x57,0xff,0x90,0xa8,0x00,0x00,0x00,0x8b,0x46,0x54,0x8b,0x4e,0x04,0x89,0x46,0x58,0x41,0xf3,0x0f,0x6f,0x46,0x5c,0x33,0xc0,0x83,0xf9,0x02,0x57,0x0f,0x4c,0xc1,0x89,0x46,0x04,0x8b,0xc8,0x8b,0x44,0x8e,0x0c,0x41,0x89,0x46,0x14,0x8d,0x04,0xcd,0x00,0x00,0x00,0x00,0x2b,0xc1,0xf3,0x0f,0x7f,0x04,0x86,0x8d,0x0c,0x86,0xf3,0x0f,0x7e,0x46,0x6c,0x66,0x0f,0xd6,0x41,0x10,0x8b,0x46,0x74,0x89,0x41,0x18,0x8b,0x4e,0x04,0x41,0x8d,0x04,0xcd,0x00,0x00,0x00,0x00,0x2b,0xc1,0x8d,0x04,0x86,0x89,0x46,0x54,0x8b,0x46,0x14,0xc7,0x00,0x00,0x00,0x00,0x00,0xc7,0x40,0x08,0x00,0x00,0x00,0x00,0x8b,0x46,0x54,0x8b,0x4e,0x14,0x8b,0x40,0x08,0x89,0x41,0x10,0x8b,0x07,0xff,0x90,0xa4,0x00,0x00,0x00};
    if (match(0x10fa42, afterDraw)) {
        endAfterDraw = image.Resolve(0x10fa4b);
        beginAfterDraw = image.Resolve(0x10fac8);
    }
    domain::session::DebugLog("[SceneMerge] verified adjacent pair end=%08X begin=%08X",
        unsigned(endCaller), unsigned(beginCaller));
    domain::session::DebugLog("[SceneMerge] after-draw pair end=%08X begin=%08X",
        unsigned(endAfterDraw), unsigned(beginAfterDraw));
}
inline bool DeferEnd(IDirect3DDevice9 *device, uintptr_t caller) {
    if (active != device || pending) return false;
    if (endCaller && caller == endCaller) expectedBegin = beginCaller;
    else if (endAfterDraw && caller == endAfterDraw) expectedBegin = beginAfterDraw;
    else return false;
    pending = device;
    return true;
}
inline bool ConsumeBegin(IDirect3DDevice9 *device, uintptr_t caller) {
    if (!pending || pending != device || caller != expectedBegin) return false;
    pending = nullptr;
    expectedBegin = 0;
    ++merged;
    return true;
}
inline void Reset() { active = pending = nullptr; expectedBegin = 0; }
} // namespace cccaster::game_interface::scene_pair_merge
