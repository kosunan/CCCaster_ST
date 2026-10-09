#include "StartupDirectEntry.hpp"
#include "core_dll/mbaa_mem/StartupPatch.hpp"
#include "core_dll/mbaa_mem/SteamV15Signatures.hpp"
#include "core_dll/common/StartupTrace.hpp"
#include <cstdlib>
#include "SteamV151Signatures.hpp"
#include "SteamV158StartupSignatures.hpp"
#include "shared_contracts/ProcessMemory.hpp"

namespace {
using cccaster::game_memory::GameRuntime;
enum class EntryMode { Versus, Training, Replay, CPU };
bool MenuBoundary() {
    return *reinterpret_cast<const uint32_t*>(GameRuntime::Preferred(0x5ca9b4))==25 &&
        *reinterpret_cast<const uint8_t*>(GameRuntime::Preferred(0x5c9c96))==1 &&
        *reinterpret_cast<const uint32_t*>(GameRuntime::Preferred(0x5cb5b0))==0;
}
bool Compatible(EntryMode mode) {
    using namespace cccaster::game_memory;
    if(!startup::MatchesMenuCode()) return false;
    for(const auto& signature : steam_v15::DirectEntry)
        if(!steam_code::Matches(signature)) return false;
    return mode == EntryMode::Replay ? steam_code::Matches(steam_v151::ReplayInit) &&
        steam_code::Matches(steam_v151::ReplayEntry) && steam_code::Matches(steam_v151::ReplayReserve)
        : mode == EntryMode::CPU ? steam_code::Matches(steam_v158_startup::CpuInit) && steam_code::Matches(steam_v158_startup::CpuEntry)
        : steam_code::Matches(mode == EntryMode::Training ? steam_v15::TrainingInit : steam_v15::VersusInit);
}
// Steam 4852B0と同じフレームを用意し、文字列選択後の正規分岐へ入る。
// 485332/48536Bが初期化・次モード予約・callee-saved復帰を行う。
__attribute__((naked, cdecl)) void Enter(uintptr_t) {
    __asm__ __volatile__("movl 4(%esp),%eax\n\tpushl %ebp\n\tmovl %esp,%ebp\n\t"
                         "subl $4,%esp\n\tpushl %esi\n\tpushl %edi\n\tjmp *%eax\n\t");
}
uintptr_t Branch(EntryMode mode) {
    return GameRuntime::Preferred(mode == EntryMode::Replay ? 0x48542c : mode == EntryMode::Training ? 0x485332 :
        mode == EntryMode::CPU ? 0x4853a4 : 0x48536b);
}
void EnterMode(EntryMode mode, uint32_t side) {
    *reinterpret_cast<uint32_t*>(GameRuntime::Preferred(0x7b450c,4))=3;
    *reinterpret_cast<uint32_t*>(GameRuntime::Preferred(0x7b4528,4))=3;
    *reinterpret_cast<uint32_t*>(GameRuntime::Preferred(0x5bf840,4))=0;
    reinterpret_cast<void (__cdecl*)()>(GameRuntime::Preferred(0x485580))();
    *reinterpret_cast<uint8_t*>(GameRuntime::Preferred(0x5c9c97))=uint8_t(side);
    Enter(Branch(mode));
}
EntryMode bootMode = EntryMode::Versus;
uint32_t bootSide = 0;
constexpr std::array<uint8_t,5> BootCall{0xE8,0x41,0xB2,0xFF,0xFF};
std::array<uint8_t,5> bootReplacement{};
__attribute__((force_align_arg_pointer)) uint32_t __cdecl BootEntry() {
    const cccaster::patch::Spec restore{"startup_boot_entry",GameRuntime::Preferred(0x48b43a),bootReplacement,BootCall};
    if (!cccaster::patch::Apply(std::span(&restore,1))) ExitProcess(ERROR_WRITE_FAULT);
    if (*reinterpret_cast<const uint8_t*>(GameRuntime::Preferred(0x5c9c96))!=1 ||
        *reinterpret_cast<const uint32_t*>(GameRuntime::Preferred(0x5cb5b0))!=0)
        return reinterpret_cast<uint32_t (__cdecl*)()>(GameRuntime::Preferred(0x486680))();
    // Steamでは起動検査wrapperがなく、48B43Aがロゴ本体を直接呼ぶ。
    // 音声停止・Steam側管理オブジェクトの準備を含むロゴ初期化は維持する。
    reinterpret_cast<void (__cdecl*)()>(GameRuntime::Preferred(0x4865e0))();
    *reinterpret_cast<uint8_t*>(GameRuntime::Preferred(0x5c9c96))=1;
    EnterMode(bootMode,bootSide);
    cccaster::domain::session::DebugLog("[StartupBootEntry] mode=%u next=%u kind=%u side=%u menu=%08X",
        unsigned(bootMode),*reinterpret_cast<const uint32_t*>(GameRuntime::Preferred(0x5ca9b4)),
        *reinterpret_cast<const uint32_t*>(GameRuntime::Preferred(0x5ca794)),bootSide,
        *reinterpret_cast<const uint32_t*>(GameRuntime::Preferred(0x5cb5b0)));
    cccaster::diagnostics::startup::Mark("boot_direct_entry");
    return 1;
}
}
namespace cccaster::game_memory::startup_direct_entry {
void Initialize(uint8_t appMode,bool isHost) {
    if (appMode>5 || !diagnostics::startup::HasGate() || diagnostics::startup::Baseline() ||
        std::getenv("CCCASTER_STARTUP_MINIMAL_BASELINE") || std::getenv("CCCASTER_STARTUP_ENTRY_BASELINE") ||
        (appMode==4 && !ReplayEnabled())) return;
    bootMode=appMode==1 ? EntryMode::Training : appMode==4 ? EntryMode::Replay :
        appMode==3 ? EntryMode::CPU : EntryMode::Versus;
    bootSide=appMode==0 && !isHost ? 1 : 0;
    if (!Compatible(bootMode) || !steam_code::Matches(steam_v158_startup::BootDispatch) ||
        !steam_code::Matches(steam_v158_startup::LogoInit)) {
        domain::session::DebugLog("[StartupBootEntry] rejected=signature; title entry retained");return;
    }
    bootReplacement={0xE8,0,0,0,0};
    const uint32_t relative=uint32_t(uintptr_t(&BootEntry)-GameRuntime::Preferred(0x48b43f));
    std::memcpy(bootReplacement.data()+1,&relative,4);
    const patch::Spec spec{"startup_boot_entry",GameRuntime::Preferred(0x48b43a),BootCall,bootReplacement};
    const auto result=patch::Apply(std::span(&spec,1));
    if (result.rollbackFailed) ExitProcess(ERROR_WRITE_FAULT);
    domain::session::DebugLog("[StartupBootEntry] installed=%u target=%u side=%u",unsigned(bool(result)),appMode,bootSide);
}
static bool Try(uint32_t currentMode, EntryMode mode, uint32_t side) {
    static bool attempted=false;
    if(attempted || currentMode!=2 || !diagnostics::startup::HasGate() || diagnostics::startup::Baseline() ||
       std::getenv("CCCASTER_STARTUP_ENTRY_BASELINE")) return false;
    const auto read=[](uint32_t va) { return *reinterpret_cast<const uint32_t*>(GameRuntime::Preferred(va,4)); };
    if(!MenuBoundary()) return false;
    attempted=true;
    if(!Compatible(mode)) {
        domain::session::DebugLog("[StartupDirectEntry] rejected=signature; normal menu retained");
        return false;
    }
    EnterMode(mode,side);
    domain::session::DebugLog("[StartupDirectEntry] training=%u current=%u next=%u kind=%u side=%u menu=%08X versus=%u replay=%u",
        unsigned(mode==EntryMode::Training),currentMode,read(0x5ca9b4),read(0x5ca794),read(0x7e9c54),read(0x5cb5b0),read(0x7e9b94),unsigned(mode==EntryMode::Replay));
    diagnostics::startup::Mark(mode==EntryMode::Replay?"direct_replay_entry":mode==EntryMode::Training?"direct_training_entry":"direct_versus_entry");
    return true;
}
bool TryTraining(uint32_t currentMode) { return Try(currentMode,EntryMode::Training,0); }
bool TryVersus(uint32_t currentMode,bool isHost) { return Try(currentMode,EntryMode::Versus,isHost?0:1); }
bool ReplayEnabled() {
    return diagnostics::startup::HasGate() && !diagnostics::startup::Baseline() &&
        !std::getenv("CCCASTER_STARTUP_REPLAY_BASELINE");
}
bool TryReplay(uint32_t currentMode) {
    return ReplayEnabled() && Try(currentMode, EntryMode::Replay, 0);
}
bool ReplayEntryPending(uint32_t currentMode) {
    return ReplayEnabled() && (currentMode != 2 || !MenuBoundary());
}
}
