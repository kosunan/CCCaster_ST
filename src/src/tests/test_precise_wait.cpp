#include "core_dll/common/PreciseWait.hpp"
#include "test_support.hpp"
#include <array>
#include <atomic>
#include <thread>
using namespace cccaster::platform;
struct Clock {
    int64_t now = 1000000, firstSpin = 0, lastSleepTarget = 0;
    int sleeps = 0, spins = 0;
    enum Wake { Exact, EarlyOnce, Late, FailedTimer } wake = Exact;
    int64_t Now() const { return now; }
    void SleepUntil(int64_t target) {
        lastSleepTarget = target;
        ++sleeps;
        if (wake == EarlyOnce && sleeps == 1) now += (target - now) / 2;
        else now = target + (wake == Late || wake == FailedTimer ? 90000 : 0);
    }
    void Relax() { if (!spins++) firstSpin = now; now += 6; }
};
int main() {
    CC_CASE("1ms前まで休止し、同じ絶対締切までスピンする");
    Clock clock;
    const auto due = clock.now + 180000;
    CC_CHECK_EQ(precise_wait_detail::WaitUntil(clock, due, PreciseSpinUs * 60), due);
    CC_CHECK_EQ(clock.lastSleepTarget, due - 60000);
    CC_CHECK_EQ(clock.firstSpin, due - 60000);
    CC_CASE("早い起床で締切を移動せず、遅い起床は実時刻で返す");
    for (auto wake : {Clock::EarlyOnce, Clock::Late, Clock::FailedTimer}) {
        Clock sample; sample.wake = wake;
        const auto target = sample.now + 180000;
        const auto observed = precise_wait_detail::WaitUntil(sample, target, 60000);
        CC_CHECK(observed >= target);
        CC_CHECK_EQ(observed, sample.now);
        CC_CHECK_EQ(sample.lastSleepTarget, target - 60000);
        if (wake == Clock::EarlyOnce) CC_CHECK(sample.sleeps > 1);
        else CC_CHECK_EQ(sample.spins, 0);
    }
    CC_CASE("期限超過と1ms以下の待機");
    for (int64_t delta : {-100, 0, 1, 30000, 60000}) {
        Clock sample;
        const auto target = sample.now + delta;
        CC_CHECK(precise_wait_detail::WaitUntil(sample, target, 60000) >= target);
        CC_CHECK_EQ(sample.sleeps, 0);
        if (delta <= 0) CC_CHECK_EQ(sample.spins, 0);
    }
    CC_CASE("休止専用では早い起床にもスピンを使わない");
    Clock sleeping; sleeping.wake = Clock::EarlyOnce;
    const auto target = sleeping.now + 30000;
    CC_CHECK_EQ(precise_wait_detail::WaitUntil(sleeping, target, 0), target);
    CC_CHECK(sleeping.sleeps > 1);
    CC_CHECK_EQ(sleeping.spins, 0);
    CC_CASE("相対時間のゼロ・負値・飽和と1/60µsの保持");
    constexpr auto limit = std::numeric_limits<int64_t>::max();
    CC_CHECK_EQ(precise_wait_detail::DeadlineAfterUs(60001, 500), 90001);
    CC_CHECK_EQ(precise_wait_detail::DeadlineAfterUs(60001, 0), 60001);
    CC_CHECK_EQ(precise_wait_detail::DeadlineAfterUs(60001, -1), 60001);
    CC_CHECK_EQ(precise_wait_detail::DeadlineAfterUs(60001, limit), limit);
    CC_CHECK_EQ(precise_wait_detail::DeadlineAfterUs(limit - 5, 1), limit);
    CC_CASE("実OSで相対／絶対待機が期限前に返らず、4スレッドで独立して待つ");
    std::atomic<int> early{0};
    std::array<std::thread, 4> threads;
    for (auto &thread : threads) thread = std::thread([&] {
        for (int64_t us : {1, 200, 500, 1000, 1200, 3000}) {
            const auto absolute = RealMonotonicTicks() + us * 60;
            const auto observed = PreciseWaitUntilTicks(absolute);
            if (observed < absolute || RealMonotonicTicks() < observed) ++early;
            const auto relative = RealMonotonicTicks() + us * 60;
            PreciseWaitUs(us);
            if (RealMonotonicTicks() < relative) ++early;
            const auto sleepDue = RealMonotonicTicks() + us * 60;
            RealSleepUs(us);
            if (RealMonotonicTicks() < sleepDue) ++early;
        }
    });
    for (auto &thread : threads) thread.join();
    CC_CHECK_EQ(early.load(), 0);
    PreciseWaitUs(0); PreciseWaitUs(-1); RealSleepUs(0); RealSleepUs(-1);
    CC_CHECK(PreciseWaitUntilTicks(-1) >= 0);
    return cccaster::test::Summarize("precise_wait");
}
