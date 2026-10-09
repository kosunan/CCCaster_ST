#include "core_dll/mbaa_mem/NativeFpsCounter.hpp"
#include "core_dll/mbaa_mem/GameBuildGuard.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/mbaa_mem/SteamV154Signatures.hpp"
#include <MinHook.h>
using cccaster::game_memory::GameRuntime;
#include <cstdint>
#include <cstring>

namespace {
bool (*replayQuery)() = nullptr;
}
extern "C" {
void* cc_native_fps_original = nullptr;
uintptr_t cc_native_fps_without_increment = 0;
__attribute__((force_align_arg_pointer)) int cc_native_fps_replay() {
    return replayQuery && replayQuery();
}
// Steam 48B15C: INC ECX; MOV [count],ECX。再計算中はこの7byteだけ省略。
// 経過時間・処理時間・1秒ごとの集計は元のSSE経路に残す。
__attribute__((naked)) void cc_native_fps_hook() {
    __asm__ __volatile__(
        "pushfl; pushal; movl %esp,%esi; subl $528,%esp; andl $-16,%esp; fxsave (%esp); "
        "call _cc_native_fps_replay; testl %eax,%eax; jz 1f; "
        "fxrstor (%esp); movl %esi,%esp; popal; popfl; jmp *_cc_native_fps_without_increment; "
        "1: fxrstor (%esp); movl %esi,%esp; popal; popfl; jmp *_cc_native_fps_original;");
}
}

namespace cccaster::game_interface::native_fps_counter {
bool Install(bool (*isReplay)()) {
    static bool installed = false;
    if (installed) return true;
    if (!isReplay || !game_build::RuntimeValidated()) return false;
    if (!cccaster::game_memory::steam_code::Matches(cccaster::game_memory::steam_v154::FpsUpdate)) return false;
    auto* site = reinterpret_cast<void*>(GameRuntime::Preferred(0x48B15C));
    cc_native_fps_without_increment = GameRuntime::Preferred(0x48B163);
    const auto init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
    replayQuery = isReplay;
    if (MH_CreateHook(site,reinterpret_cast<void*>(cc_native_fps_hook),&cc_native_fps_original) != MH_OK)
        return false;
    if (MH_EnableHook(site) != MH_OK) {
        MH_RemoveHook(site);
        return false;
    }
    installed = true;
    return true;
}
}
