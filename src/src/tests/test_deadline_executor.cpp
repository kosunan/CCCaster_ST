#include "core_dll/common/DeadlineExecutor.hpp"
#include "test_support.hpp"
#include <future>
#include <stdexcept>
#include <thread>
using namespace cccaster::platform;
static bool Await(const std::shared_ptr<DeadlineWork> &work, DeadlineWork::Result &result) {
    const auto limit = RealMonotonicTicks() + 60000000;
    while (!work->Read(result) && RealMonotonicTicks() < limit) RealSleepUs(100);
    return work->Read(result);
}
int main() {
    CC_CASE("処理途中で止まった担当を待たず、完了・公開した担当だけを採用する");
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    auto work = std::make_shared<DeadlineWork>(RealMonotonicTicks(), [&](int worker, int64_t) {
        if (worker == 0) { entered.set_value(); released.wait(); }
        return uint64_t(100 + worker);
    });
    bool rejected = false;
    std::thread slow([&] { rejected = !work->Execute(0); });
    entered.get_future().wait();
    CC_CHECK(work->Execute(1));
    DeadlineWork::Result adopted;
    CC_CHECK(work->Read(adopted));
    CC_CHECK_EQ(adopted.worker, 1); CC_CHECK_EQ(adopted.value, 101);
    const auto selectedEnd = adopted.completed;
    release.set_value(); slow.join();
    CC_CHECK(rejected && work->Read(adopted));
    CC_CHECK_EQ(adopted.worker, 1); CC_CHECK_EQ(adopted.completed, selectedEnd);

    CC_CASE("早い実行は禁止し、4担当で処理結果を返す");
    DeadlineExecutor pool({4, {}, {}, {}});
    CC_CHECK_EQ(pool.WorkerCount(), 4);
    std::atomic<int> early{0};
    for (int i = 0; i < 30; ++i) {
        const auto due = RealMonotonicTicks() + 120000;
        auto next = std::make_shared<DeadlineWork>(due, [&, due, i](int worker, int64_t started) {
            if (started < due) ++early;
            return uint64_t(i * 10 + worker);
        });
        pool.Submit(next);
        CC_CHECK(Await(next, adopted));
        CC_CHECK(adopted.worker >= 0 && adopted.worker < 4);
        CC_CHECK_EQ(adopted.value, i * 10 + adopted.worker);
        CC_CHECK(adopted.due <= adopted.started && adopted.started <= adopted.completed &&
            adopted.completed <= adopted.publicationUpper && adopted.publicationUpper <= adopted.observed);
    }
    CC_CHECK_EQ(early.load(), 0);

    CC_CASE("非採用担当が停止中でも次の依頼を完了できる");
    std::promise<void> poolEntered, poolRelease;
    auto inWork = poolEntered.get_future().share(), continueWork = poolRelease.get_future().share();
    auto blocked = std::make_shared<DeadlineWork>(RealMonotonicTicks(), [&poolEntered, inWork, continueWork](int worker, int64_t) {
        if (!worker) { poolEntered.set_value(); continueWork.wait(); }
        else inWork.wait();
        return uint64_t(worker);
    });
    pool.Submit(blocked);
    CC_CHECK(Await(blocked, adopted));
    CC_CHECK(adopted.worker != 0);
    auto following = std::make_shared<DeadlineWork>(RealMonotonicTicks(), [](int, int64_t) { return uint64_t(456); });
    pool.Submit(following);
    CC_CHECK(Await(following, adopted)); CC_CHECK_EQ(adopted.value, 456);
    poolRelease.set_value();

    CC_CASE("受取りが遅れても採用担当の処理時刻を受取り時刻で置換しない");
    const auto before = adopted.completed;
    RealSleepUs(3000);
    CC_CHECK(work->Read(adopted));
    CC_CHECK_EQ(adopted.completed, selectedEnd);
    CC_CHECK(adopted.observed > before);

    CC_CASE("依頼取消しと担当0本の呼出側縮退");
    std::atomic<int> called{0};
    auto cancelled = std::make_shared<DeadlineWork>(RealMonotonicTicks() + 300000,
        [&](int, int64_t) { ++called; return uint64_t(1); });
    pool.Submit(cancelled); pool.Submit(nullptr);
    RealSleepUs(7000);
    CC_CHECK_EQ(called.load(), 0);
    DeadlineExecutor single({0, {}, {}, {}});
    CC_CHECK_EQ(single.WorkerCount(), 0);
    auto fallback = std::make_shared<DeadlineWork>(RealMonotonicTicks(), [](int, int64_t) { return uint64_t(99); });
    single.Submit(fallback);
    CC_CHECK(fallback->Execute(DeadlineWork::Caller) && fallback->Read(adopted));
    CC_CHECK_EQ(adopted.worker, DeadlineWork::Caller); CC_CHECK_EQ(adopted.value, 99);

    CC_CASE("失敗した1担当が他担当の完了を妨げない");
    auto failure = std::make_shared<DeadlineWork>(RealMonotonicTicks(), [](int worker, int64_t) -> uint64_t {
        if (!worker) throw std::runtime_error("injected");
        return 123;
    });
    try { failure->Execute(0); CC_CHECK(false); } catch (const std::runtime_error &) {}
    CC_CHECK(failure->Execute(1)); CC_CHECK(failure->Read(adopted)); CC_CHECK_EQ(adopted.value, 123);
    return cccaster::test::Summarize("deadline_executor");
}
