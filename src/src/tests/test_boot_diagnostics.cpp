#include "launcher/HookDllImage.hpp"
#include "BuildIdentity.hpp"
#include "test_support.hpp"
#include <vector>
#include <fstream>
using namespace cccaster::boot;
static void U16(std::vector<uint8_t>& b,size_t o,uint16_t v) { b[o]=v;b[o+1]=v>>8; }
static void U32(std::vector<uint8_t>& b,size_t o,uint32_t v) { U16(b,o,v);U16(b,o+2,v>>16); }
static std::vector<uint8_t> Image() {
    std::vector<uint8_t>b(4096);
    U16(b,0,0x5a4d);U32(b,60,0x80);U32(b,0x80,0x4550);U16(b,0x84,0x14c);U16(b,0x86,1);
    U16(b,0x94,224);U16(b,0x96,0x2000);const size_t opt=0x98,sec=opt+224;
    U16(b,opt,0x10b);U32(b,opt+16,0x1100);U32(b,opt+28,0x10000000);U32(b,opt+56,0x3000);
    U32(b,opt+92,16);U32(b,opt+96,0x1200);U32(b,opt+100,0x100);
    std::memcpy(b.data()+sec,".text",5);U32(b,sec+8,0xc00);U32(b,sec+12,0x1000);
    U32(b,sec+16,0xc00);U32(b,sec+20,0x400);U32(b,sec+36,0x60000020);
    // RVA 0x1200 -> raw 0x600; 記述子と初期化関数の2 exports。
    U32(b,0x614,2);U32(b,0x618,2);U32(b,0x61c,0x1250);U32(b,0x620,0x1260);U32(b,0x624,0x1270);
    U32(b,0x650,0x1400);U32(b,0x654,0x1100);U32(b,0x660,0x1280);U32(b,0x664,0x12a0);
    U16(b,0x670,0);U16(b,0x672,1);
    std::memcpy(b.data()+0x680,"CCCasterStartupInfo",20);std::memcpy(b.data()+0x6a0,"CCCasterInitialize",19);
    const Descriptor info{Magic,Abi,sizeof(Descriptor),CCCASTER_BUILD_ID};std::memcpy(b.data()+0x800,&info,sizeof(info));
    return b;
}
int main(int argc,char**argv) {
    CC_CASE("DLLをロードせず記述子と初期化RVAを識別する");
    HookDllImage image;auto b=Image();CC_CHECK(image.Inspect(b));CC_CHECK(Compatible(image.descriptor,CCCASTER_BUILD_ID));CC_CHECK_EQ(image.initializeRva,0x1100u);
    CC_CASE("途中で切れたPE・別アーキテクチャ・転送export・不正ordinal・非実行先を拒否する");
    for(size_t n=0;n<0x850;++n) { auto cut=b;cut.resize(n);CC_CHECK(!image.Inspect(cut)); }
    auto bad=b;U16(bad,0x84,0x8664);CC_CHECK(!image.Inspect(bad));
    bad=b;U32(bad,0x654,0x1280);CC_CHECK(!image.Inspect(bad));
    bad=b;U16(bad,0x672,3);CC_CHECK(!image.Inspect(bad));
    bad=b;U32(bad,0x98+224+36,0x40000040);CC_CHECK(!image.Inspect(bad));
    CC_CASE("同じ製品VERSIONでも別ビルド・別ABIを拒否する");
    auto descriptor=Descriptor{Magic,Abi,sizeof(Descriptor),CCCASTER_BUILD_ID};
    descriptor.build[0]=descriptor.build[0]=='0'?'1':'0';CC_CHECK(!Compatible(descriptor,CCCASTER_BUILD_ID));
    descriptor=Descriptor{Magic,Abi+1,sizeof(Descriptor),CCCASTER_BUILD_ID};CC_CHECK(!Compatible(descriptor,CCCASTER_BUILD_ID));
    CC_CASE("GUIは完成した診断行だけを読み、一般的な失敗文字列と区別する");
    CC_CHECK(ParseError("[BOOT_ERROR] code=patch stage=patches win32=5\nfailed\n")==Error::Patch);
    CC_CHECK(ParseError("[BOOT_ERROR] code=patch stage=patches")==Error::None);
    CC_CHECK(ParseError("[BOOT_ERROR] code=unknown stage=x\n")==Error::None);
    CC_CHECK(ParseError("failed ERROR")==Error::None);
    CC_CHECK(ParseError("name=[BOOT_ERROR] code=patch stage=patches\n")==Error::None);
    CC_CHECK(ParseError("[BOOT_ERROR] code=patch\n next line")==Error::None);
    if(argc==2) {
        CC_CASE("ビルドした実DLLの公開記述子と初期化入口を確認する");
        std::ifstream file(argv[1],std::ios::binary);CC_CHECK(bool(file));
        std::vector<uint8_t> actual((std::istreambuf_iterator<char>(file)),{});
        CC_CHECK(image.Inspect(actual));CC_CHECK(Compatible(image.descriptor,CCCASTER_BUILD_ID));
    }
    return cccaster::test::Summarize("boot_diagnostics");
}
