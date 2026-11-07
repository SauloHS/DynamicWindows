# DynamicWindows 0.1.0

The first public release of **DynamicWindows**: a native C11/Win32 activity island for Windows 10 and 11 x64.

## Highlights

- Compact media pill with circular artwork, waveform, and brief title previews.
- Expanded player with dimmed album-art background and friendly media-source labels.
- Continuous playback progress using the source timeline and an interpolated clock.
- Countdown timer, live volume/mute controls, and power alerts.
- Slide-and-fade hide/show, brief event visibility while hidden, and outside-click collapse.
- Six native settings tabs with live preview, persistent preferences, monitor placement, appearance controls, and customizable shortcuts.
- Defaults: blank 80×28 idle pill, no border or camera-style dot, 36 px media width increase, and settings closed at startup.
- Legacy MacNotch settings can be read when no DynamicWindows profile exists.

## Download and run

Download **DynamicWindows-0.1.0-windows-x64.zip**, extract it, and launch **DynamicWindows.exe**. No installer, administrator privileges, or additional app runtime is required.

Default shortcuts:

- **Ctrl+Alt+D** — open/collapse the island
- **Ctrl+Alt+H** — hide/show
- **Ctrl+Alt+T** — timer
- **Ctrl+Alt+C** — settings
- **Ctrl+Alt+F** — Windows Search

The executable is unsigned. Windows may display a publisher or SmartScreen warning. Verify the repository and SHA-256 checksum before deciding whether to run it; do not disable Windows security features.

## Scope

This is an early release. Media capabilities and timing depend on the source application's Windows GSMTC support. DynamicWindows does not mirror other apps' notification toasts, integrate phone calls, or implement Apple services. Timers do not persist after quitting. Full-screen suppression takes priority over temporary event previews.

## Source and license

Released under **GPL-3.0**, without warranty. Corresponding source, build instructions, and the full license are included in **DynamicWindows-0.1.0-source.zip** and available at the [v0.1.0 source tag](https://github.com/SauloHS/DynamicWindows/tree/v0.1.0).
