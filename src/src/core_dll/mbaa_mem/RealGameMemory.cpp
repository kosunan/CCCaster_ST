#include "core_dll/rollback/SteamReplayEffects.hpp"
#include "core_dll/mbaa_mem/BattleProgress.hpp"
#include "core_dll/timing/SpinProbe.hpp"
#include "core_dll/common/VirtualControllerTest.hpp"
// ============================================================================
// RealGameMemory.cpp — 実メモリ読み書き（実装）
//
// ここが MbaaAddresses.hpp / MbaaInputDefs.hpp のアドレスに触れる唯一の場所
// （FastBoot のコード書換と起動時パッチを除く）。
// 入力書込みは FrameControl から移設したもので、処理内容は変えていない。
// ============================================================================

#include "core_dll/mbaa_mem/RealGameMemory.hpp"
#include "core_dll/mbaa_mem/MbaaAddresses.hpp"
#include "core_dll/mbaa_mem/MbaaInputDefs.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "core_dll/mbaa_mem/CombatStress.hpp"
#include "core_dll/mbaa_mem/GaugeStress.hpp"
#include "core_dll/hook/DriverLockProbe.hpp"
#include "core_dll/mbaa_mem/SoundPrewarm.hpp"
#include "core_dll/common/Platform.hpp"
#include "core_dll/common/ScriptedInput.hpp"
#include "shared_contracts/GameBuild.hpp"

#include <windows.h>
#include <cstring>

