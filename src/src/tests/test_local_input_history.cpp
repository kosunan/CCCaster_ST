#include "core_dll/engine/LocalInputHistory.hpp"
#include "core_dll/rollback/PredictionHistory.hpp"
#include "test_support.hpp"
using cccaster::domain::session::LocalInputHistory;
using cccaster::game_interface::GameInput;
int main() {
    CC_CASE("normal samples finalize the previous native reservation for both players");
    LocalInputHistory inputs;
    cccaster::sync::PredictionHistory history;
    history.Reset(100,2);
    inputs.Publish(100,{},{});
    CC_CHECK(history.Record(100,0,0));
    auto p1=[&](uint32_t f,uint32_t& v){return inputs.Read(f,0,v);};
    auto p2=[&](uint32_t f,uint32_t& v){return inputs.Read(f,1,v);};
    CC_CHECK_EQ(history.Reconcile(p1,p2),0u);
    const GameInput left{4,CC_BUTTON_A},right{6,CC_BUTTON_B};
    inputs.Publish(101,left,right);
    CC_CHECK(history.Record(101,left.Pack(),right.Pack()));
    CC_CHECK_EQ(history.Reconcile(p1,p2),100u);
    uint32_t a=0,b=0;
    CC_CHECK(history.ResolveReplay(100,p1,p2,a,b));
    CC_CHECK_EQ(a,left.Pack()); CC_CHECK_EQ(b,right.Pack());
    CC_CHECK(history.ResolveReplay(101,p1,p2,a,b));
    CC_CHECK_EQ(a,left.Pack()); CC_CHECK_EQ(b,right.Pack());
    CC_CHECK(!inputs.Read(101,0,a)); // future sample has not been taken
    CC_CASE("ring reuse and scene reset cannot reuse stale input");
    inputs.Publish(105,{},{});
    CC_CHECK(!inputs.Read(100,0,a));
    CC_CHECK(!inputs.Read(104,2,a));
    inputs.Clear();
    CC_CHECK(!inputs.Read(104,0,a));
    CC_CASE("only ordinary local combat enters the correction window");
    for(int mode: {1,5}) {
        CC_CHECK(LocalInputHistory::Eligible(mode,true,false,false,false,false,false,CC_BUTTON_A));
        for(int flag=0;flag<6;++flag)
            CC_CHECK(!LocalInputHistory::Eligible(mode,flag!=0,flag==1,flag==2,flag==3,flag==4,flag==5,0));
        for(auto control: {CC_BUTTON_START,CC_BUTTON_FN1,CC_BUTTON_FN2})
            CC_CHECK(!LocalInputHistory::Eligible(mode,true,false,false,false,false,false,control));
    }
    for(int mode: {0,2,3,4})
        CC_CHECK(!LocalInputHistory::Eligible(mode,true,false,false,false,false,false,0));
    return cccaster::test::Summarize("local_input_history");
}
