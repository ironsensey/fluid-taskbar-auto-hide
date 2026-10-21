// ==WindhawkMod==
// @id              fluid-taskbar-animation
// @name            Fluid taskbar animation
// @description     Customize the taskbar auto-hide animation speed and frame rate to make it feel less sluggish and janky
// @version         1.6.0
// @author          m417z
// @github          https://github.com/m417z
// @twitter         https://twitter.com/m417z
// @homepage        https://m417z.com/
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -loleaut32 -lruntimeobject -lversion -lwinmm
// ==/WindhawkMod==

// Source code is published under The GNU General Public License v3.0.
//
// Original upstream project (local modifications are described in README.md):
// https://github.com/ramensoftware/windhawk-mods/issues
//
// Original author development repository:
// https://github.com/m417z/my-windhawk-mods

// Local Fluid taskbar modifications; original copyright and GPL-3.0 retained.

// ==WindhawkModReadme==
/*
# Taskbar auto-hide speed

Customize the taskbar auto-hide animation speed and frame rate to make it feel
less sluggish and janky.

![Demonstration](https://i.imgur.com/x7pg5xX.gif)
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- showSpeedup: 140
  $name: Show animation speedup
  $description: In percentage of the original speed.
- hideSpeedup: 130
  $name: Hide animation speedup
  $description: In percentage of the original speed.
- frameRate: 0
  $name: Animation frame rate
  $description: >-
    Frames per second. Higher frame rate will use more CPU. Has no effect on the
    new Windows 11 auto-hide animation. Zero matches monitor refresh rate.
- easingMode: 2
  $name: Classic taskbar easing
  $description: Applies only to the classic taskbar. Native Windows 11 keeps its composition curve.
  $options:
  - 0: Linear
  - 1: Quadratic
  - 2: Cubic
  - 3: Quartic
  - 4: Smoothstep
- nominalDuration: 250
  $name: Classic animation duration before speedup (ms)
  $description: Used to normalize easing. Choose linear if your Windows build uses a different duration.
- highPrecisionTimer: true
  $name: Improve classic frame pacing
  $description: Requests 1 ms timer resolution only during an animation. Never busy-waits.
- removeDelay: true
  $name: Remove native Windows 11 animation delay
- oldTaskbarOnWin11: false
  $name: Customize the old taskbar on Windows 11
  $description: >-
    Enable this option to customize the old taskbar on Windows 11 (if using
    ExplorerPatcher or a similar tool).
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <winrt/Windows.UI.Composition.h>

#include <psapi.h>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <timeapi.h>
#include <mutex>
#include <vector>
#include <iterator>

struct {
    int showSpeedup;
    int hideSpeedup;
    int frameRate;
    int easingMode;
    int nominalDuration;
    bool highPrecisionTimer;
    bool removeDelay;
    bool oldTaskbarOnWin11;
} g_settings;

enum class WinVersion {
    Unsupported,
    Win10,
    Win11,
    Win11_24H2,
};

WinVersion g_winVersion;

std::atomic<bool> g_taskbarViewDllLoaded;
std::atomic<bool> g_initialized;
std::atomic<bool> g_explorerPatcherInitialized;

double g_recipCyclesPerSecond;

// A separate scope per UI thread prevents secondary monitors from corrupting
// each other's timing. Save/restore handles nested animations and exceptions.
struct SlideTiming {
    bool active = false;
    bool show = false;
    int speed = 100;
    int fps = 60;
    double start = 0;
    double lastFrame = 0;
};
thread_local SlideTiming g_slide;

// Keep this pure function shared with the portable regression tests.
static double AnimationTime(double elapsed, double duration, int mode, bool show) {
    if (elapsed <= 0) return 0;
    if (elapsed >= duration || mode == 0) return elapsed;
    double t = elapsed / duration;
    double p = show ? 1.0 - t : t;
    double shaped;
    switch (mode) {
        case 1: shaped = p * p; break;
        case 2: shaped = p * p * p; break;
        case 3: shaped = p * p * p * p; break;
        case 4: return duration * t * t * (3.0 - 2.0 * t);
        default: return elapsed;
    }
    return duration * (show ? 1.0 - shaped : shaped);
}

struct AnimationRecord {
    winrt::Windows::UI::Composition::KeyFrameAnimation animation;
    winrt::Windows::Foundation::TimeSpan duration;
    winrt::Windows::Foundation::TimeSpan delay;
};
std::mutex g_animationMutex;
std::vector<AnimationRecord> g_animations;

void SetAnimationDuration(
    winrt::Windows::UI::Composition::KeyFrameAnimation animation,
    winrt::Windows::Foundation::TimeSpan duration) {
    // From the docs: Minimum allowed value is 1ms and maximum allowed value is
    // 24 days.
    constexpr auto kMin = duration_cast<winrt::Windows::Foundation::TimeSpan>(
        std::chrono::milliseconds{1});
    constexpr auto kMax = duration_cast<winrt::Windows::Foundation::TimeSpan>(
        std::chrono::hours{24 * 24});
    if (duration < kMin) {
        duration = kMin;
    } else if (duration > kMax) {
        duration = kMax;
    }
    animation.Duration(duration);
}

void CustomizeCompositionAnimation(
    const winrt::Windows::UI::Composition::KeyFrameAnimation& animation, bool show) {
    if (!animation) return;
    std::lock_guard<std::mutex> guard(g_animationMutex);
    auto it = std::find_if(g_animations.begin(), g_animations.end(),
        [&](const auto& record) {
            return winrt::get_abi(record.animation) == winrt::get_abi(animation);
        });
    if (it == g_animations.end()) {
        g_animations.push_back({animation, animation.Duration(), animation.DelayTime()});
        it = std::prev(g_animations.end());
    }
    SetAnimationDuration(animation, it->duration * 100 /
                         (show ? g_settings.showSpeedup : g_settings.hideSpeedup));
    animation.DelayTime(g_settings.removeDelay ?
        winrt::Windows::Foundation::TimeSpan{0} : it->delay);
}

using SharedAnimations_TaskbarCollapseAnimation_t =
    void**(WINAPI*)(void* pThis, void* param1);
SharedAnimations_TaskbarCollapseAnimation_t
    SharedAnimations_TaskbarCollapseAnimation_Original;
void** WINAPI SharedAnimations_TaskbarCollapseAnimation_Hook(void* pThis,
                                                             void* param1) {
    Wh_Log(L">");

    void** ret =
        SharedAnimations_TaskbarCollapseAnimation_Original(pThis, param1);

    if (!ret || !ret[0]) return ret;
    try {
        winrt::Windows::UI::Composition::KeyFrameAnimation animation = nullptr;
        winrt::copy_from_abi(animation, ret[0]);
    
        CustomizeCompositionAnimation(animation, false);

    } catch (...) {
        Wh_Log(L"Composition animation unavailable");
    }

    return ret;
}

using SharedAnimations_TaskbarExpandAnimation_t = void**(WINAPI*)(void* pThis,
                                                                  void* param1);
SharedAnimations_TaskbarExpandAnimation_t
    SharedAnimations_TaskbarExpandAnimation_Original;
void** WINAPI SharedAnimations_TaskbarExpandAnimation_Hook(void* pThis,
                                                           void* param1) {
    Wh_Log(L">");

    void** ret =
        SharedAnimations_TaskbarExpandAnimation_Original(pThis, param1);

    if (!ret || !ret[0]) return ret;
    try {
        winrt::Windows::UI::Composition::KeyFrameAnimation animation = nullptr;
        winrt::copy_from_abi(animation, ret[0]);
    
        CustomizeCompositionAnimation(animation, true);

    } catch (...) {
        Wh_Log(L"Composition animation unavailable");
    }

    return ret;
}

void TimerInitialize() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    double cyclesPerSecond = static_cast<double>(freq.QuadPart);
    g_recipCyclesPerSecond = 1.0 / cyclesPerSecond;
}

double TimerGetCycles() {
    LARGE_INTEGER T1;
    QueryPerformanceCounter(&T1);
    return static_cast<double>(T1.QuadPart);
}

double TimerGetSeconds() {
    return TimerGetCycles() * g_recipCyclesPerSecond;
}

using TrayUI_SlideWindow_t = void(WINAPI*)(void* pThis,
                                           HWND hWnd,
                                           const RECT* rect,
                                           HMONITOR monitor,
                                           bool show,
                                           bool animate);
TrayUI_SlideWindow_t TrayUI_SlideWindow_Original;
void WINAPI TrayUI_SlideWindow_Hook(void* pThis,
                                    HWND hWnd,
                                    const RECT* rect,
                                    HMONITOR monitor,
                                    bool show,
                                    bool animate) {
    struct ScopedTimerResolution {
        bool acquired;
        explicit ScopedTimerResolution(bool enable)
            : acquired(enable && timeBeginPeriod(1) == TIMERR_NOERROR) {}
        ~ScopedTimerResolution() { if (acquired) timeEndPeriod(1); }
    } timerResolution{animate && g_settings.highPrecisionTimer};
    const SlideTiming saved = g_slide;
    struct Restore {
        SlideTiming value;
        ~Restore() { g_slide = value; }
    } restore{saved};
    int fps = g_settings.frameRate;
    if (!fps) {
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        fps = 60;
        if (GetMonitorInfoW(monitor, &info) &&
            EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) &&
            mode.dmDisplayFrequency > 1) fps = mode.dmDisplayFrequency;
    }
    const double now = TimerGetSeconds();
    g_slide = {animate, show,
               show ? g_settings.showSpeedup : g_settings.hideSpeedup,
               std::clamp(fps, 30, 360), now, now};
    TrayUI_SlideWindow_Original(pThis, hWnd, rect, monitor, show, animate);
}

using GetTickCount_t = decltype(&GetTickCount);
GetTickCount_t GetTickCount_Original;
DWORD WINAPI GetTickCount_Hook() {
    if (!g_slide.active) return GetTickCount_Original();
    double elapsed = std::max(0.0, TimerGetSeconds() - g_slide.start) *
                     1000.0 * (g_slide.speed / 100.0);
    return static_cast<DWORD>(AnimationTime(elapsed, g_settings.nominalDuration,
                                           g_settings.easingMode, g_slide.show));
}

using Sleep_t = decltype(&Sleep);
Sleep_t Sleep_Original;
void WINAPI Sleep_Hook(DWORD milliseconds) {
    // Never spin on Explorer's UI thread or shorten unrelated long waits.
    if (!g_slide.active || milliseconds > 20) {
        Sleep_Original(milliseconds);
        return;
    }
    double remaining = 1000.0 / g_slide.fps -
                       (TimerGetSeconds() - g_slide.lastFrame) * 1000.0;
    Sleep_Original(static_cast<DWORD>(std::clamp(std::ceil(remaining), 1.0, 34.0)));
    g_slide.lastFrame = TimerGetSeconds();
}

bool HookTaskbarViewDllSymbols(HMODULE module) {
    // Taskbar.View.dll
    WindhawkUtils::SYMBOL_HOOK symbolHooks[] = {
        {
            {LR"(public: struct std::pair<struct winrt::Windows::UI::Composition::KeyFrameAnimation,struct winrt::WindowsUdk::UI::Composition::AnimationLastFrameTelemetry> __cdecl winrt::Taskbar::implementation::SharedAnimations::TaskbarCollapseAnimation(void)const )"},
            &SharedAnimations_TaskbarCollapseAnimation_Original,
            SharedAnimations_TaskbarCollapseAnimation_Hook,
            true,  // For the new Windows 11 animation.
        },
        {
            {LR"(public: struct std::pair<struct winrt::Windows::UI::Composition::KeyFrameAnimation,struct winrt::WindowsUdk::UI::Composition::AnimationLastFrameTelemetry> __cdecl winrt::Taskbar::implementation::SharedAnimations::TaskbarExpandAnimation(void)const )"},
            &SharedAnimations_TaskbarExpandAnimation_Original,
            SharedAnimations_TaskbarExpandAnimation_Hook,
            true,  // For the new Windows 11 animation.
        },
    };

    if (!HookSymbols(module, symbolHooks, ARRAYSIZE(symbolHooks))) {
        Wh_Log(L"HookSymbols failed");
        return false;
    }

    return true;
}

HMODULE GetTaskbarViewModuleHandle() {
    HMODULE module = GetModuleHandle(L"Taskbar.View.dll");
    if (!module) {
        module = GetModuleHandle(L"ExplorerExtensions.dll");
    }

    return module;
}

void HandleLoadedModuleIfTaskbarView(HMODULE module, LPCWSTR lpLibFileName) {
    if (g_winVersion >= WinVersion::Win11 && !g_taskbarViewDllLoaded &&
        GetTaskbarViewModuleHandle() == module &&
        !g_taskbarViewDllLoaded.exchange(true)) {
        Wh_Log(L"Loaded %s", lpLibFileName);

        if (HookTaskbarViewDllSymbols(module)) {
            Wh_ApplyHookOperations();
        }
    }
}

bool HookTaskbarSymbols() {
    HMODULE module;
    if (g_winVersion <= WinVersion::Win10) {
        module = GetModuleHandle(nullptr);
    } else {
        module = LoadLibraryEx(L"taskbar.dll", nullptr,
                               LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) {
            Wh_Log(L"Couldn't load taskbar.dll");
            return false;
        }
    }

    // Taskbar.dll, explorer.exe
    WindhawkUtils::SYMBOL_HOOK symbolHooks[] = {
        {
            {LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool))"},
            &TrayUI_SlideWindow_Original,
            TrayUI_SlideWindow_Hook,
        },
    };

    if (!HookSymbols(module, symbolHooks, ARRAYSIZE(symbolHooks))) {
        Wh_Log(L"HookSymbols failed");
        return false;
    }

    return true;
}

VS_FIXEDFILEINFO* GetModuleVersionInfo(HMODULE hModule, UINT* puPtrLen) {
    void* pFixedFileInfo = nullptr;
    UINT uPtrLen = 0;

    HRSRC hResource =
        FindResource(hModule, MAKEINTRESOURCE(VS_VERSION_INFO), RT_VERSION);
    if (hResource) {
        HGLOBAL hGlobal = LoadResource(hModule, hResource);
        if (hGlobal) {
            void* pData = LockResource(hGlobal);
            if (pData) {
                if (!VerQueryValue(pData, L"\\", &pFixedFileInfo, &uPtrLen) ||
                    uPtrLen == 0) {
                    pFixedFileInfo = nullptr;
                    uPtrLen = 0;
                }
            }
        }
    }

    if (puPtrLen) {
        *puPtrLen = uPtrLen;
    }

    return (VS_FIXEDFILEINFO*)pFixedFileInfo;
}

WinVersion GetExplorerVersion() {
    VS_FIXEDFILEINFO* fixedFileInfo = GetModuleVersionInfo(nullptr, nullptr);
    if (!fixedFileInfo) {
        return WinVersion::Unsupported;
    }

    WORD major = HIWORD(fixedFileInfo->dwFileVersionMS);
    WORD minor = LOWORD(fixedFileInfo->dwFileVersionMS);
    WORD build = HIWORD(fixedFileInfo->dwFileVersionLS);
    WORD qfe = LOWORD(fixedFileInfo->dwFileVersionLS);

    Wh_Log(L"Version: %u.%u.%u.%u", major, minor, build, qfe);

    switch (major) {
        case 10:
            if (build < 22000) {
                return WinVersion::Win10;
            } else if (build < 26100) {
                return WinVersion::Win11;
            } else {
                return WinVersion::Win11_24H2;
            }
            break;
    }

    return WinVersion::Unsupported;
}

struct EXPLORER_PATCHER_HOOK {
    PCSTR symbol;
    void** pOriginalFunction;
    void* hookFunction = nullptr;
    bool optional = false;

    template <typename Prototype>
    EXPLORER_PATCHER_HOOK(
        PCSTR symbol,
        Prototype** originalFunction,
        std::type_identity_t<Prototype*> hookFunction = nullptr,
        bool optional = false)
        : symbol(symbol),
          pOriginalFunction(reinterpret_cast<void**>(originalFunction)),
          hookFunction(reinterpret_cast<void*>(hookFunction)),
          optional(optional) {}
};

bool HookExplorerPatcherSymbols(HMODULE explorerPatcherModule) {
    if (g_explorerPatcherInitialized.exchange(true)) {
        return true;
    }

    if (g_winVersion >= WinVersion::Win11) {
        g_winVersion = WinVersion::Win10;
    }

    EXPLORER_PATCHER_HOOK hooks[] = {
        {R"(?SlideWindow@TrayUI@@UEAAXPEAUHWND__@@PEBUtagRECT@@PEAUHMONITOR__@@_N3@Z)",
         &TrayUI_SlideWindow_Original, TrayUI_SlideWindow_Hook},
    };

    bool succeeded = true;

    for (const auto& hook : hooks) {
        void* ptr = (void*)GetProcAddress(explorerPatcherModule, hook.symbol);
        if (!ptr) {
            Wh_Log(L"ExplorerPatcher symbol%s doesn't exist: %S",
                   hook.optional ? L" (optional)" : L"", hook.symbol);
            if (!hook.optional) {
                succeeded = false;
            }
            continue;
        }

        if (hook.hookFunction) {
            Wh_SetFunctionHook(ptr, hook.hookFunction, hook.pOriginalFunction);
        } else {
            *hook.pOriginalFunction = ptr;
        }
    }

    if (!succeeded) {
        Wh_Log(L"HookExplorerPatcherSymbols failed");
    } else if (g_initialized) {
        Wh_ApplyHookOperations();
    }

    return succeeded;
}

bool IsExplorerPatcherModule(HMODULE module) {
    WCHAR moduleFilePath[MAX_PATH];
    switch (
        GetModuleFileName(module, moduleFilePath, ARRAYSIZE(moduleFilePath))) {
        case 0:
        case ARRAYSIZE(moduleFilePath):
            return false;
    }

    PCWSTR moduleFileName = wcsrchr(moduleFilePath, L'\\');
    if (!moduleFileName) {
        return false;
    }

    moduleFileName++;

    if (_wcsnicmp(L"ep_taskbar.", moduleFileName, sizeof("ep_taskbar.") - 1) ==
        0) {
        Wh_Log(L"ExplorerPatcher taskbar module: %s", moduleFileName);
        return true;
    }

    return false;
}

bool HandleLoadedExplorerPatcher() {
    HMODULE hMods[1024];
    DWORD cbNeeded;
    if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods),
                           &cbNeeded)) {
        for (size_t i = 0; i < std::min<size_t>(cbNeeded / sizeof(HMODULE), ARRAYSIZE(hMods)); i++) {
            if (IsExplorerPatcherModule(hMods[i])) {
                return HookExplorerPatcherSymbols(hMods[i]);
            }
        }
    }

    return true;
}

void HandleLoadedModuleIfExplorerPatcher(HMODULE module) {
    if (module && !((ULONG_PTR)module & 3) && !g_explorerPatcherInitialized) {
        if (IsExplorerPatcherModule(module)) {
            HookExplorerPatcherSymbols(module);
        }
    }
}

using LoadLibraryExW_t = decltype(&LoadLibraryExW);
LoadLibraryExW_t LoadLibraryExW_Original;
HMODULE WINAPI LoadLibraryExW_Hook(LPCWSTR lpLibFileName,
                                   HANDLE hFile,
                                   DWORD dwFlags) {
    HMODULE module = LoadLibraryExW_Original(lpLibFileName, hFile, dwFlags);
    if (module) {
        HandleLoadedModuleIfExplorerPatcher(module);
        HandleLoadedModuleIfTaskbarView(module, lpLibFileName);
    }

    return module;
}

void LoadSettings() {
    g_settings.showSpeedup = std::clamp(Wh_GetIntSetting(L"showSpeedup"), 1, 1000);
    g_settings.hideSpeedup = std::clamp(Wh_GetIntSetting(L"hideSpeedup"), 1, 1000);
    g_settings.frameRate = std::clamp(Wh_GetIntSetting(L"frameRate"), 0, 360);
    g_settings.easingMode = std::clamp(Wh_GetIntSetting(L"easingMode"), 0, 4);
    g_settings.nominalDuration = std::clamp(Wh_GetIntSetting(L"nominalDuration"), 50, 2000);
    g_settings.highPrecisionTimer = Wh_GetIntSetting(L"highPrecisionTimer") != 0;
    g_settings.removeDelay = Wh_GetIntSetting(L"removeDelay") != 0;
    g_settings.oldTaskbarOnWin11 = Wh_GetIntSetting(L"oldTaskbarOnWin11");
}

BOOL Wh_ModInit() {
    Wh_Log(L">");

    LoadSettings();

    g_winVersion = GetExplorerVersion();
    if (g_winVersion == WinVersion::Unsupported) {
        Wh_Log(L"Unsupported Windows version");
        return FALSE;
    }

    if (g_settings.oldTaskbarOnWin11) {
        bool hasWin10Taskbar = g_winVersion < WinVersion::Win11_24H2;

        if (g_winVersion >= WinVersion::Win11) {
            g_winVersion = WinVersion::Win10;
        }

        if (hasWin10Taskbar && !HookTaskbarSymbols()) {
            return FALSE;
        }
    } else if (g_winVersion >= WinVersion::Win11) {
        if (HMODULE taskbarViewModule = GetTaskbarViewModuleHandle()) {
            g_taskbarViewDllLoaded = true;
            if (!HookTaskbarViewDllSymbols(taskbarViewModule)) {
                return FALSE;
            }
        } else {
            Wh_Log(L"Taskbar view module not loaded yet");
        }

        if (!HookTaskbarSymbols()) {
            return FALSE;
        }
    } else {
        if (!HookTaskbarSymbols()) {
            return FALSE;
        }
    }

    if (!HandleLoadedExplorerPatcher()) {
        Wh_Log(L"HandleLoadedExplorerPatcher failed");
        return FALSE;
    }

    HMODULE kernelBaseModule = GetModuleHandle(L"kernelbase.dll");
    auto pKernelBaseLoadLibraryExW = (decltype(&LoadLibraryExW))GetProcAddress(
        kernelBaseModule, "LoadLibraryExW");
    WindhawkUtils::Wh_SetFunctionHookT(pKernelBaseLoadLibraryExW,
                                       LoadLibraryExW_Hook,
                                       &LoadLibraryExW_Original);

    auto pKernelBaseGetTickCount = (decltype(&GetTickCount))GetProcAddress(
        kernelBaseModule, "GetTickCount");
    WindhawkUtils::Wh_SetFunctionHookT(
        pKernelBaseGetTickCount, GetTickCount_Hook, &GetTickCount_Original);

    auto pKernelBaseSleep =
        (decltype(&Sleep))GetProcAddress(kernelBaseModule, "Sleep");
    WindhawkUtils::Wh_SetFunctionHookT(pKernelBaseSleep, Sleep_Hook,
                                       &Sleep_Original);

    TimerInitialize();

    g_initialized = true;

    return TRUE;
}

void Wh_ModAfterInit() {
    Wh_Log(L">");

    if (g_winVersion >= WinVersion::Win11 && !g_taskbarViewDllLoaded) {
        if (HMODULE taskbarViewModule = GetTaskbarViewModuleHandle()) {
            if (!g_taskbarViewDllLoaded.exchange(true)) {
                Wh_Log(L"Got Taskbar.View.dll");

                if (HookTaskbarViewDllSymbols(taskbarViewModule)) {
                    Wh_ApplyHookOperations();
                }
            }
        }
    }

    // Try again in case there's a race between the previous attempt and the
    // LoadLibraryExW hook.
    if (!g_explorerPatcherInitialized) {
        HandleLoadedExplorerPatcher();
    }
}

void Wh_ModUninit() {
    Wh_Log(L">");

    std::lock_guard<std::mutex> guard(g_animationMutex);
    for (auto& record : g_animations) {
        try {
            record.animation.Duration(record.duration);
            record.animation.DelayTime(record.delay);
        } catch (...) {
            Wh_Log(L"Animation object already released during unload");
        }
    }
    g_animations.clear();
}

BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    *bReload = TRUE;
    return TRUE;
}
