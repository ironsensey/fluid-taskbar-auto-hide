// Silent taskbar reveal lock. Mouse messages are observed only on taskbar UI
// threads; accessibility hit testing runs on a separate, sleeping MTA worker.
#include <windhawk_utils.h>
#include <winrt/base.h>
#include <UIAutomationClient.h>
#include <windowsx.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cwchar>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

constexpr wchar_t kPinProperty[] = L"Windhawk_FluidRevealLock_" WH_MOD_ID;
const UINT kToggleMessage = RegisterWindowMessageW(L"Windhawk_FluidRevealToggle_" WH_MOD_ID);

bool IsPinned(HWND taskbar) {
    return GetPropW(taskbar, kPinProperty) != nullptr;
}

void Toggle(HWND taskbar) {
    if (IsPinned(taskbar)) RemovePropW(taskbar, kPinProperty);
    else SetPropW(taskbar, kPinProperty, reinterpret_cast<HANDLE>(1));
}

void RemovePin(HWND taskbar) {
    RemovePropW(taskbar, kPinProperty);
}

bool IsTaskbar(HWND window) {
    wchar_t name[64]{};
    DWORD process = 0;
    return window && GetWindowThreadProcessId(window, &process) &&
        process == GetCurrentProcessId() &&
        GetClassNameW(window, name, ARRAYSIZE(name)) &&
        (wcscmp(name, L"Shell_TrayWnd") == 0 ||
         wcscmp(name, L"Shell_SecondaryTrayWnd") == 0);
}

bool IsEmptyClass(const wchar_t* name) {
    // Same empty-space peers used by Windhawk's taskbar-empty-space-clicks mod.
    return name && (wcscmp(name, L"Shell_TrayWnd") == 0 ||
        wcscmp(name, L"Shell_SecondaryTrayWnd") == 0 ||
        wcscmp(name, L"Taskbar.TaskbarFrameAutomationPeer") == 0 ||
        wcscmp(name, L"Windows.UI.Input.InputSite.WindowClass") == 0);
}

struct Click {
    HWND taskbar{};
    POINT point{};
    DWORD time{};
    bool reset = false;
};

struct DoubleClickState {
    Click previous{};
    bool armed = false;

    bool Accept(const Click& click, bool empty, DWORD interval, int width, int height) {
        if (click.reset) armed = false;
        if (!empty) { armed = false; return false; }
        const auto dx = static_cast<std::int64_t>(click.point.x) - previous.point.x;
        const auto dy = static_cast<std::int64_t>(click.point.y) - previous.point.y;
        if (armed && click.taskbar == previous.taskbar &&
            static_cast<DWORD>(click.time - previous.time) <= interval &&
            dx * 2 >= -static_cast<std::int64_t>(width) && dx * 2 <= width &&
            dy * 2 >= -static_cast<std::int64_t>(height) && dy * 2 <= height) {
            armed = false; // Consume the pair; a triple click toggles only once.
            return true;
        }
        previous = click;
        armed = true;
        return false;
    }
};

struct ClickQueue {
    std::array<Click, 32> clicks{};
    std::size_t first = 0, count = 0;

    void Push(Click click) {
        if (count == clicks.size()) {
            // Never grow an input backlog, or combine clicks across a dropped gap.
            first = count = 0;
            click.reset = true;
        }
        clicks[(first + count) % clicks.size()] = click;
        ++count;
    }
    bool Pop(Click& click) {
        if (!count) return false;
        click = clicks[first];
        first = (first + 1) % clicks.size();
        --count;
        return true;
    }
};

std::atomic<bool> g_stopping{true};
std::mutex g_queueMutex;
ClickQueue g_queue;
HANDLE g_clickEvent = nullptr, g_stopEvent = nullptr, g_worker = nullptr;

bool HasInputSiteAncestor(HWND window, HWND root) {
    for (; window && window != root; window = GetParent(window)) {
        wchar_t name[64]{};
        if (GetClassNameW(window, name, ARRAYSIZE(name)) &&
            wcscmp(name, L"Windows.UI.Input.InputSite.WindowClass") == 0) return true;
    }
    return false;
}

LRESULT CALLBACK GetMessageHook(int code, WPARAM remove, LPARAM param) {
    if (code == HC_ACTION && remove == PM_REMOVE && !g_stopping.load()) {
        const auto& message = *reinterpret_cast<const MSG*>(param);
        const bool pointer = message.message == WM_POINTERDOWN;
        const bool mouse = message.message == WM_LBUTTONDOWN ||
            message.message == WM_LBUTTONDBLCLK ||
            message.message == WM_NCLBUTTONDOWN || message.message == WM_NCLBUTTONDBLCLK;
        if (pointer || mouse) {
            HWND root = GetAncestor(message.hwnd, GA_ROOT);
            if (IsTaskbar(root)) {
                // XAML's input site receives pointer messages and can promote
                // them to mouse messages. Observe just its pointer route.
                const bool inputSite = HasInputSiteAncestor(message.hwnd, root);
                POINT point = message.pt;
                bool accept = mouse && !inputSite;
                if (pointer && inputSite) {
                    POINTER_INFO info{};
                    accept = GetPointerInfo(GET_POINTERID_WPARAM(message.wParam), &info) &&
                        info.pointerType == PT_MOUSE &&
                        (info.pointerFlags & POINTER_FLAG_FIRSTBUTTON);
                    if (accept) point = info.ptPixelLocation;
                }
                if (accept) {
                    std::lock_guard<std::mutex> guard(g_queueMutex);
                    // Stop may have begun after the initial atomic check.
                    if (!g_stopping.load()) {
                        g_queue.Push({root, point, message.time});
                        SetEvent(g_clickEvent);
                    }
                }
            }
        }
    }
    return CallNextHookEx(nullptr, code, remove, param);
}

