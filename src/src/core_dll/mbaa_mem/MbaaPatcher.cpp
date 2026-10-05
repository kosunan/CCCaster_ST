#include "core_dll/mbaa_mem/MbaaPatcher.hpp"
#include "core_dll/mbaa_mem/SteamInputPatch.hpp"
#include "core_dll/mbaa_mem/GameRuntime.hpp"
#include "core_dll/mbaa_mem/StagePatches.hpp"
#include "shared_contracts/ProcessMemory.hpp"
#include "core_dll/common/DebugLog.hpp"
namespace cccaster::game_memory {
patch::Result MbaaPatcher::ApplyStartupPatches(bool training) {
    if (steam_input::Apply(GameRuntime::Image(), GameRuntime::Edition()) != steam_input::Result::Applied)
        return {patch::Error::Mismatch, 0, 0, "steam_input"};
    constexpr uint8_t music[]{0x75,0x05,0xe8,0x46,0x61,0x05,0x00}, fixed[]{0xeb,0x05};
    const patch::Spec targets[]{
        {"boss_stage_overlay", GameRuntime::Preferred(0x58ad8c,12), stages::BossOverlayKey, stages::BossOverlayDisabled, false},
        {"training_music", GameRuntime::Preferred(0x4cb533,7), music, fixed}
    };
    return patch::Apply(std::span(targets, training ? 2 : 1));
}
patch::Result MbaaPatcher::ApplyPostLoadStagePatches() {
    static bool applied = false;
    if (applied) return {};
    const auto targets = stages::PostLoadSpecs();
    const auto result = patch::Apply(targets);
    domain::session::DebugLog("[StagePatches] success=%d patch=%s address=%08X error=%s", bool(result),result.name,unsigned(result.address),patch::Name(result.error));
    applied = bool(result);
    return result;
}
}
