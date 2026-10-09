#pragma once
#include <windows.h>
#include <cstring>
#include <cstdlib>
#include "shared_contracts/GameBuild.hpp"
#include "core_dll/mbaa_mem/GameRuntime.hpp"
#include "core_dll/common/Platform.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/timing/FrameTiming.hpp"
#include "core_dll/timing/WasapiClock.hpp"
#include "core_dll/timing/UpdateCadence.hpp"
#include "core_dll/timing/SpinProbe.hpp"
#include "core_dll/timing/FramePipeline.hpp"

namespace cccaster::game_interface::game_release_gate {
// Steam: 48B0D6 -> 50F170 -> D3D Present、48B0E4でCS解放。
// 旧CS解放を必ず実行した後に待つ。ゲーム時計更新・WT加算は削除しない。
inline bool installed = false;
inline unsigned char replacement[6]{};
using Leave = void (WINAPI *)(CRITICAL_SECTION *);
inline Leave originalLeave = nullptr;
inline uintptr_t Site = 0, PresentReturn = 0;
inline unsigned char Expected[] = {0xff,0x15,0,0,0,0};

inline void Release() {
    using Timing = core::timer::FrameTiming;
    // Normal frames have already waited before input preparation. This hook now
    // records the native entry only; legacy pacing experiments retain their gate.
    diagnostics::FramePipeline::NativeEntered();
    if (!Timing::releaseDueTicks) return;
    const auto due = Timing::releaseDueTicks;
    const auto frame = Timing::releaseFrame;
    // TLSの初期取得は最終締切前。診断中だけ最終解放時刻も従来標本へ反映する。
    auto *probe = diagnostics::SpinProbe::Enabled() && diagnostics::SpinProbe::pending
        ? &diagnostics::SpinProbe::sample : nullptr;
    Timing::releaseDueTicks = 0;
    core::timer::WasapiClock::WaitForRelease(due);
    const auto actual = platform::RealMonotonicTicks();
    // 除算・統計標本作成は次Present冒頭へ。ここでは生の実測値だけ保持。
    Timing::releasedTicks = actual;
    Timing::releaseFrame = frame;
    if (probe) probe->gameReturn = actual;
    auto &cadence = diagnostics::UpdateCadence::Get();
    if (cadence.Armed()) cadence.Capture(actual);
}
// 既存push edxのstdcall引数1個をそのまま消費する。ゲーム側の4/8byte整列にも対応。
__attribute__((stdcall, force_align_arg_pointer, noinline))
inline void LeaveAndRelease(CRITICAL_SECTION *section) {
    originalLeave(section);
    Release();
}
inline bool Write(const unsigned char *bytes) {
    DWORD protection = 0, unused = 0;
    auto *site = reinterpret_cast<void *>(Site);
    if (!VirtualProtect(site, sizeof(Expected), PAGE_EXECUTE_READWRITE, &protection)) return false;
    std::memcpy(site, bytes, sizeof(Expected));
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, sizeof(Expected)) != FALSE;
    const bool restored = VirtualProtect(site, sizeof(Expected), protection, &unused) != FALSE;
    if (!flushed || !restored) {
        domain::session::DebugLog("[GameReleaseGate] code protection/cache failure");
        ExitProcess(ERROR_DLL_INIT_FAILED);
    }
    return true;
}
inline void Install() {
    if (installed || std::getenv("CCCASTER_DISABLE_GAME_RELEASE_GATE")) return;
    using Runtime = game_memory::GameRuntime;
    if (!Runtime::IsSteam()) return;
    Site = Runtime::Preferred(0x48b0e4, 6);
    PresentReturn = Runtime::Preferred(0x50f2ec);
    const auto leaveSlot = uint32_t(Runtime::Preferred(0x56b0ec, 4));
    std::memcpy(Expected + 2, &leaveSlot, 4);
    // 分岐先まで照合し、類似版や他パッチの上へ書かない。
    unsigned char context[] = {0x0f,0x57,0xc9,0xe8,0x95,0x40,0x08,0,
        0xa1,0,0,0,0,0x83,0xc0,0x60,0x50,0xff,0x15,0,0,0,0};
    const auto graphics = uint32_t(Runtime::Preferred(0x7cdabc,4));
    std::memcpy(context + 9, &graphics, 4);
    std::memcpy(context + 19, &leaveSlot, 4);
    constexpr unsigned char present[] = {0x8b,0x47,0x04,0x6a,0,0x6a,0,0x6a,0,
        0x8b,0x08,0x6a,0,0x50,0xff,0x51,0x44,0x8b,0x77,0x78};
    if (std::memcmp(reinterpret_cast<const void *>(Runtime::Preferred(0x48b0d3)),context,sizeof(context)) ||
        std::memcmp(reinterpret_cast<const void *>(Runtime::Preferred(0x50f2db)),present,sizeof(present))) {
        domain::session::DebugLog("[GameReleaseGate] signature mismatch; Present exit retained");
        return;
    }
    replacement[0] = 0xe8; replacement[5] = 0x90;
    originalLeave = *reinterpret_cast<Leave *>(leaveSlot);
    const uint32_t displacement = uint32_t(reinterpret_cast<uintptr_t>(&LeaveAndRelease) - (Site + 5));
    std::memcpy(replacement + 1, &displacement, sizeof(displacement));
    installed = Write(replacement);
    domain::session::DebugLog("[GameReleaseGate] installed=%d site=%08X after=LeaveCriticalSection", int(installed), unsigned(Site));
}
inline void Remove() {
    if (!installed) return;
    if (std::memcmp(reinterpret_cast<const void *>(Site),replacement,sizeof(replacement)) || !Write(Expected)) {
        domain::session::DebugLog("[GameReleaseGate] restore failed");
        ExitProcess(ERROR_DLL_INIT_FAILED);
    }
    installed = false;
}
}
