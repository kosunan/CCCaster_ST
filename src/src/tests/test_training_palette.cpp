#include "core_dll/engine/TrainingPalette.hpp"
#include "core_dll/engine/TrainingPaletteMotion.hpp"
#include "test_support.hpp"
#include <limits>
using namespace cccaster::training_palette;
int main() {
    Asset asset;
    auto& page=asset.pages[2];
    page.valid[10*256+8]=page.valid[10*256+9]=1;
    page.indices[10*256+8]=page.indices[10*256+9]=7;
    Sprite sprite{4,"pose",100,200,2,1,{{2,8,10,2,1,100,200}}};
    Edit edit; edit.palette[7]=0x00563412;
    const auto first=asset.Locate(sprite,0,0), second=asset.Locate(sprite,1,0);
    CC_CASE("ポーズ座標をタイル座標に対応づけ、範囲外を拒否する");
    CC_CHECK_EQ(first,2u*65536+10u*256+8u);
    CC_CHECK_EQ(asset.Locate(sprite,-1,0),NoPixel);
    CC_CHECK_EQ(asset.Locate(sprite,2,0),NoPixel);
    CC_CHECK_EQ(asset.Color(edit,NoPixel),0u);
    CC_CASE("パレット変更は同じ色番号へ反映し、ピクセル変更は他の画素に波及しない");
    edit.palette[7]=0x00775533;
    CC_CHECK_EQ(asset.Color(edit,first),0xff775533u);
    CC_CHECK_EQ(asset.Color(edit,second),0xff775533u);
    edit.pixels[first]=0xffddbb99;
    edit.palette[7]=0x00112233;
    CC_CHECK_EQ(asset.Color(edit,first),0xffddbb99u);
    CC_CHECK_EQ(asset.Color(edit,second),0xff112233u);
    CC_CASE("色番号0は透明、鉛筆は透明領域にも描け、復元で透明に戻る");
    page.indices[10*256+8]=0; edit.pixels.clear(); edit.palette[0]=0xffffff;
    CC_CHECK_EQ(asset.Color(edit,first)>>24,0u);
    edit.pixels[first]=0xffddbb99;
    CC_CHECK_EQ(asset.Color(edit,first)>>24,255u);
    edit.pixels.erase(first);
    CC_CHECK_EQ(asset.Color(edit,first)>>24,0u);
    CC_CASE("1ストローク単位の取消・やり直しと新しい編集による履歴分岐");
    History history; const Edit original=edit;
    history.Begin(edit); edit.pixels[first]=0xff010203;
    history.Begin(edit); edit.pixels[second]=0xff030201; history.End(edit);
    const Edit painted=edit;
    CC_CHECK(history.Undo(edit)); CC_CHECK(edit==original);
    CC_CHECK(history.Redo(edit)); CC_CHECK(edit==painted);
    CC_CHECK(history.Undo(edit));
    history.Begin(edit); edit.palette[7]=0x00101010; history.End(edit);
    CC_CHECK(!history.Redo(edit));
    CC_CASE("16bit変換は透明と非ゼロ最小値を維持する");
    CC_CHECK_EQ(Pack16(0,false),0u);
    CC_CHECK_EQ(Pack16(0xff010101,false),0x8421u);
    CC_CHECK_EQ(Pack16(0xff010101,true),0xf111u);
    CC_CHECK_EQ(Pack16(0xff0000ff,false),0xfc00u);
    CC_CASE("読取り専用モーション一覧は画像番号と時間を維持し、未収録のフレームも時間を詰めない");
    std::array<uint8_t,4096> memory{};
    const auto put=[&](unsigned address,const auto& value){std::memcpy(memory.data()+address,&value,sizeof(value));};
    const auto read=[&](uint32_t address,void* out,size_t size){
        if(address>memory.size() || size>memory.size()-address)return false;
        std::memcpy(out,memory.data()+address,size);return true;
    };
    put(0x100,std::array<uint32_t,4>{1,0x120,4,1});put(0x120,uint32_t(0x200));
    put(0x200,std::array<char,5>{'T','e','s','t',0});put(0x234,uint32_t(0x300));
    put(0x300,std::array<uint32_t,4>{0,0x400,0x54,3});
    for(unsigned i=0;i<3;++i) {
        put(0x400+i*0x54+2,uint16_t(4+i*4));put(0x400+i*0x54+0xc,uint16_t(i==2 ? 7 : i+2));
        put(0x400+i*0x54+0x30,1.f);put(0x400+i*0x54+0x34,1.f);
    }
    Sprite effect=sprite;effect.id=8;effect.bank=28;effect.type=2;
    asset.sprites={sprite,effect};
    auto motions=ReadMotions(asset,0x100,read);
    CC_CHECK_EQ(motions.size(),1u);CC_CHECK_EQ(motions[0].duration,12u);CC_CHECK_EQ(motions[0].missing,1u);
    CC_CHECK(motions[0].object);CC_CHECK(motions[0].name=="Test");
    CC_CHECK_EQ(motions[0].frames[0].sprite,0u);CC_CHECK_EQ(motions[0].frames[1].sprite,1u);
    CC_CHECK_EQ(motions[0].frames[2].sprite,NoPixel);
    CC_CHECK_EQ(MotionFrameIndex(motions[0],0),0u);CC_CHECK_EQ(MotionFrameIndex(motions[0],1),0u);
    CC_CHECK_EQ(MotionFrameIndex(motions[0],2),1u);CC_CHECK_EQ(MotionFrameIndex(motions[0],5),2u);
    CC_CHECK_EQ(MotionFrameIndex(motions[0],12),0u);
    CC_CASE("プレビュー時計は表示Hzによらず60F基準で進み、停止・速度・コマ送りを独立制御する");
    MotionPlayer at60,at120;
    for(unsigned i=0;i<60;++i)at60.Advance(1.0/60,1,120);
    for(unsigned i=0;i<120;++i)at120.Advance(1.0/120,1,120);
    CC_CHECK_EQ(at60.Frame(),60u);CC_CHECK_EQ(at120.Frame(),60u);
    at60.Step(-1,120);CC_CHECK_EQ(at60.Frame(),59u);CC_CHECK(!at60.playing);
    at60.Advance(2,1,120);CC_CHECK_EQ(at60.Frame(),59u);
    at60.Seek(0,120);at60.Step(-1,120);CC_CHECK_EQ(at60.Frame(),119u);
    at60.Step(1,120);CC_CHECK_EQ(at60.Frame(),0u);
    at60.Reset();at60.Advance(1,.25,120);CC_CHECK_EQ(at60.Frame(),15u);
    at60.Seek(500,0);CC_CHECK_EQ(at60.Frame(),0u);CC_CHECK_EQ(MotionFrameIndex(Motion{},0),NoPixel);
    CC_CASE("表示原点とフレーム固有の移動・拡縮を使い、矩形の大きさが変わっても原点を維持する");
    MotionFrame frame;auto quad=MotionQuad(sprite,frame);
    CC_CHECK_EQ(quad[0].x,-28.f);CC_CHECK_EQ(quad[0].y,-24.f);
    frame.x=10;frame.y=20;frame.scaleX=-2;frame.scaleY=3;quad=MotionQuad(sprite,frame);
    CC_CHECK_EQ(quad[0].x,36.f);CC_CHECK_EQ(quad[0].y,-12.f);
    CC_CHECK_EQ(quad[1].x,32.f);CC_CHECK_EQ(quad[2].y,-9.f);
    CC_CASE("不正な件数・ポインター・ゼロ時間・非有限の変形値を安全に処理する");
    put(0x10c,uint32_t(10001));CC_CHECK(ReadMotions(asset,0x100,read).empty());put(0x10c,uint32_t(1));
    put(0x120,uint32_t(0xfffffff0));CC_CHECK(ReadMotions(asset,0x100,read).empty());put(0x120,uint32_t(0x200));
    put(0x40c,uint16_t(0));motions=ReadMotions(asset,0x100,read);CC_CHECK_EQ(motions[0].duration,11u);
    put(0x430,std::numeric_limits<float>::infinity());motions=ReadMotions(asset,0x100,read);
    CC_CHECK_EQ(motions[0].frames[0].sprite,NoPixel);CC_CHECK_EQ(motions[0].missing,2u);
    return cccaster::test::Summarize("Trainingパレット編集");
}
