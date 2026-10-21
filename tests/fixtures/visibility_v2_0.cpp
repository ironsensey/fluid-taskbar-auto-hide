// Frozen v2.0 visibility predicate for optimization equivalence checks.
// Original m417z / Fluid taskbar code, GPL-3.0.
bool BaselineCanHideTaskbarForWindow(HWND hWnd,
                             HMONITOR monitor,
                             const MONITORINFO* monitorInfo,
                             const RECT* taskbarRect) {
    if (!IsWindowVisible(hWnd) || IsWindowCloaked(hWnd) || IsIconic(hWnd) ||
        (GetWindowLong(hWnd, GWL_EXSTYLE) & WS_EX_NOACTIVATE)) {
        return false;
    }

    wchar_t windowClass[64]{};
    GetClassNameW(hWnd, windowClass, ARRAYSIZE(windowClass));
    if (wcscmp(windowClass, L"Progman") == 0 ||
        wcscmp(windowClass, L"WorkerW") == 0 ||
        wcscmp(windowClass, L"Shell_TrayWnd") == 0 ||
        wcscmp(windowClass, L"Shell_SecondaryTrayWnd") == 0 ||
        (GetWindowLongPtrW(hWnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) {
        return false;
    }

    // Ignore the Alt+Tab overlay so it doesn't affect taskbar state.
    if (hWnd == g_altTabViewHwnd) {
        return false;
    }

    if (hWnd == GetShellWindow() || GetProp(hWnd, L"DesktopWindow")) {
        return false;
    }

    // Exclude menus (#32768).
    if (GetClassWord(hWnd, GCW_ATOM) == 32768) {
        return false;
    }

    // Check this after the other checks, as it's the most expensive one.
    if (IsWindowExcluded(hWnd)) {
        return false;
    }

    WINDOWPLACEMENT wp{
        .length = sizeof(WINDOWPLACEMENT),
    };

    if (GetWindowPlacement(hWnd, &wp) && wp.showCmd == SW_SHOWMAXIMIZED) {
        if (MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST) == monitor) {
            return true;
        }

        return false;
    }

    bool isWindowArranged = pIsWindowArranged && pIsWindowArranged(hWnd);
    if (isWindowArranged &&
        MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST) != monitor) {
        return false;
    }

    RECT windowRect{};
    if (FAILED(DwmGetWindowAttribute(hWnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                                     &windowRect, sizeof(windowRect))) &&
        !GetWindowRect(hWnd, &windowRect)) {
        return false;
    }

    // Don't keep the taskbar shown for a fullscreen window.
    if (CoversMonitor(windowRect.left, windowRect.top, windowRect.right,
                      windowRect.bottom, monitorInfo->rcMonitor.left,
                      monitorInfo->rcMonitor.top, monitorInfo->rcMonitor.right,
                      monitorInfo->rcMonitor.bottom)) {
        return true;
    }

    // It makes sense to treat arranged windows (e.g. with Win+left) as
    // maximized, as they occupy the whole monitor height. Still check for
    // intersection, as a window can also just occupy the upper side of the
    // screen (e.g. Win+left, Win+up).
    if (g_settings.mode == Mode::intersected ||
        (g_settings.mode == Mode::maximized && g_settings.snappedWindows && isWindowArranged)) {
        RECT intersectRect;
        if (IntersectRect(&intersectRect, &windowRect, taskbarRect)) {
            return true;
        }
    }

    return false;
}