namespace cccaster::game_interface {

using cccaster::domain::session::DebugLog;

namespace {

/// 入力書込み先はポインタ経由。ゲーム側が未初期化だと NULL になる。
char *InputBasePtr() {
    return *reinterpret_cast<char **>(CC_PTR_TO_WRITE_INPUT_ADDR);
}

void LogNullInputBase() {
    static uint32_t s_nullCount = 0;
    if (s_nullCount++ % 120 == 0) {
        DebugLog("[RealGameMemory] Input base pointer is NULL (count=%u)", s_nullCount);
    }
}

} // namespace

bool RealGameMemory::IsAvailable() const {
    return !IsBadReadPtr(CC_GAME_MODE_ADDR, sizeof(uint32_t));
}

uint32_t RealGameMemory::GameMode() const {
    return *CC_GAME_MODE_ADDR;
}

uint8_t RealGameMemory::IntroState() const {
    return *CC_INTRO_STATE_ADDR;
}

void RealGameMemory::SetTrainingHold(bool hold) {
    if (hold == trainingHold_) return;
    // 通常pauseはトレーニングメニューを開いてしまうため使用しない。
    // 0x423998で1減算した後も停止する2をセットし、次Presentで元に戻す。
    // 55DF00は同処理の「この更新で全体停止した」フラグ。保存前の値も保全する。
    auto *freeze = reinterpret_cast<uint32_t *>(game_memory::GameRuntime::Address(0x562A48));
    auto *active = reinterpret_cast<uint32_t *>(game_memory::GameRuntime::Address(0x55DF00));
    if (hold) {
        trainingFreezeBefore_ = *freeze;
        trainingFreezeActiveBefore_ = *active;
        *freeze = 2;
    } else {
        *freeze = trainingFreezeBefore_;
        *active = trainingFreezeActiveBefore_;
    }
    trainingHold_ = hold;
}

uint32_t RealGameMemory::WorldTimer() const {
    return *CC_WORLD_TIMER_ADDR;
}

uint32_t RealGameMemory::RealTimer() const {
    return *CC_REAL_TIMER_ADDR;
}

uint32_t RealGameMemory::MenuStateCounter() const {
    return *CC_MENU_STATE_COUNTER_ADDR;
}

domain::session::MatchResultFacts RealGameMemory::ReadMatchResult() const {
    // 対象版の既存アドレスを再利用。勝数は既存snapshot保存とMEM診断の対象。
    // ゲームスレッド上、再計算完了後のRETRY入口でのみ読み取る。
    if (GameMode() != CC_GAME_MODE_RETRY ||
        IsBadReadPtr(CC_P1_WINS_ADDR, 4) || IsBadReadPtr(CC_P2_WINS_ADDR, 4) ||
        IsBadReadPtr(CC_WIN_COUNT_VS_ADDR, 4)) return {};
    return {*CC_P1_WINS_ADDR, *CC_P2_WINS_ADDR, *CC_WIN_COUNT_VS_ADDR, true};
}

void RealGameMemory::WriteInput(GameInput p1, GameInput p2) {
    if (cccaster::testing::gauge_stress::active) p1 = p2 = {};
    if (cccaster::testing::combat_stress::Active()) {
        p1.direction = p2.direction = Dir::Neutral;
    }
    char *base = InputBasePtr();
    if (!base) {
        LogNullInputBase();
        return;
    }
    // 入力採取時点ではなく、ゲームが消費するフレームの選択段階で判定する。
    // 旧CCCasterのキャラセレ終了防止と同じ条件。ムーン・カラー選択の戻る操作は残す。
    if (*CC_GAME_MODE_ADDR == CC_GAME_MODE_CHARA_SELECT) {
        constexpr auto keepButtons = static_cast<uint16_t>(~(CC_BUTTON_B | CC_BUTTON_CANCEL));
        if (*CC_P1_SELECTOR_MODE_ADDR == CC_SELECT_CHARA)
            p1.buttons &= keepButtons;
        if (*CC_P2_SELECTOR_MODE_ADDR == CC_SELECT_CHARA)
            p2.buttons &= keepButtons;
    }
    *reinterpret_cast<uint32_t *>(base + CC_P1_OFFSET_DIRECTION) = p1.direction;
    *reinterpret_cast<uint16_t *>(base + CC_P1_OFFSET_BUTTONS) = p1.buttons;
    *reinterpret_cast<uint32_t *>(base + CC_P2_OFFSET_DIRECTION) = p2.direction;
    *reinterpret_cast<uint16_t *>(base + CC_P2_OFFSET_BUTTONS) = p2.buttons;
}

void InstallRealGameMemory() {
    static RealGameMemory s_real;
    InstallGameMemory(&s_real);
}

bool RealGameMemory::ReadRng(RngState &state) const {
    if (IsBadReadPtr(CC_RNG_STATE0_ADDR, 4) || IsBadReadPtr(CC_RNG_STATE1_ADDR, 4) ||
        IsBadReadPtr(CC_RNG_STATE2_ADDR, 4) || IsBadReadPtr(CC_RNG_STATE3_ADDR, 220))
        return false;
    state[0] = *CC_RNG_STATE0_ADDR;
    state[1] = *CC_RNG_STATE1_ADDR;
    state[2] = *CC_RNG_STATE2_ADDR;
    std::memcpy(state.data() + 3, CC_RNG_STATE3_ADDR, 220);
    return true;
}
bool RealGameMemory::WriteRng(const RngState &state) {
    if (IsBadWritePtr(CC_RNG_STATE0_ADDR, 4) || IsBadWritePtr(CC_RNG_STATE1_ADDR, 4) ||
        IsBadWritePtr(CC_RNG_STATE2_ADDR, 4) || IsBadWritePtr(CC_RNG_STATE3_ADDR, 220))
        return false;
    *CC_RNG_STATE0_ADDR = state[0];
    *CC_RNG_STATE1_ADDR = state[1];
    *CC_RNG_STATE2_ADDR = state[2];
    std::memcpy(CC_RNG_STATE3_ADDR, state.data() + 3, 220);
    return true;
}

} // namespace cccaster::game_interface

