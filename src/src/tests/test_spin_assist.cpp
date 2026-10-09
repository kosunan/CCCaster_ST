#include "core_dll/timing/SpinAssist.hpp"
#include <future>
#include <chrono>
#include <stdexcept>
using namespace cccaster::core::timer;
static void Check(bool ok) { if (!ok) std::abort(); }
namespace cccaster::platform { int64_t RealMonotonicTicks() { return 1000; } }
static std::array<std::promise<void>, 4> paused;
static std::promise<void> resume;
static auto resumed = resume.get_future().share();
int main() {
#ifdef _WIN32
    _putenv("CCCASTER_SPIN_PRESENT=1");
    _putenv("CCCASTER_SPIN_PUBLICATION=0");
    _putenv("CCCASTER_SPIN_CAPTURE=0");
#else
    setenv("CCCASTER_SPIN_PRESENT", "1", 1);
    setenv("CCCASTER_SPIN_PUBLICATION", "0", 1);
    setenv("CCCASTER_SPIN_CAPTURE", "0", 1);
#endif
    Check(!SpinPrototype::Any()); // 廃止設定だけで共有巡回へ切り替わらない。
    SpinChannels channels;
    std::atomic<int> calls{0};
    SpinTask future(200, 500, [](void *p, int) { ++*static_cast<std::atomic<int> *>(p); return true; }, &calls);
    channels.Publish(SpinChannels::Publication, future);
    Check(channels.Tick(0, 100) == 200 && calls == 0);
    channels.Tick(0, 200); channels.Tick(0, 300);
    Check(calls == 1 && future.Ready());
    channels.Tick(1, 501); Check(calls == 1); // 窓終了後に無限スピンしない。
    int winner = -1;
    Check(future.Observed(999, &winner) == 999 && winner == -1); // ゲーム通過後の観測を遡及採用しない。
    Check(future.Observed(1001, &winner) == 1000 && winner == 0);
    channels.Retire(SpinChannels::Publication);

    struct Context { std::promise<void> entered, release; } context;
    auto entered = context.entered.get_future();
    SpinTask blocked(0, 10000, [](void *p, int) {
        auto &c = *static_cast<Context *>(p);
        c.entered.set_value(); c.release.get_future().wait(); return true;
    }, &context);
    channels.Publish(SpinChannels::Capture, blocked);
    auto worker = std::async(std::launch::async, [&] { channels.Tick(0, 100); });
    entered.wait();
    auto retire = std::async(std::launch::async, [&] { channels.Retire(SpinChannels::Capture); });
    Check(retire.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout);
    // 採取1本が中断されても別のワーカーは公開を観測できる。
    SpinTask publication(0, 10000, [](void *, int) { return true; }, nullptr);
    channels.Publish(SpinChannels::Publication, publication);
    channels.Tick(1, 100);
    Check(publication.Ready());
    context.release.set_value(); worker.get(); retire.get();
    channels.Retire(SpinChannels::Publication);

    SpinTask throws(0, 10000, [](void *, int) -> bool { throw std::runtime_error("test"); }, nullptr);
    channels.Publish(SpinChannels::Capture, throws);
    try { channels.Tick(0, 100); Check(false); } catch (const std::runtime_error &) {}
    channels.Retire(SpinChannels::Capture); // 例外でも参照を残さない。
    std::array<std::thread, 4> observers;
    for (int i = 0; i < 4; ++i) {
        auto ready = paused[i].get_future();
        Check(channels.Observe(SpinChannels::Publication, 0, 10000, [](void *arg, int) {
            const auto value = *static_cast<int64_t *>(arg);
            paused[value].set_value(); resumed.wait();
            Check(*static_cast<int64_t *>(arg) == value); // 中断中の引数を書き換えない。
            return true;
        }, i) != nullptr);
        observers[i] = std::thread([&, i] { channels.Tick(i, 250); });
        ready.wait();
    }
    Check(channels.Observe(SpinChannels::Publication, 0, 10000,
        [](void *, int) { return true; }, 4) != nullptr); // 4本停止中でも5枠目で即時に再arm。
    channels.Detach(SpinChannels::Publication); // ゲーム側は停止した観測者を待たない。
    resume.set_value();
    for (auto &observer : observers) observer.join();
    // 同じアドレスを繰り返し再利用し、取り消し済み要求の参照漏れを検出。
    std::atomic<bool> stop{false};
    std::thread reader([&] { while (!stop) channels.Tick(0, 250); });
    for (int i = 0; i < 10000; ++i) {
        SpinTask task(200, 500, [](void *, int) { return true; }, nullptr);
        channels.Publish(SpinChannels::Publication, task);
        channels.Retire(SpinChannels::Publication);
    }
    stop = true; reader.join();
}
