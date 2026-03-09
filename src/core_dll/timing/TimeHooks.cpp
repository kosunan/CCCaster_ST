#include "core_dll/timing/TimeHooks.hpp"
#include "MinHook.h"
#include <atomic>
#include <iostream>

#pragma comment(lib, "winmm.lib")

namespace cccaster::core::hooks {

bool TimeHooks::s_initialized = false;
uint32_t TimeHooks::s_multiplier = 1;
bool TimeHooks::s_sleepBypass = false;

// Time Hack State
static std::atomic<LONGLONG> g_addedQPC{0};
static std::atomic<DWORD> g_addedGTC{0};
static std::atomic<DWORD> g_addedTGT{0};

static LARGE_INTEGER g_prevQPC{0};
static DWORD g_prevGTC = 0;

// Function pointers for original APIs
typedef void (WINAPI *Sleep_t)(DWORD);
static Sleep_t pOrigSleep = nullptr;

typedef BOOL (WINAPI *QueryPerformanceCounter_t)(LARGE_INTEGER*);
static QueryPerformanceCounter_t pOrigQPC = nullptr;

typedef DWORD (WINAPI *GetTickCount_t)(VOID);
static GetTickCount_t pOrigGTC = nullptr;

typedef DWORD (WINAPI *timeGetTime_t)(VOID);
static timeGetTime_t pOrigTGT = nullptr;

// =========================================================
// Hooked Functions
// =========================================================

void WINAPI Hooked_Sleep(DWORD dwMilliseconds) {
    if (TimeHooks::s_sleepBypass) {
        if (pOrigSleep) pOrigSleep(0);
        else Sleep(0);
        return;
    }
    if (pOrigSleep) pOrigSleep(dwMilliseconds);
    else Sleep(dwMilliseconds);
}

BOOL WINAPI Hooked_QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount) {
    BOOL ret = pOrigQPC ? pOrigQPC(lpPerformanceCount) : QueryPerformanceCounter(lpPerformanceCount);
    if (ret) {
        lpPerformanceCount->QuadPart += g_addedQPC.load(std::memory_order_relaxed);
    }
    return ret;
}

DWORD WINAPI Hooked_GetTickCount(VOID) {
    DWORD ret = pOrigGTC ? pOrigGTC() : GetTickCount();
    return ret + g_addedGTC.load(std::memory_order_relaxed);
}

DWORD WINAPI Hooked_timeGetTime(VOID) {
    DWORD ret = pOrigTGT ? pOrigTGT() : timeGetTime();
    return ret + g_addedTGT.load(std::memory_order_relaxed);
}

// =========================================================
// Core Update Thread (To maintain monotonic increasing time)
// =========================================================
static bool g_timeThreadRunning = false;
static HANDLE g_hTimeThread = nullptr;

static DWORD WINAPI TimeUpdateThread(LPVOID) {
    timeBeginPeriod(1);
    
    while (g_timeThreadRunning) {
        LARGE_INTEGER nowQPC;
        if (pOrigQPC) pOrigQPC(&nowQPC); else QueryPerformanceCounter(&nowQPC);
        DWORD nowGTC;
        if (pOrigGTC) nowGTC = pOrigGTC(); else nowGTC = GetTickCount();

        LONGLONG elapsedQPC = nowQPC.QuadPart - g_prevQPC.QuadPart;
        DWORD elapsedGTC = nowGTC - g_prevGTC;

        g_prevQPC = nowQPC;
        g_prevGTC = nowGTC;

        uint32_t currentMult = TimeHooks::s_multiplier;
        if (currentMult > 1) {
            g_addedQPC.fetch_add(elapsedQPC * (currentMult - 1), std::memory_order_relaxed);
            g_addedGTC.fetch_add(elapsedGTC * (currentMult - 1), std::memory_order_relaxed);
            g_addedTGT.fetch_add(elapsedGTC * (currentMult - 1), std::memory_order_relaxed);
        }

        if (pOrigSleep) pOrigSleep(1); else Sleep(1);
    }
    timeEndPeriod(1);
    return 0;
}