#include "core_dll/rollback/SteamSnapshotBuilder.hpp"
#include "core_dll/mbaa_mem/SteamReplayCursor.hpp"
#include "core_dll/rollback/ReplayEffects.hpp"
#include "core_dll/rollback/IntroSoundClock.hpp"
namespace cccaster::game_interface {
static cccaster::sync::PointerSnapshot &SnapshotDumper() {
    static cccaster::sync::PointerSnapshot dumper;
    static const bool configured = [&] {
        std::vector<cccaster::sync::SnapshotNode> nodes;
        if (!cccaster::sync::BuildSteamSnapshotNodes(game_memory::GameRuntime::Image(), nodes) ||
            !dumper.Configure(nodes, true)) return false;
        DebugLog("[SteamSnapshot] configured nodes=%u bytes=%u resourceGeneration=%u",
            unsigned(nodes.size()), unsigned(dumper.Size()), cccaster::sync::steam_pointers::CaptureGeneration());
        return true;
    }();
    (void)configured;
    return dumper;
}
struct SteamSnapshotTail {
    uint32_t resourceGeneration = 0;
    cccaster::mbaa::steam::replay::Snapshot cursors{};
    std::array<uint32_t, 1500> soundUntil{};
};
size_t RealGameMemory::SnapshotSize() const {
    if (!SnapshotDumper().Size() || !cccaster::sync::InstallReplayEffects()) return 0;
    return SnapshotDumper().Size() + sizeof(SteamSnapshotTail);
}
bool RealGameMemory::SaveSnapshot(std::span<char> data) {
    const auto size = SnapshotDumper().Size();
    SteamSnapshotTail tail;
    tail.resourceGeneration = cccaster::sync::steam_pointers::CaptureGeneration();
    if (!size || !tail.resourceGeneration || data.size() != size + sizeof(tail) ||
        !cccaster::mbaa::steam::replay::Capture(tail.cursors) ||
        !SnapshotDumper().Save(data.first(size)) ||
        !cccaster::sync::steam_pointers::ValidateGeneration(tail.resourceGeneration)) return false;
    tail.soundUntil = cccaster::sync::IntroSoundClock::until;
    std::memcpy(data.data() + size, &tail, sizeof(tail));
    return true;
}
bool RealGameMemory::LoadSnapshot(std::span<char> data) {
    const auto size = SnapshotDumper().Size();
    SteamSnapshotTail tail;
    if (!size || data.size() != size + sizeof(tail)) return false;
    std::memcpy(&tail, data.data() + size, sizeof(tail));
    if (!cccaster::sync::steam_pointers::ValidateGeneration(tail.resourceGeneration)) {
        DebugLog("[SteamSnapshot] restore rejected generation=%u current=%u", tail.resourceGeneration,
                 cccaster::sync::steam_pointers::CaptureGeneration());
        return false;
    }
    if (!SnapshotDumper().ValidateLoad(data.first(size))) {
        DebugLog("[SteamSnapshot] restore rejected pointer targets"); return false;
    }
    if (!cccaster::mbaa::steam::replay::Restore(tail.cursors)) {
        DebugLog("[SteamSnapshot] restore rejected replay cursor round=%u rng=%u", unsigned(tail.cursors.round), tail.cursors.rngEndOffset);
        return false;
    }
    if (!SnapshotDumper().Load(data.first(size))) return false;
    cccaster::sync::IntroSoundClock::until = tail.soundUntil;
    return true;
}
} // namespace cccaster::game_interface

namespace cccaster::game_interface {
bool RealGameMemory::CanPredict() const {
    if (GameMode() != CC_GAME_MODE_IN_GAME || IntroState() != 0)
        return false;
    const bool p1 = *CC_P1_PUPPET_STATE_ADDR ? *CC_P3_NO_INPUT_FLAG_ADDR : *CC_P1_NO_INPUT_FLAG_ADDR;
    const bool p2 = *CC_P2_PUPPET_STATE_ADDR ? *CC_P4_NO_INPUT_FLAG_ADDR : *CC_P2_NO_INPUT_FLAG_ADDR;
    return !(p1 && p2);
}
bool RealGameMemory::CanRollback() const {
    return AllowsRollback(ClassifyBattle(GameMode() == CC_GAME_MODE_IN_GAME, IntroState(), !CanPredict()));
}
void RealGameMemory::AlignIntroRng() {
    // 0x4683EE..0x468426の演出用乱数は通常RNGとは別系統。
    // 合流済みRNGのindex/55語を複製するが、戦闘RNGそのものは進めない。
    *reinterpret_cast<uint32_t *>(game_memory::GameRuntime::Address(0x563948)) = *CC_RNG_STATE2_ADDR;
    *reinterpret_cast<uint32_t *>(game_memory::GameRuntime::Address(0x56394C)) = 0;
    std::memcpy(reinterpret_cast<void *>(game_memory::GameRuntime::Address(0x563950)), CC_RNG_STATE3_ADDR, 220);
}
} // namespace cccaster::game_interface

