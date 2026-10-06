#pragma once
#include "shared_contracts/ProcessMemory.hpp"
#include "core_dll/mbaa_mem/GameRuntime.hpp"
#include "core_dll/common/DebugLog.hpp"
namespace cccaster::game_memory::stage_rematch {
inline bool patched = false;
// Steam 2017-01-05: 元ゲームの完了判定を残して通常ONCEの再戦暗転だけ短縮。
inline bool Set(bool enable) {
    if (patched == enable) return true;
    if (!GameRuntime::IsSteam()) return false;
    constexpr std::array<uint8_t,4> retry{0x83,0x79,0x44,0x1e};
    auto fastRetry=retry; fastRetry[3]=0;
    const patch::Spec sites[]{
        {"retry_fade",GameRuntime::Preferred(0x4945a7,4),enable?retry:fastRetry,enable?fastRetry:retry}
    };
    const auto result=patch::Apply(sites);
    if(!result) { domain::session::DebugLog("[StageRematchAsm] FAILED site=%s error=%s",result.name,patch::Name(result.error)); if(result.rollbackFailed) ExitProcess(ERROR_WRITE_FAULT);return false; }
    patched=enable;
    domain::session::DebugLog("[StageRematchAsm] enabled=%u patches=%u",unsigned(enable),unsigned(std::size(sites)));
    return true;
}
}
