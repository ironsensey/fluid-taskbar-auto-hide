#include <cassert>
#include <cmath>
#include <iostream>
#include "../animation-math.inc"
#include "../visibility-math.inc"

int main() {
    unsigned samples = 0;
    for (int mode = 0; mode <= 4; ++mode) {
        for (bool show : {false, true}) {
            for (double duration : {50.0, 250.0, 2000.0}) {
                double previous = 0;
                assert(AnimationTime(-1, duration, mode, show) == 0);
                for (int i = 0; i <= 20000; ++i) {
                    double time = duration * i / 10000.0;
                    double value = AnimationTime(time, duration, mode, show);
                    assert(std::isfinite(value));
                    assert(value + 1e-9 >= previous);
                    assert(value >= 0);
                    if (time < duration) assert(value <= duration);
                    if (time >= duration) assert(value == time);
                    if (mode == 0) assert(value == time);
                    previous = value;
                    ++samples;
                }
                assert(AnimationTime(0, duration, mode, show) == 0);
                assert(AnimationTime(duration, duration, mode, show) == duration);
            }
        }
    }
    assert(AnimationTime(125, 250, 2, true) > 125);
    assert(AnimationTime(125, 250, 2, false) < 125);
    // Negative screen coordinates, exact fullscreen, overscan, and small windows.
    assert(CoversMonitor(-1920, 0, 0, 1080, -1920, 0, 0, 1080));
    assert(CoversMonitor(-1922, -2, 2, 1082, -1920, 0, 0, 1080));
    assert(!CoversMonitor(-1920, 0, -1, 1080, -1920, 0, 0, 1080));
    assert(!CoversMonitor(20, 20, 800, 600, 0, 0, 1920, 1080));
    for (unsigned mask = 0; mask < 16; ++mask) {
        bool pins[4];
        for (int slot = 0; slot < 4; ++slot) pins[slot] = (mask >> slot) & 1;
        for (int slot = -1; slot <= 5; ++slot) {
            assert(!UseAdaptivePin(false, slot, pins));
            bool expected = slot >= 0 && slot < 4 && ((mask >> slot) & 1);
            assert(UseAdaptivePin(true, slot, pins) == expected);
        }
    }
    std::cout << "Passed " << samples << " animation samples and geometry/display-policy checks\n";
}
