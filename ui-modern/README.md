# TPFanCtrl2 Modern UI (Windows 11)

Experimental **native Win32** dashboard that attaches to the existing TPFanCtrl2 engine via the `Global\\TPFanControl_State` shared memory mapping. It does not read/write EC ports or load PawnIO and does not replace the engine/service.

Features:
- Windows 11 styled custom drawn dashboard, automatically following the system light/dark preference.
- DPI-aware temperature and fan RPM summaries and twelve EC sensor readings.
- BIOS, Smart and Manual mode commands via the existing engine protocol, with acknowledgement tracking.
- Manual levels intentionally restricted to 1-7; zero (stop fan) is not offered.
- Keyboard: B/S/M to switch modes, arrow keys to adjust manual level, F5 to refresh.
- No Electron, WebView, Qt, MFC, or other third-party UI frameworks.

## Build

Requires Windows SDK, MSVC C++ and CMake.

```powershell
cmake -S ui-modern -B build-modern -A Win32
cmake --build build-modern --config Release --parallel
```

Run the original `TPFanControl` service before starting `build-modern/Release/TPFanCtrlModern.exe`. Closing the UI will not stop the service.

## Known limitations / safety

- **Experimental, untested on real ThinkPad hardware.** Keep the original UI/service and use BIOS control as fallback.
- Does not edit the fan curve or manage startup yet. Use the original tool and `TPFanControl.ini`.
- Sensor names are generic; each model's EC registers may differ. A visible sensor is not proof the measurement is supported.
- This is a separate dashboard, not a replacement tray window. For the first iteration, only its UI is new.
- Shared memory is the legacy upstream protocol. Do not run incompatible competing fan-controller programs.
- Control commands are not considered confirmed until the service acknowledges them; communication can still fail.
- Its UI is responsible for selection only; hardware safeguards remain the responsibility of the existing engine.

Upstream: [mews-se/TPFanCtrl2](https://github.com/mews-se/TPFanCtrl2) (Unlicense).
