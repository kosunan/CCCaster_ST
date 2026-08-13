// ============================================================================
// Platform.cpp — OS 依存処理の実装
//
// windows.h を include してよいのはこのファイルだけ（Platform.hpp は含めない）。
// ============================================================================

#include "core_dll/common/Platform.hpp"

#ifdef _WIN32
  #include <windows.h>
  #include "core_dll/hook/TimeHooks.hpp"
#else
  #include <ctime>
  #include <csignal>
  #include <atomic>
  #include <thread>
  #include <chrono>
#endif

#include <cstdlib>

namespace cccaster::platform {

// ============================================================================
// RealMonotonicUs
// ============================================================================
int64_t RealMonotonicUs() {
#ifdef _WIN32
    // マジックスタティックで一度だけ取得する。旧実装の
    // `static LARGE_INTEGER s_freq; if (s_freq.QuadPart == 0) ...` は
    // 32bit ビルドで 64bit ストアが2回に割れるため、複数スレッドから
    // 呼ばれると理論上 torn read になる。呼び出し元が
    // ゲームスレッド・通信スレッド・harness main と増えたので直しておく。
    static const int64_t freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<int64_t>(f.QuadPart);
    }();
    if (freq == 0) return 0;   // 取得失敗時のゼロ除算回避

    LARGE_INTEGER now;
    // フック後の QPC は 1000 倍速になっているため、必ず Real 版を使う
    cccaster::core::hooks::TimeHooks::RealQueryPerformanceCounter(&now);

    // 素直に `q * 1000000 / freq` と書くと int64 の乗算が先に溢れる。
    // QPC は起動時からの経過なので、10MHz なら **約 10.7 日で UB に入る**。
    // 商と剰余に分けて桁を落としてから掛ける。
    const int64_t q = now.QuadPart;
    return (q / freq) * 1000000LL + (q % freq) * 1000000LL / freq;
#else
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000LL + ts.tv_nsec / 1000;
#endif
}

// ============================================================================
// 待機
// ============================================================================
void SleepMs(uint32_t ms) {
#ifdef _WIN32
    // 意図的にフック後の Sleep を呼ぶ。実機では 0ms に化ける（現状の挙動）。
    ::Sleep(static_cast<DWORD>(ms));
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}

void RealSleepMs(uint32_t ms) {
#ifdef _WIN32
    cccaster::core::hooks::TimeHooks::RealSleep(static_cast<DWORD>(ms));
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}

void CpuRelax() {
#if defined(_WIN32)
    YieldProcessor();
#elif defined(__i386__) || defined(__x86_64__)
    __builtin_ia32_pause();
#else
    std::this_thread::yield();
#endif
}

// ============================================================================
// タイマー分解能
// ============================================================================
void BeginHighResolutionTimers() {
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
}

void EndHighResolutionTimers() {
#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

// ============================================================================
// 中断要求
// ============================================================================
#ifndef _WIN32
namespace {
std::atomic<bool> g_abortRequested{false};
void OnSignal(int sig) {
    g_abortRequested.store(true, std::memory_order_release);
    // 2回目は既定動作（即死）に戻す。フラグを立てるだけだと、
    // SceneRunner::Step() の中断チェック (H) に到達しない状態
    // （同期待ちなど）で Ctrl+C が効かなくなる。
    std::signal(sig, SIG_DFL);
}
}
#endif

bool IsAbortRequested() {
#ifdef _WIN32
    return (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
#else
    return g_abortRequested.load(std::memory_order_acquire);
#endif
}

void InstallAbortHandler() {
#ifndef _WIN32
    std::signal(SIGINT,  OnSignal);
    std::signal(SIGTERM, OnSignal);
#endif
}

// ============================================================================
// プロセス終了
// ============================================================================
void TerminateSelf() {
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), 1);
    // TerminateProcess は戻らないが、コンパイラにそれを教える手段がないため
    for (;;) {}
#else
    std::_Exit(1);
#endif
}

} // namespace cccaster::platform
