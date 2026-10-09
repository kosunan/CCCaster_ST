#include "core_dll/timing/BoundaryRace.hpp"
#include <chrono>
#include <cstdlib>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif
using namespace cccaster::core::timer;
static void Check(bool ok) { if (!ok) std::abort(); }
// 呼出側の実際のnewを1回だけ失敗させる。別スレッドの確保には干渉しない。
static thread_local bool failNextAllocation = false;
static void CheckAllocation() {
    if (failNextAllocation) { failNextAllocation = false; throw std::bad_alloc(); }
}
void *operator new(std::size_t size) {
    CheckAllocation();
    if (auto p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void *operator new(std::size_t size, std::align_val_t alignment) {
    CheckAllocation();
    const auto align = static_cast<std::size_t>(alignment);
#ifdef _WIN32
    auto p = _aligned_malloc(size ? size : align, align);
#else
    auto p = std::aligned_alloc(align, ((size ? size : 1) + align - 1) / align * align);
#endif
    if (p) return p;
    throw std::bad_alloc();
}
void operator delete(void *p, std::align_val_t) noexcept {
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete(void *p, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(p, alignment); }
static uint32_t candidates = 0;
static std::atomic<int> pins{0}, available{0};
void HookLog(const char *) {}
namespace cccaster::platform {
uint32_t CurrentPhysicalCoreMask() { return 2; }
uint32_t BoundaryCpuCandidates(uint32_t) { return candidates; }
TimingCpuPin::TimingCpuPin(int, uint32_t) {
    const auto slot = pins.fetch_add(1);
    if (slot < available.load()) cpu_ = slot + 2;
}
TimingCpuPin::~TimingCpuPin() {}
TimingThread::TimingThread(const char *) {}
TimingThread::~TimingThread() {}
int64_t RealMonotonicTicks() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() * 60;
}
void RealSleepMs(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
void CpuRelax() { std::this_thread::yield(); }
}
int main() {
#ifdef _WIN32
    _putenv("CCCASTER_BOUNDARY_WORKERS=");
    _putenv("CCCASTER_BOUNDARY_CPUS=");
#else
    unsetenv("CCCASTER_BOUNDARY_WORKERS"); unsetenv("CCCASTER_BOUNDARY_CPUS");
#endif
    Check(BoundaryWorkerLimit(nullptr) == 4);
    Check(BoundaryWorkerLimit("0") == 0 && BoundaryWorkerLimit("2") == 2);
    Check(BoundaryWorkerLimit("") == 4 && BoundaryWorkerLimit("40") == 0);
    Check(!BoundaryStallEnabled(nullptr) && !BoundaryStallEnabled("") && !BoundaryStallEnabled("0"));
    Check(BoundaryStallEnabled("1") && !BoundaryStallEnabled("11"));
    const uint32_t single[] = {1}, smt[] = {3}, four[] = {3, 12, 48, 192};
    Check(BoundaryCpuCandidates(1, 1, single, 1) == 0);
    Check(BoundaryCpuCandidates(3, 3, smt, 1) == 0);
    Check(BoundaryCpuCandidates(255, 12, four, 4) == (16 | 64));
    Check(BoundaryCpuCandidates(4, 12, four, 4) == 0); // 単一論理CPUへの制限
    Check(BoundaryCpuCandidates(255, 0, four, 4) == 0); // ゲームがコア間を移動
    Check(BoundaryCpuCandidates(4 | 32 | 128, 12, four, 4) == (32 | 128));
    {
        BoundaryRace race;
        Check(!race.Enabled() && pins == 0); // 追加時計スレッドの生成そのものがない
        race.Arm(100, 90);
        Check(race.Read(110).boundary == 110);
    }
    candidates = 4 | 16; available = 0;
    {
        BoundaryRace race;
        Check(!race.Enabled() && pins == 2); // 全コアが他プロセスに使用中でも正常終了
    }
    pins = 0; candidates = 4 | 16 | 64 | 256; available = 2;
    {
        BoundaryRace race;
        Check(race.Enabled() && pins == 4);
        auto now = cccaster::platform::RealMonotonicTicks();
        race.Arm(now + 60000, now);
        Check(race.Read(now + 60000).workers == 2); // 一部だけ確保できても縮退して使用
        Check(race.HasWork());
        failNextAllocation = true;
        race.Arm(now + 120000, now);
        Check(!failNextAllocation && !race.HasWork());
        const auto fallback = race.Read(now + 120001);
        Check(fallback.boundary == now + 120001 && fallback.winner == -1 && fallback.valid == 0);
        // 回復後の次の締切には再び補助を利用できる。
        race.Arm(now + 180000, now);
        Check(race.HasWork());
        // Arm直後の終了でも通知を失わずjoinできる（CTest timeoutで監視）。
    }
}
