#include "core_dll/engine/TrainingHitbox.hpp"
#include "test_support.hpp"
using namespace cccaster::training_hitbox;
int main() {
    CC_CASE("最終合成の全比率・黒帯と整数切捨てを固定座標で確認");
    CC_CHECK((CompositeRect(1920,1080,0,0,0)==Rect{0,0,1920,1080}));
    CC_CHECK((CompositeRect(1920,1080,1,1920,1080)==Rect{240,0,1680,1080}));
    CC_CHECK((CompositeRect(640,480,2,0,0)==Rect{0,0,640,480}));
    CC_CHECK((CompositeRect(1280,720,3,0,0)==Rect{160,0,1120,720}));
    CC_CHECK((CompositeRect(640,480,4,0,0)==Rect{53,0,586,480}));
    CC_CHECK((CompositeRect(1920,1080,5,0,0)==Rect{0,34,1920,1046}));
    CC_CHECK((CompositeRect(640,480,6,0,0)==Rect{64,0,576,480}));
    // AUTOの全画面問い合わせは保存された通常窓寸法。実モニター比率を使わない。
    CC_CHECK((CompositeRect(800,600,1,800,600)==Rect{0,0,800,600}));
    CC_CHECK((CompositeRect(640,480,1,1280,1024)==Rect{0,15,640,465}));
    CC_CHECK((CompositeRect(0,480,1,640,480)==Rect{}));
    CC_CHECK((CompositeRect(640,480,1,0,0)==Rect{}));
    CC_CASE("元命令の負値SAR・左向き・拘束補正・2倍を固定値で確認");
    const Rect raw{-10,-40,20,0};
    CC_CHECK((Transform(raw,{})==Rect{310,392,340,432}));
    Pose pose;pose.x=-1;pose.y=-129;pose.left=true;pose.doubleSize=true;
    CC_CHECK((Transform(raw,pose)==Rect{279,350,339,430}));
    pose.attached=true;pose.offsetX=129;pose.offsetY=385;pose.cameraX=256;pose.cameraY=512;
    CC_CHECK((Transform(raw,pose)==Rect{279,350,339,430}));
    CC_CASE("拡大前の符号付き端点と逆順の正規化");
    CC_CHECK((Transform({32767,5,-32768,-3},{})==Rect{-32448,429,33087,437}));
    CC_CASE("分類済みスロットだけを描画し未知スロットを除外");
    CC_CHECK(DefenseKind(0)==Kind::Push);
    for(unsigned i=1;i<=8;++i)CC_CHECK(DefenseKind(i)==Kind::Hurt);
    CC_CHECK(DefenseKind(9)==Kind::Shield);CC_CHECK(DefenseKind(11)==Kind::Clash);
    CC_CHECK(DefenseKind(10)==Kind::Count);CC_CHECK(DefenseKind(12)==Kind::Count);
    CC_CASE("個別のON/OFFと選択の循環は他項目を変更しない");
    Options state;CC_CHECK(!state.Any());state.Move(-1);CC_CHECK_EQ(state.selected,5u);
    state.Set(true);CC_CHECK_EQ(state.Mask(),32u);state.Move(1);state.Toggle();CC_CHECK_EQ(state.Mask(),33u);
    state.Set(false);CC_CHECK_EQ(state.Mask(),32u);
    return cccaster::test::Summarize("Training HITBOX");
}
