"""Compile real click recognition, properties, queue and input observation against Win32 stubs."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/click-lock.cpp').read_text()
start = source.index('constexpr wchar_t kPinProperty')
end = source.index('bool HitEmpty(')
production = source[start:end]
stubs = r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cwchar>
#include <mutex>
#include <string>
#include <unordered_map>
#define CALLBACK
#define WH_MOD_ID L"test"
#define ARRAYSIZE(x) (sizeof(x)/sizeof((x)[0]))
using HWND = std::uintptr_t;
using HANDLE = void*;
using HHOOK = void*;
using DWORD = std::uint32_t;
using UINT = unsigned;
using WPARAM = std::uintptr_t;
using LPARAM = std::intptr_t;
using LRESULT = std::intptr_t;
struct POINT { long x, y; };
struct MSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; };
struct POINTER_INFO { unsigned pointerType, pointerFlags; POINT ptPixelLocation; };
constexpr int HC_ACTION = 0, PM_REMOVE = 1, GA_ROOT = 2;
constexpr UINT WM_LBUTTONDOWN = 0x201, WM_LBUTTONDBLCLK = 0x203;
constexpr UINT WM_NCLBUTTONDOWN = 0xa1, WM_NCLBUTTONDBLCLK = 0xa3, WM_POINTERDOWN = 0x246;
constexpr unsigned PT_MOUSE = 4, PT_TOUCH = 2, POINTER_FLAG_FIRSTBUTTON = 0x10;
#define GET_POINTERID_WPARAM(x) (static_cast<unsigned>(x) & 0xffff)
std::unordered_map<HWND, HANDLE> pins;
int notifications = 0, nextCalls = 0;
POINTER_INFO info{PT_MOUSE, POINTER_FLAG_FIRSTBUTTON, {20, 20}};
UINT RegisterWindowMessageW(const wchar_t*) { return 0xc010; }
HANDLE GetPropW(HWND taskbar, const wchar_t*) { return pins.contains(taskbar) ? pins[taskbar] : nullptr; }
HANDLE RemovePropW(HWND taskbar, const wchar_t*) {
    HANDLE old = GetPropW(taskbar, L""); pins.erase(taskbar); return old;
}
bool SetPropW(HWND taskbar, const wchar_t*, HANDLE value) { pins[taskbar] = value; return true; }
DWORD GetCurrentProcessId() { return 42; }
DWORD GetWindowThreadProcessId(HWND, DWORD* process) { if (process) *process = 42; return 1; }
int GetClassNameW(HWND window, wchar_t* buffer, unsigned size) {
    const wchar_t* name = window == 1 ? L"Shell_TrayWnd" :
        window == 2 ? L"Shell_SecondaryTrayWnd" :
        (window == 12 || window == 21) ? L"Windows.UI.Input.InputSite.WindowClass" : L"Button";
    std::wcsncpy(buffer, name, size); return 1;
}
HWND GetParent(HWND window) { return window == 21 ? 2 : window >= 10 ? 1 : 0; }
HWND GetAncestor(HWND window, int) { return window == 21 ? 2 : window >= 10 ? 1 : window; }
bool GetPointerInfo(unsigned, POINTER_INFO* result) { *result = info; return true; }
bool SetEvent(HANDLE) { ++notifications; return true; }
LRESULT CallNextHookEx(HHOOK, int, WPARAM, LPARAM) { ++nextCalls; return 777; }
'''
tests = r'''
Click click(HWND window, DWORD time, long x = 10, long y = 10) {
    return {window, {x, y}, time};
}
int main() {
    assert(IsEmptyClass(L"Shell_TrayWnd"));
    assert(IsEmptyClass(L"Shell_SecondaryTrayWnd"));
    assert(IsEmptyClass(L"Taskbar.TaskbarFrameAutomationPeer"));
    assert(IsEmptyClass(L"Windows.UI.Input.InputSite.WindowClass"));
    for (const wchar_t* button : {L"Button", L"Taskbar.TaskListButtonAutomationPeer",
                                  L"SystemTray.NormalButton", L"TrayClockWClass", L"", static_cast<const wchar_t*>(nullptr)})
        assert(!IsEmptyClass(button));
    Toggle(1); assert(IsPinned(1) && !IsPinned(2));
    Toggle(2); assert(IsPinned(1) && IsPinned(2));
    Toggle(1); assert(!IsPinned(1) && IsPinned(2));
    RemovePin(2); assert(!IsPinned(2));
    DoubleClickState state;
    assert(!state.Accept(click(1, 100), true, 500, 4, 4));
    assert(state.Accept(click(1, 200), true, 500, 4, 4));
    assert(!state.Accept(click(1, 300), true, 500, 4, 4));
    assert(state.Accept(click(1, 400), true, 500, 4, 4));
    state = {};
    assert(!state.Accept(click(1, 100), true, 500, 4, 4));
    assert(!state.Accept(click(2, 200), true, 500, 4, 4)); // Different displays.
    assert(!state.Accept(click(2, 300), false, 500, 4, 4)); // An icon resets the pair.
    assert(!state.Accept(click(2, 400), true, 500, 4, 4));
    assert(!state.Accept(click(2, 1000), true, 500, 4, 4)); // Too late.
    assert(!state.Accept(click(2, 1100, 13), true, 500, 4, 4)); // Too far.
    assert(state.Accept(click(2, 1200, 15), true, 500, 4, 4)); // Rectangle edge.
    state = {};
    assert(!state.Accept(click(1, 0xfffffff0u, -1920, -100), true, 500, 4, 4));
    assert(state.Accept(click(1, 50, -1920, -100), true, 500, 4, 4)); // Tick wrap.
    state = {};
    assert(!state.Accept(click(1, 100), true, 500, 4, 4));
    Click reset = click(1, 200); reset.reset = true;
    assert(!state.Accept(reset, true, 500, 4, 4));
    ClickQueue queue;
    for (unsigned i = 0; i < 10000; ++i) {
        queue.Push(click(1, i)); assert(queue.count <= 32);
    }
    Click output;
    assert(queue.Pop(output) && output.reset); // Overflow never pairs across a gap.
    while (queue.Pop(output)) {}
    assert(!queue.Pop(output));

    g_stopping = false;
    auto observe = [](HWND target, UINT message, WPARAM remove = PM_REMOVE) {
        MSG msg{target, message, 1, 0, 100, {10, 10}};
        return GetMessageHook(HC_ACTION, remove, reinterpret_cast<LPARAM>(&msg));
    };
    assert(observe(1, WM_LBUTTONDOWN, 0) == 777 && g_queue.count == 0);
    assert(observe(3, WM_LBUTTONDOWN) == 777 && g_queue.count == 0);
    assert(observe(1, WM_LBUTTONDOWN) == 777 && g_queue.count == 1);
    assert(observe(1, WM_LBUTTONDBLCLK) == 777 && g_queue.count == 2);
    assert(observe(12, WM_POINTERDOWN) == 777 && g_queue.count == 3);
    assert(observe(12, WM_LBUTTONDOWN) == 777 && g_queue.count == 3); // Promotion ignored.
    assert(observe(21, WM_POINTERDOWN) == 777 && g_queue.count == 4);
    assert(g_queue.Pop(output) && output.taskbar == 1);
    assert(g_queue.Pop(output) && output.taskbar == 1);
    assert(g_queue.Pop(output) && output.taskbar == 1 && output.point.x == 20);
    assert(g_queue.Pop(output) && output.taskbar == 2);
    info.pointerType = PT_TOUCH;
    assert(observe(12, WM_POINTERDOWN) == 777 && g_queue.count == 0);
    info.pointerType = PT_MOUSE; info.pointerFlags = 0;
    assert(observe(12, WM_POINTERDOWN) == 777 && g_queue.count == 0);
    g_stopping = true;
    assert(observe(1, WM_LBUTTONDOWN) == 777 && g_queue.count == 0);
    assert(notifications == 4 && nextCalls == 10);
}
'''
with tempfile.TemporaryDirectory(prefix='fluid-click-lock-') as temp:
    cpp = Path(temp) / 'click.cpp'
    binary = Path(temp) / 'click'
    cpp.write_text(stubs + production + tests)
    subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic',
                    '-fsanitize=address,undefined', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Silent click lock: empty peers, timing/distance, display isolation, wraparound, bounded queue and mouse/pointer routing passed (stub host)')
