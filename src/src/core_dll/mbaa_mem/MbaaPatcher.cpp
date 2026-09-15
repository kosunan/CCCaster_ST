#include "core_dll/mbaa_mem/MbaaPatcher.hpp"
#include "core_dll/mbaa_mem/SteamInputPatch.hpp"
#include "core_dll/mbaa_mem/GameRuntime.hpp"
#include "core_dll/common/DebugLog.hpp"
namespace cccaster::game_memory {
bool MbaaPatcher::ApplyStartupPatches(bool training) {
    const auto result = steam_input::Apply(GameRuntime::Image(), GameRuntime::Edition());
    if (result != steam_input::Result::Applied) return false;
    if (training) {
        // Steam 4CB52D: round比較後のBGM再開callだけを飛ばす。
        // リセット中の戦闘初期化と選曲はゲーム本来の経路を通す。
        auto *site = reinterpret_cast<uint8_t *>(GameRuntime::Preferred(0x4cb533, 7));
        constexpr uint8_t expected[] = {0x75,0x05,0xe8,0x46,0x61,0x05,0x00};
        if (!site || std::memcmp(site,expected,sizeof(expected))) return false;
        DWORD protection{}, ignored{};
        if (!VirtualProtect(site,1,PAGE_EXECUTE_READWRITE,&protection)) return false;
        *site = 0xeb;
        if (!FlushInstructionCache(GetCurrentProcess(),site,1) ||
            !VirtualProtect(site,1,protection,&ignored)) ExitProcess(ERROR_WRITE_FAULT);
    }
    domain::session::DebugLog("[MbaaPatcher] Steam input source patches applied");
    return true;
}
}
