"""Test production native-animation bookkeeping against reference-counted WinRT stubs."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/animation.cpp').read_text()
a = source.index('struct AnimationRecord')
b = source.index('using SharedAnimations_TaskbarCollapseAnimation_t', a)
bookkeeping = source[a:b]
a = source.index('void Wh_ModUninit()')
b = source.index('BOOL Wh_ModSettingsChanged', a)
unload = source[a:b]
stubs = r'''
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>
namespace winrt {
namespace Windows::Foundation {
using TimeSpan = std::chrono::duration<long long, std::ratio<1, 10000000>>;
}
struct AnimationState {
    Windows::Foundation::TimeSpan duration, delay;
    int durationWrites = 0, delayWrites = 0;
    bool unavailable = false;
};
namespace Windows::UI::Composition {
struct KeyFrameAnimation {
    std::shared_ptr<AnimationState> state;
    explicit operator bool() const { return !!state; }
    Foundation::TimeSpan Duration() const {
        if (state->unavailable) throw std::runtime_error("disconnected");
        return state->duration;
    }
    void Duration(Foundation::TimeSpan value) const {
        if (state->unavailable) throw std::runtime_error("disconnected");
        state->duration = value; ++state->durationWrites;
    }
    Foundation::TimeSpan DelayTime() const {
        if (state->unavailable) throw std::runtime_error("disconnected");
        return state->delay;
    }
    void DelayTime(Foundation::TimeSpan value) const {
        if (state->unavailable) throw std::runtime_error("disconnected");
        state->delay = value; ++state->delayWrites;
    }
};
}
void* get_abi(const Windows::UI::Composition::KeyFrameAnimation& animation) {
    return animation.state.get();
}
}
struct { int showSpeedup = 140, hideSpeedup = 130; bool removeDelay = true; } g_settings;
void Wh_Log(const wchar_t*) {}
'''
tests = r'''
using Animation = winrt::Windows::UI::Composition::KeyFrameAnimation;
using Span = winrt::Windows::Foundation::TimeSpan;
Animation makeAnimation() {
    return {std::make_shared<winrt::AnimationState>(winrt::AnimationState{Span{2800000}, Span{300000}})};
}
int main() {
    // Reusing an animation must not repeatedly reset its composition properties.
    auto repeated = makeAnimation();
    for (int i = 0; i < 20000; ++i) CustomizeCompositionAnimation(repeated, true);
    assert(g_animations.size() == 1);
    assert(repeated.Duration() == Span{2000000});
    assert(repeated.state->durationWrites == 1 && repeated.state->delayWrites == 1);
    CustomizeCompositionAnimation(repeated, false);
    assert(repeated.Duration() == Span{2800000} * 100 / 130);
    CustomizeCompositionAnimation(repeated, true);
    assert(repeated.Duration() == Span{2000000}); // Original, not compounded.
    Wh_ModUninit();
    assert(repeated.Duration() == Span{2800000} && repeated.DelayTime() == Span{300000});

    // Simulate thousands of freshly returned native animations during a session.
    std::vector<Animation> animations;
    std::vector<std::weak_ptr<winrt::AnimationState>> references;
    for (int i = 0; i < 5000; ++i) {
        animations.push_back(makeAnimation());
        references.push_back(animations.back().state);
        CustomizeCompositionAnimation(animations.back(), true);
        assert(g_animations.size() <= kMaxTrackedAnimations);
    }
    for (std::size_t i = 0; i < animations.size() - kMaxTrackedAnimations; ++i) {
        assert(animations[i].Duration() == Span{2800000});
        assert(animations[i].DelayTime() == Span{300000});
    }
    // Revisit an evicted live object: restoration prevents compounded speedup.
    CustomizeCompositionAnimation(animations.front(), true);
    assert(animations.front().Duration() == Span{2000000});
    animations.clear();
    std::size_t retained = 0;
    for (const auto& weak : references) if (!weak.expired()) ++retained;
    assert(retained == kMaxTrackedAnimations);
    assert(g_animations.capacity() <= kMaxTrackedAnimations);
    Wh_ModUninit();
    for (const auto& weak : references) assert(weak.expired());

    auto first = makeAnimation(), second = makeAnimation();
    CustomizeCompositionAnimation(first, true);
    CustomizeCompositionAnimation(second, true);
    first.state->unavailable = true;
    Wh_ModUninit(); // A disconnected object must not skip cleanup of others.
    assert(second.Duration() == Span{2800000} && second.DelayTime() == Span{300000});
    assert(g_animations.empty());
}
'''
with tempfile.TemporaryDirectory(prefix='fluid-animation-cache-') as temp:
    cpp = Path(temp) / 'cache.cpp'
    binary = Path(temp) / 'cache'
    cpp.write_text(stubs + bookkeeping + unload + tests)
    subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic',
                    '-fsanitize=address,undefined', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Native bookkeeping: 20,000 reuses / 5,000 new objects; cache bounded, eviction restored, references released (stub host)')
