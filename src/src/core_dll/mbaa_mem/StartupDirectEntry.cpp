#include "StartupDirectEntry.hpp"
#include "core_dll/mbaa_mem/StartupPatch.hpp"
#include "core_dll/mbaa_mem/SteamV15Signatures.hpp"
#include "core_dll/common/StartupTrace.hpp"
#include <cstdlib>
#include "SteamV151Signatures.hpp"

namespace {
using cccaster::game_memory::GameRuntime;
enum class EntryMode { Versus, Training, Replay };
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
        : steam_code::Matches(mode == EntryMode::Training ? steam_v15::TrainingInit : steam_v15::VersusInit);
}
// Steam 4852B0と同じフレームを用意し、文字列選択後の正規分岐へ入る。
// 485332/48536Bが初期化・次モード予約・callee-saved復帰を行う。
__attribute__((naked, cdecl)) void Enter(uintptr_t) {
    __asm__ __volatile__("movl 4(%esp),%eax\n\tpushl %ebp\n\tmovl %esp,%ebp\n\t"
                         "subl $4,%esp\n\tpushl %esi\n\tpushl %edi\n\tjmp *%eax\n\t");
}
}
namespace cccaster::game_memory::startup_direct_entry {
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
    // 485791/48579Bと4856F3/4856FDの共通準備。メニューオブジェクト生成だけを省略。
    *reinterpret_cast<uint32_t*>(GameRuntime::Preferred(0x7b450c,4))=3;
    *reinterpret_cast<uint32_t*>(GameRuntime::Preferred(0x7b4528,4))=3;
    *reinterpret_cast<uint32_t*>(GameRuntime::Preferred(0x5bf840,4))=0;
    reinterpret_cast<void (__cdecl*)()>(GameRuntime::Preferred(0x485580))();
    *reinterpret_cast<uint8_t*>(GameRuntime::Preferred(0x5c9c97))=uint8_t(side);
    Enter(GameRuntime::Preferred(mode==EntryMode::Replay?0x48542c:mode==EntryMode::Training?0x485332:0x48536b));
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
