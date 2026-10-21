#include <cassert>
#include <algorithm>
#include <cmath>
#include <iostream>
#include "../animation-math.inc"
#include "../visibility-math.inc"

int main() {
    unsigned samples = 0;
    for (int mode = 0; mode <= 5; ++mode) {
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
    // Smootherstep has gentler endpoint velocity and acceleration than cubic.
    assert(AnimationTime(1, 250, 5, true) < AnimationTime(1, 250, 4, true));
    assert(250 - AnimationTime(249, 250, 5, true) <
           250 - AnimationTime(249, 250, 4, true));
    // Simulate frame scheduling with oversleep and variable render cost.
    for (double fps : {30.0, 60.0, 90.0, 144.0, 240.0, 360.0}) {
        double period = 1.0 / fps;
        double now = 0;
        double deadline = period;
        for (int frame = 0; frame < 1000; ++frame) {
            now += (frame % 3) * 0.0001; // work before Sleep
            unsigned sleep = FrameSleepMilliseconds(now, deadline);
            assert(sleep <= 34);
            now += sleep / 1000.0 + 0.0002; // scheduler oversleep
            assert(now >= deadline - 1e-9);
            assert(now - deadline < 0.0013);
            deadline = AdvanceFrameDeadline(deadline, now, period);
        }
        // Absolute deadlines stay on the cadence instead of adding overhead.
        assert(std::abs(deadline - period * 1001) < 1e-8);
        assert(FrameSleepMilliseconds(deadline + 0.1, deadline) == 0);
        double resumed = AdvanceFrameDeadline(deadline, deadline + 0.1, period);
        assert(std::abs(resumed - (deadline + 0.1 + period)) < 1e-9);
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
