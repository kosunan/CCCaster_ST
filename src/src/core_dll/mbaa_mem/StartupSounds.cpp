#include "StartupSounds.hpp"
#include "SteamV158StartupSignatures.hpp"
using cccaster::game_memory::GameRuntime;
#include "core_dll/mbaa_mem/StartupPatch.hpp"
#include "core_dll/common/StartupTrace.hpp"
#include "shared_contracts/ProcessMemory.hpp"
#include "core_dll/hook/HookBatch.hpp"
#include "core_dll/rollback/ReplayEffects.hpp"
#include <cstdio>
#include <cstdlib>
namespace {
bool active=false, deferred=false;
std::array<bool,200> attempted{};
constexpr std::array<uint8_t,5> original{0xE8,0x0F,0xC2,0x0A,0};
constexpr std::array<uint8_t,5> skip{0x90,0x90,0x90,0x90,0x90};
// Steam 521110の1音分。ローダー/名前登録はECX,EDX、音量適用はthiscall。
void Load(uint32_t sound,const char* path,const char* name) {
    if (!reinterpret_cast<unsigned (__fastcall*)(const char*,uint32_t)>(GameRuntime::Preferred(0x520f90))(path,sound)) return;
    reinterpret_cast<void (__fastcall*)(uint32_t,const char*)>(GameRuntime::Preferred(0x520bf0))(sound,name);
    const auto object=reinterpret_cast<uintptr_t*>(GameRuntime::Preferred(0x7d2d90,1500*4))[sound];
    if (!object) return;
    auto volume=*reinterpret_cast<const int*>(GameRuntime::Preferred(0x7d4df0));
    const auto config=GameRuntime::Config(0);
    if ((!config || !*reinterpret_cast<const int*>(config+0x150)) &&
        *reinterpret_cast<const int*>(GameRuntime::Preferred(0x5bf844))) volume=-10000;
    reinterpret_cast<void (__thiscall*)(void*,int)>(GameRuntime::Preferred(0x466d20))(reinterpret_cast<void*>(object),volume);
}

}
namespace cccaster::game_memory::startup_sounds {
bool Active(){return active;}
void Initialize(uint8_t mode) {
    if(mode>1 || !diagnostics::startup::HasGate() || diagnostics::startup::Baseline() ||
       std::getenv("CCCASTER_STARTUP_MINIMAL_BASELINE") || !startup::MatchesMenuCode())return;
    for(const auto& signature:steam_v158_startup::Sounds)
        if(!steam_code::Matches(signature))return;
    // キャラ選択のSEも初回要求から再生する。既存の再計算抑止と同じ再生入口を使う。
    hook_batch::Scope hooks(true);
    if(!sync::InstallReplayEffects())return;
    const patch::Spec spec{"startup_common_sounds",GameRuntime::Preferred(0x474efc),original,skip};
    const auto result=patch::Apply(std::span(&spec,1));
    if(result.rollbackFailed)ExitProcess(ERROR_WRITE_FAULT);
    active=deferred=bool(result);
    domain::session::DebugLog("[StartupSounds] deferred=%u count=200",unsigned(active));
}
void Ensure(uint32_t sound) {
    if(!deferred || sound>=attempted.size() || attempted[sound])return;
    if(!*reinterpret_cast<const uint32_t*>(GameRuntime::Preferred(0x7d4698)))return;
    attempted[sound]=true;
    if(reinterpret_cast<const uint32_t*>(GameRuntime::Preferred(0x7d2d90,1500*4))[sound])return;
    char name[16],path[64];
    std::snprintf(name,sizeof(name),"SE%03u",sound);
    std::snprintf(path,sizeof(path),".\\se\\normal_se\\%s",name);
    Load(sound,path,name);
    if(diagnostics::startup::Enabled())domain::session::DebugLog("[StartupSounds] loaded=%u sound=%u",
        unsigned(reinterpret_cast<const uint32_t*>(GameRuntime::Preferred(0x7d2d90,1500*4))[sound]!=0),sound);
}
void PrepareBattle() {
    if(!deferred)return;
    unsigned early=0;for(bool value:attempted)early+=value;
    const auto begin=diagnostics::startup::QpcUs();
    for(uint32_t sound=0;sound<attempted.size();++sound)Ensure(sound);
    deferred=false;
    const patch::Spec spec{"startup_common_sounds_restore",GameRuntime::Preferred(0x474efc),skip,original};
    if(!patch::Apply(std::span(&spec,1)))ExitProcess(ERROR_WRITE_FAULT);
    active=false;
    domain::session::DebugLog("[StartupSounds] battleReady=1 early=%u deferred=%u elapsedUs=%lld",
        early,200-early,diagnostics::startup::QpcUs()-begin);
}
}