// =========================================================
// Public Methods
// =========================================================

void TimeHooks::Initialize() {
    if (s_initialized) return;

    if (MH_Initialize() != MH_OK) {
        std::cerr << "[TimeHooks] MinHook Initialize failed.\n";
        return;
    }

    HMODULE hKernel32 = GetModuleHandle("kernel32.dll");
    if (hKernel32) {
        void* pSleep = (void*)GetProcAddress(hKernel32, "Sleep");
        void* pQPC = (void*)GetProcAddress(hKernel32, "QueryPerformanceCounter");
        void* pGTC = (void*)GetProcAddress(hKernel32, "GetTickCount");

        if (pSleep) MH_CreateHook(pSleep, (void*)&Hooked_Sleep, (void**)&pOrigSleep);
        if (pQPC) MH_CreateHook(pQPC, (void*)&Hooked_QueryPerformanceCounter, (void**)&pOrigQPC);
        if (pGTC) MH_CreateHook(pGTC, (void*)&Hooked_GetTickCount, (void**)&pOrigGTC);

        if (pSleep) MH_EnableHook(pSleep);
        if (pQPC) MH_EnableHook(pQPC);
        if (pGTC) MH_EnableHook(pGTC);
    }

    HMODULE hWinmm = GetModuleHandle("winmm.dll");
    if (!hWinmm) hWinmm = LoadLibraryA("winmm.dll");
    if (hWinmm) {
        void* pTGT = (void*)GetProcAddress(hWinmm, "timeGetTime");
        if (pTGT && MH_CreateHook(pTGT, (void*)&Hooked_timeGetTime, (void**)&pOrigTGT) == MH_OK) {
            MH_EnableHook(pTGT);
        }
    }

    // Initialize previous time tracking before starting the thread
    if (pOrigQPC) pOrigQPC(&g_prevQPC); else QueryPerformanceCounter(&g_prevQPC);
    if (pOrigGTC) g_prevGTC = pOrigGTC(); else g_prevGTC = GetTickCount();

    g_timeThreadRunning = true;
    g_hTimeThread = CreateThread(nullptr, 0, TimeUpdateThread, nullptr, 0, nullptr);

    s_initialized = true;
    std::cout << "[TimeHooks] Initialized successfully. APIs Hooked.\n";
}

void TimeHooks::Shutdown() {
    if (!s_initialized) return;

    g_timeThreadRunning = false;
    if (g_hTimeThread) {
        WaitForSingleObject(g_hTimeThread, 1000);
        CloseHandle(g_hTimeThread);
        g_hTimeThread = nullptr;
    }

    MH_DisableHook(MH_ALL_HOOKS);
    s_initialized = false;
    std::cout << "[TimeHooks] Shutdown complete.\n";
}

void TimeHooks::SetTimeMultiplier(uint32_t multiplier) {
    if (multiplier == 0) multiplier = 1;
    s_multiplier = multiplier;
}

void TimeHooks::SetSleepBypass(bool bypass) {
    s_sleepBypass = bypass;
}

void TimeHooks::RealQueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount) {
    if (pOrigQPC) {
        pOrigQPC(lpPerformanceCount);
    } else {
        QueryPerformanceCounter(lpPerformanceCount);
    }
}

DWORD TimeHooks::RealGetTickCount() {
    return pOrigGTC ? pOrigGTC() : GetTickCount();
}

DWORD TimeHooks::RealTimeGetTime() {
    return pOrigTGT ? pOrigTGT() : timeGetTime();
}

void TimeHooks::RealSleep(DWORD dwMilliseconds) {
    if (pOrigSleep) {
        pOrigSleep(dwMilliseconds);
    } else {
        Sleep(dwMilliseconds);
    }
}

} // namespace cccaster::core::hooks
