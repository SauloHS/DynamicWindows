# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project identity

**DynamicWindows** is a portable, native **C11 / Win32** activity island for Windows 10/11 x64. The public repository is `SauloHS/DynamicWindows`; the license is **GPL-3.0**. Keep application code in C: the custom interface uses GDI+'s flat C API, not C++, Electron, or a browser runtime.

The executable is `build/DynamicWindows.exe`. `MacNotch` is the previous name; remaining references in configuration loading are intentional compatibility support, not unfinished branding.

## Build and verification

Run commands from the repository root using PowerShell. Requirements: **MinGW-w64 GCC, Ninja, CMake 3.20+, clang-format**. CMake explicitly rejects non-MinGW toolchains because the required GDI+ C declarations come from MinGW headers.

```powershell
# Configure and build.
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_RC_COMPILER=windres -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Fast application-only build.
cmake --build build --target dynamicwindows

# Completion gates for code changes.
cmake --build build --target typecheck
ctest --test-dir build --output-on-failure
cmake --build build --target lint
```

If the compiler is not on PATH, use `-DCMAKE_C_COMPILER=C:/mingw64/bin/gcc.exe` and `-DCMAKE_RC_COMPILER=C:/mingw64/bin/windres.exe`. Use a different build directory when changing toolchains. `BUILD_TESTING=OFF` excludes test executables; leave it enabled for development.

`typecheck` runs C11 syntax/type checking with warnings treated as errors. `lint` **checks formatting**, not static analysis. Format only changed C/header files with `clang-format -i` before running lint; the style is LLVM-based, four-space indentation, 100 columns.

### Focused tests

```powershell
ctest --test-dir build -R '^core$' --output-on-failure
ctest --test-dir build -R '^configuration$' --output-on-failure
ctest --test-dir build -R '^native_smoke$' --output-on-failure
```

- `core`: platform-independent timer, activity priority, geometry, and spring behavior.
- `configuration`: defaults, validation, placement, malformed input, and Unicode save/load.
- `native_smoke`: launches the actual renderer, writes PNGs to `build/smoke`, and checks transparent hit-testing and MSAA names/roles. It does not prove every interactive behavior.

Honor an explicit request to skip tests. Report skipped checks; do not present an earlier pass as proof of newly changed code. Documentation-only changes do not need application tests.

### Run the actual app

```powershell
.\build\DynamicWindows.exe
.\build\DynamicWindows.exe --settings
.\build\DynamicWindows.exe --isolated --view media --demo --run-for 15
```

`--demo` simulates media only; system controls stay live. `--isolated` bypasses loading the normal profile and suppresses opening settings at startup. For settings changes, pass a disposable **absolute** `--config` path rather than editing the real user profile.

Optional actual-window verification requires Python 3 and an interactive Windows desktop, without third-party Python packages:

```powershell
python -I tests/gui_check.py build/DynamicWindows.exe build/smoke --settings
python -I tests/gui_check.py build/DynamicWindows.exe build/smoke --search
python -I tests/gui_check.py build/DynamicWindows.exe build/smoke --live-media
```

These checks move the pointer, manipulate focus, and capture screenshots. `--live-media` starts `media_publisher.exe` to exercise real GSMTC metadata and transport commands without playing audio. Quit an existing instance first: the app is single-instance, and a running executable also locks the linker output. A linker **Permission denied** can mean the app is running; do not kill the user's session or silently replace it with a differently named build.

## Architecture: follow ownership

| Responsibility | Owning seam |
|---|---|
| Windows lifecycle, input, effective visibility, activity sizing, scheduling | `main.c` and the shared `App` state in `app.h` |
| Countdown state, activity selection, rounded hit geometry, exact spring integration | `island.c/.h`, built as `island_core` |
| Drawing, artwork decoding, layered-window presentation, logical hit regions | `render.c` |
| Windows media acquisition and transport commands | Dedicated MTA worker in `media.c`; snapshot contract in `media.h` |
| Core Audio output/mute/peak and system power | `platform.c` |
| Headphone connections and driver-reported Bluetooth battery | Dedicated MTA worker and snapshot/delta contract in `headphones.c/.h` |
| Defaults, normalization, INI persistence, shortcut parsing | `config.c/.h` |
| Modeless native settings, draft/baseline, validation, preview | `settings.c/.h` |
| Screen-reader object, roles, names, values, actions | `accessibility.c` |

The UI thread owns `App` and drawing resources. Media COM objects stay on the worker. The worker allocates `MediaSnapshot` messages and posts `WM_DYNAMICWINDOWS_MEDIA`; the UI consumes them and releases their buffers. Commands travel back through the service's atomic command state and wake event. Do not move blocking async waits or artwork acquisition into the UI frame loop.

The main loop combines Windows messages with a high-resolution waitable frame timer through `MsgWaitForMultipleObjectsEx`. `frame()` advances springs and playback interpolation, polls system state, decides effective visibility, and renders. `app_animate()` wakes fast updates when a transition starts. Preserve this separation rather than running integration work at the animation frame rate.

## Behavior invariants

### Compact geometry and visibility

