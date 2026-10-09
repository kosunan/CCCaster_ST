#include "core_dll/rollback/SteamReplayEffects.hpp"
#include "core_dll/hook/HookBatch.hpp"
#include "core_dll/mbaa_mem/StartupSounds.hpp"
#include "core_dll/rollback/ReplayEffects.hpp"
#include "core_dll/rollback/IntroSoundClock.hpp"
#include "core_dll/mbaa_mem/MbaaAddresses.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/common/ScriptedInput.hpp"
#include "core_dll/common/Platform.hpp"
#include "core_dll/mbaa_mem/CombatStress.hpp"
#include "core_dll/rollback/SoundApiProbe.hpp"
#include <windows.h>
#include <MinHook.h>
#include <array>
#include "shared_contracts/NetplaySettings.hpp"
#include <cstring>
#include <cstdlib>

namespace {
struct Sounds {
    uint32_t frame = 0;
    std::array<uint8_t, 1500> played{};
};
std::array<Sounds, cccaster::public_api::NetplaySettings::RollbackHistoryFrames> history;
std::array<uint8_t, 1500> suppressed{};
uint32_t activeFrame = 0;
bool replaying = false;
bool introPreview = false;
uint32_t skipped = 0;
bool soundProbe = false;
using SoundUpdate = uintptr_t (__cdecl *)();
SoundUpdate soundUpdate = nullptr;
struct SoundSample { uint32_t frame, calls, suppressed, pid, tid, serial; bool replay, warmup; int64_t ticks, begin; };
std::array<SoundSample, 32> soundSamples{};
unsigned soundSamplesUsed = 0, soundDrops = 0, soundCalls = 0;
__attribute__((force_align_arg_pointer)) uintptr_t __cdecl ProbeSoundUpdate() {
    const bool warmup = cccaster::diagnostics::sound_api::Enabled() && !cccaster::diagnostics::sound_api::attempted;
    cccaster::diagnostics::sound_api::Prepare();
    cccaster::diagnostics::sound_api::Begin(activeFrame, replaying);
    const auto beforeCalls = soundCalls, beforeSkipped = skipped;
    const auto pid = GetCurrentProcessId(), tid = GetCurrentThreadId();
    const auto started = cccaster::platform::RealMonotonicTicks();
    const auto result = soundUpdate();
    const auto elapsed = cccaster::platform::RealMonotonicTicks() - started;
    cccaster::diagnostics::sound_api::End();
    if (soundSamplesUsed < soundSamples.size())
        soundSamples[soundSamplesUsed++] = {activeFrame, soundCalls - beforeCalls,
            skipped - beforeSkipped, pid, tid, cccaster::diagnostics::sound_api::serial,
            replaying, warmup, elapsed, started};
    else ++soundDrops;
    return result;
}
} // namespace
extern "C" {
void *cccaster_sound_status_original = nullptr;
__attribute__((force_align_arg_pointer)) int __cdecl cccaster_intro_sound_status(uint32_t sound) {
    const bool p1Over = *CC_P1_PUPPET_STATE_ADDR ? *CC_P3_NO_INPUT_FLAG_ADDR : *CC_P1_NO_INPUT_FLAG_ADDR;
    const bool p2Over = *CC_P2_PUPPET_STATE_ADDR ? *CC_P4_NO_INPUT_FLAG_ADDR : *CC_P2_NO_INPUT_FLAG_ADDR;
    if (!activeFrame || !cccaster::sync::IntroSoundClock::ControlsScript(
            *CC_GAME_MODE_ADDR == CC_GAME_MODE_IN_GAME, *CC_INTRO_STATE_ADDR, p1Over, p2Over)) return -1;
    const bool playing = cccaster::sync::IntroSoundClock::Playing(sound, activeFrame);
    static const bool voiceTrace = std::getenv("CCCASTER_INTRO_VOICE_TRACE") != nullptr;
    if (voiceTrace && sound < 1500 && cccaster::sync::IntroSoundClock::until[sound])
        cccaster::domain::session::DebugLog("[IntroVoiceWait] frame=%u sound=%u until=%u result=%d", activeFrame,
            sound, cccaster::sync::IntroSoundClock::until[sound], playing);
    return playing;
}
// Steamはsound番号のラッパーをscript側へインライン化している。
// 4C2088/4C20C0の呼出しだけを同期Fへ置換し、他のGetStatusは原処理へ戻す。
__attribute__((force_align_arg_pointer)) int __cdecl cccaster_steam_intro_status(
        uint32_t object, uint32_t caller, uint32_t sound, uintptr_t scriptFrame) {
    using cccaster::game_memory::GameRuntime;
    const auto image = GameRuntime::Image();
    if (caller == image.Resolve(0xc208d)) {
        const auto slot = *reinterpret_cast<const uint32_t *>(scriptFrame - 0x2c);
        const auto base = image.Resolve(0x3d2d90, 6000);
        if (slot < base || slot - base >= 6000 || (slot - base) % 4) return -1;
        sound = (slot - base) / 4;
    } else if (caller != image.Resolve(0xc20c5)) return -1;
    if (sound >= 1500 || *reinterpret_cast<const uint32_t *>(image.Resolve(0x3d2d90) + sound * 4) != object)
        return -1;
    return cccaster_intro_sound_status(sound);
}
__attribute__((naked)) void cccaster_sound_status_hook() {
    __asm__ __volatile__(
        "pushfl\n\tpushal\n\tpushl %ebp\n\tpushl %eax\n\tpushl 44(%esp)\n\tpushl %ecx\n\t"
        "call _cccaster_steam_intro_status\n\taddl $16,%esp\n\t"
        "testl %eax,%eax\n\tjs 1f\n\tmovl %eax,28(%esp)\n\tpopal\n\tpopfl\n\tret\n\t"
        "1: popal\n\tpopfl\n\tjmp *_cccaster_sound_status_original\n\t");
}
void *cccaster_rng_original = nullptr;
__attribute__((force_align_arg_pointer)) void __cdecl cccaster_trace_rng(uint32_t caller) {
    if (*CC_P1_NO_INPUT_FLAG_ADDR && *CC_P2_NO_INPUT_FLAG_ADDR)
        cccaster::domain::session::DebugLog("[RNGCALL] %u %08X count=%u", activeFrame, caller,
                                            *CC_RNG_STATE1_ADDR);
}
__attribute__((naked)) void cccaster_rng_hook() {
    __asm__ __volatile__("pushfl\n\tpushal\n\tpushl 36(%esp)\n\tcall _cccaster_trace_rng\n\taddl "
                         "$4,%esp\n\tpopal\n\tpopfl\n\tjmp *_cccaster_rng_original\n\t");
}
void *cccaster_sfx_original = nullptr;
uintptr_t cccaster_sfx_skip = 0;
__attribute__((force_align_arg_pointer)) int __cdecl cccaster_sfx_should_play(uint32_t sound) {
    // 表示だけの先行1更新では、履歴・音声時計への記録も実際の再生も抑止する。
    if (introPreview) return 0;
    cccaster::game_memory::startup_sounds::Ensure(sound);
    if (soundProbe) ++soundCalls;
    cccaster::diagnostics::sound_api::SetSound(sound);
    if (sound >= 1500)
        return 1;
    // 決着へまたがる音声も含めて開始Fを保持する。判定の置換はイントロと
    // 双方操作終了後だけで、通常戦闘の音声APIは元の処理へ戻す。
    if (activeFrame && *CC_GAME_MODE_ADDR == CC_GAME_MODE_IN_GAME) {
        cccaster::sync::IntroSoundClock::Start(sound, activeFrame);
        static const bool voiceTrace = std::getenv("CCCASTER_INTRO_VOICE_TRACE") != nullptr;
        if (voiceTrace)
            cccaster::domain::session::DebugLog("[IntroVoice] frame=%u sound=%u duration=%u replay=%d", activeFrame,
                sound, cccaster::sync::IntroSoundClock::duration[sound], replaying);
    }
    auto &slot = history[activeFrame % history.size()];
    if (activeFrame && slot.frame == activeFrame)
        slot.played[sound] = 1;
    if (replaying && suppressed[sound]) {
        ++skipped;
        return 0;
    }
    if (replaying)
        suppressed[sound] = 1;
    return 1;
}
// このゲーム版のESIは効果音番号。元命令列はInstallで照合する。
__attribute__((naked)) void cccaster_sfx_hook() {
    __asm__ __volatile__(
        "pushfl\n\tpushal\n\tpushl %esi\n\tcall _cccaster_sfx_should_play\n\taddl $4,%esp\n\t"
        "testl %eax,%eax\n\tjz 1f\n\tpopal\n\tpopfl\n\tjmp *_cccaster_sfx_original\n\t"
        "1: popal\n\tpopfl\n\tjmp *_cccaster_sfx_skip\n\t");
}
}
namespace cccaster::sync {
bool InstallReplayEffects() {
    static bool installed = false;
    if (installed) return true;

        const auto image = cccaster::game_memory::GameRuntime::Image();
        const auto edition = cccaster::game_memory::GameRuntime::Edition();
        steam_effects::Sites sites;
        if (!steam_effects::Resolve(edition, image, sites) ||
            !steam_effects::Install(edition, image, reinterpret_cast<void*>(cccaster_sfx_hook),
                &cccaster_sfx_original, &cccaster_sfx_skip)) return false;
        if (std::getenv("CCCASTER_SOUND_PROBE") || cccaster::diagnostics::sound_api::Enabled()) {
            if (MH_CreateHook(sites.update, reinterpret_cast<void*>(ProbeSoundUpdate),
                    reinterpret_cast<void**>(&soundUpdate)) != MH_OK ||
                cccaster::hook_batch::Enable(sites.update) != MH_OK) return false;
            soundProbe = true;
        }
        if (std::getenv("CCCASTER_TRACE_RNG")) {
            void* rng = steam_effects::ResolveRngTrace(edition, image);
            if (!rng || MH_CreateHook(rng, reinterpret_cast<void*>(cccaster_rng_hook),
                    &cccaster_rng_original) != MH_OK || cccaster::hook_batch::Enable(rng) != MH_OK) return false;
            cccaster::domain::session::DebugLog("[RNGCALL] Steam function trace excludes inline RNG updates");
        }
        // 466CE0 thiscall GetStatus + scriptの両呼出元を再配置込みで照合。
        constexpr std::array<uint8_t, 16> statusEntry{0x55,0x8b,0xec,0x51,0x53,0x56,0x8b,0xd9,
            0x33,0xf6,0x57,0x33,0xff,0x39,0x73,0x10};
        constexpr std::array<uint8_t, 7> rangeCall{0x8b,0xca,0xe8,0x53,0x4c,0xfa,0xff};
        constexpr std::array<uint8_t, 7> singleCall{0xe8,0x1b,0x4c,0xfa,0xff,0x85,0xc0};
        if (!steam_effects::Match(image,0x66ce0,statusEntry,std::array<size_t,0>{}) ||
            !steam_effects::Match(image,0xc2086,rangeCall,std::array<size_t,0>{}) ||
            !steam_effects::Match(image,0xc20c0,singleCall,std::array<size_t,0>{})) return false;
        auto *status = reinterpret_cast<void *>(image.Resolve(0x66ce0));
        if (MH_CreateHook(status,reinterpret_cast<void *>(cccaster_sound_status_hook),
                &cccaster_sound_status_original) != MH_OK || cccaster::hook_batch::Enable(status) != MH_OK) return false;
        cccaster::testing::combat_stress::Install();
        installed = true;
        return true;
}

void BeginSimulationEffects(uint32_t frame) {
    activeFrame = frame;
    auto &slot = history[frame % history.size()];
    slot.frame = frame;
    slot.played.fill(0);
}
void BeginReplayEffects(uint32_t from, uint32_t target) {
    suppressed.fill(0);
    skipped = 0;
    for (uint32_t f = from; f < target; ++f) {
        const auto &slot = history[f % history.size()];
        if (slot.frame != f)
            continue;
        for (size_t i = 0; i < suppressed.size(); ++i)
            suppressed[i] |= slot.played[i];
    }
    replaying = true;
}
void EndReplayEffects() {
    replaying = false;
    if (cccaster::testing::IsScriptedInputEnabled())
        cccaster::domain::session::DebugLog("[Rollback] SFX suppressed=%u", skipped);
}
void SetIntroPreviewEffects(bool active) { introPreview = active; }
void FlushSoundProbe() {
    cccaster::diagnostics::sound_api::Flush();
    for (unsigned i = 0; i < soundSamplesUsed; ++i) {
        const auto &s = soundSamples[i];
        cccaster::domain::session::DebugLog("[SoundProbe] f=%u replay=%d calls=%u suppressed=%u ticks=%lld begin=%lld end=%lld pid=%u tid=%u seq=%u warmup=%d",
            s.frame, s.replay, s.calls, s.suppressed, s.ticks, s.begin, s.begin + s.ticks, s.pid, s.tid, s.serial, s.warmup);
    }
    if (soundDrops) cccaster::domain::session::DebugLog("[SoundProbeDrop] count=%u", soundDrops);
    soundSamplesUsed = soundDrops = 0;
}
} // namespace cccaster::sync
