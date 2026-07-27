// ============================================================================
// stub_wasapi_clock.cpp — WasapiClock の置換スタブ
//
// NetplayClock.cpp が要求する唯一の外部シンボルが WasapiClock::GetTimeUs()。
// 本物は WASAPI (COM) を初期化するため、テストでは常に 0 を返す実装に差し替える。
// NetplayClock の計算は全て引数で渡された時刻で行われるため影響しない。
// ============================================================================

#include "core_dll/timing/WasapiClock.hpp"

namespace cccaster {
namespace core {
namespace timer {

int64_t WasapiClock::GetTimeUs() {
    return 0;
}

} // namespace timer
} // namespace core
} // namespace cccaster
