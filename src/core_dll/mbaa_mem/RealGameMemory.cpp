// ============================================================================
// RealGameMemory.cpp — 実メモリ読み書き（実装）
//
// ここが MbaaAddresses.hpp / MbaaInputDefs.hpp のアドレスに触れる唯一の場所
// （FastBoot のコード書換と起動時パッチを除く）。
// 入力書込みは FrameControl から移設したもので、処理内容は変えていない。
// ============================================================================

#include "core_dll/mbaa_mem/RealGameMemory.hpp"
#include "core_dll/mbaa_mem/MbaaAddresses.hpp"
#include "core_dll/mbaa_mem/MbaaInputDefs.hpp"
#include "core_dll/common/DebugLog.hpp"

#include <windows.h>

namespace cccaster::game_interface {

using cccaster::domain::session::DebugLog;

namespace {

/// 入力書込み先はポインタ経由。ゲーム側が未初期化だと NULL になる。
char* InputBasePtr() {
    return *reinterpret_cast<char**>(CC_PTR_TO_WRITE_INPUT_ADDR);
}

void LogNullInputBase() {
    static uint32_t s_nullCount = 0;
    if (s_nullCount++ % 120 == 0) {
        DebugLog("[RealGameMemory] Input base pointer is NULL (count=%u)", s_nullCount);
    }
}

} // namespace

bool RealGameMemory::IsAvailable() const {
    return !IsBadReadPtr(CC_GAME_MODE_ADDR, sizeof(uint32_t));
}

uint32_t RealGameMemory::GameMode() const {
    return *CC_GAME_MODE_ADDR;
}

uint8_t RealGameMemory::IntroState() const {
    return *CC_INTRO_STATE_ADDR;
}

uint32_t RealGameMemory::WorldTimer() const {
    return *CC_WORLD_TIMER_ADDR;
}

uint32_t RealGameMemory::RealTimer() const {
    return *CC_REAL_TIMER_ADDR;
}

uint32_t RealGameMemory::MenuStateCounter() const {
    return *CC_MENU_STATE_COUNTER_ADDR;
}

void RealGameMemory::WriteInput(GameInput p1, GameInput p2) {
    char* base = InputBasePtr();
    if (!base) {
        LogNullInputBase();
        return;
    }
    *reinterpret_cast<uint32_t*>(base + CC_P1_OFFSET_DIRECTION) = p1.direction;
    *reinterpret_cast<uint16_t*>(base + CC_P1_OFFSET_BUTTONS)   = p1.buttons;
    *reinterpret_cast<uint32_t*>(base + CC_P2_OFFSET_DIRECTION) = p2.direction;
    *reinterpret_cast<uint16_t*>(base + CC_P2_OFFSET_BUTTONS)   = p2.buttons;
}

void InstallRealGameMemory() {
    static RealGameMemory s_real;
    InstallGameMemory(&s_real);
}

} // namespace cccaster::game_interface
