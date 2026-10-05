#include "shared_contracts/GameCompatibility.hpp"
#include "shared_contracts/NativePath.hpp"
#include "test_support.hpp"
#include <fstream>
#include <iostream>
#include <vector>
using namespace cccaster;
using namespace game_compat;
static void Put16(std::vector<uint8_t>&b,size_t p,uint16_t v) { b[p]=v; b[p+1]=v>>8; }
static void Put32(std::vector<uint8_t>&b,size_t p,uint32_t v) { Put16(b,p,v); Put16(b,p+2,v>>16); }
static std::vector<uint8_t> Fixture() {
    std::vector<uint8_t>b(0x154000);
    Put16(b,0,0x5a4d); Put32(b,60,0x100); Put32(b,0x100,0x4550);
    Put16(b,0x104,0x14c); Put16(b,0x106,3); Put16(b,0x114,224); Put16(b,0x116,0x102);
    Put16(b,0x118,0x10b); Put32(b,0x128,0xe3d7e); Put32(b,0x134,Base);
    Put32(b,0x150,0x3b4000); Put32(b,0x154,0x1000);
    const Section sections[]{ {0x1000,0x11918f,0x1000,0x11a000,Read|Execute},
        {0x11b000,0x2f5ee,0x11b000,0x30000,Read}, {0x14b000,0x266d64,0x14b000,0x9000,Read|Write} };
    for(size_t i=0;i<3;++i) {
        const size_t s=0x1f8+i*40;
        // セクション名は自由。名前を版識別に使わない。
        b[s]='A'+i; Put32(b,s+8,sections[i].size); Put32(b,s+12,sections[i].rva);
        Put32(b,s+16,sections[i].rawSize); Put32(b,s+20,sections[i].raw); Put32(b,s+36,sections[i].flags);
    }
    for(const auto &s:Required)
        for(size_t i=0;i<s.hex.size()/2;++i) b[s.rva+i]=Hex(s.hex[i*2])*16+Hex(s.hex[i*2+1]);
    return b;
}
int main(int argc,char**argv) {
    auto fixture=Fixture();
    CC_CASE("固定ハッシュを持たないEXEでも必要な入力・状態配置が合えば許可する");
    CC_CHECK(bool(Inspect(fixture)));
    auto b=fixture; Put32(b,0x108,0xabcdef01); Put32(b,0x158,0x12345678);
    b[2]^=1; b[0x2000]^=1; b[0x14d2c0]=0xcb; b[0x14d2c1]=0xcd;
    b.insert(b.end(),{'o','v','e','r','l','a','y'});
    CC_CHECK(bool(Inspect(b)));
    CC_CASE("入口の位置と元命令に既知版の固定値を課さない");
    Put32(b,0x128,0x2010); b[0x2010]=0x55; b[0x2011]=0x8b; CC_CHECK(bool(Inspect(b)));
    CC_CASE("必須署名の改変は箇所とアドレスを返して拒否する");
    for(const auto &s:Required) {
        b=fixture; b[s.rva+s.hex.size()/4]^=1;
        const auto result=Inspect(b); CC_CHECK(!result); CC_CHECK(result.issue==Issue::Signature);
        CC_CHECK(std::string_view(result.name)==std::string_view(s.name));
        CC_CHECK_EQ(result.address,Base+s.rva+uint32_t(s.hex.size()/4));
    }
    CC_CASE("不正PE・異なる配置・データ保護・切断・重複を拒否する");
    CC_CHECK(!Inspect({}));
    b=fixture; Put16(b,0x104,0x8664); CC_CHECK(!Inspect(b));
    b=fixture; Put16(b,0x118,0x20b); CC_CHECK(!Inspect(b));
    b=fixture; Put16(b,0x116,0x2102); CC_CHECK(!Inspect(b));
    b=fixture; Put32(b,0x134,0x500000); CC_CHECK(!Inspect(b));
    b=fixture; Put32(b,0x128,0x14b000); CC_CHECK(!Inspect(b));
    b=fixture; Put32(b,0x1f8+80+36,Read); CC_CHECK(Inspect(b).issue==Issue::DataLayout);
    b=fixture; Put32(b,0x1f8+80+8,0x200000); CC_CHECK(Inspect(b).issue==Issue::DataLayout);
    b=fixture; Put32(b,0x1f8+40+12,0x1000); CC_CHECK(!Inspect(b));
    b=fixture; Put32(b,0x1f8+40+20,0x1000); CC_CHECK(!Inspect(b));
    b=fixture; Put32(b,0x1f8+20,0xfffff000); CC_CHECK(!Inspect(b));
    b=fixture; b.resize(0x153fff); CC_CHECK(!Inspect(b));
    CC_CASE("raw配置変更をRVAから解決し、ロード後読取り失敗も拒否する");
    b=fixture; b.insert(b.begin()+0x1000,0x1000,0);
    for(size_t i=0;i<3;++i) { const size_t s=0x1f8+i*40+20; Put32(b,s,game_build::U32(b,s)+0x1000); }
    CC_CHECK(bool(Inspect(b)));
    Image layout; CC_CHECK(layout.Parse(std::span(b).first(4096),false));
    const auto unreadable=Check(layout,[](uint32_t,std::span<uint8_t>){return false;});
    CC_CHECK(unreadable.issue==Issue::ReadFailure);
    // 実ゲームファイルはローカル引数でのみ指定。ここでは実行しない。
    for(int i=1;i<argc;++i) {
        std::ifstream file(Utf8Path(argv[i]),std::ios::binary|std::ios::ate);
        CC_CHECK(bool(file)); if(!file) continue;
        std::vector<uint8_t> bytes(size_t(file.tellg())); file.seekg(0);
        file.read(reinterpret_cast<char *>(bytes.data()),bytes.size());
        const auto result=Inspect(bytes);
        std::cout<<argv[i]<<": "<<Name(result.issue)<<" site="<<result.name<<" address="<<std::hex<<result.address<<std::dec<<"\n";
        CC_CHECK(bool(result));
        if(!result) continue;
        Image actual; CC_CHECK(actual.Parse(bytes,true));
        const size_t nt=game_build::U32(bytes,60),opt=nt+24,table=opt+game_build::U16(bytes,nt+20);
        auto variant=bytes; Put32(variant,nt+8,0); Put32(variant,opt+64,0);
        for(size_t n=0;n<actual.count;++n) { std::memset(variant.data()+table+n*40,0,8); variant[table+n*40]='X'; }
        const auto keyboard=actual.Offset(0x14d2c0,20); CC_CHECK(keyboard!=SIZE_MAX);
        for(size_t k=0;k<20;++k) variant[keyboard+k]=uint8_t(k);
        variant.push_back(42); CC_CHECK(bool(Inspect(variant)));
        for(const auto &s:Required) {
            variant=bytes; variant[actual.Offset(s.rva,1)]^=1;
            CC_CHECK(Inspect(variant).issue==Issue::Signature);
        }
    }
    return test::Summarize("game_compatibility");
}
