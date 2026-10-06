#include "core_dll/timing/DisplayCadence.hpp"
#include "core_dll/timing/FrameCadence.hpp"
#include "core_dll/timing/IdlePresentation.hpp"
#include <cstdio>
#include <cstdlib>

void check(bool value) { if (!value) std::abort(); }
static unsigned calls = 0;
static int64_t available = 0;
void present(int64_t remaining) { ++calls; available = remaining; }
int main() {
    using namespace cccaster::core::timer;
    for (auto hz : {60u, 120u, 144u, 165u, 240u, 360u, 500u}) {
        DisplayCadence display;
        FrameCadence game; game.ResetTicks(7000000);
        check(display.Configure(hz, 1, 7000000));
        for (unsigned i = 0; i < hz * 600; ++i) {
            const auto due = display.Next();
            check(!display.Due(due - 1) && display.Due(due));
            display.Presented(due);
            check(!display.Due(due));
        }
        for (unsigned i = 0; i < 36000; ++i) game.AdvanceCorrected(0);
        check(display.Next() == game.NextTicks()); // 表示Hzによらず600秒で36000ゲーム更新
        check(display.Missed() == 0);
    }
    DisplayCadence fractional;
    check(fractional.Configure(60000, 1001, 0));
    for (int i = 0; i < 60000; ++i) fractional.Presented(fractional.Next());
    check(fractional.Next() == 60000000ll * 1001); // 59.94を60へ丸めない
    DisplayCadence late;
    check(late.Configure(240, 1, 0));
    late.Presented(1500000);
    check(late.Missed() == 6 && late.Next() == 1750000);
    check(!late.Due(1500000)); // 遅延6枠を一度に連打しない
    check(late.Configure(144000, 1001, 123)); // モニター変更は新しい原点
    check(late.Next() == 123 && late.Missed() == 0);
    check(!late.Configure(0, 1, 0) && !late.Configure(60, 0, 0));
    check(!late.Configure(1, 1, 0) && !late.Configure(1001, 1, 0));
    IdlePresentation::Pump(10000); check(calls == 0);
    {
        IdlePresentation::Scope scope(present);
        IdlePresentation::Pump(1000); check(calls == 0); // 更新直前は表示しない
        IdlePresentation::Pump(1001); check(calls == 1 && available == 1001);
        { IdlePresentation::Scope replay(nullptr); IdlePresentation::Pump(10000); }
        check(calls == 1);
        IdlePresentation::Pump(10000); check(calls == 2);
    }
    IdlePresentation::Pump(10000); check(calls == 2);
    {
        IdlePresentation::Scope scope(present);
        for (int64_t guard : {200, 1000, 2000, 3000}) {
            const auto before = calls;
            for (int64_t remaining : {-1, 0, 199, 200, 999, 1000})
                IdlePresentation::Pump(guard + remaining, guard);
            check(calls == before); // スピン中と、スピン直前の最低1msには提示しない。
            IdlePresentation::Pump(guard + 1001, guard);
            check(calls == before + 1 && available == 1001); // ドライバ余裕もスピン開始を基準にする。
        }
    }
    std::puts("display cadence: passed");
}
