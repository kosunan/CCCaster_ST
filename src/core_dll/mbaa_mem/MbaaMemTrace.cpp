// ============================================================================
// MbaaMemTrace.cpp — 実機ゲームメモリの毎フレーム記録（実装）
//
// 出力形式（1行1フレーム、空白区切り）:
//   [MEM] netFrame mode intro state WT RT roundTimer menuCtr
//         rng0 rng1 p1seq p2seq p1hp p2hp roundCnt p1win p2win
//
// netFrame を先頭に置くのは、両プロセスをこの番号で突き合わせるため。
// 同じ netFrame で rng や seq が違えば、その時点で状態が分岐している。
// ============================================================================

#include "core_dll/mbaa_mem/MbaaMemTrace.hpp"
#include "core_dll/mbaa_mem/MbaaAddresses.hpp"
#include "core_dll/common/DebugLog.hpp"

#include <windows.h>
#include <cstdlib>

namespace cccaster::game_memory {

using cccaster::domain::session::DebugLog;

namespace {

/// 読めないアドレスに触れてクラッシュしないようにする。
/// ゲーム起動直後はまだマップされていない領域がある。
template <typename T>
T SafeRead(const T* addr, T fallback = T{}) {
    if (IsBadReadPtr(addr, sizeof(T))) return fallback;
    return *addr;
}

} // namespace

bool MbaaMemTrace::IsEnabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("CCCASTER_MEM_TRACE");
        return v && v[0] == '1';
    }();
    return enabled;
}

void MbaaMemTrace::Sample(uint32_t netFrame) {
    if (!IsEnabled()) return;

    DebugLog("[MEM] %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u",
             netFrame,
             SafeRead(CC_GAME_MODE_ADDR),
             static_cast<uint32_t>(SafeRead(CC_INTRO_STATE_ADDR)),
             SafeRead(CC_GAME_STATE_ADDR),
             SafeRead(CC_WORLD_TIMER_ADDR),
             SafeRead(CC_REAL_TIMER_ADDR),
             SafeRead(CC_ROUND_TIMER_ADDR),
             SafeRead(CC_MENU_STATE_COUNTER_ADDR),
             // ── デシンク指標 ──
             SafeRead(CC_RNG_STATE0_ADDR),
             SafeRead(CC_RNG_STATE1_ADDR),
             SafeRead(CC_P1_SEQUENCE_ADDR),
             SafeRead(CC_P2_SEQUENCE_ADDR),
             SafeRead(CC_P1_HEALTH_ADDR),
             SafeRead(CC_P2_HEALTH_ADDR),
             SafeRead(CC_ROUND_COUNT_ADDR),
             SafeRead(CC_P1_WINS_ADDR),
             SafeRead(CC_P2_WINS_ADDR));
}

} // namespace cccaster::game_memory
