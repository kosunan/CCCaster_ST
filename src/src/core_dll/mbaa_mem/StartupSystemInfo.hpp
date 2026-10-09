#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include "core_dll/common/StartupTrace.hpp"
#include "SteamV158StartupSignatures.hpp"
#include "core_dll/mbaa_mem/StartupPatch.hpp"
#include "shared_contracts/ProcessMemory.hpp"

namespace cccaster::game_memory::startup_system_info {
inline bool active = false;
inline bool dialogSkipped = false;
inline bool ChangeDialog(bool skip) {
    constexpr std::array<uint8_t,2> original{0x6A,0x00},skipped{0xEB,0x1C};
    // Steam 4F3090: INI/能力検査を終えた後、引数pushからDialogBox呼出しまで一括で省略。
    // 4F30D5から保存・窓初期化へ戻る。CALLのHIGHLOWは署名側で再配置を正規化する。
    if (!startup::MatchesMenuCode() || !steam_code::Matches(steam_v158_startup::SettingsDialogTail)) return false;
    const patch::Spec spec{"startup_settings_dialog",GameRuntime::Preferred(0x4f30b7),
        skip ? original : skipped,skip ? skipped : original};
    const auto result=patch::Apply(std::span(&spec,1));
    if(result.rollbackFailed)ExitProcess(ERROR_WRITE_FAULT);
    if(result)dialogSkipped=skip;
    return bool(result);
}
inline constexpr uint8_t Original[] = {0xE8,0x9C,0x02,0x00,0x00};
inline constexpr uint8_t Skipped[] = {0x90,0x90,0x90,0x90,0x90};
inline bool Change(bool skip) {
    // Steam WM_INITDIALOGからの情報収集呼出し。ECXにHWNDを渡す。
    // 4F0CF0..4F0D07: vBと異なりEBPフレームを使い、スタック引数はない。
    constexpr uint8_t prefix[] = {0x55,0x8b,0xec,0x81,0x7d,0x0c,0x10,0x01,0,0,
        0x75,0x08,0x8b,0x4d,0x08};
    constexpr uint8_t suffix[] = {0x33,0xc0,0x5d,0xc3};
    const auto base = GameRuntime::Preferred(0x4f0cf0,24);
    if (!base || !startup::MatchesMenuCode() ||
        std::memcmp(reinterpret_cast<void*>(base),prefix,sizeof(prefix)) ||
        std::memcmp(reinterpret_cast<void*>(base+20),suffix,sizeof(suffix))) return false;
    auto *site = reinterpret_cast<void*>(base+15);
    if (std::memcmp(site,skip ? Original : Skipped,5)) return false;
    DWORD protection{}, ignored{};
    if (!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&protection)) return false;
    std::memcpy(site,skip ? Skipped : Original,5);
    const bool flushed = FlushInstructionCache(GetCurrentProcess(),site,5) != 0;
    const bool protectedAgain = VirtualProtect(site,5,protection,&ignored) != 0;
    if (!flushed || !protectedAgain) ExitProcess(ERROR_WRITE_FAULT);
    active = skip;
    return true;
}
inline void Initialize(uint8_t mode) {
    if (mode > 1 || !cccaster::diagnostics::startup::HasGate() ||
        cccaster::diagnostics::startup::Baseline() || std::getenv("CCCASTER_STARTUP_SECONDS_BASELINE")) return;
    if (!std::getenv("CCCASTER_STARTUP_MINIMAL_BASELINE") && ChangeDialog(true)) {
        domain::session::DebugLog("[StartupSystemInfo] dialogSkipped=1");
        return;
    }
    // ゲーム入口は準備イベントを待って停止中。設定読込み・他の設定ページは維持。
    cccaster::domain::session::DebugLog("[StartupSystemInfo] skip=%u",Change(true) ? 1 : 0);
}
inline void Restore() {
    if (dialogSkipped) {
        if (!ChangeDialog(false)) ExitProcess(ERROR_WRITE_FAULT);
        domain::session::DebugLog("[StartupSystemInfo] dialogRestored=1");
    }
    if (!active) return;
    // 最初のゲームフレームで復元。以後に設定画面を開いた場合は通常の情報収集。
    if (!Change(false)) ExitProcess(ERROR_WRITE_FAULT);
    cccaster::domain::session::DebugLog("[StartupSystemInfo] restored=1");
}
}
