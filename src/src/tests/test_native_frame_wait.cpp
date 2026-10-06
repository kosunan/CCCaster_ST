#include "core_dll/mbaa_mem/NativeFrameWait.hpp"
#include "shared_contracts/ProcessMemory.hpp"
#include "fixtures/native_frame_wait.hpp"
#include "test_support.hpp"
#include <cmath>
#include <string>

extern "C" { uintptr_t cc_wait_test_entry=0; }
namespace nw = cccaster::game_memory::native_frame_wait;
namespace {
uint64_t ticks;
unsigned sleeps, queries, movies, networks;
int movieResult, networkResult;
BOOL WINAPI Counter(LARGE_INTEGER* value) { ++queries; ticks += 100; value->QuadPart = ticks; return TRUE; }
DWORD WINAPI Milliseconds() { ++queries; ticks += 1000; return DWORD(ticks / 1000); }
void WINAPI SleepStub(DWORD ms) { ++sleeps; ticks += ms * 1000; }
int __cdecl Movie() { ++movies; return movieResult; }
int __cdecl Network() { ++networks; return networkResult; }
template<class T> void Put(uintptr_t address, T value) { std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value)); }
void Jump(uintptr_t site, uintptr_t target) {
    Put<uint8_t>(site, 0xe9); Put<int32_t>(site + 1, int32_t(target - site - 5));
}
struct Memory : cccaster::patch::ProcessMemory {
    unsigned writes = 0;
    bool Write(uintptr_t a, const void* p, size_t n) { ++writes; return ProcessMemory::Write(a, p, n); }
};
struct alignas(8) Timer {
    uint32_t qpc = 1, unused = 0;
    int64_t frequency = 1000000, frozen = 0, start = 1000000, reference = 1000000;
    double frozenSeconds = 0, startSeconds = 1, referenceSeconds = 1;
    float accumulator = 0, elapsed = 0, processing = 0;
    uint32_t pending = 0;
};
static_assert(sizeof(Timer) == 0x50);
// Steam: ECX=Timer、XMM1=間隔、calleeはRETのみ。
__attribute__((naked)) unsigned __cdecl Run(Timer*,float) {
    __asm__ __volatile__("movl 4(%esp),%ecx\n\tmovss 8(%esp),%xmm1\n\tjmp *_cc_wait_test_entry\n\t");
}
uintptr_t At(uint32_t address) {return cccaster::game_memory::GameRuntime::Preferred(address);}
unsigned FpuTop() { unsigned short status; __asm__ __volatile__("fnstsw %0" : "=am"(status)); return (status >> 11) & 7; }
void ResetCode() {
    const auto target=At(0x476e80);
    std::memcpy(reinterpret_cast<void*>(target),NativeWaitFixture,sizeof(NativeWaitFixture));
    for(auto offset:NativeWaitRelocations) {
        uint32_t value; std::memcpy(&value,reinterpret_cast<void*>(target+offset),4);
        Put(target+offset,value+uint32_t(cccaster::game_memory::GameRuntime::Image().base-0x400000));
    }
}
void ResetCounters() { ticks = 1000000; sleeps = queries = movies = networks = 0; movieResult = 0; networkResult = 1; }
}
int main() {
    auto* mapped=VirtualAlloc(nullptr,0xf3e000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
    CC_CHECK(mapped); if(!mapped) return 1;
    CC_CHECK(cccaster::game_memory::GameRuntime::Initialize(cccaster::game_build::Edition::Steam20170105,
        {reinterpret_cast<uintptr_t>(mapped),0xf3e000}));
    cc_wait_test_entry=At(0x476e80);
    ResetCode();
    Put(At(0x56b0c4),&Counter); Put(At(0x56b344),&Milliseconds); Put(At(0x56b05c),&SleepStub);
    Put<float>(At(0x58ce5c),1.0f/60); Put<double>(At(0x58ce10),0.0);
    Put<double>(At(0x58cf40),0.001); Put<double>(At(0x58e360),0.0);
    Put<double>(At(0x58e368),4294967296.0); Put<double>(At(0x58cf88),0.015);
    Put<double>(At(0x58d188),1000.0);
    Put<uint32_t>(At(0x7cdabc),At(0x800000));
    Jump(At(0x46aa10),reinterpret_cast<uintptr_t>(&Movie)); Jump(At(0x4ea540),reinterpret_cast<uintptr_t>(&Network));
    // テスト値は符号付き32bit範囲内。実関数の同範囲の変換と同じSSE命令。
    const uint8_t toDouble[]{0xf2,0x0f,0x2a,0xc1,0xc3}, toInt[]{0xf2,0x0f,0x2c,0xc0,0xc3};
    std::memcpy(reinterpret_cast<void*>(At(0x547cbf)),toDouble,sizeof(toDouble));
    std::memcpy(reinterpret_cast<void*>(At(0x547b01)),toInt,sizeof(toInt));
    CC_CHECK(FlushInstructionCache(GetCurrentProcess(),nullptr,0));

    CC_CASE("元EXEのfixtureは固定Sleepと期限までの反復を行う");
    ResetCounters(); Timer original;
    CC_CHECK_EQ(Run(&original, 1.0f / 60), 1);
    CC_CHECK(sleeps >= 1); CC_CHECK(queries > 3); CC_CHECK(original.elapsed >= 1.0f / 60);

    CC_CASE("第2署名・分岐先・通信不足入口が不一致なら一切書かない");
    for (auto address : {nw::SleepSite,nw::LoopSite,nw::PendingSite,uintptr_t(0x477379),uintptr_t(0x477114)}) {
        ResetCode(); Put<uint8_t>(At(address),0xcc);
        Memory memory;
        const auto result = nw::Apply(memory);
        CC_CHECK_EQ(result.error, cccaster::patch::Error::Mismatch); CC_CHECK_EQ(memory.writes, 0);
    }
    ResetCode(); Memory memory;
    CC_CASE("3分岐の10バイトだけを書き換え、時計IATと残りの命令を保持する");
    CC_CHECK(bool(nw::Apply(memory))); CC_CHECK_EQ(memory.writes, 3);
    for (size_t i = 0; i < sizeof(NativeWaitFixture); ++i) {
        const auto address = 0x476e80 + i;
        if (address >= nw::SleepSite && address < nw::SleepSite + sizeof(nw::SleepJump)) continue;
        if (address >= nw::LoopSite && address < nw::LoopSite + sizeof(nw::LoopJump)) continue;
        if(address>=nw::PendingSite && address<nw::PendingSite+sizeof(nw::PendingJump)) continue;
        auto expected=NativeWaitFixture[i];
        for(auto relocation:NativeWaitRelocations) if(i>=relocation && i<relocation+4) {
            uint32_t value;std::memcpy(&value,NativeWaitFixture+relocation,4);
            value+=uint32_t(cccaster::game_memory::GameRuntime::Image().base-0x400000);
            expected=uint8_t(value>>((i-relocation)*8));
        }
        CC_CHECK_EQ(*reinterpret_cast<uint8_t*>(At(address)),expected);
    }
    CC_CHECK_EQ(*reinterpret_cast<uintptr_t*>(At(0x56b0c4)), reinterpret_cast<uintptr_t>(&Counter));
    CC_CHECK_EQ(*reinterpret_cast<uintptr_t*>(At(0x56b344)), reinterpret_cast<uintptr_t>(&Milliseconds));
    CC_CHECK_EQ(*reinterpret_cast<uintptr_t*>(At(0x56b05c)), reinterpret_cast<uintptr_t>(&SleepStub));

    CC_CASE("QPC/代替時計・動画・本体通信の各分岐で待機せずFPUと時刻更新を保つ");
    for (unsigned qpc = 0; qpc <= 1; ++qpc)
    for (unsigned video = 0; video <= 1; ++video)
    for (unsigned network = 0; network <= 1; ++network)
    for (unsigned vsync = 0; vsync <= 1; ++vsync)
    for (unsigned pending = 0; pending <= 2; ++pending)
    for (unsigned stopped = 0; stopped <= 1; ++stopped) {
        ResetCounters(); movieResult = video; networkResult = !stopped;
        Timer timer; timer.qpc = qpc; timer.pending = pending;
        Put<uint32_t>(At(0x5b4d30), network ? 1 : 0xffff); Put<uint32_t>(At(0x5ca9b4), 1);
        Put<uint32_t>(At(0x800184), vsync);
        const auto top = FpuTop();
        const auto result = Run(&timer, 1.0f / 60);
        CC_CHECK_EQ(FpuTop(), top);
        CC_CHECK_EQ(result, pending == 1 ? 0 : 1);
        CC_CHECK_EQ(sleeps, 0); CC_CHECK_EQ(movies, pending == 0 ? 1 : 0);
        CC_CHECK_EQ(networks, pending == 1 ? 0 : network);
        CC_CHECK_EQ(timer.pending, 0);
        CC_CHECK_EQ(queries, pending == 1 ? 1 : 3);
        CC_CHECK(std::abs(timer.processing - (qpc ? .0001f : .001f)) < .000001f);
        if (pending == 1) {
            CC_CHECK_EQ(timer.reference, 1000000); CC_CHECK(timer.referenceSeconds == 1);
            CC_CHECK(timer.elapsed == 0);
        } else {
            CC_CHECK(std::abs(timer.elapsed - (qpc ? .0002f : .002f)) < .000001f);
            if (qpc) CC_CHECK_EQ(timer.reference, int64_t(ticks));
            else CC_CHECK(std::abs(timer.referenceSeconds - double(ticks) / 1000000) < .000001);
        }
    }
    VirtualFree(mapped, 0, MEM_RELEASE);
    return cccaster::test::Summarize("native_frame_wait");
}
