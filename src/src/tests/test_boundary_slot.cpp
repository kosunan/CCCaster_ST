#include "core_dll/timing/BoundarySlot.hpp"
#include <cstdio>
#include <cstdlib>
#include <thread>
using cccaster::core::timer::BoundarySlot;
static void Check(bool value) { if (!value) std::abort(); }
int main() {
    BoundarySlot slot;
    BoundarySlot::Sample sample;
    Check(!slot.Read(100, sample));
    slot.Publish({100, 101, 90});
    Check(slot.Read(100, sample) && sample.stamp == 101);
    Check(!slot.Read(200, sample)); // 前の締切の結果を次フレームへ流用しない。
    slot.sequence.fetch_add(1); // 公開の途中で書手が止まっても読手は待たない。
    Check(!slot.Read(100, sample));
    slot.sequence.fetch_add(1);
    slot.Publish({100, 99, 90});
    Check(!slot.Read(100, sample)); // 締切より前の値を採用しない。
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (int i = 1; i <= 100000; ++i) slot.Publish({100, 100 + i, i});
        done.store(true);
    });
    while (!done.load())
        if (slot.Read(100, sample)) Check(sample.stamp == sample.entered + 100);
    writer.join();
    Check(slot.Read(100, sample) && sample.stamp == 100100);
    std::puts("境界公開: 古い世代・公開中断・締切前・並行読取りを検査");
}