- **Idle and compact media share idle height and corner settings.** Media width is `idle_width + media_extra_width`. A brief title preview changes content, not geometry. Do not switch back to legacy `media_width` or `media_height` when its timeout expires.
- Runtime dimensions come from **`app_target_size()`**, which applies configuration. `island_size()` contains core reference dimensions; it is not the configurable runtime sizing authority.
- Defaults are a **blank 80 × 28** idle pill, **36 px** extra media width, no border or camera-style dot, and settings closed at startup. Keep `config_defaults()`, `resources/defaults.ini`, and default-sensitive tests aligned when intentionally changing them.
- Manual hiding, idle hiding, full-screen suppression, temporary event visibility, and expansion are distinct states. An event may temporarily reveal a hidden island **without clearing the user's hidden preference**. Full-screen suppression wins.
- Hiding uses a visibility spring plus slide/fade; do not call `SW_HIDE` at the start of the animation. Interruptions preserve spring velocity. Respect Windows' reduced-motion preference.
- Outside-click handling closes manually opened controls without swallowing the click. Hover-opened previews use their separate leave delay; dismissal must not immediately reopen through hover.

### Media clock and ABI

GSMTC reports snapshots, not a continuously advancing clock. Use timeline `LastUpdatedTime` and the existing worker/UI interpolation. **Do not reset elapsed playback to an unchanged timeline position on every poll**; that causes stalling and backward jumps. Track changes, seeks, playback-state changes, and playback rate need distinct treatment.

Timeline and seek values use **100-nanosecond ticks**; UI durations use seconds or milliseconds. `LastUpdatedTime` is UTC since 1601, whereas countdown deadlines and local sampling use monotonic time. Account for nonzero timeline start times and valid seek bounds.

`media.c` consumes hand-declared, typed WinRT C vtables with slot assertions. Preserve method order, calling conventions, GUIDs, and **one-byte WinRT Boolean** values. `IAsyncInfo` is a separate queried interface, not extra methods inserted into `IAsyncOperation`'s vtable. Balance returned interfaces, HSTRINGs, async objects, and COM initialization on all exit paths. Keep the existing `uuid`/`oleacc` library order when changing linkage.

Unsupported transport controls remain disabled. Display friendly source labels, not raw Windows application identifiers. Compact artwork is circular and fits the pill; expanded artwork is a dimmed background by default. The waveform measures **system output**, not isolated Spotify audio.

### Rendering and native settings

The renderer draws into a **premultiplied-alpha DIB** and presents through `UpdateLayeredWindow`. Sizes and hit rectangles are logical coordinates; input must convert screen/client coordinates and divide by DPI scale. Expanded content introduces an additional panel transform that hit regions must also follow. Transparent padding must remain click-through.

Settings use real child controls and metadata-driven fields. Preserve stable `SettingsControlId` values used by the GUI driver. Tab pages are siblings of the tab control: the tab background must stay **behind** them. Scrolling moves controls without copying stale pixels (`SWP_NOCOPYBITS`/`SWP_NOREDRAW`) and redraws the page **and all children**. Parent-only invalidation recreates ghosted labels and clipped edit contents.

GUI automation must be DPI-aware. For cross-process edit controls, use direct **`WM_SETTEXT` / `WM_GETTEXT`** messages. `SetWindowTextW` / `GetWindowTextW` can affect/read the external caption instead of the control's internal text; that can falsely implicate working settings code. Use `PrintWindow` for settings captures when another window obscures it.

### Headphones

Headphone discovery enumerates active render endpoints on its own MTA worker. Match Bluetooth PnP nodes using the physical container GUID, never friendly-name guesses. Windows' reported battery property is optional; validate type, size, and 0–100 range, with `-1` meaning unavailable. Wired headphone jack detection depends on driver form-factor/state reporting. The first successful snapshot is a baseline, not a burst of connection alerts. Do not count a transient acquisition failure as a disconnect. Drain posted snapshots after stopping the worker.

The `headphones` CTest target is headless and uses synthetic snapshots. The original `assets/earbuds.svg` is embedded as RCDATA 101 and loaded once into GDI+ paths; this small loader supports only the absolute M/L/C/Z commands used by our own asset, not arbitrary SVG input. Persistent headphone status preserves idle height/radius. While connected, it adds `media_extra_width` to the active compact width; do not hard-code a separate headphone width. Connection previews use a pill radius and a numeric battery ring; unknown battery must not look like zero. Honor a user request not to execute any tests; compilation is not a runtime pass.

### Configuration transactions

`%LOCALAPPDATA%\DynamicWindows\settings.ini` is personal state. Legacy MacNotch settings are read only as a fallback; Apply writes to the new profile and does not delete the old file.

Settings have a draft and an applied baseline. Live preview calls `app_apply_config(..., false)`; only Apply persists. Cancel restores the baseline. Invalid edits never silently become zero or get saved. Global shortcut registration and configuration failure roll back to the previous bindings/configuration. `None` disables a shortcut; collisions are rejected within the same scope.

For a new setting, update the `AppConfig` field, defaults, normalization, load/save mapping, settings metadata, owning runtime code, and relevant tests. Keep preset documentation synchronized. Do not hard-code a configurable setting elsewhere to bypass this path.

## Releases and scope

Do not commit, push, tag, or publish unless requested; ask before committing when authorization is unclear. Existing tags/releases are not disposable build outputs.

A version change spans **CMake**, `resources/dynamicwindows.rc`, `resources/dynamicwindows.manifest`, and release documentation. Publish the executable together with GPL license, corresponding source, and SHA-256 checksums. Package from reviewed tracked files, not the entire working directory. `build/`, `dist/`, private INIs, desktop captures, and stray tool-generated files do not belong in the public source archive. Inspect any unexpected file before deleting it.

The release is unsigned; do not describe it as signed or recommend disabling Windows security. Retain GPL license/About notices and source availability.

The app currently has no network requests, telemetry, microphone/camera capture, or Notification Center mirroring. The camera-style dot is decorative. Apple services, calls, and other apps' toasts are not implemented; do not imply those integrations exist or substitute simulated activity outside explicit demo mode.