namespace cccaster::game_interface {
bool RealGameMemory::BeginReplay(uint32_t from, uint32_t target) {
    cccaster::sync::BeginReplayEffects(from, target);
    return true;
}
void RealGameMemory::EndReplay() {
    cccaster::sync::EndReplayEffects();
}
void RealGameMemory::BeginSimulation(uint32_t f) {
    // 起動時に設定する試験オプション。毎FのCRT環境変数参照を締切後に持ち込まない。
    static const bool quickRetry = std::getenv("CCCASTER_TEST_RETRY_QUICK") != nullptr;
    static const bool quickKo = std::getenv("CCCASTER_TEST_ROUND_KO") != nullptr;
    static const bool quickDraw = std::getenv("CCCASTER_TEST_ROUND_DRAW") != nullptr;
    using Probe = cccaster::diagnostics::SpinProbe;
    const bool probe = Probe::Enabled() && Probe::pending && Probe::sample.frame == f;
    auto *sample = probe ? &Probe::sample : nullptr;
    if (probe) sample->simEntry = Probe::Now();
    // 再戦疎通の短時間実機試験専用。通常対戦では無効。
    // 再計算でも同じFで適用し、ゲーム本来の時間切れ・勝敗・再戦遷移を通す。
    if ((cccaster::testing::IsScriptedInputEnabled() || cccaster::testing::IsVirtualControllerTest()) && (quickRetry || quickKo || quickDraw) &&
        CanPredict() && f % 65536 >= 600) {
        if (quickKo) {
            // 時間切れと分けたKO経路の自動試験。ゲームスレッドでのみ変更する。
            if (*CC_P2_HEALTH_ADDR) {
                *CC_P1_HEALTH_ADDR = *CC_P1_RED_HEALTH_ADDR = 11400;
                *CC_P2_HEALTH_ADDR = *CC_P2_RED_HEALTH_ADDR = 0;
                cccaster::diagnostics::DeferredNumericLog::Log("[RetryTest] KO frame=%u", f);
            }
        } else if (*CC_ROUND_TIMER_ADDR > 1) {
            *CC_ROUND_TIMER_ADDR = 1;
            *CC_P1_HEALTH_ADDR = *CC_P1_RED_HEALTH_ADDR = 11400;
            *CC_P2_HEALTH_ADDR = *CC_P2_RED_HEALTH_ADDR = quickDraw ? 11400 : 5000;
            cccaster::diagnostics::DeferredNumericLog::Log("[RetryTest] SHORTEN frame=%u draw=%d", f, int(quickDraw));
        }
    }
    if (probe) sample->retryEnd = Probe::Now();
    cccaster::testing::combat_stress::Begin(CanPredict(), f);
    if (probe) sample->stressEnd = Probe::Now();
    cccaster::testing::gauge_stress::Begin(CanPredict(), f);
    if (probe) sample->gaugeEnd = Probe::Now();
    cccaster::diagnostics::driver_lock::SetFrame(f);
    cccaster::sync::BeginSimulationEffects(f);
    if (probe) sample->effectsEnd = Probe::Now();
}
bool RealGameMemory::PrepareBattleAudio() {
    // 音源配置の署名はフック設置前にInstallReplayEffectsが照合する。
    // 設置後のGetStatus入口を元バイト列と再比較してはいけない。
    if (!cccaster::sync::InstallReplayEffects()) return false;
    using SoundClock = cccaster::sync::IntroSoundClock;
    SoundClock::until.fill(0);
    SoundClock::duration.fill(0);
    // 起動合流中に音源の実データ長を読む。再計算・締切直前にはCOM照会しない。
    if (GameMode() == CC_GAME_MODE_IN_GAME && IntroState() != 0) {
        const auto objects = reinterpret_cast<uintptr_t *>(game_memory::GameRuntime::Address(0x76C6F8));
        for (unsigned i = 0; i < SoundClock::duration.size(); ++i) {
            if (!objects[i]) continue;
            const auto buffers = *reinterpret_cast<IDirectSoundBuffer ***>(objects[i] + 4);
            const auto count = *reinterpret_cast<const int *>(objects[i] + 0x10);
            if (!count) continue;
            if (!buffers || count < 1 || count > 64) return false;
            for (int j = 0; j < count; ++j) {
                if (!buffers[j]) continue;
                DSBCAPS caps{}; caps.dwSize = sizeof(caps);
                WAVEFORMATEX format{};
                DWORD frequency = 0;
                if (FAILED(buffers[j]->GetCaps(&caps)) ||
                    FAILED(buffers[j]->GetFormat(&format, sizeof(format), nullptr)) ||
                    FAILED(buffers[j]->GetFrequency(&frequency)) || !format.nBlockAlign || !frequency)
                    return false;
                const auto frames = SoundClock::Frames(caps.dwBufferBytes, format.nBlockAlign, frequency);
                if (frames > SoundClock::duration[i]) SoundClock::duration[i] = frames;
            }
        }
        uint32_t hash = 2166136261u;
        for (auto frames : SoundClock::duration) hash = (hash ^ frames) * 16777619u;
        DebugLog("[IntroSoundClock] initialized=1 sources=%u hash=%u", unsigned(SoundClock::duration.size()), hash);
    }
    if (cccaster::testing::IsScriptedInputEnabled() || cccaster::testing::IsVirtualControllerTest()) {
        const auto p1 = reinterpret_cast<const uint32_t *>(game_memory::GameRuntime::Address(0x74D83C));
        const auto p2 = reinterpret_cast<const uint32_t *>(game_memory::GameRuntime::Address(0x74D868));
        DebugLog("[Select] LOADED p1=%u/%u/%u p2=%u/%u/%u stage=%u",
                 p1[1], p1[4], p1[0], p2[1], p2[4], p2[0], *CC_STAGE_SELECTOR_ADDR);
    }
    if (std::getenv("CCCASTER_DISABLE_SOUND_PREWARM")) {
        DebugLog("[SoundPrewarm] disabled=1");
        return true;
    }
    // フェーズ合流前、戦闘開始演出だけ。再計算や通常の戦闘更新には入れない。
    if (GameMode() != CC_GAME_MODE_IN_GAME || IntroState() == 0) {
        DebugLog("[SoundPrewarm] skipped=no_intro");
        return true;
    }
    const auto started = cccaster::platform::RealMonotonicTicks();
    unsigned prepared = 0, skippedBuffers = 0;
    auto objects = reinterpret_cast<uintptr_t *>(game_memory::GameRuntime::Address(0x76c6f8));
    for (unsigned i = 0; i < 1500; ++i) {
        if (!objects[i]) continue;
        auto list = *reinterpret_cast<IDirectSoundBuffer ***>(objects[i] + 4);
        const auto count = *reinterpret_cast<int *>(objects[i] + 0x10);
        if (!list || count < 1 || count > 64) continue;
        for (int j = 0; j < count; ++j) {
            if (!list[j]) continue;
            const auto result = sound_prewarm::Prepare(*list[j]);
            if (result == sound_prewarm::Result::FailedRestoration) {
                DebugLog("[SoundPrewarm] failed=restoration sound=%u slot=%d", i, j);
                return false;
            }
            if (result == sound_prewarm::Result::Prepared) ++prepared; else ++skippedBuffers;
        }
    }
    DebugLog("[SoundPrewarm] prepared=%u skipped=%u ticks=%lld restored=1", prepared, skippedBuffers,
        cccaster::platform::RealMonotonicTicks() - started);
    return true;
}
} // namespace cccaster::game_interface
