#include "shared_contracts/CheckedPatch.hpp"
#include "test_support.hpp"
#include <algorithm>
#ifdef _WIN32
#include "shared_contracts/ProcessMemory.hpp"
#endif
using namespace cccaster::patch;
struct Memory {
    uint8_t data[128]{};
    int writes=0, protects=0, restores=0, flushes=0, reads=0;
    int failWrite=0, failProtect=0, failRestore=0, failFlush=0, failRead=0;
    uint32_t protection[2]{0x20,0x20};
    uint32_t LastError() const { return 123; }
    bool SinglePage(uintptr_t address,size_t size) const { return address && address+size <= 129; }
    bool Read(uintptr_t address,void *out,size_t size) {
        if (++reads==failRead) return false;
        std::memcpy(out,data+address-1,size); return true;
    }
    bool Write(uintptr_t address,const void *in,size_t size) {
        if (++writes==failWrite) { data[address-1]=0xee; return false; }
        std::memcpy(data+address-1,in,size); return true;
    }
    bool MakeWritable(uintptr_t address,size_t,bool,uint32_t &old) {
        if (++protects==failProtect) return false;
        old=protection[address>64]; protection[address>64]=0x40; return true;
    }
    bool RestoreProtection(uintptr_t address,size_t,uint32_t old) {
        if (++restores==failRestore) return false;
        protection[address>64]=old; return true;
    }
    bool Flush(uintptr_t,size_t) { return ++flushes!=failFlush; }
};
int main() {
    const uint8_t old[]{1,2,3}, changed[]{0x90,0x90};
    const Spec specs[]{{"one",1,old,changed},{"two",65,old,changed}};
    auto fresh=[&] { Memory m; std::memcpy(m.data,old,3); std::memcpy(m.data+64,old,3); return m; };
    CC_CASE("全署名を検査してから適用し、保護と命令キャッシュを更新する");
    auto m=fresh(); auto r=Apply(m,specs);
    CC_CHECK(bool(r)); CC_CHECK_EQ(m.writes,2); CC_CHECK_EQ(m.flushes,2);
    CC_CHECK_EQ(m.data[2],3); CC_CHECK_EQ(m.protection[0],0x20u); CC_CHECK_EQ(m.protection[1],0x20u);
    CC_CASE("後続署名が違う場合も先行箇所を書き込まない");
    m=fresh(); m.data[65]=8; r=Apply(m,specs);
    CC_CHECK(r.error==Error::Mismatch); CC_CHECK_EQ(m.writes,0); CC_CHECK_EQ(m.protects,0);
    CC_CASE("部分書込み・保護・検証・キャッシュ失敗時は元バイトと保護へ戻す");
    for (int mode=0;mode<5;++mode) {
        m=fresh();
        if(mode==0) m.failWrite=2;
        if(mode==1) m.failRestore=2;
        if(mode==2) m.failFlush=2;
        if(mode==3) m.failProtect=2;
        if(mode==4) m.failRead=4;
        r=Apply(m,specs);
        CC_CHECK(!r); CC_CHECK(!r.rollbackFailed); CC_CHECK_EQ(r.address,uintptr_t(65));
        CC_CHECK(!std::memcmp(m.data,old,3)); CC_CHECK(!std::memcmp(m.data+64,old,3));
        CC_CHECK_EQ(m.protection[0],0x20u); CC_CHECK_EQ(m.protection[1],0x20u);
    }
    CC_CASE("復元失敗を成功扱いしない");
    m=fresh();m.failWrite=2;m.failRestore=3;r=Apply(m,specs);CC_CHECK(!r);CC_CHECK(r.rollbackFailed);
    CC_CASE("重複範囲と空署名を拒否する");
    m=fresh(); const Spec overlap[]{specs[0],specs[0]};r=Apply(m,overlap);CC_CHECK(r.error==Error::InvalidSpec);CC_CHECK_EQ(m.writes,0);
    const Spec empty{"empty",1,{},changed};r=Apply(m,std::span(&empty,1));CC_CHECK(r.error==Error::InvalidSpec);
#ifdef _WIN32
    CC_CASE("実Win32のRXページを書換え、RXへ戻し、ページ境界を拒否する");
    auto *page=static_cast<uint8_t *>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    CC_CHECK(page!=nullptr);std::memcpy(page,old,3);DWORD protection=0;
    CC_CHECK(VirtualProtect(page,8192,PAGE_EXECUTE_READ,&protection)!=0);
    ProcessMemory process;
    const Spec real{"real",reinterpret_cast<uintptr_t>(page),old,changed};
    r=Apply(process,std::span(&real,1));CC_CHECK(bool(r));CC_CHECK(!std::memcmp(page,changed,2));
    MEMORY_BASIC_INFORMATION info{};CC_CHECK(VirtualQuery(page,&info,sizeof(info))!=0);CC_CHECK_EQ(info.Protect,DWORD(PAGE_EXECUTE_READ));
    const Spec crossing{"crossing",reinterpret_cast<uintptr_t>(page+4095),old,changed};
    r=Apply(process,std::span(&crossing,1));CC_CHECK(r.error==Error::InvalidSpec);
    VirtualFree(page,0,MEM_RELEASE);
#endif
    return cccaster::test::Summarize("checked_patch");
}
