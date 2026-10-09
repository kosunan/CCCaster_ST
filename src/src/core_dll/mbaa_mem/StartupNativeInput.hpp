#pragma once
#include "core_dll/mbaa_mem/StartupSystemInfo.hpp"
#include <MinHook.h>
namespace cccaster::game_memory::startup_native_input {
inline bool active=false;
inline void* originalInitialize=nullptr;
// Steamは旧40FF50を4EFED0へインライン化。ECX=入力管理、stack+4=HWND欄、RET4。
inline __attribute__((force_align_arg_pointer)) unsigned __fastcall InitializeEmptyDevices(void* object,void*,HWND* window) {
    *reinterpret_cast<HWND*>(GameRuntime::Preferred(0x5bb6e0))=*window;
    reinterpret_cast<unsigned (__cdecl*)()>(GameRuntime::Preferred(0x467820))();
    reinterpret_cast<void (__thiscall*)(void*)>(GameRuntime::Preferred(0x4efe80))(object);
    return 1;
}
inline bool ChangeEnum(bool enable) {
    std::array<uint8_t,5> original{0xA1,0,0,0,0};
    constexpr std::array<uint8_t,5> skip{0xE9,0x14,0,0,0};
    const auto object=uint32_t(GameRuntime::Preferred(0x5bb6e4));
    std::memcpy(original.data()+1,&object,4);
    const patch::Spec spec{"startup_native_joystick_enum",GameRuntime::Preferred(0x467845),
        enable ? original : skip,enable ? skip : original};
    const auto result=patch::Apply(std::span(&spec,1));
    if(result.rollbackFailed)ExitProcess(ERROR_WRITE_FAULT);
    return bool(result);
}
inline void Initialize() {
    if(!startup_system_info::dialogSkipped || std::getenv("CCCASTER_STARTUP_MINIMAL_BASELINE"))return;
    for(const auto& signature:steam_v158_startup::NativeInput)
        if(!steam_code::Matches(signature)){domain::session::DebugLog("[StartupNativeInput] rejected=signature");return;}
    const auto init=MH_Initialize();
    if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return;
    auto* site=reinterpret_cast<void*>(GameRuntime::Preferred(0x4efed0));
    if(MH_CreateHook(site,reinterpret_cast<void*>(InitializeEmptyDevices),&originalInitialize)!=MH_OK)return;
    if(!ChangeEnum(true)){MH_RemoveHook(site);return;}
    if(MH_EnableHook(site)!=MH_OK){
        if(!ChangeEnum(false))ExitProcess(ERROR_WRITE_FAULT);
        MH_RemoveHook(site);return;
    }
    active=true;
    domain::session::DebugLog("[StartupNativeInput] duplicateDevicesSkipped=1");
}
inline void Restore() {
    if(!active)return;
    auto* site=reinterpret_cast<void*>(GameRuntime::Preferred(0x4efed0));
    if(MH_DisableHook(site)!=MH_OK || !ChangeEnum(false))ExitProcess(ERROR_WRITE_FAULT);
    if(MH_RemoveHook(site)!=MH_OK)ExitProcess(ERROR_WRITE_FAULT);
    active=false;
}
}
