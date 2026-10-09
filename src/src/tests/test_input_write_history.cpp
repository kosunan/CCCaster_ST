#include "core_dll/rollback/InputWriteHistory.hpp"
#include "core_dll/mbaa_mem/MbaaInputDefs.hpp"
#include "test_support.hpp"
using namespace cccaster::sync;
using cccaster::game_interface::GameInput;
int main() {
    ActorInputContext c;
    c.actor = 0x555134;
    c.masks = {CC_BUTTON_A,CC_BUTTON_B,CC_BUTTON_C,CC_BUTTON_D,CC_BUTTON_E,CC_BUTTON_AB};
    CC_CASE("native button edge, hold, release and full-width E");
    auto value = ExpectedActorInput(c,GameInput{0,CC_BUTTON_A}.Pack());
    CC_CHECK_EQ(value.buttons,0x1001u);
    c.previousButtons=0x1001;
    CC_CHECK_EQ(ExpectedActorInput(c,GameInput{0,CC_BUTTON_A}.Pack()).buttons,0x1000u);
    CC_CHECK_EQ(ExpectedActorInput(c,0).released,1u);
    c.previousButtons=0;
    CC_CHECK_EQ(ExpectedActorInput(c,GameInput{0,CC_BUTTON_E}.Pack()).buttons,0x17017u);
    CC_CHECK_EQ(ExpectedActorInput(c,GameInput{2,CC_BUTTON_E}.Pack()).buttons,0x13013u);
    CC_CHECK_EQ(ExpectedActorInput(c,GameInput{4,CC_BUTTON_E}.Pack()).buttons,0x19019u);
    CC_CHECK_EQ(ExpectedActorInput(c,GameInput{0,CC_BUTTON_AB}.Pack()).buttons,0x3003u);
    c.previousButtons=0x17017;c.macroMode=1;
    CC_CHECK_EQ(ExpectedActorInput(c,GameInput{0,CC_BUTTON_E}.Pack()).buttons,0x17000u);
    c.stateKind=1;
    CC_CHECK_EQ(ExpectedActorInput(c,GameInput{0,CC_BUTTON_E}.Pack()).buttons,0x10000u);
    CC_CASE("two native direction transforms are not identical");
    c.facing=1;c.reverse=0x100;
    value=ExpectedActorInput(c,GameInput{3,0}.Pack());
    CC_CHECK_EQ(value.relative,9);
    CC_CHECK_EQ(value.direction,7);
    c.facing=2;
    value=ExpectedActorInput(c,GameInput{3,0}.Pack());
    CC_CHECK_EQ(value.relative,7);
    CC_CHECK_EQ(value.direction,3);
    CC_CASE("actual address mismatch triggers even when supplied input did not change");
    InputWriteHistory history;
    history.Begin(10);
    c.facing=0;c.reverse=0;c.previousButtons=0;c.stateKind=0;c.macroMode=0;
    auto* observed=history.Current(c.actor);
    observed->context=c;observed->prepared=observed->observed=true;
    observed->applied=GameInput{4,CC_BUTTON_A}.Pack();
    observed->actual=ExpectedActorInput(c,observed->applied);
    observed->actual.direction=6; // the memory store is stale, despite unchanged raw history
    unsigned reports=0;
    const auto raw=observed->applied;
    auto mismatch=history.Compare(10,11,[&](uint32_t,unsigned,uint32_t& out){out=raw;return true;},
        [&](uint32_t f,const InputWriteHistory::Write& w,uint32_t input,const ActorInputValue&){
            ++reports;history.Request(f,w.context.actor,input);
        });
    CC_CHECK_EQ(mismatch.first,10u);CC_CHECK_EQ(mismatch.players,1u);CC_CHECK_EQ(reports,1u);
    CC_CHECK(!history.Corrected(c.actor,raw,false));
    history.Begin(10); // a real replay replaces observations but keeps the correction request
    CC_CHECK(history.Corrected(c.actor,raw,true));
    CC_CHECK(!history.Corrected(c.actor,raw,true));
    CC_CASE("both players and missing older remote input");
    for (unsigned i=0;i<2;++i) {
        history.Begin(20+i);
        auto* w=history.Current(0x555134+i*0xAFC);
        w->context=c;w->context.actor=0x555134+i*0xAFC;w->context.source=i;
        w->prepared=w->observed=true;w->actual={};
    }
    mismatch=history.Compare(20,22,[&](uint32_t f,unsigned,uint32_t& out){out=raw;return f==21;},
                             [](auto,const auto&,auto,const auto&){});
    CC_CHECK_EQ(mismatch.first,21u);CC_CHECK_EQ(mismatch.players,2u);
    mismatch=history.Compare(20,22,[&](uint32_t,unsigned,uint32_t& out){out=raw;return true;},
                             [](auto,const auto&,auto,const auto&){});
    CC_CHECK_EQ(mismatch.first,20u);CC_CHECK_EQ(mismatch.players,3u);
    CC_CASE("Steam ASLR actor base and all four slots use the supplied runtime address");
    constexpr uintptr_t relocated = 0x0033C374;
    history.Reset(relocated); history.Begin(42);
    for (unsigned i=0; i<4; ++i) {
        const auto actor = relocated + i * 0xAFC;
        CC_CHECK(history.Current(actor) != nullptr);
        history.Request(42, actor, 17+i);
        CC_CHECK(!history.Corrected(actor, 18+i, true));
        CC_CHECK(history.Corrected(actor, 17+i, true));
    }
    CC_CHECK(history.Current(0x555134) == nullptr);
    CC_CHECK(history.Current(relocated-1) == nullptr);
    CC_CHECK(history.Current(relocated+1) == nullptr);
    CC_CHECK(history.Current(relocated+4*0xAFC) == nullptr);
    return cccaster::test::Summarize("input_write_history");
}
