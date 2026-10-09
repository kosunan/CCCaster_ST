#include "core_dll/mbaa_mem/NativeInputWrites.hpp"
#include "core_dll/mbaa_mem/GameBuildGuard.hpp"
#include "core_dll/common/DeferredNumericLog.hpp"
#include "core_dll/common/ScriptedInput.hpp"
#include "core_dll/common/InputTrace.hpp"
#include "core_dll/mbaa_mem/SteamV154Signatures.hpp"
#include <MinHook.h>
using cccaster::game_memory::GameRuntime;
#include <cstring>

namespace {
cccaster::sync::InputWriteHistory history;
bool enabled = false, installed = false;
struct Registers { uint32_t edi,esi,ebp,esp,ebx,edx,ecx,eax,flags; };
template<class T> T Read(uintptr_t address) { return *reinterpret_cast<const T*>(address); }
bool Trace() { return cccaster::testing::IsInputTraceEnabled() || cccaster::testing::IsScriptedInputEnabled(); }
}
extern "C" {
void* cc_native_input_before_original = nullptr;
void* cc_native_input_after_original = nullptr;
__attribute__((force_align_arg_pointer)) void cc_native_input_before(const Registers* r) {
    if (!enabled || r->ecx > 1 || r->edx < 0x2E6) return;
    const auto actor = uintptr_t(r->edx - 0x2E6);
    auto* w = history.Current(actor);
    if (!w) return;
    auto& c = w->context;
    c.actor = actor; c.source = uint8_t(r->ecx);
    c.previousButtons = Read<uint32_t>(actor+0x2E8);
    c.facing = Read<uint8_t>(actor+0x310);
    c.reverse = Read<uint16_t>(actor+0x196);
    const auto player = Read<uintptr_t>(actor+0x324);
    const auto definition = Read<uintptr_t>(actor+0x31C);
    c.macroMode = Read<uint32_t>(player+0xAF8);
    c.stateKind = Read<uint8_t>(Read<uintptr_t>(definition+0x38)+0xC);
    const auto input = Read<uintptr_t>(GameRuntime::Address(0x76E6AC));
    std::memcpy(c.masks.data(),reinterpret_cast<void*>(input+0x68+c.source*0x80),sizeof(c.masks));
    w->applied = cccaster::game_interface::GameInput{
        uint16_t(Read<uint32_t>(input+0x18+c.source*0x14)),Read<uint16_t>(input+0x24+c.source*0x14)}.Pack();
    // Replay playback replaces these fields after the Steam native conversion. Netplay uses recording, not playback.
    w->prepared = Read<uint32_t>(GameRuntime::Address(0x77BF2C)) != 2;
}
__attribute__((force_align_arg_pointer)) void cc_native_input_after(const Registers* r) {
    if (!enabled) return;
    auto* w = history.Current(r->esi);
    if (!w) return;
    const auto actor = uintptr_t(r->esi);
    w->actual = {Read<uint8_t>(actor+0x2E6),Read<uint8_t>(actor+0x2E7),
                 Read<uint32_t>(actor+0x2E8),Read<uint32_t>(actor+0x2EC)};
    w->observed = true;
    if (!w->prepared) return;
    const auto expected = cccaster::sync::ExpectedActorInput(w->context,w->applied);
    const bool equal = w->actual == expected;
    history.fault |= !equal;
    const bool corrected = history.Corrected(actor,w->applied,equal);
    if (Trace() || !equal)
        cccaster::diagnostics::DeferredNumericLog::Log(
            "[NativeInputWrite] WRITE frame=%u player=%u address=%08X raw=%08X direction=%u buttons=%08X released=%08X equal=%u",
            history.Active(),unsigned(w->context.source+1),unsigned(actor+0x2E7),w->applied,
            unsigned(w->actual.direction),w->actual.buttons,w->actual.released,unsigned(equal));
    if (corrected && Trace())
        cccaster::diagnostics::DeferredNumericLog::Log(
            "[NativeInputWrite] CORRECTED frame=%u player=%u address=%08X raw=%08X direction=%u buttons=%08X released=%08X",
            history.Active(),unsigned(w->context.source+1),unsigned(actor+0x2E7),w->applied,
            unsigned(w->actual.direction),w->actual.buttons,w->actual.released);
}
// Preserve registers, flags, x87 and SSE state around observation of native stores.
#define CC_INPUT_OBSERVER(name,callback,original) \
__attribute__((naked)) void name() { __asm__ __volatile__( \
    "pushfl; pushal; movl %esp,%esi; subl $528,%esp; andl $-16,%esp; fxsave (%esp); " \
    "subl $12,%esp; pushl %esi; call _" #callback "; addl $16,%esp; fxrstor (%esp); " \
    "movl %esi,%esp; popal; popfl; jmp *_" #original); }
CC_INPUT_OBSERVER(cc_native_input_before_hook,cc_native_input_before,cc_native_input_before_original)
CC_INPUT_OBSERVER(cc_native_input_after_hook,cc_native_input_after,cc_native_input_after_original)
#undef CC_INPUT_OBSERVER
}
namespace cccaster::game_interface::native_input_writes {
bool Configure(bool active) {
    enabled = false;
    history.Reset(GameRuntime::Address(0x555134));
    if (!active) return true;
    if (!installed) {
        if (!game_build::RuntimeValidated()) return false;
        using namespace cccaster::game_memory;
        if (!steam_code::Matches(steam_v154::InputBefore) ||
            !steam_code::Matches(steam_v154::InputAfter)) return false;
        const auto init = MH_Initialize();
        if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
        if (MH_CreateHook(reinterpret_cast<void*>(GameRuntime::Preferred(0x4C2F10)),reinterpret_cast<void*>(cc_native_input_before_hook),&cc_native_input_before_original) != MH_OK ||
            MH_CreateHook(reinterpret_cast<void*>(GameRuntime::Preferred(0x4C576C)),reinterpret_cast<void*>(cc_native_input_after_hook),&cc_native_input_after_original) != MH_OK ||
            MH_EnableHook(reinterpret_cast<void*>(GameRuntime::Preferred(0x4C2F10))) != MH_OK ||
            MH_EnableHook(reinterpret_cast<void*>(GameRuntime::Preferred(0x4C576C))) != MH_OK) return false;
        installed = true;
    }
    enabled = true;
    domain::session::DebugLog("[NativeInputWrite] ACTIVE before=004C2F10 after=004C576C p1=005BC65B p2=005BD157");
    return true;
}
void Begin(uint32_t frame) { if (enabled) history.Begin(frame); }
sync::InputWriteHistory* History() { return enabled ? &history : nullptr; }
}