bool HitEmpty(IUIAutomation* automation, const Click& click) {
    if (!IsTaskbar(click.taskbar)) return false;
    const HWND hit = WindowFromPhysicalPoint(click.point);
    if (!hit || GetAncestor(hit, GA_ROOT) != click.taskbar) return false;
    winrt::com_ptr<IUIAutomationElement> element;
    if (FAILED(automation->ElementFromPoint(click.point, element.put())) || !element)
        return false;
    BSTR className = nullptr;
    const HRESULT result = element->get_CurrentClassName(&className);
    const bool empty = SUCCEEDED(result) && IsEmptyClass(className);
    SysFreeString(className);
    return empty;
}

DWORD WINAPI Worker(void*) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) { g_stopping = true; return 0; }
    {
        winrt::com_ptr<IUIAutomation> automation;
        const HRESULT result = CoCreateInstance(__uuidof(CUIAutomation), nullptr,
            CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), automation.put_void());
        if (FAILED(result) || !automation) {
            Wh_Log(L"Reveal-lock accessibility initialization failed");
            g_stopping = true;
        } else {
            // A provider timeout must not block Explorer's UI thread. Queries
            // stay on this worker, never inside the input hook or taskbar WndProc.
            if (auto options = automation.try_as<IUIAutomation2>()) {
                options->put_ConnectionTimeout(200);
                options->put_TransactionTimeout(200);
            }
            DoubleClickState state;
            HANDLE events[] = {g_stopEvent, g_clickEvent};
            while (!g_stopping.load() &&
                   WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
                Click click;
                while (!g_stopping.load()) {
                    {
                        std::lock_guard<std::mutex> guard(g_queueMutex);
                        if (!g_queue.Pop(click)) break;
                    }
                    const DWORD interval = GetDoubleClickTime();
                    const DWORD age = GetTickCount() - click.time;
                    if (age > std::max<DWORD>(interval * 2, 1000)) {
                        state.armed = false;
                        continue;
                    }
                    if (state.Accept(click, HitEmpty(automation.get(), click), interval,
                                     GetSystemMetrics(SM_CXDOUBLECLK),
                                     GetSystemMetrics(SM_CYDOUBLECLK))) {
                        if (!g_stopping.load() && IsTaskbar(click.taskbar))
                            PostMessageW(click.taskbar, kToggleMessage, 0, 0);
                    }
                }
            }
        }
    }
    g_stopping = true;
    CoUninitialize();
    return 0;
}

struct ThreadHook {
    HHOOK hook{};
    std::unordered_set<HWND> taskbars;
};
std::mutex g_hooksMutex;
std::unordered_map<DWORD, ThreadHook> g_hooks;

void TrackTaskbar(HWND taskbar) {
    if (g_stopping.load() || !IsTaskbar(taskbar)) return;
    const DWORD thread = GetWindowThreadProcessId(taskbar, nullptr);
    std::lock_guard<std::mutex> guard(g_hooksMutex);
    auto it = g_hooks.find(thread);
    if (it == g_hooks.end()) {
        HHOOK hook = SetWindowsHookExW(WH_GETMESSAGE, GetMessageHook, nullptr, thread);
        if (!hook) { Wh_Log(L"Reveal-lock input hook failed"); return; }
        it = g_hooks.emplace(thread, ThreadHook{hook, {}}).first;
    }
    it->second.taskbars.insert(taskbar);
}

void ForgetTaskbar(HWND taskbar) {
    RemovePin(taskbar);
    std::lock_guard<std::mutex> guard(g_hooksMutex);
    for (auto it = g_hooks.begin(); it != g_hooks.end(); ++it) {
        if (it->second.taskbars.erase(taskbar) && it->second.taskbars.empty()) {
            UnhookWindowsHookEx(it->second.hook);
            g_hooks.erase(it);
            break;
        }
    }
}

bool Start() {
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_clickEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_stopEvent || !g_clickEvent) {
        if (g_stopEvent) CloseHandle(g_stopEvent);
        if (g_clickEvent) CloseHandle(g_clickEvent);
        g_stopEvent = g_clickEvent = nullptr;
        return false;
    }
    g_stopping = false;
    g_worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    if (!g_worker) {
        g_stopping = true;
        CloseHandle(g_stopEvent); CloseHandle(g_clickEvent);
        g_stopEvent = g_clickEvent = nullptr;
        return false;
    }
    return true;
}

void Stop() {
    g_stopping = true;
    {
        std::lock_guard<std::mutex> guard(g_hooksMutex);
        for (const auto& [thread, item] : g_hooks) {
            UnhookWindowsHookEx(item.hook);
            for (HWND taskbar : item.taskbars) RemovePin(taskbar);
        }
        g_hooks.clear();
    }
    // Synchronize with an input callback that passed the first stopping check.
    { std::lock_guard<std::mutex> guard(g_queueMutex); }
    if (g_stopEvent) SetEvent(g_stopEvent);
    if (g_worker) { WaitForSingleObject(g_worker, INFINITE); CloseHandle(g_worker); }
    if (g_clickEvent) CloseHandle(g_clickEvent);
    if (g_stopEvent) CloseHandle(g_stopEvent);
    g_worker = g_clickEvent = g_stopEvent = nullptr;
    std::lock_guard<std::mutex> guard(g_queueMutex);
    g_queue = {};
}
