#define NOMINMAX
#include <windows.h>
#include "core_dll/common/DeadlineExecutor.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace cccaster::platform;
static uint64_t Compute(uint64_t value) {
    for (int i = 0; i < 32; ++i) value = (value ^ (value >> 13)) * 0x9e3779b185ebca87ULL + i;
    return value;
}
static std::vector<DWORD_PTR> PhysicalCpus() {
    DWORD_PTR allowed = 0, system = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &allowed, &system)) return {};
    DWORD bytes = 0; GetLogicalProcessorInformation(nullptr, &bytes);
    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> info(bytes / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
    if (!GetLogicalProcessorInformation(info.data(), &bytes)) return {};
    std::vector<DWORD_PTR> cpus;
    for (const auto &item : info) if (item.Relationship == RelationProcessorCore) {
        const auto mask = item.ProcessorMask & allowed;
        if (mask) cpus.push_back(mask & (~mask + 1));
    }
    return cpus;
}
struct Samples {
    int workers = 0, duration = 0, injected = 0, readerBounds = 0;
    const char *mode = "normal";
    std::array<int, 5> winners{};
    std::vector<int64_t> started, completed, publication, reception;
    std::vector<DeadlineWork::Result> accepted;
};
static void Distribution(const char *name, std::vector<int64_t> values) {
    std::sort(values.begin(), values.end());
    std::printf("\"%s\":{\"n\":%zu,\"p50_us\":%.6f,\"p99_us\":%.6f,\"max_us\":%.6f}",
        name, values.size(), values[(values.size()-1)/2]/60.0,
        values[(values.size()*99+99)/100-1]/60.0, values.back()/60.0);
}
int main(int argc, char **argv) {
    const int count = argc > 1 ? std::atoi(argv[1]) : 1200;
    if (count < 100 || count > 10000) return 2;
    auto cpus = PhysicalCpus();
    if (cpus.size() < 5) { std::fputs("4 separate worker cores plus caller core required\n", stderr); return 2; }
    SetThreadAffinityMask(GetCurrentThread(), cpus[0]);
    std::vector<Samples> all;
    int failures = 0;
    for (int duration : {2000, 16667}) for (int workers : {1, 4}) {
        for (int fault = 0; fault < (workers == 4 && duration == 2000 ? 2 : 1); ++fault) {
            Samples sample; sample.workers = workers; sample.duration = duration;
            sample.mode = fault ? "worker0_stall_and_late_reader" : "normal";
            std::atomic<int> injected{0};
            DeadlineExecutor::Options options;
            options.workers = workers;
            options.enter = [&](int index) { return SetThreadAffinityMask(GetCurrentThread(), cpus[index+1]) != 0; };
            {
                DeadlineExecutor pool(std::move(options));
                if (pool.WorkerCount() != workers) return 3;
                for (int i = 0; i < count; ++i) {
                    const auto due = RealMonotonicTicks() + duration * 60;
                    const auto expected = Compute(uint64_t(i + 1));
                    auto work = std::make_shared<DeadlineWork>(due, [&, i, fault](int worker, int64_t) {
                        if (fault && worker == 0 && i % 20 == 0) {
                            ++injected; RealSleepUs(3000);
                        }
                        return Compute(uint64_t(i + 1));
                    });
                    pool.Submit(work);
                    // 読手の復帰は採用担当の測定から独立。故障条件では読手も3ms遅らせる。
                    RealSleepUs(duration + (fault ? 3000 : 250));
                    DeadlineWork::Result adopted;
                    const auto timeout = RealMonotonicTicks() + 6000000;
                    while (!work->Read(adopted) && RealMonotonicTicks() < timeout) RealSleepUs(100);
                    if (!work->Read(adopted)) return 4;
                    if (adopted.worker < 0 || adopted.worker >= workers || adopted.value != expected ||
                        adopted.started < due || adopted.completed < adopted.started ||
                        adopted.publicationUpper < adopted.completed || adopted.observed < adopted.publicationUpper) ++failures;
                    ++sample.winners[adopted.worker];
                    sample.started.push_back(adopted.started - due);
                    sample.completed.push_back(adopted.completed - due);
                    sample.publication.push_back(adopted.publicationUpper - due);
                    sample.reception.push_back(adopted.observed - adopted.completed);
                    sample.readerBounds += adopted.upperFromReader;
                    sample.accepted.push_back(adopted);
                }
            }
            sample.injected = injected.load();
            if (fault && !sample.injected) ++failures;
            all.push_back(std::move(sample));
        }
    }
    std::printf("{\"meaning\":\"adopted worker only; publication is an upper bound; reception is separate\",\"caller_cpu_mask\":%llu,\"worker_cpu_masks\":[%llu,%llu,%llu,%llu],\"priority_class\":%lu,\"thread_priority\":%d,\"samples_per_case\":%d,\"failures\":%d,\"rows\":[",
        static_cast<unsigned long long>(cpus[0]), static_cast<unsigned long long>(cpus[1]),
        static_cast<unsigned long long>(cpus[2]), static_cast<unsigned long long>(cpus[3]),
        static_cast<unsigned long long>(cpus[4]), GetPriorityClass(GetCurrentProcess()),
        GetThreadPriority(GetCurrentThread()), count, failures);
    bool first = true;
    for (const auto &s : all) {
        std::printf("%s{\"workers\":%d,\"duration_us\":%d,\"mode\":\"%s\",\"injected\":%d,\"reader_bounds\":%d,\"winners\":[%d,%d,%d,%d,%d],",
            first ? "" : ",", s.workers, s.duration, s.mode, s.injected, s.readerBounds,
            s.winners[0],s.winners[1],s.winners[2],s.winners[3],s.winners[4]);
        Distribution("adopted_start_late",s.started); std::putchar(',');
        Distribution("adopted_complete_late",s.completed); std::putchar(',');
        Distribution("adopted_publication_upper_late",s.publication); std::putchar(',');
        Distribution("reader_after_completion",s.reception);
        std::printf(",\"accepted_samples\":[");
        bool firstSample = true;
        for (const auto &a : s.accepted) {
            std::printf("%s{\"worker\":%d,\"due\":%lld,\"watching\":%lld,\"start\":%lld,\"done\":%lld,\"pub\":%lld,\"seen\":%lld,\"reader\":%d}",
                firstSample ? "" : ",", a.worker, static_cast<long long>(a.due),
                static_cast<long long>(a.watching), static_cast<long long>(a.started),
                static_cast<long long>(a.completed), static_cast<long long>(a.publicationUpper),
                static_cast<long long>(a.observed), int(a.upperFromReader));
            firstSample = false;
        }
        std::printf("]}"); first = false;
    }
    std::printf("],\"passed\":%s}\n", failures ? "false" : "true");
    return failures ? 1 : 0;
}
