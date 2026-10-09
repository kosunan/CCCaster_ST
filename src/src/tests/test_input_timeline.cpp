#include "core_dll/sync/NetplaySession.hpp"
#include "tests/test_support.hpp"
#include "core_dll/sync/InputTimeline.hpp"
#include "core_dll/sync/MatchInputBuffer.hpp"
#include "core_dll/hook/DirectInputHook.hpp"
#include "core_dll/ui/State_Ui_Logic.hpp"
#include "core_dll/engine/SelectionOptions.hpp"
#include <vector>
#include <thread>
namespace cccaster::core::timer { extern int64_t testClockTicks; }
namespace {
uint32_t physical = 0;
bool mapping = false;
unsigned polls = 0;
} // namespace
namespace cccaster::game_interface {
void DirectInputHook::Poll() {
    ++polls;
}
uint32_t DirectInputHook::GetLocalPlayerInput(bool, bool) {
    return physical;
}
} // namespace cccaster::game_interface
namespace cccaster::domain::ui {
bool StateUiLogic::IsMappingWindowOpen() {
    return mapping;
}
} // namespace cccaster::domain::ui
void HookLog(const char *) {}
namespace cccaster::platform {
int64_t RealMonotonicUs() {
    return 0;
}
int64_t RealMonotonicTicks() {
    return 0;
}
} // namespace cccaster::platform
using namespace cccaster::core::sync;
int main() {
    CC_CASE("複数時計の採取要求は1回だけ実行し、停止/再開/再初期化前の要求は捨てる");
    auto &race = InputTimeline::GetInstance();
    race.Reset();
    race.Begin(65536, 65539, cccaster::game_interface::GamePhase::InGame, true, 60000000);
    const auto old = race.NextCapture();
    cccaster::core::timer::testClockTicks = old.due - 1;
    CC_CHECK(!race.TryPump(old, 0, 0));
    CC_CHECK_EQ(polls, 0u);
    race.Pause(); race.Resume();
    cccaster::core::timer::testClockTicks = old.due;
    CC_CHECK(race.TryPump(old, 0, 0));
    CC_CHECK_EQ(polls, 0u);
    const auto resumed = race.NextCapture();
    race.Reset();
    race.Begin(65536, 65539, cccaster::game_interface::GamePhase::InGame, true, old.due);
    CC_CHECK(race.TryPump(resumed, 0, 0));
    CC_CHECK_EQ(polls, 0u);
    const auto target = race.NextCapture();
    std::vector<std::thread> claimants;
    for (int i = 0; i < 5; ++i) claimants.emplace_back([&, i] {
        while (!race.TryPump(target, 0, i - 1)) std::this_thread::yield();
    });
    for (auto &thread : claimants) thread.join();
    CC_CHECK_EQ(polls, 1u);
    CC_CHECK(race.HasCaptured(target.frame));
    CC_CHECK_EQ(race.NextCapture().frame, target.frame + 1);
    cccaster::core::timer::testClockTicks = 0;
    polls = 0;
    CC_CASE("未来の開始時刻より前に入力を採取しない");
    auto &future = InputTimeline::GetInstance();
    future.Reset();
    future.Begin(65536, 65539, cccaster::game_interface::GamePhase::InGame, true, 60000000);
    future.Pump(999999, 16666, 59999940);
    CC_CHECK(!future.HasCaptured(65539));
    future.Pump(1000000, 16666, 60000000);
    CC_CHECK(future.HasCaptured(65539));
    CC_CHECK_EQ(future.CapturedDeadlineTicks(65539), 60000000);

    CC_CASE("共通開始は提案・受領・確定を経て異なる時計原点へ変換する");
    EpochStartGate host, client;
    host.Begin(65536); client.Begin(65536);
    CC_CHECK(host.Update(true, client.local, 0, (1000000LL*60), (50000LL*60)) == EpochStartGate::Waiting);
    const auto offer = host.local;
    CC_CHECK_EQ(offer.hostTicks, (1500000LL*60));
    CC_CHECK(client.Update(false, offer, offer.hostTicks + (20000LL*60), (1030000LL*60), (50000LL*60)) == EpochStartGate::Waiting);
    CC_CHECK_EQ(client.local.stage, EpochStart::Accepted);
    host.Update(true, client.local, 0, (1040000LL*60), (50000LL*60));
    CC_CHECK_EQ(host.local.stage, EpochStart::Commit);
    CC_CHECK(client.Update(false, host.local, 999, (1070000LL*60), (50000LL*60)) == EpochStartGate::Armed);
    CC_CHECK_EQ(client.dueTicks, host.dueTicks + (20000LL*60)); // 受領後の推定変動で再計算しない。
    CC_CHECK(host.Update(true, client.local, 0, (1080000LL*60), (50000LL*60)) == EpochStartGate::Armed);
    CC_CHECK(!offer.NewerThan(host.local));
    auto corrupt = host.local; corrupt.hostTicks++;
    CC_CHECK(!corrupt.NewerThan(host.local));

    CC_CASE("提案喪失時は再提案、確定後の遅着は開始せず失敗する");
    host.Begin(131072); client.Begin(131072);
    host.Update(true, client.local, 0, (1000000LL*60), (50000LL*60));
    const auto stale = host.local;
    host.Update(true, client.local, 0, (1490000LL*60), (50000LL*60));
    CC_CHECK(host.local.serial > stale.serial);
    client.Update(false, host.local, host.dueTicks, (1500000LL*60), (50000LL*60));
    CC_CHECK(!stale.NewerThan(client.local));
    host.Update(true, client.local, 0, (1510000LL*60), (50000LL*60));
    CC_CHECK(client.Update(false, host.local, host.dueTicks, host.dueTicks, (50000LL*60)) == EpochStartGate::Expired);
    CC_CHECK(host.Update(true, client.local, 0, host.dueTicks, (50000LL*60)) == EpochStartGate::Expired);

    CC_CASE("遅い受領応答では確定往復の余裕を確保して再提案する");
    host.Begin(196608); client.Begin(196608);
    host.Update(true, client.local, 0, 1000000LL*60, 350000LL*60);
    const auto lateOffer = host.local;
    auto lateAccepted = lateOffer; lateAccepted.stage = EpochStart::Accepted;
    host.Update(true, lateAccepted, 0, host.dueTicks - 350000LL*60, 350000LL*60);
    CC_CHECK(host.local.stage == EpochStart::Offer && host.local.serial > lateOffer.serial);

    CC_CASE("高遅延・揺れ・20%喪失でも共通時刻に確定し余分な静止を短縮する");
    for (const int64_t rttUs : {40000LL, 350000LL}) {
        EpochStartGate h, c;
        h.Begin(262144); c.Begin(262144);
        EpochStart peerH{}, peerC{};
        struct Packet { int64_t due; bool toHost; EpochStart value; };
        std::vector<Packet> packets;
        constexpr int64_t offset = 20000;
        bool armedH = false, armedC = false;
        unsigned serial = 0;
        for (int64_t us = 1000000; us < 5000000 && !(armedH && armedC); us += 1000) {
            for (auto it = packets.begin(); it != packets.end();) {
                if (it->due > us) { ++it; continue; }
                auto &peer = it->toHost ? peerH : peerC;
                if (it->value.NewerThan(peer)) peer = it->value;
                it = packets.erase(it);
            }
            const auto hr = h.Update(true, peerH, 0, us*60, rttUs*60);
            const auto cr = c.Update(false, peerC, peerC.hostTicks + offset*60, (us+offset)*60, rttUs*60);
            CC_CHECK(hr != EpochStartGate::Expired && cr != EpochStartGate::Expired);
            armedH |= hr == EpochStartGate::Armed;
            armedC |= cr == EpochStartGate::Armed;
            if (us % 20000 == 0) {
                for (const bool toHost : {false, true}) {
                    ++serial;
                    if (serial % 5 == 0) continue;
                    const auto jitter = (int(serial % 3) - 1) * std::min<int64_t>(rttUs/8, 25000);
                    packets.push_back({us + rttUs/2 + jitter, toHost, toHost ? c.local : h.local});
                }
            }
            if (armedH && armedC) CC_CHECK(us*60 < h.dueTicks && (us+offset)*60 < c.dueTicks);
        }
        CC_CHECK(armedH && armedC);
        CC_CHECK_EQ(c.dueTicks, h.dueTicks + offset*60);
        CC_CHECK(h.dueTicks - 1000000LL*60 < (rttUs == 350000 ? 1900000LL : 700000LL)*60);
    }

    polls = 0;
    auto &timeline = InputTimeline::GetInstance();
    auto &buffer = MatchInputBuffer::GetInstance();
    buffer.Initialize(0, 2, 4);
    timeline.Reset();
    timeline.Begin(65536, 65539, cccaster::game_interface::GamePhase::InGame, true);
    physical = 16;
    timeline.Pump(0, 16666);
    uint32_t input = 0;
    CC_CASE("ゲームが進まなくても独立した締切で入力を蓄える");
    CC_CHECK(timeline.HasCaptured(65539));
    physical = 2;
    timeline.Pump(16665, 16666);
    CC_CHECK_EQ(polls, 1);
    timeline.Pump(16666, 16666);
    CC_CHECK_EQ(polls, 1);
    timeline.Pump(16666, 16666, 1000000);
    CC_CHECK_EQ(polls, 2);
    CC_CHECK(buffer.TryGetLocalInput(65539, input));
    CC_CHECK_EQ(input, 16);
    CC_CHECK(buffer.TryGetLocalInput(65540, input));
    CC_CHECK_EQ(input, 2);
    CC_CASE("採取スレッドの遅延は以前の値を維持し、最新枠だけ実測する");
    physical = 4;
    timeline.Pump(66666, 16666, 4000000);
    CC_CHECK(buffer.TryGetLocalInput(65541, input));
    CC_CHECK_EQ(input, 2);
    CC_CHECK(buffer.TryGetLocalInput(65542, input));
    CC_CHECK_EQ(input, 2);
    CC_CHECK(buffer.TryGetLocalInput(65543, input));
    CC_CHECK_EQ(input, 4);
    CC_CHECK_EQ(polls, 3);
    CC_CASE("採取の遅延をゲーム更新の予定時刻へ持ち越さない");
    CC_CHECK_EQ(timeline.CapturedDeadlineUs(65541), 33334);
    CC_CHECK_EQ(timeline.CapturedDeadlineUs(65542), 50000);
    CC_CHECK_EQ(timeline.CapturedDeadlineUs(65543), 66667);
    CC_CHECK_EQ(timeline.CapturedDeadlineUs(99999), 0);
    CC_CASE("F4設定中と閉じた後の押しっぱなしを公開前に遮断する");
    mapping = true;
    timeline.Pump(83333, 16666, 5000000);
    CC_CHECK(buffer.TryGetLocalInput(65544, input));
    CC_CHECK_EQ(input, 0);
    mapping = false;
    timeline.Pump(100000, 16666);
    CC_CHECK(buffer.TryGetLocalInput(65545, input));
    CC_CHECK_EQ(input, 0);
    physical = 0;
    timeline.Pump(116666, 16666, 7000000);
    physical = 8;
    timeline.Pump(133333, 16666, 8000000);
    CC_CHECK(buffer.TryGetLocalInput(65547, input));
    CC_CHECK_EQ(input, 8);
    CC_CASE("境界停止中は採取せず、未消費枠を周回上書きしない");
    timeline.Pause();
    timeline.Pump(150000, 16666);
    CC_CHECK(!timeline.HasCaptured(65548));
    timeline.Resume();
    timeline.Pump(20000000, 16666);
    CC_CHECK(timeline.HasOverflowed());
    CC_CHECK(buffer.TryGetLocalInput(65539, input));
    CC_CHECK_EQ(input, 16);
    CC_CASE("受信時刻の更新では公開済み締切を動かさない");
    timeline.Reset(); buffer.Initialize(0,2,4);
    timeline.Begin(65536,65539,cccaster::game_interface::GamePhase::InGame,false,60000000);
    auto &state = cccaster::core::netplay::NetplaySession::GetMutableState();
    {
        std::lock_guard lock(state.scheduleMutex);
        state.peerSchedule = {65536,65539,60000000+6000,59999999,0,60000,0,true,1};
    }
    timeline.PumpTicks(59999999,0);
    CC_CHECK_EQ(timeline.NextDeadlineTicks(),60000000);
    timeline.PumpTicks(60000000,0);
    CC_CHECK_EQ(timeline.CapturedDeadlineTicks(65539),60000000);
    CC_CHECK(timeline.NextDeadlineTicks()>61000000);
    CC_CHECK(timeline.NextDeadlineTicks()<=61000001);
    const auto held = timeline.NextDeadlineTicks();
    {
        std::lock_guard lock(state.scheduleMutex);
        state.peerSchedule.dueTicks += 60000;
        state.peerSchedule.stampTicks++;
    }
    timeline.PumpTicks(60000100,0);
    CC_CHECK_EQ(timeline.NextDeadlineTicks(),held);
    CC_CASE("別世代と未学習の時計から通常位相補正を受けない");
    timeline.Reset(); timeline.Begin(65536,65539,cccaster::game_interface::GamePhase::InGame,false,60000000);
    state.peerSchedule.modelReady=false;
    timeline.PumpTicks(60000000,0);
    CC_CHECK_EQ(timeline.NextDeadlineTicks(),61000000);
    state.peerSchedule.base=131072; state.peerSchedule.modelReady=true;
    timeline.PumpTicks(61000000,0);
    CC_CHECK_EQ(timeline.NextDeadlineTicks(),62000000);
    timeline.Pause();
    using namespace cccaster::core::timer;
    CC_CASE("比例補正は正負対称・最大0.25µs/Fで端数も収束させる");
    for (const int sign : {-1,1}) {
        PhaseFollower follower;
        FrameCadence cadence; cadence.ResetTicks(0);
        int64_t error=sign*60000, previous=0;
        for (uint32_t frame=0;frame<15000;++frame) {
            const auto parts=follower.UpdateParts(error,frame,true);
            CC_CHECK(std::abs(parts)<=15*ClockParts);
            CC_CHECK(std::abs(parts-previous)<=ClockParts);
            CC_CHECK_EQ(follower.UpdateParts(error,frame,true),0);
            const auto before=cadence.NextTicks();
            cadence.AdvanceCorrected(parts);
            error-=cadence.NextTicks()-before-ClockFrame;
            CC_CHECK(sign*error>=0);
            previous=parts;
        }
        CC_CHECK(std::abs(error)<=1);
    }
    CC_CASE("1tick未満の補正も捨てず、反転と観測失効でも補正量を滑らかに変える");
    PhaseFollower follower;
    CC_CHECK_EQ(follower.UpdateParts(1,1,true),ClockParts/600);
    CC_CHECK_EQ(follower.UpdateParts(-1,2,true),-ClockParts/600);
    CC_CHECK_EQ(follower.UpdateParts(-60000,3,false),0);
    follower.Reset();
    int64_t last=0;
    for(uint32_t f=0;f<120;++f) {
        const auto parts=follower.UpdateParts(f<30?600000:-600000,f,f<90);
        CC_CHECK(std::abs(parts-last)<=ClockParts);
        last=parts;
    }
    CC_CHECK_EQ(last,0);
    CC_CASE("1F以上の復帰は新しい同方向観測2つを要求し重複適用しない");
    follower.Reset();
    CC_CHECK_EQ(follower.RecoveryTicks(-3000000,10,1),0);
    CC_CHECK_EQ(follower.RecoveryTicks(-3000000,10,1),0);
    CC_CHECK_EQ(follower.RecoveryTicks(-3000000,10,2),-2985000);
    CC_CHECK_EQ(follower.RecoveryTicks(-3000000,10,3),0);
    CC_CASE("平均RTTの窓は一時スパイクと継続遅延を区別し、空白をゼロ扱いしない");
    MeanNetworkDelay average;
    NetworkPacing pacing;
    int64_t pacingNow = ClockSecond;
    for (int i = 0; i < 360; ++i) {
        const auto mean = average.Add(pacingNow, i >= 120 && i < 138 ? 600000 : 40000);
        CC_CHECK_EQ(pacing.Update(pacingNow, mean, 2), uint32_t(ClockFrame));
        pacingNow += ClockFrame;
    }
    CC_CHECK_EQ(average.Mean(), 40000);
    for (int i = 0; i < 360; ++i) {
        pacing.Update(pacingNow, average.Add(pacingNow, 360000), 2);
        pacingNow += ClockFrame;
    }
    CC_CHECK_EQ(average.Mean(), 360000);
    CC_CHECK_EQ(pacing.Requested(), 20000u * 60);
    // 通信観測が途切れても周期を急に戻さない。
    CC_CHECK_EQ(pacing.Update(pacingNow, 0, 2), 20000u * 60);
    for (int i = 0; i < 360; ++i) {
        pacing.Update(pacingNow, average.Add(pacingNow, 40000), 2);
        pacingNow += ClockFrame;
    }
    CC_CHECK_EQ(pacing.Requested(), uint32_t(ClockFrame));
    pacing.Reset();
    CC_CHECK_EQ(pacing.Update(pacingNow, 300000, 2), uint32_t(ClockFrame));
    for (int i = 0; i < 200; ++i) {
        pacingNow += ClockSecond / 10;
        CC_CHECK(pacing.Update(pacingNow, 6000000, 0) <= NetworkPacing::Maximum);
    }
    CC_CHECK_EQ(pacing.Requested(), NetworkPacing::Maximum);
    CC_CHECK_EQ(average.Add(pacingNow + ClockSecond * 10, 80000), 80000);
    CC_CASE("クライアントはホストの減速周期へ追従し、公開済み締切と入力番号を保つ");
    timeline.Reset(); buffer.Initialize(0,2,7);
    SettingsCommands::Reset(2,7);
    state.appliedFrame = 65536; state.consumedFrame = 65536;
    timeline.Begin(65536,65539,cccaster::game_interface::GamePhase::InGame,false,60000000);
    state.peerSchedule = {65536,65539,60000000,59999999,0,0,
        (30000LL*60-ClockFrame)*ClockParts,false,0,30000*60,30000*60};
    timeline.PumpTicks(60000000,0);
    CC_CHECK_EQ(timeline.CapturedDeadlineTicks(65539),60000000);
    CC_CHECK_EQ(timeline.NextDeadlineTicks(),61800000);
    state.peerSchedule.periodTicks = ClockFrame;
    state.peerSchedule.periodCorrectionParts = 0;
    state.peerSchedule.frame = 65540; state.peerSchedule.dueTicks = 61800000;
    state.peerSchedule.stampTicks = 61000000;
    timeline.PumpTicks(61000000,0);
    CC_CHECK_EQ(timeline.NextDeadlineTicks(),61800000);
    timeline.PumpTicks(61800000,0);
    CC_CHECK_EQ(timeline.CapturedDeadlineTicks(65540),61800000);
    CC_CHECK_EQ(timeline.NextDeadlineTicks(),62800000);
    CC_CHECK_EQ(SettingsCommands::delay.load(),2);
    CC_CASE("ロードのローカル入力は対戦履歴・再送位置・通信時計を変更しない");
    timeline.Reset(); buffer.Initialize(65539, 2, 7);
    buffer.WriteLocal(65539, 123, 0, false);
    mapping = false; physical = 16;
    timeline.Begin(0, 1, cccaster::game_interface::GamePhase::Loading, true, 60000000);
    timeline.PumpTicks(60000000, 0);
    uint32_t loadingTick = 0;
    CC_CHECK_EQ(buffer.ReadLoadingInput(loadingTick), 16);
    CC_CHECK_EQ(loadingTick, 1);
    CC_CHECK_EQ(buffer.GetWriteHead(), 65539);
    CC_CHECK(buffer.TryGetLocalInput(65539, input)); CC_CHECK_EQ(input, 123);
    CC_CHECK_EQ(state.localSchedule.base, 0);
    CC_CHECK_EQ(state.localSchedule.frame, 0);
    // 10秒を超えるロードも対戦リングを上書きせず入力受付を継続する。
    for (uint32_t i = 1; i <= 700; ++i) timeline.PumpTicks(60000000LL + i * ClockFrame, 0);
    CC_CHECK(!timeline.HasOverflowed());
    CC_CHECK_EQ(buffer.GetWriteHead(), 65539);
    CC_CHECK(buffer.TryGetLocalInput(65539, input)); CC_CHECK_EQ(input, 123);
    CC_CASE("ロード中もF4設定と閉じた直後の押しっぱなしを遮断する");
    mapping = true;
    timeline.PumpTicks(761000000, 0); CC_CHECK_EQ(buffer.ReadLoadingInput(loadingTick), 0);
    mapping = false;
    timeline.PumpTicks(762000000, 0); CC_CHECK_EQ(buffer.ReadLoadingInput(loadingTick), 0);
    physical = 0;
    timeline.PumpTicks(763000000, 0);
    physical = 32;
    timeline.PumpTicks(764000000, 0); CC_CHECK_EQ(buffer.ReadLoadingInput(loadingTick), 32);
    CC_CASE("ロード終了後は通常世代の先頭入力から採取し直す");
    timeline.Pause(); physical = 0;
    timeline.Begin(131072, 131075, cccaster::game_interface::GamePhase::InGame, true, 800000000);
    timeline.PumpTicks(799999999, 0); CC_CHECK(!timeline.HasCaptured(131075));
    timeline.PumpTicks(800000000, 0);
    CC_CHECK(buffer.TryGetLocalInput(131075, input)); CC_CHECK_EQ(input, 0);
    using cccaster::game_interface::GameInput;
    using cccaster::game_interface::GamePhase;
    namespace options = cccaster::domain::scene::selection_options;
    auto beginMenu = [&] {
        timeline.Reset(); buffer.Initialize(65536, 2, 7);
        SettingsCommands::Reset(2, 7);
        state.peerSchedule = {};
        mapping = false; physical = 0;
        timeline.Begin(65536, 65537, GamePhase::CharaSelect, true, 60000000);
    };
    auto captureMenu = [&] {
        const auto frame = timeline.NextCapture().frame;
        GameInput menuInput;
        CC_CHECK(!timeline.TryGetMenuInput(frame, menuInput));
        timeline.PumpTicks(timeline.NextDeadlineTicks(), 0);
        CC_CHECK(timeline.TryGetMenuInput(frame, menuInput));
        timeline.SetConsumed(frame);
        return menuInput;
    };
    auto changed = [](const options::Result &result) {
        return result.animation >= 0 || result.hudStep || result.resolutionStep ||
               result.fullscreen >= 0 || result.nativeStep;
    };
    CC_CASE("左メニューの全A対応項目は保持で1回、解放後の再押下で次の1回だけ変更する");
    for (const auto buttons : {CC_BUTTON_A, CC_BUTTON_CONFIRM, CC_BUTTON_A | CC_BUTTON_CONFIRM}) {
        for (unsigned row = 1; row < options::Menu::RowCount; ++row) {
            beginMenu();
            options::Menu menu, filteredMenu;
            menu.open = filteredMenu.open = true;
            menu.row = filteredMenu.row = row;
            unsigned changes = 0, filteredChanges = 0;
            physical = GameInput{0, static_cast<uint16_t>(buttons)}.Pack();
            for (unsigned i = 0; i < 24; ++i) {
                const auto menuInput = captureMenu();
                CC_CHECK_EQ(menuInput.Pack(), physical);
                changes += changed(menu.Step(menuInput, 0, true, false, true, true));
                // 従来のゲーム入力をメニューへ渡すと複数回押下になることも再現する。
                CC_CHECK(buffer.TryGetLocalInput(timeline.SampledFrame(), input));
                filteredChanges += changed(filteredMenu.Step(GameInput::Unpack(input), 0, true, false, true, true));
            }
            CC_CHECK_EQ(changes, 1u);
            CC_CHECK(filteredChanges > 1);
            physical = 0;
            CC_CHECK(!changed(menu.Step(captureMenu(), 0, true, false, true, true)));
            physical = GameInput{0, static_cast<uint16_t>(buttons)}.Pack();
            for (unsigned i = 0; i < 12; ++i)
                changes += changed(menu.Step(captureMenu(), 0, true, false, true, true));
            CC_CHECK_EQ(changes, 2u);
        }
    }
    CC_CASE("メニューを閉じてもA保持中はキャラ選択へ流さず、全解放の後だけ通す");
    beginMenu();
    options::Menu closing;
    closing.open = true; closing.row = 2;
    physical = CC_BUTTON_A | CC_BUTTON_CONFIRM;
    closing.Step(captureMenu(), 0, true, false, true, true);
    CC_CHECK(closing.Step(captureMenu(), options::Close, true, false, true, true).block);
    for (unsigned i = 0; i < 12; ++i)
        CC_CHECK(closing.Step(captureMenu(), 0, true, false, true, true).block);
    physical = 0;
    CC_CHECK(closing.Step(captureMenu(), 0, true, false, true, true).block);
    physical = CC_BUTTON_A | CC_BUTTON_CONFIRM;
    CC_CHECK(!closing.Step(captureMenu(), 0, true, false, true, true).block);
    CC_CASE("メニュー用入力もF4設定中と閉じた直後のA保持を遮断する");
    beginMenu();
    physical = CC_BUTTON_A | CC_BUTTON_CONFIRM;
    mapping = true;
    for (unsigned i = 0; i < 12; ++i) CC_CHECK(captureMenu().IsNeutral());
    mapping = false;
    for (unsigned i = 0; i < 12; ++i) CC_CHECK(captureMenu().IsNeutral());
    physical = 0; CC_CHECK(captureMenu().IsNeutral());
    physical = CC_BUTTON_A | CC_BUTTON_CONFIRM;
    CC_CHECK_EQ(captureMenu().Pack(), physical);
    CC_CASE("遅れて消費するメニュー入力は同じFの保持状態を使い、最新の解放で書き換えない");
    beginMenu();
    physical = CC_BUTTON_A | CC_BUTTON_CONFIRM;
    const auto heldInput = captureMenu();
    const auto firstHeld = timeline.SampledFrame();
    physical = 0;
    timeline.PumpTicks(timeline.NextDeadlineTicks() + 2 * ClockFrame, 0);
    GameInput captured;
    for (unsigned i = 0; i < 3; ++i) {
        CC_CHECK(timeline.TryGetMenuInput(firstHeld + i, captured));
        CC_CHECK_EQ(captured.Pack(), heldInput.Pack());
    }
    CC_CHECK(timeline.TryGetMenuInput(firstHeld + 3, captured));
    CC_CHECK(captured.IsNeutral());
    timeline.SetConsumed(timeline.SampledFrame());
    CC_CASE("周回上書き・停止・世代変更・対戦中のメニュー入力を誤取得しない");
    for (unsigned i = 0; i < MatchInputBuffer::RING_SIZE; ++i) captureMenu();
    CC_CHECK(!timeline.TryGetMenuInput(firstHeld, captured));
    const auto lastMenuFrame = timeline.SampledFrame();
    timeline.Pause(); CC_CHECK(!timeline.TryGetMenuInput(lastMenuFrame, captured));
    timeline.Resume(); CC_CHECK(timeline.TryGetMenuInput(lastMenuFrame, captured));
    timeline.Begin(131072, 131073, GamePhase::CharaSelect, true, 800000000);
    CC_CHECK(!timeline.TryGetMenuInput(lastMenuFrame, captured));
    CC_CHECK(!timeline.TryGetMenuInput(131073, captured));
    captureMenu();
    timeline.Reset(); CC_CHECK(!timeline.TryGetMenuInput(131073, captured));
    timeline.Begin(131072, 131073, GamePhase::InGame, true, 800000000);
    timeline.PumpTicks(800000000, 0);
    CC_CHECK(!timeline.TryGetMenuInput(131073, captured));
    return cccaster::test::Summarize("input_timeline");
}

namespace cccaster::core::netplay {
NetplaySession &NetplaySession::GetInstance() {
    static NetplaySession s;
    return s;
}
void NetplaySession::Stop() {}
} // namespace cccaster::core::netplay
