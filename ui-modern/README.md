# Native Windows 11 interface — integrated build

**One executable: `TPFanControl.exe`.** The modern native Win32 UI is compiled into the original C++ fan-control program. No extra monitoring utility is required.

## Controls
- Directly switch BIOS Auto, Smart, Manual control modes.
- Adjust Manual fan level 1–7 (no fan-stop/0 option).
- Select Smart profile 1/2.
- Start with Windows ON/OFF using the existing service and Startup shortcut mechanism (UAC may be required).
- Monitor live EC temperatures and fan RPM.
- The existing single-program Windows service, original engine, polling, thermal safeguards and tray icon are retained.

## Install / Test
Download the `TPFanCtrl2-OneApp-win11` build artifact. The ZIP contains:
- `TPFanControl.exe` — native UI + fan engine.
- `TPFanControl.ini` — existing configuration.
- `LpcACPIEC.bin` — PawnIO EC module.

Extract together, install PawnIO if necessary (`winget install namazso.PawnIO`), and launch **only** `TPFanControl.exe`. To keep fan control active from system boot, turn on **Start with Windows** in the UI. This installs its own background service from the same EXE. Do not run competing fan utilities.

Safety: Experimental GUI integration; Windows build success is not hardware validation. Back up your INI and retain the original program for rollback. Direct manual control and profile switching are included; editing custom temperature curves still uses `TPFanControl.ini` until a safe GUI editor with validated live reload is implemented.

Build (Windows VS2022 / Windows SDK):
```powershell
msbuild fancontrol\\fancontrol.sln /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v143 /m
```

## CPU and memory optimization (2026-10-13)

- **No redraw when numbers are unchanged:** even if the EC engine publishes a new sequence, a repaint is requested only when displayed temperatures, RPM, mode, connectivity, command acknowledgement or startup status changes.
- **No display timer in tray:** hiding or minimizing the dashboard stops its 1-second timer and releases the cached GDI back buffer. The original fan controller continues its safety-critical EC polling unchanged.
- **Reuse drawing surfaces while visible:** avoid allocating a large GDI bitmap and memory DC on every paint. Recreate on resize/DPI change, release on hide/destroy.
- **Less Service Control Manager traffic:** startup status checks are at most once per minute while open, with an immediate refresh after opening/toggling.
- **No hidden legacy ListView rebuilds:** only update the tray icon's maximum-temperature source value.
- **Tray icon deduplication:** if displayed numbers/color/font and tooltip have not changed, skip icon allocation and Windows shell updates. Tooltip-only updates avoid icon recreation.
- **Explorer restarts:** release old dynamic icon objects before creating replacements.

Hardware-critical sampling interval (the original `Cycle`), temperature/fan curves, read-error handling and BIOS fallback are unchanged.

### Repeatable benchmark

Build #11 is the pre-optimization reference; benchmark both versions on the same hardware and workload. Launch the app, let it settle for a few minutes, then run from a PowerShell prompt at the repository root:

```powershell
.\\tools\\measure-resources.ps1 -DurationSeconds 120 -IntervalSeconds 2 -OutputCsv before.csv
```

Repeat with the optimized build, first **with the dashboard visible**, then **hidden in the tray**. The report lists PID and Windows Session ID (the optional service and the interactive app may both be named `TPFanControl.exe`), mean private bytes/working set, process handles and CPU percentage relative to one core. The script does not change the service or fan settings. No before/after hardware measurements have been performed by CI.
