#include "test_support.hpp"
#include "core_dll/mbaa_mem/FrameBar.hpp"
#include "core_dll/mbaa_mem/TrainingAnimation.hpp"
#include "core_dll/ui/HudDisplay.hpp"
using namespace cccaster;
int main() {
    FrameBarHistory h;
    TrainingFrameSample s;
    s.valid = true; s.pattern[0] = 1;
    s.detail[0].valid = s.detail[1].valid = true;
    s.detail[0].attackBoxesKnown = true;
    s.detail[0].attackBoxCount = 1;
    auto step = [&] { ++s.trueFrame; ++s.simulationFrame; h.Update(1, s); };
    CC_CASE("行動不能と攻撃判定を独立記録し停止を重ねる");
    step(); CC_CHECK_EQ(h.Size(), 0u);
    s.inactionable[0] = 8; step();
    CC_CHECK_EQ(h.At(0).players[0].state, FrameBarState::Busy);
    CC_CHECK_EQ(h.At(0).players[0].runFrame, 1u);
    s.attacking[0] = true; s.playerStopped[0] = true; step();
    CC_CHECK_EQ(h.At(1).players[0].state, FrameBarState::Busy);
    CC_CHECK_EQ(h.At(1).players[0].runFrame, 2u);
    CC_CHECK_EQ(h.At(1).players[0].signalFrame, 1u);
    CC_CHECK(h.At(1).players[0].busy && h.At(1).players[0].stopped);
    CC_CHECK(!h.At(1).players[1].stopped);
    s.blockstun[1] = true; s.inactionable[1] = 1; s.pattern[1] = 18; step();
    CC_CHECK_EQ(h.At(2).players[1].state, FrameBarState::Stun);
    CC_CHECK_EQ(h.At(2).players[0].runFrame, 3u);
    CC_CHECK_EQ(h.At(2).players[0].signalFrame, 2u);
    CC_CHECK_EQ(h.At(2).players[1].runFrame, 1u);
    s.blockstun[1] = false; s.detail[1].thrown = true; s.inactionable[1] = 0; s.pattern[1] = 350; step();
    CC_CHECK_EQ(h.At(3).players[1].state, FrameBarState::Stun);
    s.globalFreeze = true; step(); CC_CHECK(h.At(4).players[1].stopped);

    CC_CASE("描画重複は追加せず、メニュー停止も記録しない");
    h.Update(1, s); CC_CHECK_EQ(h.Size(), 5u);
    s.paused = true; step(); CC_CHECK_EQ(h.Size(), 5u);
    CC_CHECK_EQ(h.At(4).players[0].runFrame, 5u);
    s.paused = false;
    CC_CASE("45Fを超えた履歴は古い側から捨てる");
    s.globalFreeze = false; s.playerStopped[0] = false;
    for (unsigned i = 0; i < 100; ++i) step();
    CC_CHECK_EQ(h.Size(), 45u);
    CC_CHECK_EQ(h.At(0).players[0].state, FrameBarState::Busy);
    CC_CHECK_EQ(h.At(0).players[0].runFrame, 61u);
    CC_CHECK_EQ(h.At(44).players[0].runFrame, 105u);

    CC_CASE("双方ニュートラル5Fで保持し次動作で新規記録");
    s.inactionable[0] = 0; s.detail[1].thrown = false; s.pattern[1] = 0; s.attacking[0] = false;
    for (unsigned i = 0; i < 4; ++i) step();
    CC_CHECK(!h.Holding());
    step();
    CC_CHECK(h.Holding());
    for (unsigned i = 0; i < 100; ++i) step();
    CC_CHECK_EQ(h.At(39).players[0].state, FrameBarState::Busy);
    CC_CHECK_EQ(h.At(40).players[0].state, FrameBarState::Ready);
    s.inactionable[0] = 1; step(); CC_CHECK_EQ(h.Size(), 1u);
    CC_CHECK_EQ(h.At(0).players[0].runFrame, 1u);

    CC_CASE("欠測、巻戻し、ラウンド、交代、モード変更で履歴破棄");
    s.trueFrame += 2; h.Update(1, s); CC_CHECK_EQ(h.Size(), 1u);
    CC_CHECK_EQ(h.At(0).players[0].runFrame, 1u);
    step(); CC_CHECK_EQ(h.Size(), 2u);
    s.trueFrame = s.simulationFrame = 1; h.Update(1, s); CC_CHECK_EQ(h.Size(), 1u);
    step(); ++s.round; step(); CC_CHECK_EQ(h.Size(), 1u);
    step(); s.activeCharacter[0] = 2; step(); CC_CHECK_EQ(h.Size(), 1u);
    step(); h.Update(2, s); CC_CHECK_EQ(h.Size(), 1u);
    h.Update(0, s); CC_CHECK_EQ(h.Size(), 0u);
    h.Update(2, s); CC_CHECK_EQ(h.Size(), 1u);
    h.Update(2, {}); CC_CHECK_EQ(h.Size(), 0u);
    CC_CASE("参照の優先順: 防御配列形式、被弾、ジャンプ、無敵、通常動作");
    s = {}; s.valid = true; s.pattern[0] = 1; s.inactionable[0] = 1;
    s.detail[0].animationKnown = true; s.detail[0].defenseSlotCount = 2;
    const uint32_t hitPatterns[]{17,18,19,26,29,30,350,354,900,901,902,903,904,905,906,907,908};
    for (auto pattern : hitPatterns) {
        s.pattern[0] = pattern;
        CC_CHECK_EQ(ClassifyFrameBar(s, 0).state, FrameBarState::Stun);
    }
    s.pattern[0] = 900; s.detail[0].defenseSlotCount = 10;
    CC_CHECK_EQ(ClassifyFrameBar(s, 0).state, FrameBarState::Shield);
    s.detail[0].defenseSlotCount = 12;
    CC_CHECK_EQ(ClassifyFrameBar(s, 0).state, FrameBarState::Clash);
    s.detail[0].defenseSlotCount = 1;
    CC_CHECK_EQ(ClassifyFrameBar(s, 0).state, FrameBarState::Stun);
    for (uint32_t pattern : {34u,35u,36u,37u,38u,39u,40u,54u,476u}) {
        s.pattern[0] = pattern;
        CC_CHECK_EQ(ClassifyFrameBar(s, 0).state, FrameBarState::Jump);
    }
    s.pattern[0] = 1;
    CC_CHECK_EQ(ClassifyFrameBar(s, 0).state, FrameBarState::Invulnerable);
    s.detail[0].defenseSlotCount = 2;
    CC_CHECK_EQ(ClassifyFrameBar(s, 0).state, FrameBarState::Busy);
    for (uint32_t pattern : {0u,10u,11u,12u,13u,14u,15u,16u,20u,594u}) {
        s.pattern[0] = pattern;
        CC_CHECK_EQ(ClassifyFrameBar(s, 0).state, FrameBarState::Ready);
    }
    s.detail[0].strikeProtected = true;
    CC_CHECK_EQ(ClassifyFrameBar(s, 0).state, FrameBarState::Invulnerable);
    s.detail[0].strikeProtected = false; s.detail[0].animationKnown = false;
    s.detail[0].defenseSlotCount = 0; s.pattern[0] = 1;
    CC_CHECK_EQ(ClassifyFrameBar(s, 0).state, FrameBarState::Busy);
    s.inactionable[0] = 0; s.pattern[0] = 350;
    CC_CHECK_EQ(ClassifyFrameBar(s, 0).state, FrameBarState::Stun);
    s.pattern[0] = 900;
    CC_CHECK_EQ(ClassifyFrameBar(s, 0).state, FrameBarState::Ready);

    CC_CASE("主状態と攻撃属性・実矩形・ヒットストップを混同しない");
    s.inactionable[0] = 1; s.attacking[0] = true;
    s.detail[0].attackBoxesKnown = true;
    auto cell = ClassifyFrameBar(s, 0);
    CC_CHECK_EQ(cell.state, FrameBarState::Stun);
    CC_CHECK_EQ(cell.signal, FrameBarSignal::Attack); CC_CHECK(!cell.activeBoxes);
    s.detail[0].attackBoxCount = 1; s.detail[0].remainingHits = 0;
    cell = ClassifyFrameBar(s, 0);
    CC_CHECK(cell.activeBoxes); // 接触可否と矩形の存在は別。
    s.detail[0].hitstop = 1;
    cell = ClassifyFrameBar(s, 0);
    CC_CHECK_EQ(cell.signal, FrameBarSignal::Hitstop); CC_CHECK(cell.activeBoxes);
    s.detail[0].attackBoxesKnown = false;
    cell = ClassifyFrameBar(s, 0);
    CC_CHECK_EQ(cell.signal, FrameBarSignal::Hitstop); CC_CHECK(!cell.activeBoxes);
    s.detail[0].hitstop = 0; s.attacking[0] = false; s.playerStopped[0] = true;
    cell = ClassifyFrameBar(s, 0);
    CC_CHECK_EQ(cell.signal, FrameBarSignal::None); CC_CHECK(cell.stopped);

    CC_CASE("各色の連続数は独立し、停止前後で攻撃の番号を再始動する");
    h.Reset(); s = {}; s.valid = true; s.pattern[0] = 1; s.inactionable[0] = 1;
    s.attacking[0] = true; step(); step();
    CC_CHECK_EQ(h.At(1).players[0].signalFrame, 2u);
    s.detail[0].hitstop = 2; step();
    CC_CHECK_EQ(h.At(2).players[0].runFrame, 3u);
    CC_CHECK_EQ(h.At(2).players[0].signalFrame, 1u);
    s.detail[0].hitstop = 0; step();
    CC_CHECK_EQ(h.At(3).players[0].runFrame, 4u);
    CC_CHECK_EQ(h.At(3).players[0].signalFrame, 1u);
    s.attacking[0] = false; step();
    CC_CHECK_EQ(h.At(4).players[0].signalFrame, 0u);
    h.Reset(); s = {}; s.valid = true; s.detail[0].airborne = true; step();
    CC_CHECK_EQ(h.Size(), 1u); // 空中下線も記録継続の理由。

    CC_CASE("タイマー集約停止と個別停止を混同せず、保護・スローを重ねる");
    h.Reset(); s = {}; s.valid = true; s.pattern[0] = 1; s.timerSuppressed = true; step();
    CC_CHECK_EQ(h.Size(), 0u); // 補助タイマーだけで空白を流し続けない。
    s.detail[0].strikeProtected = true; s.detail[0].throwProtected = true; step();
    CC_CHECK(h.At(0).players[0].timerSuppressed && !h.At(0).players[0].stopped);
    ++s.trueFrame; h.Update(1, s);
    CC_CHECK(h.At(1).players[0].slow);
    CC_CHECK(h.At(1).players[0].detail.strikeProtected && h.At(1).players[0].detail.throwProtected);
    s.detail[0].strikeProtected = s.detail[0].throwProtected = false;
    for (unsigned i = 0; i < 4; ++i) step();
    CC_CHECK(!h.Holding()); step(); CC_CHECK(h.Holding());

    CC_CASE("同色の次動作と動作時計の巻戻しで区間を区切る");
    h.Reset(); s = {}; s.valid = true; s.pattern[0] = 1; s.detail[0].valid = true; s.inactionable[0] = 2;
    s.pattern[0] = 100; s.detail[0].patternFrame = 1; step();
    s.detail[0].patternFrame = 2; step(); CC_CHECK_EQ(h.At(1).players[0].runFrame, 2u);
    s.pattern[0] = 101; s.detail[0].patternFrame = 1; step();
    CC_CHECK(h.At(2).players[0].boundary); CC_CHECK_EQ(h.At(2).players[0].runFrame, 1u);
    s.detail[0].patternFrame = 3; step(); s.detail[0].patternFrame = 1; step();
    CC_CHECK(h.At(4).players[0].boundary); CC_CHECK_EQ(h.At(4).players[0].runFrame, 1u);
    s.inactionable[0] = 0; step(); ++s.pattern[0]; step();
    CC_CHECK_EQ(h.At(6).players[0].runFrame, 2u);

    CC_CASE("矩形配列の排他的上限・NULL穴・読取失敗・255件を扱う");
    std::array<uint8_t, 0x800> memory{};
    const auto put32 = [&](size_t offset, uint32_t value) { std::memcpy(memory.data() + offset, &value, 4); };
    unsigned readCalls = 0;
    const auto read = [&](uint32_t address, void *output, size_t length) {
        ++readCalls;
        if (address < 0x1000 || address - 0x1000 > memory.size() ||
            length > memory.size() - (address - 0x1000)) return false;
        std::memcpy(output, memory.data() + address - 0x1000, length);
        return true;
    };
    TrainingActorDetail detail;
    put32(0x38, 0x1100); memory[0x10C] = 1;
    put32(0x50, 0x1200); memory[0x43] = 3;
    put32(0x200, 0x1500); put32(0x204, 0); put32(0x208, 0x1508); put32(0x20C, 0x1510);
    ReadTrainingAnimation(0x1000, true, detail, read);
    CC_CHECK(detail.stanceKnown && detail.stance == 1 && detail.attackBoxesKnown);
    CC_CHECK_EQ(detail.attackBoxCount, 2u);
    put32(0x50, 0); ReadTrainingAnimation(0x1000, true, detail, read);
    CC_CHECK(!detail.attackBoxesKnown);
    memory[0x43] = 0; ReadTrainingAnimation(0x1000, true, detail, read);
    CC_CHECK(detail.attackBoxesKnown && detail.attackBoxCount == 0);
    memory[0x43] = 255; put32(0x50, 0x1200);
    for (size_t i = 0; i < 255; ++i) put32(0x200 + i * 4, 0x1600);
    ReadTrainingAnimation(0x1000, true, detail, read); CC_CHECK_EQ(detail.attackBoxCount, 255u);
    put32(0x50, 0x17FC); ReadTrainingAnimation(0x1000, true, detail, read);
    CC_CHECK(!detail.attackBoxesKnown);
    ReadTrainingAnimation(0x1000, false, detail, read);
    CC_CHECK(detail.attackBoxesKnown && detail.attackBoxCount == 0);
    ReadTrainingAnimation(0xFFFFFFF0, true, detail, read);
    CC_CHECK(!detail.stanceKnown && !detail.attackBoxesKnown);
    ReadTrainingAnimation(0, true, detail, read);
    CC_CHECK(!detail.stanceKnown && !detail.attackBoxesKnown);

    CC_CASE("HURTだけを数え、押し合い・シールド・相殺は無敵判定から除く");
    memory.fill(0); put32(0x4C, 0x1200); memory[0x42] = 12;
    for (size_t slot : {0u, 9u, 11u}) put32(0x200 + slot * 4, 0x1600);
    ReadTrainingAnimation(0x1000, false, detail, read);
    CC_CHECK(detail.hurtBoxesKnown && detail.hurtBoxCount == 0);
    s = {}; s.detail[0] = detail;
    CC_CHECK(ClassifyFrameBar(s, 0).strikeInvulnerable);
    put32(0x204, 0x1600); put32(0x220, 0x1608);
    ReadTrainingAnimation(0x1000, false, detail, read);
    CC_CHECK(detail.hurtBoxesKnown && detail.hurtBoxCount == 2);
    s.detail[0] = detail; CC_CHECK(!ClassifyFrameBar(s, 0).strikeInvulnerable);
    s.detail[0].strikeProtected = true; CC_CHECK(ClassifyFrameBar(s, 0).strikeInvulnerable);
    put32(0x204, 0x17FC); // 座標は読まず、非NULLの枠を数える。偽の無敵にしない。
    readCalls = 0;
    ReadTrainingAnimation(0x1000, false, detail, read);
    CC_CHECK(detail.hurtBoxesKnown && detail.hurtBoxCount == 2);
    CC_CHECK_EQ(readCalls, 2u); // animation + 防御配列。HURT数に比例する読取りなし。
    CC_CHECK(detail.animationKnown && detail.defenseSlotCount == 12);
    put32(0x4C, 0); ReadTrainingAnimation(0x1000, false, detail, read);
    s.detail[0] = detail;
    CC_CHECK(!detail.hurtBoxesKnown && !ClassifyFrameBar(s, 0).strikeInvulnerable);
    memory[0x42] = 1; ReadTrainingAnimation(0x1000, false, detail, read);
    CC_CHECK(detail.hurtBoxesKnown && detail.hurtBoxCount == 0);
    ReadTrainingAnimation(0, false, detail, read); CC_CHECK(!detail.hurtBoxesKnown && !detail.animationKnown);

    CC_CASE("対象版のガード受付条件を符号付き予約番号で判別する");
    memory.fill(0); put32(0x38, 0x1100); memory[0x111] = 1;
    ReadTrainingAnimation(0x1000, false, detail, read);
    detail.reservedPattern = -1;
    CC_CHECK(TrainingRecoveryGuardEligible(detail, 0, 0, 0));
    for (int reservation : {0, 10, 100}) {
        detail.reservedPattern = reservation;
        CC_CHECK(TrainingRecoveryGuardEligible(detail, 0, 0, 0));
    }
    for (int reservation : {1, 5, 9}) {
        detail.reservedPattern = reservation;
        CC_CHECK(!TrainingRecoveryGuardEligible(detail, 0, 0, 0));
    }
    detail.reservedPattern = -1;
    CC_CHECK(!TrainingRecoveryGuardEligible(detail, -1, 0, 0));
    CC_CHECK(!TrainingRecoveryGuardEligible(detail, 0, 1, 0));
    CC_CHECK(!TrainingRecoveryGuardEligible(detail, 0, 0, 1));
    put32(0x118, 0x80000000u); ReadTrainingAnimation(0x1000, false, detail, read);
    CC_CHECK(!TrainingRecoveryGuardEligible(detail, 0, 0, 0));
    put32(0x118, 0); memory[0x111] = 2; ReadTrainingAnimation(0x1000, false, detail, read);
    CC_CHECK(!TrainingRecoveryGuardEligible(detail, 0, 0, 0));
    ReadTrainingAnimation(0, false, detail, read);
    CC_CHECK(!TrainingRecoveryGuardEligible(detail, 0, 0, 0));

    CC_CASE("連続した硬直解消の1FだけをSTARTとし、その後5Fで保持する");
    h.Reset(); s = {}; s.valid = true; s.pattern[0] = 1; s.detail[0].valid = s.detail[0].stanceKnown = true; s.detail[0].canMove = 1;
    s.inactionable[0] = 15; step(); s.inactionable[0] = 0; step();
    CC_CHECK_EQ(h.At(1).players[0].state, FrameBarState::StartWait);
    CC_CHECK_EQ(h.At(1).players[0].runFrame, 1u);
    step(); CC_CHECK_EQ(h.At(2).players[0].state, FrameBarState::Ready);
    for (int i = 0; i < 3; ++i) step();
    CC_CHECK(!h.Holding()); step(); CC_CHECK(h.Holding());

    CC_CASE("欠測・停止・ポーズ・スロー・受付不可を復帰1Fと誤認しない");
    for (unsigned condition = 0; condition < 6; ++condition) {
        h.Reset(); s = {}; s.valid = true; s.pattern[0] = 1; s.detail[0].valid = s.detail[0].stanceKnown = true; s.detail[0].canMove = 1;
        s.inactionable[0] = 1; step();
        if (condition == 0) s.trueFrame += 2;
        if (condition == 1) s.playerStopped[0] = true;
        if (condition == 2) { s.paused = true; step(); s.paused = false; }
        if (condition == 3) --s.simulationFrame;
        if (condition == 4) s.detail[0].canMove = 0;
        if (condition == 5) { s.attacking[0] = true; s.detail[0].attackBoxesKnown = true; s.detail[0].attackBoxCount = 1; }
        s.inactionable[0] = 0; step();
        if (h.Size()) CC_CHECK(h.At(h.Size() - 1).players[0].state != FrameBarState::StartWait);
    }

    CC_CASE("行動開始前の予約は濃い緑にせず、最初の動作セルから記録する");
    h.Reset(); s = {}; s.valid = true; s.pattern[0] = 1;
    s.detail[0].valid = s.detail[0].stanceKnown = true; s.detail[0].canMove = 1;
    for (int reservation : {0, 1, 9, 10, 36, 62}) {
        s.detail[0].reservedPattern = reservation;
        CC_CHECK_EQ(ClassifyFrameBar(s, 0).state, FrameBarState::Ready);
        step(); CC_CHECK_EQ(h.Size(), 0u);
    }
    s.detail[0].reservedPattern = -1; s.inactionable[0] = 1; step();
    CC_CHECK_EQ(h.Size(), 1u);
    CC_CHECK_EQ(h.At(0).players[0].state, FrameBarState::Busy);
    CC_CHECK_EQ(h.At(0).players[0].runFrame, 1u);

    CC_CASE("予約の有無に関係なく復帰境界の1Fだけを濃い緑にする");
    s.inactionable[0] = 0; s.detail[0].reservedPattern = 1;
    s.detail[0].guardEligible = false; step();
    CC_CHECK_EQ(h.At(1).players[0].state, FrameBarState::StartWait);
    CC_CHECK(!h.At(1).players[0].detail.guardEligible);
    step(); CC_CHECK_EQ(h.At(2).players[0].state, FrameBarState::Ready);
    for (int i = 0; i < 4; ++i) step();
    CC_CHECK(h.Holding());
    const auto heldSize = h.Size();
    step(); CC_CHECK_EQ(h.Size(), heldSize); // 新しい予約だけで保持を解除しない。

    CC_CASE("相手動作中と停止中も行動前の予約をSTARTにしない");
    h.Reset(); s = {}; s.valid = true; s.pattern[1] = 1; s.inactionable[1] = 1;
    s.detail[0].valid = s.detail[0].stanceKnown = true; s.detail[0].canMove = 1;
    s.detail[0].reservedPattern = 1; step();
    CC_CHECK_EQ(h.At(0).players[0].state, FrameBarState::Ready);
    s.playerStopped[0] = true; step();
    CC_CHECK_EQ(h.At(1).players[0].state, FrameBarState::Ready);
    CC_CHECK(h.At(1).players[0].stopped);
    s.playerStopped[0] = false; s.detail[0].hurtBoxesKnown = true; step();
    CC_CHECK_EQ(h.At(2).players[0].state, FrameBarState::Ready);
    CC_CHECK(h.At(2).players[0].strikeInvulnerable); // HURT消失の白は維持。

    CC_CASE("HURT消失区間で白いセルの番号を始め直す");
    h.Reset(); s = {}; s.valid = true; s.pattern[0] = 1; s.inactionable[0] = 1;
    s.detail[0].hurtBoxesKnown = true; s.detail[0].hurtBoxCount = 1;
    step(); step(); s.detail[0].hurtBoxCount = 0; step();
    CC_CHECK(h.At(2).players[0].strikeInvulnerable);
    CC_CHECK_EQ(h.At(2).players[0].runFrame, 1u);
    step(); CC_CHECK_EQ(h.At(3).players[0].runFrame, 2u);
    s.detail[0].hurtBoxCount = 1; step();
    CC_CHECK_EQ(h.At(4).players[0].runFrame, 1u);

    // 続く表示切替テストのために動作中の1列へ戻す。
    h.Reset(); s = {}; s.valid = true; s.pattern[0] = 1; s.inactionable[0] = 1;
    CC_CASE("詳細モードだけに学習表示を出し、切替で履歴を失わない");
    using domain::ui::FrameBarDisplay;
    using domain::ui::HudDisplay;
    CC_CHECK(!FrameBarDisplay::Available(0));
    CC_CHECK(FrameBarDisplay::Available(1) && FrameBarDisplay::Available(2));
    CC_CHECK(!FrameBarDisplay::Available(255));
    CC_CHECK(FrameBarDisplay::Available(4));
    h.Update(4, s); CC_CHECK_EQ(h.Size(), 1u);
    for (unsigned cycle = 0; cycle < 2; ++cycle) {
        CC_CHECK(HudDisplay::Visible() && !HudDisplay::Detailed());
        for (int app : {0, 1, 2, 4}) CC_CHECK(!FrameBarDisplay::Visible(app));
        HudDisplay::Cycle();
        CC_CHECK(HudDisplay::Visible() && HudDisplay::Detailed());
        CC_CHECK(!FrameBarDisplay::Visible(0));
        for (int app : {1, 2, 4}) CC_CHECK(FrameBarDisplay::Visible(app));
        HudDisplay::Cycle();
        CC_CHECK(!HudDisplay::Visible() && !HudDisplay::Detailed());
        for (int app : {0, 1, 2, 4}) CC_CHECK(!FrameBarDisplay::Visible(app));
        HudDisplay::Cycle();
        CC_CHECK_EQ(h.Size(), 1u);
    }
    h.Update(4, {}); CC_CHECK_EQ(h.Size(), 0u);
    return cccaster::test::Summarize("frame_bar");
}
