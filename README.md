# Fluid taskbar auto-hide

Two companion Windhawk mods for Windows 10/11 x64. They are separate so the
visibility hooks and animation hooks can be enabled, tested, or disabled independently.
This repository started empty. The supplied `tb hide.txt` has missing C++ punctuation
throughout; these sources use valid upstream implementations instead of guessing at
all the damaged symbols.

## Install on Windows

1. Install [Windhawk](https://windhawk.net/).
2. Disable the original **Taskbar auto-hide speed**, **Taskbar auto-hide when
   maximized**, and any other mods controlling the same behavior to avoid duplicate hooks.
3. In Windhawk, choose **Create a new mod**, replace the template with the entire
   contents of `fluid-taskbar-visibility.wh.cpp`, then compile and enable it.
4. Repeat for `fluid-taskbar-animation.wh.cpp`. Each file is standalone; no helper
   files need to be copied into Windhawk.
5. Open each mod's Settings tab. Both are required for the combined behavior;
   visibility alone also works without customized animations.

The visibility mod uses the upstream taskbar auto-hide state machinery, enables
auto-hide while loaded, and restores the original state on unload. No registry edits
or restart loop are needed. When using ExplorerPatcher's old taskbar, enable
`oldTaskbarOnWin11` in **both** mods.

## Visibility settings

By default the taskbar stays visible on the desktop and when using small windows.
A visible, non-minimized maximized or fullscreen app on a display allows **that
same display's** taskbar to auto-hide. Other displays stay pinned according to
their own windows. The normal Windows pointer/flyout interaction remains in control
when auto-hide is permitted; this mod does not force-hide a taskbar while you use it.

- **Pin taskbar unless an app is maximized or fullscreen:** master switch.
  Off means normal Windows auto-hide, not permanently visible.
- **Pin when windowed — displays 1–4:** independent switches. Off means normal
  Windows auto-hide on that display. All four switches remain visible; disconnected
  slots have no effect. Windhawk's settings editor does not dynamically gray them out.
- Display slots are contiguous: primary display first, then remaining connected
  displays ordered by Windows device name. These are mod slots, not necessarily
  the numbers in Windows Settings. Recheck toggles after changing the primary
  display or connecting/disconnecting monitors. Displays beyond four use Windows
  auto-hide normally.
- **Treat snapped windows as maximized:** off by default, so tiled small windows
  keep the taskbar visible. Enable to let edge-snapped windows trigger auto-hide.
- **Mode:** maximized/fullscreen by default; optional intersection mode or never
  auto-hide on selected displays.
- **Apply only to foreground window:** off by default. When enabled, a maximized
  background app no longer determines visibility while a small foreground app is used.
- **Excluded programs:** process names, paths, or app IDs that should not trigger hiding.
- **Primary monitor only:** optional additional restriction; normally leave off.

## Animation and responsiveness changes

- Default reveal/hide speed: 140% / 130%; adjustable with bounded inputs.
- Automatic refresh-rate detection (FPS = 0), or 30–360 FPS for the classic taskbar.
- Linear, quadratic, cubic, quartic, or smoothstep easing for the classic taskbar.
  Default cubic uses a nominal 250 ms duration; use linear if the timing differs on
  your Windows build. Native Windows 11 composition retains its native curve.
- Optional short-lived 1 ms timer resolution; no busy-wait or spin loop.
- Optional removal of native Windows 11 animation delay, with original durations
  and delays restored on unload, including animation objects rebuilt by Explorer.
- Timing is isolated per thread and restored after nested animations. Long Sleep
  calls are passed through, and out-of-animation calls keep their original behavior.
- Removed the bounce-based virtual clock: moving time backwards can break an
  animation's elapsed-time calculations. Other easing functions are monotonic.
- Settings changes reload the mods rather than mutate shared settings while worker
  callbacks use them. Bounds checks prevent module-enumeration buffer overreads.
- Defensive WinRT/null handling keeps animation failures from unwinding into Explorer.
- Desktop, wallpaper-host, taskbar, menu, cloaked, minimized, and tool windows are
  excluded from fullscreen detection. Fullscreen detection handles negative monitor
  coordinates and windows that extend past monitor edges.

The volume-flyout freeze is **not confirmed fixed**. The busy-wait and shared timing
hazards have been addressed, but reproducing the reported bug requires Windows.
The classic clock hooks still affect same-thread reentrant calls during SlideWindow;
if volume problems persist, disable the animation mod first to isolate them.

## Validation

Portable checks (Linux or another machine with Python 3 and GCC with sanitizers):

```sh
cd /workspace/fluid-taskbar-auto-hide
./tests/run.sh
```

The runner checks that embedded helper functions match the tested source, then runs
600,030 timing samples (monotonicity, endpoints, bounds, and completion), fullscreen
geometry checks, and all four-display toggle combinations under AddressSanitizer
and UndefinedBehaviorSanitizer. Temporary binaries are removed automatically.
These tests do **not** compile the Windows mods or exercise Explorer hooks.

### Required Windows validation — not executed in the Linux cloud environment

- Compile both source files in Windhawk; confirm successful initialization in logs.
- Desktop / one small window / several small windows: taskbar stays visible.
- Maximize, restore, minimize, close, and fullscreen an app: correct visibility
  transitions, including maximized background apps and foreground-only mode.
- Two displays: maximize on one only, move the app, and independently toggle each
  display switch; verify unused display slots have no effect. Repeat on four displays
  when hardware is available. Check mixed DPI, negative coordinates and hotplugging.
- Open volume/Quick Settings from the taskbar and via keyboard; drag the volume
  slider repeatedly during reveal/hide. Verify clicks, hover and flyout dismissal.
- Exercise Start, calendar, notifications, task view, Alt+Tab and Show desktop.
- Test the snapped-window setting, exclusions, speed/FPS bounds, and live settings
  changes. Disable/unload each mod and verify original Windows behavior returns.
- Repeat after restarting Explorer and with ExplorerPatcher if used.

No Windows runtime testing or successful Windows compilation is claimed. Unsupported
Windows updates may change private Explorer symbols and require hook updates.
No program can promise to be bug-free on every Windows build.

## Attribution

Derived from m417z's GPL-3.0 Windhawk mods, downloaded from
[ramensoftware/windhawk-mods](https://github.com/ramensoftware/windhawk-mods):
`mods/taskbar-auto-hide-speed.wh.cpp` (1.1) and
`mods/taskbar-auto-hide-when-maximized.wh.cpp` (1.2.6).
Original attribution is retained in each file. This repository contains local
modifications; it is not an upstream release. See `LICENSE` for GPL version 3.
