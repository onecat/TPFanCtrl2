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
