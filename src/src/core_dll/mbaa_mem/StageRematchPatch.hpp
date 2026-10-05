#pragma once
#include "shared_contracts/ProcessMemory.hpp"
#include "core_dll/mbaa_mem/GameRuntime.hpp"
#include "core_dll/common/DebugLog.hpp"
namespace cccaster::game_memory::stage_rematch {
inline bool patched = false;
inline const float FadeStep = 1.0f;
// Steam 2017-01-05: 元ゲームの完了判定を残して再選択演出だけ短縮。
inline bool Set(bool enable) {
    if (patched == enable) return true;
    if (!GameRuntime::IsSteam()) return false;
    constexpr std::array<uint8_t,4> retry{0x83,0x79,0x44,0x1e};
    constexpr std::array<uint8_t,6> character{0xc7,0,0x1e,0,0,0};
    constexpr std::array<uint8_t,8> moon{0xc7,0x44,0xdf,0x14,0x1e,0,0,0};
    constexpr std::array<uint8_t,4> gridWait{0x83,0x7e,0x34,0x1e};
    constexpr std::array<uint8_t,6> gridTime{0x8d,0x3c,0x02,0xc1,0xff,2};
    constexpr std::array<uint8_t,6> fastGridTime{0xbf,0xff,0xff,0xff,0x7f,0x90};
    auto fastRetry=retry; fastRetry[3]=0;
    auto fastCharacter=character; fastCharacter[2]=0;
    auto fastMoon=moon; fastMoon[4]=0;
    auto fastGridWait=gridWait; fastGridWait[3]=0;
    std::array<uint8_t,8> gridFade{0xf3,0x0f,0x10,0x15,0,0,0,0};
    std::array<uint8_t,8> fade{0xf3,0x0f,0x58,0x0d,0,0,0,0};
    const auto gridStep=uint32_t(GameRuntime::Preferred(0x58ce90,4));
    const auto selectionStep=uint32_t(GameRuntime::Preferred(0x58ce80,4));
    std::memcpy(gridFade.data()+4,&gridStep,4); std::memcpy(fade.data()+4,&selectionStep,4);
    auto fastGridFade=gridFade,fastFade=fade;
    const auto step=uint32_t(reinterpret_cast<uintptr_t>(&FadeStep));
    std::memcpy(fastGridFade.data()+4,&step,4);std::memcpy(fastFade.data()+4,&step,4);
    const patch::Spec sites[]{
        {"retry_fade",GameRuntime::Preferred(0x4945a7,4),enable?retry:fastRetry,enable?fastRetry:retry},
        {"character_wait",GameRuntime::Preferred(0x481841,6),enable?character:fastCharacter,enable?fastCharacter:character},
        {"moon_wait",GameRuntime::Preferred(0x481c58,8),enable?moon:fastMoon,enable?fastMoon:moon},
        {"grid_wait",GameRuntime::Preferred(0x482593,4),enable?gridWait:fastGridWait,enable?fastGridWait:gridWait},
        {"grid_stagger",GameRuntime::Preferred(0x4e5690,6),enable?gridTime:fastGridTime,enable?fastGridTime:gridTime},
        {"grid_fade",GameRuntime::Preferred(0x4e5683,8),enable?gridFade:fastGridFade,enable?fastGridFade:gridFade},
        {"selection_fade",GameRuntime::Preferred(0x4800fa,8),enable?fade:fastFade,enable?fastFade:fade}
    };
    const auto result=patch::Apply(sites);
    if(!result) { domain::session::DebugLog("[StageRematchAsm] FAILED site=%s error=%s",result.name,patch::Name(result.error)); if(result.rollbackFailed) ExitProcess(ERROR_WRITE_FAULT);return false; }
    patched=enable;
    domain::session::DebugLog("[StageRematchAsm] enabled=%u patches=%u",unsigned(enable),unsigned(std::size(sites)));
    return true;
}
}
