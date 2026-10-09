// Windows実機の待機誤差／スレッドCPU時間。ログ整形は全採取後だけ。
#define NOMINMAX
#include <windows.h>
#include "core_dll/common/PreciseWait.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace cccaster::platform;
// 変更前のPlatform.cppと同じ100µs余裕・整数µs時計による比較用。
static void LegacyWait(int64_t us) {
    struct Timer {
        HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, 0x2, TIMER_ALL_ACCESS);
        Timer() { if (!handle) handle = CreateWaitableTimerW(nullptr, FALSE, nullptr); }
        ~Timer() { if (handle) CloseHandle(handle); }
    };
    thread_local Timer timer;
    const auto due = RealMonotonicUs() + us;
    if (timer.handle && us > 150) {
        LARGE_INTEGER relative; relative.QuadPart = -(us - 100) * 10;
        if (SetWaitableTimer(timer.handle, &relative, 0, nullptr, nullptr, FALSE))
            WaitForSingleObject(timer.handle, DWORD(us / 1000 + 10));
    } else if (!timer.handle && us >= 1000) Sleep(DWORD(us / 1000));
    while (RealMonotonicUs() < due) CpuRelax();
}
static uint64_t CpuTicks() {
    FILETIME created, exited, kernel, user;
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) std::abort();
    return (uint64_t(kernel.dwHighDateTime) << 32 | kernel.dwLowDateTime) +
           (uint64_t(user.dwHighDateTime) << 32 | user.dwLowDateTime);
}
struct Result {
    const char *name;
    void (*wait)(int64_t);
    int64_t duration;
    std::vector<int64_t> errors;
    uint64_t cpu = 0;
    int64_t wall = 0;
};
int main(int argc, char **argv) {
    const int samples = argc > 1 ? std::atoi(argv[1]) : 1200;
    if (samples < 100 || samples > 10000) return 2;
    std::vector<Result> results;
    for (int64_t us : {500, 1000, 2000, 16667}) {
        results.push_back({"legacy_100us", LegacyWait, us, {}});
        results.push_back({"precise_1000us", PreciseWaitUs, us, {}});
    }
    results.push_back({"sleep_only", RealSleepUs, 500, {}});
    for (auto &result : results) {
        result.errors.reserve(samples);
        for (int i = 0; i < 3; ++i) result.wait(result.duration);
    }
    // 50標本のバッチを正順／逆順で交互に採取し、測定順の偏りを抑える。
    for (int offset = 0; offset < samples; offset += 50) {
        for (size_t index = 0; index < results.size(); ++index) {
            auto &result = results[(offset / 50) % 2 ? results.size() - index - 1 : index];
            const auto cpu = CpuTicks();
            const auto start = RealMonotonicTicks();
            for (int i = offset; i < std::min(samples, offset + 50); ++i) {
                const auto begin = RealMonotonicTicks();
                result.wait(result.duration);
                result.errors.push_back(RealMonotonicTicks() - begin - result.duration * 60);
            }
            result.wall += RealMonotonicTicks() - start;
            result.cpu += CpuTicks() - cpu;
        }
    }
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    SYSTEM_INFO info{}; GetSystemInfo(&info);
    std::printf("{\"samples_per_case\":%d,\"qpc_hz\":%lld,\"logical_cpus\":%lu,"
        "\"priority_class\":%lu,\"thread_priority\":%d,\"rows\":[", samples,
        static_cast<long long>(frequency.QuadPart), info.dwNumberOfProcessors,
        GetPriorityClass(GetCurrentProcess()), GetThreadPriority(GetCurrentThread()));
    bool first = true;
    int newEarly = 0;
    for (auto &result : results) {
        auto &values = result.errors;
        std::sort(values.begin(), values.end());
        const auto count = [&](int64_t above) { return std::count_if(values.begin(), values.end(),
            [=](int64_t value) { return value > above; }); };
        const auto early = std::count_if(values.begin(), values.end(), [](int64_t value) { return value < 0; });
        if (result.wait != LegacyWait) newEarly += int(early);
        const auto p = [&](double q) { return values[size_t((values.size() - 1) * q)] / 60.0; };
        std::printf("%s{\"mode\":\"%s\",\"duration_us\":%lld,\"n\":%zu,"
            "\"min_us\":%.6f,\"p50_us\":%.6f,\"p95_us\":%.6f,\"p99_us\":%.6f,\"max_us\":%.6f,"
            "\"early\":%lld,\"over_3us\":%lld,\"over_50us\":%lld,\"cpu_ms\":%.3f,"
            "\"wall_ms\":%.3f,\"one_cpu_percent\":%.3f}", first ? "" : ",", result.name,
            static_cast<long long>(result.duration), values.size(), p(0), p(.5), p(.95), p(.99), p(1),
            static_cast<long long>(early), static_cast<long long>(count(180)), static_cast<long long>(count(3000)),
            result.cpu / 10000.0, result.wall / 60000.0, result.cpu * 600.0 / result.wall);
        first = false;
    }
    std::printf("],\"new_early_returns\":%d}\n", newEarly);
    return newEarly ? 1 : 0;
}
