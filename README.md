# 🎮 Xbox-PIN-Controller (fork)

🌐 **English** · [Español](README.es.md)

Type your **Windows Hello PIN on the lock / sign-in screen with a game controller**, using a button layout inspired by the Xbox console passkey screen.

This is a fork of [MikeCoder96/Xbox-PIN-Controller](https://github.com/MikeCoder96/Xbox-PIN-Controller). It was reworked and tested on a **Lenovo Legion Go** (built-in controllers, with and without Handheld Companion), on Windows 11 x64. Other controllers and devices follow the same code paths but have not been tested.

> ⚠️ **Read before installing.** `XboxPINController.dll` is a Credential Provider: Windows loads it into `LogonUI.exe` and it runs as **SYSTEM** on the sign-in screen, where it reads your controller and injects the keystrokes of your PIN. Only install a DLL you trust. Prefer building it yourself (see below) or at least verify the SHA-256 of the release file.

## What it does

- **Any controller button wakes the lock screen** (the first press only wakes it), then the PIN box takes the digits.
- Reads the controller with **DirectInput, Windows.Gaming.Input and XInput** at the same time, so it works on handhelds whose built-in pad is HID-only or XInput-only. One physical press types exactly one digit even if several engines or devices see it.
- Works on a **cold boot** (no Handheld Companion running) and on **Win+L**.
- Embedded on-screen guide with the button → digit mapping.
- Optional diagnostic logging (off by default).

| Controller | Types |
|---|---|
| D-Pad ↑ ← ↓ → | `1` `2` `3` `4` |
| LT / RT | `5` / `6` |
| LB / RB | `7` / `8` |
| Y / X | `9` / `0` |
| A | Enter (sign in) |
| B | Backspace |

## Install from a release (no compiler needed)

1. Open the [latest release](../../releases/latest) and download **`XboxPINController.dll`**, **`install.ps1`** and **`uninstall.ps1`** into the same folder.
2. Verify the DLL: `Get-FileHash .\XboxPINController.dll -Algorithm SHA256` must match the hash in the release notes.
3. Right-click `install.ps1` → *Run with PowerShell* (it asks for Administrator rights by itself). It copies the DLL to `C:\Windows\System32`, registers the Credential Provider, sets the Windows 11 controller-navigation compatibility value, and, **if [HidHide](https://github.com/nefarius/HidHide) is installed, adds `LogonUI.exe` to its application whitelist** so the sign-in screen can see the controller (Handheld Companion keeps HidHide cloaking the physical pad otherwise).
4. Lock the PC (Win+L) and press a button on the controller.

**Uninstall:** run `uninstall.ps1` (Administrator). It removes the DLL, the registry entries, restores the compatibility value, and removes `LogonUI.exe` from the HidHide whitelist.

Keep another way to sign in available (keyboard, touch, password) while testing. A wrong PIN counts toward the Windows Hello lockout like any other.

## Build from source

Requires Visual Studio Build Tools 2022 (C++ workload, toolset `v143`) and Windows SDK 10.0.26100.0:

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.Windows11SDK.26100 --includeRecommended"
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" "Xbox Pin Controller.sln" /p:Configuration=Release /p:Platform=x64
```

The output is `x64\Release\XboxPINController.dll`; `install.ps1` picks it up from there.

## Logging & diagnostics

Logging is off by default.

- `enable-logging.ps1` / `disable-logging.ps1` (Administrator) toggle it. `view-log.ps1` shows `C:\Windows\Temp\XboxPINController.log`; it is written by SYSTEM, so reading it needs Administrator.
- Live output also goes to `OutputDebugStringW` (Sysinternals DebugView).
- By default the log does **not** record which digit or button was pressed. `HKLM\SOFTWARE\XboxPINController\LogKeys = 1` records them, which is the same as logging your PIN: use it only with a throwaway PIN and turn it off afterwards.

## Documentation

The technical write-up is in Spanish:

- [`SOLUCION_Y_ARQUITECTURA.md`](SOLUCION_Y_ARQUITECTURA.md): root causes (the lock screen takes input on the `Default` desktop, HidHide hiding the pad from LogonUI, the Legion Go switching USB modes, all-zero DirectInput states), architecture and log reference.
- [`DOCUMENTACION.md`](DOCUMENTACION.md): original technical documentation, with an update note at the top of the changelog.
- [`PENDIENTES.md`](PENDIENTES.md): open items and untested scenarios.

## Credits and license

- Original project: **MikeCoder96 & Contributors** ([upstream repository](https://github.com/MikeCoder96/Xbox-PIN-Controller)). The credential provider skeleton derives from Microsoft's Windows Credential Provider sample (Copyright © Microsoft Corporation).
- **License:** the upstream repository does not declare a license, so no license is granted here either: all rights in the original code remain with its authors. The changes in this fork are published for reference and personal use; if you want to redistribute or reuse it, please ask the upstream author.
- `combo.png` is an original illustration of the button layout, inspired by the Xbox console passkey screen. Xbox is a trademark of Microsoft; this project is not affiliated with or endorsed by Microsoft or Xbox.
