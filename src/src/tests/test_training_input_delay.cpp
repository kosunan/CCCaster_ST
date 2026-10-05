#include "core_dll/engine/TrainingInputDelay.hpp"
#include <cstdio>
using cccaster::domain::session::TrainingInputDelay;
using Input = TrainingInputDelay::Input;
int main() {
    int failed = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { std::fprintf(stderr, "%s\n", message); ++failed; }
    };
    for (int delay = 0; delay <= 8; ++delay) {
        TrainingInputDelay queue;
        const Input p1{6, CC_BUTTON_A}, p2{4, CC_BUTTON_B};
        for (int frame = 0; frame < 30; ++frame) {
            const auto out = queue.Apply(frame == 0 ? p1 : Input{}, frame == 3 ? p2 : Input{}, delay, true);
            check(out[0] == (frame == delay ? p1 : Input{}), "P1 impulse must arrive exactly D frames later");
            check(out[1] == (frame == delay + 3 ? p2 : Input{}), "P2 impulse must arrive exactly D frames later");
        }
    }
    TrainingInputDelay queue;
    const Input attack{6, CC_BUTTON_C};
    queue.Apply(attack, {}, 8, true);
    check(queue.Apply({}, {}, 0, true)[0].IsNeutral(), "switching to D0 discards old buffered attack");
    for (int i = 0; i < 12; ++i)
        check(queue.Apply({}, {}, 8, true)[0].IsNeutral(), "D changes cannot resurrect old input");
    for (auto control : {CC_BUTTON_START, CC_BUTTON_FN1, CC_BUTTON_FN2}) {
        queue.Apply(attack, {}, 8, true);
        const Input command{0, static_cast<uint16_t>(control)};
        check(queue.Apply({}, command, 8, true)[1] == command, "training/menu controls must be immediate for P2");
        for (int i = 0; i < 10; ++i)
            check(queue.Apply({}, {}, 8, true)[0].IsNeutral(), "control boundary clears pending attack");
    }
    queue.Apply(attack, {}, 4, true);
    check(queue.Apply({4, 0}, {}, 4, false)[0] == Input{4, 0}, "menu and character selection navigation is immediate");
    for (int i = 0; i < 8; ++i)
        check(queue.Apply({}, {}, 4, true)[0].IsNeutral(), "mapping/pause/scene boundary clears history");
    queue.Apply(attack, {}, 4, true);
    queue.Clear();
    for (int i = 0; i < 8; ++i)
        check(queue.Apply({}, {}, 4, true)[0].IsNeutral(), "state restore clears buffered attack");
    return failed ? 1 : 0;
}
