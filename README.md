# Fluid taskbar auto-hide

A single standalone Windhawk mod for Windows 10/11 x64:
[`fluid-taskbar-auto-hide.wh.cpp`](fluid-taskbar-auto-hide.wh.cpp).
Copy **only this file**; it contains both animation and visibility logic, all settings,
and everything needed by Windhawk. No other repository files are required to install it.

This repository started empty. The supplied `tb hide.txt` has missing C++ punctuation
throughout; these sources use valid upstream implementations instead of guessing at
all the damaged symbols.

## Install on Windows

1. Install [Windhawk](https://windhawk.net/).
2. Disable both earlier Fluid companion mods, the original **Taskbar auto-hide speed**,
   **Taskbar auto-hide when maximized**, and other mods controlling the same behavior.
3. Open [`fluid-taskbar-auto-hide.wh.cpp`](fluid-taskbar-auto-hide.wh.cpp), choose
   **Raw** on GitHub, and copy the entire file.
4. In Windhawk, choose **Create a new mod**, replace the entire template with the
   copied code, then compile and enable it.
5. All animation and visibility controls are now together in this mod's Settings tab.

The visibility mod uses the upstream taskbar auto-hide state machinery, enables
auto-hide while loaded, and restores the original state on unload. No registry edits
or restart loop are needed. When using ExplorerPatcher's old taskbar, enable
`oldTaskbarOnWin11` in the combined mod.

## Hidden double-click reveal lock (v2.3)

Double-click **empty taskbar space** to hold that display's taskbar in the
revealed position. Double-click empty space on the same taskbar again to release
the manual lock and resume your existing visibility rules. Unlocking does not
force a taskbar to hide while the pointer is over it or adaptive rules keep it up.

This shortcut is always available, including when adaptive pinning is switched off.
Each display has its own lock. It adds no setting, popup, message, sound, notification,
tray icon, or lock indicator. Locks are session-only and reset on taskbar recreation,
Explorer restart, mod reload/settings reload, or disable.

App buttons, Start, search, tray/volume controls, and clock elements are not treated
as empty space. Hit testing follows the empty accessibility peers used by Windhawk's
`taskbar-empty-space-clicks` mod. Input observation is restricted to taskbar UI
threads, and the original input messages are passed through. Only candidate left
clicks wake a separate accessibility worker; there is no polling/keep-alive timer.
The worker has a bounded queue, uses the Windows double-click time and distance,
and keeps accessibility queries off Explorer's UI thread. Promoted mouse messages
from the Windows 11 input site are ignored to avoid toggling twice for one click.

The click recognition, routing, property toggles, queue limits, and lifecycle cleanup
have been tested with Win32 stubs. Native accessibility/provider behavior and
Windows compilation remain unverified in this Linux environment.

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
- Linear, quadratic, cubic, quartic, smoothstep, or smootherstep easing for the classic taskbar.
  Default smootherstep uses a nominal 250 ms duration and eases both velocity
  and acceleration at the endpoints; use linear if the timing differs on
  your Windows build. Native Windows 11 composition retains its native curve.
- Frame deadlines stay on the selected FPS cadence instead of accumulating render
  and scheduler delays. Late frames yield; long stalls resume without catch-up bursts.
- Optional short-lived 1 ms timer resolution, requested only when the classic
  taskbar actually sleeps between frames; no busy-wait or spin loop.
- Optional removal of native Windows 11 animation delay, with original durations
  and delays restored on unload, including animation objects rebuilt by Explorer.
- Timing is isolated per thread and restored after nested animations. Long Sleep
  calls are passed through, and out-of-animation calls keep their original behavior.
- Removed the bounce-based virtual clock: moving time backwards can break an
  animation's elapsed-time calculations. Other easing functions are monotonic.
- Settings changes reload the mod rather than mutate shared settings while worker
  callbacks use them. Bounds checks prevent module-enumeration buffer overreads.
- Defensive WinRT/null handling keeps animation failures from unwinding into Explorer.
- Desktop, wallpaper-host, taskbar, menu, cloaked, minimized, and tool windows are
  excluded from fullscreen detection. Fullscreen detection handles negative monitor
  coordinates and windows that extend past monitor edges.

The volume-flyout freeze is **not confirmed fixed**. The busy-wait and shared timing
hazards have been addressed, but reproducing the reported bug requires Windows.
The classic clock hooks still affect same-thread reentrant calls during SlideWindow;
if volume problems persist, set show/hide speed to 100%, choose linear easing, and turn off high-precision
frame pacing and delay removal to help isolate the animation changes.

## Long-session and idle stutter changes in v2.2

The previous version retained each distinct native composition animation it saw,
which could grow the object list and the cost of searching it over a long session.
The updated mod keeps at most 32 records. Before evicting a live animation it
restores its original duration/delay; unloading also restores all remaining records.
Repeated animations no longer receive property writes when their duration/delay
already matches the requested value.

Automatic refresh-rate results are cached per monitor and UI thread, with a fixed
limit of eight entries. The cache is invalidated by display/settings changes and
taskbar recreation, alongside the visibility display cache. The first reveal after
several idle minutes no longer repeats a successful display-mode lookup. Failed
lookups are retried, and explicitly selected FPS continues to bypass detection.
There is no periodic wake-up timer to keep the taskbar warm.

After replacing the source in Windhawk, compile it and disable/re-enable the mod
once so the old instance releases its animation records. All 18 settings and their
current defaults are preserved. These fixes address confirmed code-level growing
state and repeated queries; the reported intermittent Windows stutter has not been
reproduced in this Linux environment, so universally smooth rendering is not claimed.

## Lower overhead in v2.1

All 18 settings and the existing visibility rules remain available. The event
coalescing interval stays at 200 ms, and animation speed and FPS settings are not
reduced to save power.

- Monitor-slot mappings are cached until a display/settings change or taskbar
  recreation. Repeated window events no longer enumerate and sort every display.
- Small/ineligible windows skip the expensive cloak, process-path and app-ID
  exclusion queries. Exclusions are still checked before any qualifying window
  is allowed to trigger auto-hide.
- Taskbar bounds are fetched only when intersection/snapped-window rules need them.
- Window-event monitoring stays off when adaptive pinning is disabled, all four
  pins are off, or selected displays are set to never auto-hide.
- Repeated movement events share the already pending update. Task View and Alt+Tab
  state transitions continue to be processed. Verbose per-event process/title
  diagnostics were removed.
- Instant animations skip refresh-rate/clock queries. High-resolution timer
  requests are deferred until a paced sleep, then released when that animation ends.

For the new motion curve, select **Smootherstep (gentle start and stop)** in
**Classic taskbar easing**. It is the default for fresh installs; Windhawk may
retain your previous selection when replacing an existing mod. Windows 11's native
composition animation keeps its system curve; these custom easing/FPS controls
apply to the classic taskbar path.

Counted stub tests reduce 400 repeated slot lookups to one display enumeration,
and confirm the visibility decisions still match v2.0. These are operation counts,
not a measured battery-life improvement. Actual energy use and perceived smoothness
still need Windows hardware testing.

## Validation

Portable checks (Linux or another machine with Python 3 and GCC with sanitizers):

```sh
cd /workspace/fluid-taskbar-auto-hide
./tests/run.sh
```

The runner checks that embedded helper functions match the tested source, then runs
720,036 timing samples (monotonicity, endpoints, bounds, and completion), fullscreen
geometry checks, and all four-display toggle combinations under AddressSanitizer
and UndefinedBehaviorSanitizer. Temporary binaries are removed automatically.
The combined lifecycle is also compiled against a stub host to check initialization,
shared module dispatch, reload, unload order, and cleanup after initialization failure.
The power-path tests also compile the actual optimized functions against counted
Win32 stubs: display-cache invalidation, timer acquisition/release, nesting and
exceptions, and 98,304 visibility cases compared with the v2.0 predicate. They check
that irrelevant windows avoid process/app-ID queries and that disabled adaptive
pinning does not start window-event monitoring. Cadence simulations cover
30, 60, 90, 144, 240, and 360 FPS with variable rendering cost and scheduler delay.
The long-session tests exercise 20,000 reused animations and 5,000 new animation
objects with reference-counted WinRT stubs. They check bounded retention, original
timing restoration on eviction/re-entry/unload, cleanup after a disconnected object,
and absence of redundant property writes. Simulated four-minute/hour idle gaps,
display-change invalidation, lookup failure/retry, cache limits, and explicit FPS
are covered by the refresh-rate tests.
These tests do **not** compile the Windows hooks or exercise Explorer.

### Required Windows validation — not executed in the Linux cloud environment

- Compile the single `fluid-taskbar-auto-hide.wh.cpp` file in Windhawk; confirm successful initialization in logs.
- Double-click empty space on the primary and secondary taskbars. Verify each locks
  independently with no visible indicator; repeated window changes must not hide a
  locked taskbar. Double-click again and move the pointer away to confirm normal
  rules resume. Check app icons, Start, search, volume, clock, slow clicks, far-apart
  clicks and triple-clicks do not cause unintended locks. Repeat with adaptive
  pinning off. Verify reload/disable/recreated taskbars clear locks.
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
  changes. Disable/unload the mod and verify original Windows behavior returns.
- Repeat after restarting Explorer and with ExplorerPatcher if used.
- Leave the taskbar unused for four minutes and for an hour, then reveal/hide it
  repeatedly. Also exercise several thousand reveals in a session and check that
  Explorer's memory use does not keep increasing due to retained animation objects.
- Compare CPU use and power on the same battery/refresh-rate settings while idle,
  dragging windows, and repeatedly revealing/hiding. Check frame pacing at 60 Hz
  and high refresh rates. No battery-life percentage has been measured here.

No Windows runtime testing or successful Windows compilation is claimed. Unsupported
Windows updates may change private Explorer symbols and require hook updates.
No program can promise to be bug-free on every Windows build.

## Attribution

Derived from m417z's GPL-3.0 Windhawk mods, downloaded from
[ramensoftware/windhawk-mods](https://github.com/ramensoftware/windhawk-mods):
`mods/taskbar-auto-hide-speed.wh.cpp` (1.1) and
`mods/taskbar-auto-hide-when-maximized.wh.cpp` (1.2.6).
The empty-area accessibility peer allowlist follows
`mods/taskbar-empty-space-clicks.wh.cpp` (upstream GPL-3.0); the input observation
and reveal-lock implementation here are local. Original attribution is retained
in the source files. This repository contains local
modifications; it is not an upstream release. See `LICENSE` for GPL version 3.

## Source layout for development

`src/animation.cpp` and `src/visibility.cpp` contain the component implementations.
`src/click-lock.cpp` contains the hidden reveal lock, emitted inline in its own
namespace before visibility hooks. Its worker starts with the mod and stops before
animation/visibility cleanup.
`python3 tools/build_single_file.py` regenerates the one installable `.wh.cpp` file.
`src/runtime-state.inc` holds the shared display-change generation, emitted inline
before both components. All includes are emitted outside the component namespaces. There is one shared
`LoadLibraryExW` hook, one copy of each lifecycle entry point, one settings block,
and one shared old-taskbar setting. The combined lifecycle initializes both
components, restores visibility if animation initialization fails, and unloads
both components. `./tests/run.sh` checks the generated file is current.

In the October 22 cloud validation, this sandbox's process tracing prevented
LeakSanitizer from starting. The suite ran with `ASAN_OPTIONS=detect_leaks=0`, keeping
AddressSanitizer and UndefinedBehaviorSanitizer active. The reference-counted
bookkeeping tests explicitly verify release of all cached animation references;
this does not substitute for a native Windows leak profiler.
