#include "core_dll/timing/Metronome.hpp"
#include "core_dll/timing/WasapiClock.hpp"
#include "core_dll/timing/IdlePresentation.hpp"
#include <cstdlib>
#include <cstdio>
static int64_t now = 60000000;
static unsigned preciseCalls = 0, sleepCalls = 0;
static unsigned presents = 0;
static int64_t spinBegins = 0, presentCost = 0;
static bool spun = false;
void HookLog(const char *) {}
namespace cccaster::core::timer {
int64_t WasapiClock::GetTimeTicks() { return now; }
void WasapiClock::PrepareRelease(int64_t) {}
}
namespace cccaster::platform {
int64_t RealMonotonicTicks() { return now; }
void RealSleepMs(uint32_t ms) { now += ms * 60000; ++sleepCalls; }
void RealSleepUs(int64_t us) { now += us * 60; ++preciseCalls; }
void CpuRelax() { now += 6; spun = true; }
}
static void require(bool condition) { if (!condition) std::abort(); }
static void present(int64_t availableUs) {
    require(!spun && now < spinBegins);
    require(availableUs == (spinBegins - now) / 60);
    ++presents;
    now += presentCost * 60;
}
int main() {
#ifdef _WIN32
    _putenv("CCCASTER_TIME_SCALE=1");
    _putenv("CCCASTER_TEST_OFFLINE_PACING=");
#else
    setenv("CCCASTER_TIME_SCALE", "1", 1);
    unsetenv("CCCASTER_TEST_OFFLINE_PACING");
#endif
    cccaster::core::netplay::Metronome clock;
    clock.Start();
    const auto first = clock.WaitForNextTick(false, 200 * 60);
    require(first == 61000000 && now >= first - 12000 && now < first - 12000 + 6);
    require(preciseCalls > 0 && sleepCalls == 0);
    now += 83 * 60; // 入力準備83µsを周期へ加えない。
    const auto second = clock.WaitForNextTick(false, 200 * 60);
    require(second - first == 1000000);
    require(now >= second - 12000 && now < second - 12000 + 6);
    now = second + 1100000; // 次の締切を少し超える処理も、周期の基準を動かさない。
    const auto late = now;
    require(clock.WaitForNextTick(false, 12000) == second + 1000000 && now == late);
    const auto skipped = clock.WaitForNextTick(true);
    require(skipped == second + 2000000 && now == late);
    const auto normal = clock.WaitForNextTick(false);
    require(normal == second + 3000000 && now >= normal && now < normal + 6);
    require(sleepCalls > 0); // 既定の補助待機の経路は維持。
    clock.SetFramePeriodTicks(30000 * 60);
    require(clock.GetCurrentIntervalUs() == 30000);
    const auto slow = clock.WaitForNextTick(false);
    require(slow - normal == 30000 * 60 && now >= slow);
    clock.SetFramePeriodTicks(1); // 不正な要求でゼロ周期へ落とさない。
    require(clock.GetCurrentIntervalUs() == 30000);
    clock.SetFramePeriodTicks(cccaster::core::timer::NetworkPacing::Normal);
    require(clock.WaitForNextTick(false) - slow == 1000000);
    clock.Stop();
    require(!clock.IsRunning());
    // 実際の補助待機へ提示コストを注入。最終スピンに入った後のコールバックを禁止する。
    for (int64_t guard : {200, 1000, 2000, 3000}) {
        for (int64_t cost : {0, 800, 20000}) {
            cccaster::core::netplay::Metronome paced;
            paced.Start();
            const auto due = now + 1000000;
            spinBegins = due - 200 * 60 - guard * 60;
            presentCost = cost; spun = false; presents = 0;
            preciseCalls = sleepCalls = 0;
            cccaster::core::timer::IdlePresentation::Scope scope(present);
            require(paced.WaitForNextTick(false, 200 * 60, guard) == due);
            require(presents > 0 && now >= due - 200 * 60);
            if (cost == 20000)
                require(!spun && preciseCalls == 0 && sleepCalls == 0); // 提示が期限超過しても追加で眠らない。
            else require(spun && now < due - 200 * 60 + 6);
            const auto before = presents;
            paced.WaitForNextTick(true, 200 * 60, guard);
            require(presents == before); // 再計算・追いつきの待機省略へ提示を追加しない。
            paced.Stop();
        }
    }
    std::puts("metronome: 絶対締切・準備余裕・遅延・待機省略 OK");
}
