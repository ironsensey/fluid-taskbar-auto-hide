"""Exercise production hot paths against counted Win32 stubs; no Windows runtime claim."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
visibility = (ROOT / 'src/visibility.cpp').read_text()
animation = (ROOT / 'src/animation.cpp').read_text()


def section(source, start, end):
    return source[source.index(start):source.index(end, source.index(start))]


def run(name, source):
    with tempfile.TemporaryDirectory(prefix='fluid-power-') as temp:
        cpp = Path(temp) / f'{name}.cpp'
        binary = Path(temp) / name
        cpp.write_text(source)
        subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-pthread', '-fsanitize=address,undefined', str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


cache_stubs = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cwchar>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using HMONITOR = int;
using HDC = void*;
using LPRECT = void*;
using LPARAM = std::intptr_t;
using BOOL = int;
constexpr BOOL TRUE = 1, FALSE = 0;
constexpr unsigned MONITORINFOF_PRIMARY = 1;
struct MONITORINFOEXW { unsigned cbSize; unsigned dwFlags; wchar_t szDevice[32]; };
std::vector<int> nativeMonitors{1, 2, 3, 4};
int primary = 1, enumerations = 0;
bool failEnumeration = false;
BOOL GetMonitorInfoW(HMONITOR monitor, MONITORINFOEXW* info) {
    info->dwFlags = monitor == primary ? MONITORINFOF_PRIMARY : 0;
    std::swprintf(info->szDevice, 32, L"DISPLAY%d", monitor);
    return TRUE;
}
BOOL EnumDisplayMonitors(void*, void*, BOOL (*callback)(HMONITOR, HDC, LPRECT, LPARAM), LPARAM data) {
    ++enumerations;
    for (int monitor : nativeMonitors) {
        if (!callback(monitor, nullptr, nullptr, data) || failEnumeration) return FALSE;
    }
    return TRUE;
}
'''
cache_tests = r'''
int main() {
    for (int i = 0; i < 100; ++i)
        for (int monitor : nativeMonitors) assert(GetMonitorSlot(monitor) == monitor - 1);
    assert(enumerations == 1); // Previously: 400 complete enumerations and sorts.
    primary = 2;
    InvalidateMonitorSlots();
    assert(GetMonitorSlot(2) == 0 && GetMonitorSlot(1) == 1);
    assert(enumerations == 2);
    nativeMonitors.pop_back();
    InvalidateMonitorSlots();
    assert(GetMonitorSlot(4) == -1);
    assert(enumerations == 3);
    nativeMonitors.push_back(4);
    InvalidateMonitorSlots();
    failEnumeration = true;
    assert(GetMonitorSlot(2) == -1);
    failEnumeration = false;
    assert(GetMonitorSlot(4) == 3); // A partial failed build must not be cached.
    assert(enumerations == 5);
    InvalidateMonitorSlots();
    auto query = [] { for (int i = 0; i < 100; ++i) assert(GetMonitorSlot(1) == 1); };
    std::thread first(query), second(query);
    first.join(); second.join();
    assert(enumerations == 6); // One rebuild even with concurrent readers.
}
'''
run('monitor_cache', cache_stubs + section(visibility, 'struct MonitorSlot', 'bool AdaptivePinningEnabled') + cache_tests)
print('Display cache: 400 lookups / 1 enumeration; hotplug, primary change, retry and concurrent access passed')

animation_stubs = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <vector>
#define WINAPI
using HWND = int;
using HMONITOR = int;
using DWORD = unsigned long;
struct RECT {};
struct MONITORINFOEXW { unsigned cbSize; wchar_t szDevice[32]; };
struct DEVMODEW { unsigned dmSize; int dmDisplayFrequency; };
constexpr int ENUM_CURRENT_SETTINGS = -1, TIMERR_NOERROR = 0;
struct {
    int frameRate = 0, showSpeedup = 140, hideSpeedup = 130;
    int nominalDuration = 250, easingMode = 5;
    bool highPrecisionTimer = true;
} g_settings;
double now = 0;
int beginCalls = 0, endCalls = 0, refreshQueries = 0, clockQueries = 0;
bool failTimer = false;
std::vector<DWORD> sleeps;
int timeBeginPeriod(int) { ++beginCalls; return failTimer ? 1 : 0; }
void timeEndPeriod(int) { ++endCalls; }
bool GetMonitorInfoW(HMONITOR, MONITORINFOEXW*) { ++refreshQueries; return true; }
bool EnumDisplaySettingsW(const wchar_t*, int, DEVMODEW* mode) { mode->dmDisplayFrequency = 144; return true; }
double TimerGetSeconds() { ++clockQueries; return now; }
DWORD GetTickCount() { return 12345; }
void Sleep(DWORD ms) { sleeps.push_back(ms); now += ms / 1000.0; }
'''
slide_struct = section(animation, 'struct SlideTiming', '// Keep this pure function')
slide_hooks = section(animation, 'using TrayUI_SlideWindow_t', 'bool HookTaskbarViewDllSymbols')
animation_tests = r'''
enum class Action { NoSleep, Frames, LongSleep, Late, Nested, Throw } action;
int depth = 0;
void FakeSlide(void*, HWND, const RECT*, HMONITOR, bool, bool animate) {
    if (!animate) { assert(!g_slide.active); return; }
    assert(g_slide.active);
    if (action == Action::NoSleep) return;
    if (action == Action::LongSleep) { Sleep_Hook(100); return; }
    if (action == Action::Late) { now += 0.2; Sleep_Hook(10); return; }
    Sleep_Hook(10);
    if (action == Action::Throw) throw std::runtime_error("animation failure");
    if (action == Action::Nested && depth == 0) {
        auto outer = g_slide;
        ++depth;
        TrayUI_SlideWindow_Hook(nullptr, 0, nullptr, 1, true, false);
        assert(g_slide.timerResolutionAcquired == outer.timerResolutionAcquired);
        TrayUI_SlideWindow_Hook(nullptr, 0, nullptr, 1, true, true);
        --depth;
        assert(g_slide.start == outer.start && g_slide.nextFrame == outer.nextFrame);
    }
    Sleep_Hook(10); Sleep_Hook(10);
}
void reset(Action next) {
    assert(!g_slide.active);
    now = 0; beginCalls = endCalls = refreshQueries = clockQueries = 0;
    failTimer = false; sleeps.clear(); action = next;
}
void slide(bool animate = true) { TrayUI_SlideWindow_Hook(nullptr, 0, nullptr, 1, true, animate); }
int main() {
    TrayUI_SlideWindow_Original = FakeSlide;
    GetTickCount_Original = GetTickCount;
    Sleep_Original = Sleep;
    reset(Action::NoSleep); slide(false);
    assert(!beginCalls && !endCalls && !refreshQueries && !clockQueries);
    reset(Action::NoSleep); slide();
    assert(!beginCalls && !endCalls);
    reset(Action::Frames); slide();
    assert(beginCalls == 1 && endCalls == 1 && sleeps.size() == 3);
    assert(!g_slide.active && GetTickCount_Hook() == 12345);
    reset(Action::LongSleep); slide();
    assert(!beginCalls && !endCalls && sleeps == std::vector<DWORD>{100});
    reset(Action::Late); slide();
    assert(!beginCalls && !endCalls && sleeps == std::vector<DWORD>{0});
    reset(Action::Frames); failTimer = true; slide();
    assert(beginCalls == 1 && endCalls == 0); // No repeated failed requests per frame.
    reset(Action::Nested); slide();
    assert(beginCalls == 2 && endCalls == 2 && !g_slide.active);
    reset(Action::Throw);
    try { slide(); assert(false); } catch (const std::runtime_error&) {}
    assert(beginCalls == 1 && endCalls == 1 && !g_slide.active);
    reset(Action::Frames); g_settings.highPrecisionTimer = false; slide();
    assert(!beginCalls && !endCalls);
}
'''
run('lazy_timer', animation_stubs + slide_struct + (ROOT / 'animation-math.inc').read_text() + slide_hooks + animation_tests)
print('Animation power paths: zero timer requests for instant/compositor-only/long-wait/late paths; scoped cleanup passed')

predicate_stubs = r'''
#include <algorithm>
#include <cassert>
#include <cwchar>
#include <iterator>
using HWND = int;
using HMONITOR = int;
constexpr int GWL_EXSTYLE = 1, WS_EX_NOACTIVATE = 1, WS_EX_TOOLWINDOW = 2;
constexpr int GCW_ATOM = 1, SW_SHOWMAXIMIZED = 3, MONITOR_DEFAULTTONEAREST = 1;
constexpr int DWMWA_EXTENDED_FRAME_BOUNDS = 1;
#define ARRAYSIZE(x) (sizeof(x)/sizeof((x)[0]))
#define FAILED(x) ((x) < 0)
struct RECT { long left, top, right, bottom; };
struct MONITORINFO { RECT rcMonitor; };
struct WINDOWPLACEMENT { unsigned length; int showCmd = 0; };
enum class Mode { maximized, intersected, never };
struct {
    Mode mode = Mode::maximized;
    bool snappedWindows = false, adaptiveEnabled = true;
    bool pinDisplays[4]{true, true, true, true};
} g_settings;
struct Window {
    bool visible = true, iconic = false, noActivate = false, tool = false;
    bool maximized = false, arranged = false, cloaked = false, excluded = false;
    bool sameMonitor = true, desktop = false, menu = false;
    const wchar_t* name = L"App";
    RECT rect{100, 100, 800, 600};
} window;
HWND g_altTabViewHwnd = 0;
int exclusions = 0, cloakQueries = 0;
bool IsWindowVisible(HWND) { return window.visible; }
bool IsIconic(HWND) { return window.iconic; }
long GetWindowLong(HWND, int) { return window.noActivate ? WS_EX_NOACTIVATE : 0; }
long GetWindowLongPtrW(HWND, int) { return window.tool ? WS_EX_TOOLWINDOW : 0; }
int GetClassNameW(HWND, wchar_t* name, unsigned size) { std::wcsncpy(name, window.name, size); return 1; }
HWND GetShellWindow() { return 999; }
void* GetProp(HWND, const wchar_t*) { return window.desktop ? &window : nullptr; }
int GetClassWord(HWND, int) { return window.menu ? 32768 : 0; }
bool IsWindowCloaked(HWND) { ++cloakQueries; return window.cloaked; }
bool IsWindowExcluded(HWND) { ++exclusions; return window.excluded; }
bool GetWindowPlacement(HWND, WINDOWPLACEMENT* wp) { wp->showCmd = window.maximized ? SW_SHOWMAXIMIZED : 1; return true; }
HMONITOR MonitorFromWindow(HWND, int) { return window.sameMonitor ? 1 : 2; }
bool IsWindowArranged(HWND) { return window.arranged; }
auto pIsWindowArranged = IsWindowArranged;
int DwmGetWindowAttribute(HWND, int, RECT* rect, unsigned) { *rect = window.rect; return 0; }
bool GetWindowRect(HWND, RECT* rect) { *rect = window.rect; return true; }
bool IntersectRect(RECT* result, const RECT* a, const RECT* b) {
    *result = {std::max(a->left, b->left), std::max(a->top, b->top),
               std::min(a->right, b->right), std::min(a->bottom, b->bottom)};
    return result->left < result->right && result->top < result->bottom;
}
'''
predicate_tests = r'''
int main() {
    MONITORINFO monitor{{0, 0, 1920, 1080}};
    RECT taskbar{0, 1040, 1920, 1080};
    for (Mode mode : {Mode::maximized, Mode::intersected, Mode::never}) {
        g_settings.mode = mode;
        for (bool snapped : {false, true}) {
            g_settings.snappedWindows = snapped;
            for (RECT rect : {RECT{100, 100, 800, 600}, RECT{0, 0, 1920, 1080},
                              RECT{-5, -5, 1925, 1085}, RECT{0, 100, 1000, 1060}}) {
                for (unsigned mask = 0; mask < 4096; ++mask) {
                    window = {};
                    window.rect = rect;
                    window.visible = mask & 1; window.iconic = mask & 2;
                    window.noActivate = mask & 4; window.tool = mask & 8;
                    window.maximized = mask & 16; window.arranged = mask & 32;
                    window.cloaked = mask & 64; window.excluded = mask & 128;
                    window.sameMonitor = mask & 256; window.desktop = mask & 512;
                    g_altTabViewHwnd = (mask & 1024) ? 1 : 0;
                    window.menu = mask & 2048;
                    bool baseline = BaselineCanHideTaskbarForWindow(1, 1, &monitor, &taskbar);
                    assert(CanHideTaskbarForWindow(1, 1, &monitor, &taskbar) == baseline);
                }
            }
        }
    }
    window = {}; g_altTabViewHwnd = 0; g_settings.mode = Mode::maximized;
    exclusions = cloakQueries = 0;
    assert(!CanHideTaskbarForWindow(1, 1, &monitor, &taskbar));
    assert(exclusions == 0 && cloakQueries == 0);
    assert(!BaselineCanHideTaskbarForWindow(1, 1, &monitor, &taskbar));
    assert(exclusions == 1 && cloakQueries == 1);
    window.maximized = true;
    exclusions = cloakQueries = 0;
    assert(CanHideTaskbarForWindow(1, 1, &monitor, &taskbar));
    assert(exclusions == 1 && cloakQueries == 1);
    for (unsigned mask = 0; mask < 16; ++mask) {
        for (int i = 0; i < 4; ++i) g_settings.pinDisplays[i] = (mask >> i) & 1;
        for (bool enabled : {false, true}) {
            g_settings.adaptiveEnabled = enabled;
            for (Mode mode : {Mode::maximized, Mode::intersected, Mode::never}) {
                g_settings.mode = mode;
                assert(NeedsWindowEvents() == (enabled && mask != 0 && mode != Mode::never));
            }
        }
    }
}
'''
run('visibility_equivalence', predicate_stubs +
    section((ROOT / 'visibility-math.inc').read_text(), 'static bool CoversMonitor', 'static bool UseAdaptivePin') +
    (ROOT / 'tests/fixtures/visibility_v2_0.cpp').read_text() +
    section(visibility, 'bool CanHideTaskbarForWindow', 'struct MonitorSlot') +
    section(visibility, 'bool AdaptivePinningEnabled', 'bool ShouldKeepTaskbarShown') + predicate_tests)
print('98,304 visibility cases match v2.0; irrelevant-window queries eliminated; event-monitor gating passed')
