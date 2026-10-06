#include "test_support.hpp"
#include "core_dll/engine/RematchChoice.hpp"
#include "core_dll/engine/RetryInputGate.hpp"
#include "core_dll/sync/RetrySelection.hpp"
#include "core_dll/engine/StageRematch.hpp"
#include "core_dll/mbaa_mem/RandomStage.hpp"
#include "core_dll/mbaa_mem/RoundTest.hpp"
using namespace cccaster::domain::scene;
using namespace cccaster::game_interface;
const GameInput confirm{0, CC_BUTTON_A};
void open(RematchChoice &r) {
    for (int i = 0; i < 31; ++i)
        r.Step({}, {});
}
int main() {
    CC_CASE("試験の境界注入は世代内Fで決まり巻戻しても再現する");
    using cccaster::testing::RoundTestFrame;
    using cccaster::testing::RoundTestDue;
    for (const char *value : {"", "0", "-1", "120x", "60001", "99999999999999999999"})
        CC_CHECK_EQ(RoundTestFrame(value), 600u);
    CC_CHECK_EQ(RoundTestFrame(nullptr), 600u);
    CC_CHECK_EQ(RoundTestFrame("120"), 120u);
    CC_CHECK_EQ(RoundTestFrame("60000"), 60000u);
    CC_CHECK_EQ(RoundTestFrame(nullptr, 0), 0u);
    CC_CHECK_EQ(RoundTestFrame("3", 0), 3u);
    CC_CHECK(RoundTestDue(196608 + 120, 120, 3));
    CC_CHECK(!RoundTestDue(393216 + 120, 120, 3));
    CC_CHECK(RoundTestDue(196608 + 120, 120, 3)); // 上限後から巻戻しても同じ条件。
    for (auto epoch : {65536u, 131072u, 196608u}) {
        CC_CHECK(RoundTestDue(epoch + 121, 120));
        CC_CHECK(!RoundTestDue(epoch + 119, 120));
        CC_CHECK(RoundTestDue(epoch + 120, 120));
        CC_CHECK(!RoundTestDue(epoch, 120));
    }
    CC_CASE("全55ステージから指定8件だけをランダム除外する");
    std::array<uint32_t, 60> available;
    available.fill(1);
    for (auto gap : {11, 14, 15, 33}) available[gap] = 0;
    cccaster::game_memory::stages::RandomPool pool(available);
    CC_CHECK_EQ(pool.count, 47u);
    std::array<uint32_t, 60> hits{};
    for (uint32_t r = pool.count; r < pool.count * 2; ++r) {
        uint32_t stage = 0;
        CC_CHECK(pool.Pick(r, stage));
        ++hits[stage];
    }
    for (auto stage : {0, 11, 14, 15, 32, 33, 43, 44, 51, 54, 57, 58, 59})
        CC_CHECK_EQ(hits[stage], 0u);
    for (uint32_t stage = 1; stage < 60; ++stage)
        if (available[stage] && cccaster::game_memory::stages::RandomAllowed(stage)) CC_CHECK_EQ(hits[stage], 1u);
    CC_CASE("ランダム再戦は直前の1件を除き残る46候補を等確率で選ぶ");
    for (uint32_t previous = 1; previous < 60; ++previous) {
        if (!hits[previous]) continue;
        const cccaster::game_memory::stages::RandomPool next(available, previous);
        CC_CHECK_EQ(next.count, 46u);
        std::array<uint32_t, 60> nextHits{};
        for (uint32_t r = next.count; r < next.count * 2; ++r) {
            uint32_t stage = 0;
            CC_CHECK(next.Pick(r, stage));
            CC_CHECK(stage != previous);
            ++nextHits[stage];
        }
        for (uint32_t stage = 0; stage < 60; ++stage)
            CC_CHECK_EQ(nextHits[stage], stage == previous ? 0u : hits[stage]);
    }
    // 元から候補にない番号を除いても、他の候補は減らさない。
    for (auto previous : {0u, 11u, 59u, UINT32_MAX})
        CC_CHECK_EQ(cccaster::game_memory::stages::RandomPool(available, previous).count, 47u);
    uint32_t selected = 123;
    CC_CHECK(!pool.Pick(0, selected)); // 剰余の偏りを生む末端は再抽選。
    CC_CHECK_EQ(selected, 123u);
    available.fill(0);
    CC_CHECK(!cccaster::game_memory::stages::RandomPool(available).Pick(UINT32_MAX, selected));
    available[55] = 1;
    CC_CHECK(cccaster::game_memory::stages::RandomPool(available).Pick(0, selected));
    CC_CHECK_EQ(selected, 55u);
    // 代替候補がない異常環境でも同じ番号へフォールバックせず、失敗を返す。
    CC_CHECK(!cccaster::game_memory::stages::RandomPool(available, 55).Pick(0, selected));
    CC_CHECK_EQ(selected, 55u);

    CC_CASE("RANDOMのONCEだけ再抽選し、固定指定とキャラセレは従来通り");
    cccaster::core::sync::SelectionState host, guest;
    host.epoch = guest.epoch = 65536; host.revision = guest.revision = 3;
    host.character = 51; host.selector = 4; host.moon = 2; host.color = 35;
    guest.character = 33; guest.selector = 31; guest.moon = 1; guest.color = 15;
    host.confirmed = guest.confirmed = host.stageConfirmed = 1; host.stage = 55;
    host.ack = guest.ack = 3;
    StageRematch retry;
    CC_CHECK(!retry.Begin(0, true, host, guest));
    host.randomStage = 1;
    CC_CHECK(!retry.Begin(1, true, host, guest));
    CC_CHECK(!retry.Begin(-1, true, host, guest));
    CC_CHECK(retry.Begin(0, true, host, guest));
    auto again = retry.Restore(131072, true, 12);
    CC_CHECK(again.Valid());
    CC_CHECK_EQ(again.character, host.character); CC_CHECK_EQ(again.moon, 2u); CC_CHECK_EQ(again.color, 35u);
    CC_CHECK_EQ(again.stage, 12u); CC_CHECK_EQ(again.randomStage, 1u); CC_CHECK_EQ(again.ack, 0u);
    CC_CHECK_EQ(again.revision, 1u);
    CC_CHECK(!again.PeerHasFinal(guest)); // 前回のACKを流用しない。
    CC_CHECK(retry.Begin(0, false, guest, host));
    auto nextGuest = retry.Restore(131072, false, 0);
    CC_CHECK(nextGuest.Valid());
    CC_CHECK_EQ(nextGuest.character, guest.character); CC_CHECK_EQ(nextGuest.moon, 1u); CC_CHECK_EQ(nextGuest.color, 15u);
    CC_CHECK_EQ(nextGuest.stageConfirmed, 0u); CC_CHECK_EQ(nextGuest.randomStage, 0u);
    CC_CHECK(!again.PeerHasFinal(nextGuest));
    nextGuest.ack = again.revision;
    CC_CHECK(again.PeerHasFinal(nextGuest));
    CC_CHECK(retry.Begin(0, true, again, nextGuest)); // 2回目以降のONCEも再抽選。
    CC_CHECK(!retry.Begin(1, false, nextGuest, again));

    using R = cccaster::core::sync::RetrySelection;
    CC_CASE("本来のメニューの確定通知は双方ONCEとACKがそろうまで待つ");
    R a{65536, 1, 0}, b{65536, 0, 1};
    CC_CHECK_EQ(a.Result(b), -1);
    b.choice = 1;
    CC_CHECK(!a.CanRelease(b));
    a.ack = 1;
    CC_CHECK(a.CanRelease(b));
    CC_CHECK(b.CanRelease(a));
    CC_CASE("片側キャラセレは相手の選択を待たず受信確認だけを待つ");
    a = {65536, 2, 0}; b = {65536, 0, 0};
    CC_CHECK(!a.CanRelease(b));
    b.ack = 2;
    CC_CHECK(a.CanRelease(b));
    CC_CHECK(b.CanRelease(a));
    CC_CHECK_EQ(a.Result(b), 1);
    CC_CASE("通知損失・順序逆転・古い世代で確定を取り消さない");
    R received{65536, 0, 0};
    CC_CHECK(received.Accept(a));
    CC_CHECK(received.Accept({65536, 0, 0}));
    CC_CHECK_EQ(received.choice, 2u);
    CC_CHECK(!received.Accept({65536, 1, 0}));
    CC_CHECK(!received.Accept({0, 0, 0}));
    CC_CHECK(!received.Accept({65536, 3, 0}));
    CC_CHECK(!received.Accept({65537, 0, 0}));
    CC_CHECK(received.Accept({131072, 0, 0}));
    CC_CHECK_EQ(received.choice, 0u);
    CC_CHECK(!a.CanRelease(received));
    CC_CASE("カーソル移動直後と画面入口の押しっぱなしを遮断する");
    RetryInputGate gate;
    for (int i=0; i<60; ++i) CC_CHECK(gate.Apply(confirm).IsNeutral());
    gate.Apply({});
    CC_CHECK_EQ(gate.Apply(confirm).buttons, CC_BUTTON_A | CC_BUTTON_CONFIRM);
    CC_CHECK_EQ(gate.Apply({Dir::Down, CC_BUTTON_A}).buttons, 0);
    CC_CHECK_EQ(gate.Apply(confirm).buttons, 0);
    CC_CHECK_EQ(gate.Apply(confirm).buttons, 0);
    CC_CHECK_EQ(gate.Apply(confirm).buttons, CC_BUTTON_A | CC_BUTTON_CONFIRM);
    CC_CHECK_EQ(gate.Apply({0, CC_BUTTON_CONFIRM}).buttons, CC_BUTTON_CONFIRM);
    CC_CHECK_EQ(gate.Apply({0, CC_BUTTON_B | CC_BUTTON_CANCEL | CC_BUTTON_START}).buttons, 0);
    CC_CASE("両者ワンスまで待つ");
    RematchChoice r;
    open(r);
    r.Step(confirm, {});
    CC_CHECK_EQ(r.result, -1);
    for (int i = 0; i < 2000; ++i)
        r.Step(confirm, {});
    CC_CHECK_EQ(r.result, -1);
    r.Step({}, confirm);
    CC_CHECK_EQ(r.result, 0);
    CC_CASE("どちらかがキャラセレなら未決定の相手を待たない");
    for (int who = 0; who < 2; ++who) {
        r.Reset();
        open(r);
        r.Step(who == 0 ? GameInput{Dir::Down, 0} : GameInput{},
               who == 1 ? GameInput{Dir::Down, 0} : GameInput{});
        r.Step(who == 0 ? confirm : GameInput{}, who == 1 ? confirm : GameInput{});
        CC_CHECK_EQ(r.result, 1);
    }
    CC_CASE("同時選択はキャラセレ優先");
    r.Reset();
    open(r);
    r.Step({}, {Dir::Down, 0});
    r.Step(confirm, confirm);
    CC_CHECK_EQ(r.result, 1);
    CC_CASE("持ち越し入力では確定しない");
    r.Reset();
    for (int i = 0; i < 100; ++i)
        r.Step(confirm, confirm);
    CC_CHECK_EQ(r.result, -1);
    r.Step({}, {});
    r.Step(confirm, confirm);
    CC_CHECK_EQ(r.result, 0);
    CC_CASE("保存・キャンセル・スタートは選択しない");
    r.Reset();
    open(r);
    for (int i = 0; i < 50; ++i) {
        r.Step({Dir::Down, CC_BUTTON_B | CC_BUTTON_START}, {});
        r.Step({}, {});
    }
    CC_CHECK_EQ(r.players[0].cursor, 1);
    CC_CHECK_EQ(r.result, -1);
    CC_CASE("世代リセットで前回の同意を破棄");
    r.Reset();
    CC_CHECK_EQ(r.players[0].choice, -1);
    CC_CHECK_EQ(r.result, -1);
    return cccaster::test::Summarize("rematch");
}
