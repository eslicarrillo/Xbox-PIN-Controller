#define DIRECTINPUT_VERSION 0x0800
#include "Gamepad.h"
#include "Logger.h"
#include <dinput.h>
#include <vector>
#include <string>
#include <chrono>
#include <mutex>
#include <algorithm>
#include <atomic>
#include <cstdlib>

#include <roapi.h>
#include <windows.gaming.input.h>
#include <wrl.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Gaming::Input;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;

// -------------------------------------------------------------------------
// Tuning
// -------------------------------------------------------------------------
static const int CAPTURE_TICK_MS            = 15;    // Polling period of the capture thread
static const int DI_REENUM_INTERVAL_MS      = 2500;  // Re-scan DirectInput devices (late USB init at boot, resume, mode switch)
static const int DI_DEVICE_LOST_MS          = 1500;  // Drop a DirectInput device that keeps failing for this long
static const int XINPUT_PROBE_INTERVAL_MS   = 1000;  // Probe empty XInput slots at this rate (probing empty slots is slow)
static const int XINPUT_ENABLE_INTERVAL_MS  = 1500;  // Periodically refresh XInputEnable(TRUE)
static const int ACTION_REFIRE_GUARD_MS     = 100;   // Minimum time between two key strokes of the same action (covers an engine seeing a tap late)
static const int DI_TRIGGER_PRESS_DELTA     = 20000; // DirectInput trigger: distance from rest to count as pressed
static const int DI_TRIGGER_RELEASE_DELTA   = 10000; // DirectInput trigger: distance from rest to count as released
static const int DI_CALIBRATION_STABLE_READS = 3;    // Consecutive stable reads required to accept the rest position
static const int DI_TRIGGER_STUCK_MS        = 5000;  // A trigger "held" this long means the rest position was wrong
static const double WGI_TRIGGER_PRESS       = 0.5;
static const double WGI_TRIGGER_RELEASE     = 0.3;
static const BYTE XINPUT_TRIGGER_RELEASE    = 20;    // Press uses XINPUT_GAMEPAD_TRIGGER_THRESHOLD (30)

static ULONGLONG NowMs()
{
    return (ULONGLONG)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Only the most recently started capture thread that is still running injects keystrokes.
// LogonUI can keep an old credential alive while it creates a new one (both would type),
// and can also destroy the newer one first (the survivor must take over, not go silent).
static std::mutex s_controllersMutex;
static std::vector<XInputController*> s_runningControllers; // last element = active

static XInputController* RegisterController(XInputController* c)
{
    std::lock_guard<std::mutex> lock(s_controllersMutex);
    XInputController* previous = s_runningControllers.empty() ? nullptr : s_runningControllers.back();
    s_runningControllers.erase(std::remove(s_runningControllers.begin(), s_runningControllers.end(), c), s_runningControllers.end());
    s_runningControllers.push_back(c);
    return previous;
}

static void UnregisterController(XInputController* c)
{
    std::lock_guard<std::mutex> lock(s_controllersMutex);
    s_runningControllers.erase(std::remove(s_runningControllers.begin(), s_runningControllers.end(), c), s_runningControllers.end());
}

static bool IsActiveController(XInputController* c)
{
    std::lock_guard<std::mutex> lock(s_controllersMutex);
    return !s_runningControllers.empty() && s_runningControllers.back() == c;
}

// Key identities (which digit, which raw button) are PIN material. They are written to the
// log only when HKLM\SOFTWARE\XboxPINController\LogKeys = 1 (testing with a throwaway PIN).
static bool LogKeysEnabled()
{
    static std::atomic<ULONGLONG> s_lastCheckMs{ 0 };
    static std::atomic<bool> s_value{ false };

    ULONGLONG now = NowMs();
    ULONGLONG last = s_lastCheckMs.load();
    if (last == 0 || now - last > 2000) {
        bool enabled = false;
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\XboxPINController", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD value = 0, size = sizeof(value), type = 0;
            if (RegQueryValueExW(hKey, L"LogKeys", nullptr, &type, (LPBYTE)&value, &size) == ERROR_SUCCESS && type == REG_DWORD) {
                enabled = (value != 0);
            }
            RegCloseKey(hKey);
        }
        s_value = enabled;
        s_lastCheckMs = now;
    }
    return s_value.load();
}

// -------------------------------------------------------------------------
// Logical PIN actions. Every engine reports a bitmask of these; the capture loop
// merges all engines and only types on the rising edge of the merged mask, so the
// same physical press seen by several engines/devices produces exactly one key.
// Bits 0-9 are the digits '0'-'9'.
// -------------------------------------------------------------------------
static const int ACTION_ENTER = 10;
static const int ACTION_BACK  = 11;
static const int ACTION_COUNT = 12;

static inline UINT32 ActionBit(int action) { return 1u << action; }

static WORD GetVkForAction(int action)
{
    if (action == ACTION_ENTER) return VK_RETURN;
    if (action == ACTION_BACK)  return VK_BACK;
    return (WORD)('0' + action);
}

static const wchar_t* GetActionName(int action)
{
    static const wchar_t* names[ACTION_COUNT] = {
        L"PIN '0'", L"PIN '1'", L"PIN '2'", L"PIN '3'", L"PIN '4'",
        L"PIN '5'", L"PIN '6'", L"PIN '7'", L"PIN '8'", L"PIN '9'",
        L"Enter", L"Borrar"
    };
    if (action < 0 || action >= ACTION_COUNT) return L"?";
    if (action < ACTION_ENTER && !LogKeysEnabled()) return L"PIN digito";
    return names[action];
}

// Where the keystrokes will land: tells "not read" apart from "read but delivered to nothing".
static void LogInjectionTarget()
{
    GUITHREADINFO gti = {};
    gti.cbSize = sizeof(gti);
    if (!GetGUIThreadInfo(0, &gti)) {
        LOG_DEBUG(L"Destino de teclas: GetGUIThreadInfo fallo (GetLastError=%lu)", GetLastError());
        return;
    }
    wchar_t cls[64] = L"-";
    HWND target = gti.hwndFocus ? gti.hwndFocus : gti.hwndActive;
    if (target) GetClassNameW(target, cls, ARRAYSIZE(cls));
    LOG_DEBUG(L"Destino de teclas: foco=%p activa=%p clase='%s'", gti.hwndFocus, gti.hwndActive, cls);
}

// -------------------------------------------------------------------------
// Keystroke injection with hardware scan codes (Required for Windows Hello PIN)
// -------------------------------------------------------------------------
static UINT InjectKey(WORD vk, WORD* pScanCode, DWORD* pError)
{
    WORD scanCode = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    *pScanCode = scanCode;

    INPUT in[2] = {};
    // Key down
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[0].ki.wScan = scanCode;
    in[0].ki.dwFlags = 0;
    if (scanCode & 0xE000) in[0].ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;

    // Key up
    in[1] = in[0];
    in[1].ki.dwFlags |= KEYEVENTF_KEYUP;

    UINT sent = SendInput(2, in, sizeof(INPUT));
    *pError = (sent == 2) ? ERROR_SUCCESS : GetLastError();
    return sent;
}

static std::wstring GetDesktopName(HDESK desktop)
{
    wchar_t name[64] = L"?";
    DWORD needed = 0;
    GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &needed);
    return name;
}

// The lock screen (before the PIN box exists) does not live on the desktop LogonUI's thread is
// attached to: SendInput and GetGUIThreadInfo fail with ERROR_ACCESS_DENIED there, so a press
// on the controller could not wake it. Attach a short-lived worker to the desktop that is
// receiving input right now and inject from it. SetThreadDesktop requires a thread that owns
// no windows or hooks, which is why the capture thread (DirectInput/WGI) is not used.
static UINT SendKeyOnInputDesktop(WORD vk, WORD* pScanCode, DWORD* pError)
{
    UINT sent = 0;
    DWORD error = ERROR_SUCCESS;
    std::thread worker([&] {
        HDESK input = OpenInputDesktop(0, FALSE, GENERIC_ALL);
        if (!input) {
            error = GetLastError();
            LOG_ERROR(L"SendKeyStroke: OpenInputDesktop fallo (GetLastError=%lu)", error);
            return;
        }
        if (!SetThreadDesktop(input)) {
            error = GetLastError();
            LOG_ERROR(L"SendKeyStroke: SetThreadDesktop('%s') fallo (GetLastError=%lu)", GetDesktopName(input).c_str(), error);
            CloseDesktop(input);
            return;
        }
        LOG_INFO(L"SendKeyStroke: Escritorio de entrada='%s'", GetDesktopName(input).c_str());
        sent = InjectKey(vk, pScanCode, &error);
        CloseDesktop(input);
    });
    worker.join();
    *pError = error;
    return sent;
}

static void SendKeyStroke(WORD vk)
{
    WORD scanCode = 0;
    DWORD error = ERROR_SUCCESS;
    UINT sent = InjectKey(vk, &scanCode, &error);
    if (sent != 2 && error == ERROR_ACCESS_DENIED)
    {
        LOG_WARN(L"SendKeyStroke: SendInput denegado en el escritorio '%s'; reintentando en el escritorio de entrada",
            GetDesktopName(GetThreadDesktop(GetCurrentThreadId())).c_str());
        sent = SendKeyOnInputDesktop(vk, &scanCode, &error);
    }
    if (sent != 2)
    {
        LOG_ERROR(L"SendKeyStroke: SendInput fallo (enviados %u/2, GetLastError=%lu)", sent, error);
        return;
    }
    if (vk >= '0' && vk <= '9' && !LogKeysEnabled()) {
        LOG_INFO(L"SendKeyStroke: Enviado digito (scan=0x%02X) - Retorno SendInput: %u", scanCode, sent);
    } else {
        LOG_INFO(L"SendKeyStroke: Enviado VK 0x%02X (scan=0x%02X) - Retorno SendInput: %u", vk, scanCode, sent);
    }
}

// -------------------------------------------------------------------------
// DirectInput 8 Engine (Direct hardware communication for Legion Go & HID)
// -------------------------------------------------------------------------
struct DIDeviceInfo {
    LPDIRECTINPUTDEVICE8 pDevice;
    GUID guidInstance;
    std::wstring name;
    bool isXInputDevice;     // Xbox-class HID ("IG_"): both triggers share the Z axis, XInput/WGI read them instead
    bool isCalibrated;
    int restLZ;
    int restLRz;
    LONG pendingLZ;
    LONG pendingLRz;
    int stableReads;
    bool ltDown;
    bool rtDown;
    ULONGLONG ltDownSince;
    ULONGLONG rtDownSince;
    UINT32 actions;          // Last known pressed actions (kept during short read failures)
    ULONGLONG lastGoodReadMs;
    BYTE prevButtons[128];   // Only used for raw-button logging (LogKeys)
    DWORD prevPov;
    bool everRead;           // Delivered at least one state; never-readable interfaces get a cooldown
};

// A DirectInput interface that never delivered a state (some Legion HID collections enumerate
// as joysticks but cannot be read) is not retried for this long, instead of every re-scan.
static const ULONGLONG DI_UNREADABLE_COOLDOWN_MS = 60000;
struct DICooldown {
    GUID guidInstance;
    ULONGLONG retryAtMs;
};

// A = Enter, B = Borrar, X = 0, Y = 9, LB = 7, RB = 8
static const int s_diButtonActions[] = { ACTION_ENTER, ACTION_BACK, 0, 9, 7, 8 };

class DirectInputEngine
{
public:
    ~DirectInputEngine()
    {
        for (auto& dev : _devices) ReleaseDevice(dev);
        _devices.clear();
        if (_pDI) {
            _pDI->Release();
            _pDI = nullptr;
        }
    }

    size_t DeviceCount() const { return _devices.size(); }

    // Adds devices that appeared since the last scan. Safe to call repeatedly.
    void Enumerate()
    {
        if (!_pDI) {
            HRESULT hr = DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8, (VOID**)&_pDI, NULL);
            if (FAILED(hr) || !_pDI) {
                LOG_WARN(L"[DirectInput Engine] Fallo DirectInput8Create (0x%08X)", hr);
                _pDI = nullptr;
                return;
            }
        }

        size_t before = _devices.size();
        HRESULT hr = _pDI->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumCallback, this, DIEDFL_ATTACHEDONLY);
        if (FAILED(hr)) {
            LOG_WARN(L"[DirectInput Engine] EnumDevices fallo (0x%08X)", hr);
        }
        if (_devices.size() != before) {
            LOG_INFO(L"[DirectInput Engine] %zu dispositivos DirectInput activos", _devices.size());
        }
    }

    UINT32 Poll(ULONGLONG nowMs)
    {
        UINT32 merged = 0;
        for (size_t i = 0; i < _devices.size(); ) {
            DIDeviceInfo& dev = _devices[i];

            HRESULT hr = dev.pDevice->Poll();
            if (FAILED(hr)) {
                dev.pDevice->Acquire();
                dev.pDevice->Poll();
            }

            DIJOYSTATE2 js = {};
            hr = dev.pDevice->GetDeviceState(sizeof(DIJOYSTATE2), &js);
            if (FAILED(hr)) {
                if (nowMs - dev.lastGoodReadMs > (ULONGLONG)DI_DEVICE_LOST_MS) {
                    if (dev.everRead) {
                        LOG_WARN(L"[DirectInput: %s] Dispositivo perdido (0x%08X), se volvera a enumerar", dev.name.c_str(), hr);
                    } else {
                        LOG_INFO(L"[DirectInput: %s] Interfaz ilegible (0x%08X), se ignora %llu s", dev.name.c_str(), hr,
                            DI_UNREADABLE_COOLDOWN_MS / 1000);
                        _cooldowns.push_back({ dev.guidInstance, nowMs + DI_UNREADABLE_COOLDOWN_MS });
                    }
                    ReleaseDevice(dev);
                    _devices.erase(_devices.begin() + i);
                    continue;
                }
                merged |= dev.actions;
                ++i;
                continue;
            }
            dev.lastGoodReadMs = nowMs;
            dev.everRead = true;

            if (LogKeysEnabled()) LogRawChanges(dev, js);

            // A device that was just acquired / re-plugged (mode switch) returns an all-zero state
            // until its first real report. Zeros would read as POV=UP plus both triggers pressed
            // (distance from the 32767 rest position), i.e. phantom '1', '5' and '6'.
            if (!(js.lX || js.lY || js.lZ || js.lRx || js.lRy || js.lRz)) {
                dev.actions = 0;
                ++i;
                continue;
            }

            UINT32 actions = 0;
            for (int b = 0; b < (int)ARRAYSIZE(s_diButtonActions); ++b) {
                if (js.rgbButtons[b] & 0x80) actions |= ActionBit(s_diButtonActions[b]);
            }

            DWORD pov = js.rgdwPOV[0];
            if (LOWORD(pov) != 0xFFFF) {
                if (pov == 0)          actions |= ActionBit(1); // UP
                else if (pov == 27000) actions |= ActionBit(2); // LEFT
                else if (pov == 18000) actions |= ActionBit(3); // DOWN
                else if (pov == 9000)  actions |= ActionBit(4); // RIGHT
            }

            if (!dev.isXInputDevice) {
                UpdateTriggers(dev, js, nowMs);
                if (dev.ltDown) actions |= ActionBit(5);
                if (dev.rtDown) actions |= ActionBit(6);
            }

            dev.actions = actions;
            merged |= actions;
            ++i;
        }
        return merged;
    }

private:
    LPDIRECTINPUT8 _pDI = nullptr;
    std::vector<DIDeviceInfo> _devices;
    std::vector<DICooldown> _cooldowns;

    bool InCooldown(const GUID& guid)
    {
        ULONGLONG now = NowMs();
        for (size_t i = 0; i < _cooldowns.size(); ) {
            if (now >= _cooldowns[i].retryAtMs) {
                _cooldowns.erase(_cooldowns.begin() + i);
            } else {
                if (IsEqualGUID(_cooldowns[i].guidInstance, guid)) return true;
                ++i;
            }
        }
        return false;
    }

    // Verifies the assumed button order (A,B,X,Y,LB,RB = buttons 0-5) on a given controller:
    // every raw button/POV change is logged, including the ones that have no PIN mapping.
    static void LogRawChanges(DIDeviceInfo& dev, const DIJOYSTATE2& js)
    {
        for (int b = 0; b < 128; ++b) {
            BYTE down = js.rgbButtons[b] & 0x80;
            if (down && !dev.prevButtons[b]) {
                LOG_DEBUG(L"[DirectInput: %s] Boton crudo %d presionado%s", dev.name.c_str(), b,
                    b < (int)ARRAYSIZE(s_diButtonActions) ? L"" : L" (sin mapeo)");
            }
            dev.prevButtons[b] = down;
        }
        if (js.rgdwPOV[0] != dev.prevPov) {
            LOG_DEBUG(L"[DirectInput: %s] POV crudo %lu", dev.name.c_str(), js.rgdwPOV[0]);
            dev.prevPov = js.rgdwPOV[0];
        }
    }

    static void ReleaseDevice(DIDeviceInfo& dev)
    {
        if (dev.pDevice) {
            dev.pDevice->Unacquire();
            dev.pDevice->Release();
            dev.pDevice = nullptr;
        }
    }

    static BOOL CALLBACK EnumCallback(const DIDEVICEINSTANCE* pdidInstance, VOID* pContext)
    {
        static_cast<DirectInputEngine*>(pContext)->AddDevice(pdidInstance);
        return DIENUM_CONTINUE;
    }

    void AddDevice(const DIDEVICEINSTANCE* pdidInstance)
    {
        for (const auto& dev : _devices) {
            if (IsEqualGUID(dev.guidInstance, pdidInstance->guidInstance)) return;
        }
        if (InCooldown(pdidInstance->guidInstance)) return;

        LPDIRECTINPUTDEVICE8 pDevice = nullptr;
        HRESULT hr = _pDI->CreateDevice(pdidInstance->guidInstance, &pDevice, NULL);
        if (FAILED(hr) || !pDevice) {
            LOG_WARN(L"[DirectInput Engine] CreateDevice fallo para %s (0x%08X)", pdidInstance->tszProductName, hr);
            return;
        }

        hr = pDevice->SetDataFormat(&c_dfDIJoystick2);
        if (FAILED(hr)) {
            LOG_WARN(L"[DirectInput Engine] SetDataFormat fallo para %s (0x%08X)", pdidInstance->tszProductName, hr);
            pDevice->Release();
            return;
        }

        hr = pDevice->SetCooperativeLevel(GetDesktopWindow(), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
        if (FAILED(hr)) {
            LOG_WARN(L"[DirectInput Engine] SetCooperativeLevel fallo para %s (0x%08X)", pdidInstance->tszProductName, hr);
        }
        pDevice->Acquire();

        DIDeviceInfo info = {};
        info.pDevice = pDevice;
        info.guidInstance = pdidInstance->guidInstance;
        info.name = pdidInstance->tszProductName;
        info.isXInputDevice = IsXInputDevice(pDevice);
        info.lastGoodReadMs = NowMs();
        info.prevPov = (DWORD)-1;
        _devices.push_back(info);

        LOG_INFO(L"[DirectInput Engine] Conectado a dispositivo: %s%s", pdidInstance->tszProductName,
            info.isXInputDevice ? L" (clase Xbox: gatillos via XInput/WGI)" : L"");
    }

    // Xbox-class controllers expose an "IG_" HID interface. Their triggers are merged on
    // one axis in DirectInput (RT would read as LT), so triggers are taken from XInput/WGI.
    static bool IsXInputDevice(LPDIRECTINPUTDEVICE8 pDevice)
    {
        DIPROPGUIDANDPATH gp = {};
        gp.diph.dwSize = sizeof(DIPROPGUIDANDPATH);
        gp.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        gp.diph.dwObj = 0;
        gp.diph.dwHow = DIPH_DEVICE;
        if (FAILED(pDevice->GetProperty(DIPROP_GUIDANDPATH, &gp.diph))) return false;
        _wcsupr_s(gp.wszPath, ARRAYSIZE(gp.wszPath));
        return wcsstr(gp.wszPath, L"IG_") != nullptr;
    }

    static void UpdateTriggers(DIDeviceInfo& dev, const DIJOYSTATE2& js, ULONGLONG nowMs)
    {
        // Calibrate the rest position only from real data: a freshly acquired device
        // returns an all-zero state until its first HID report arrives, and using that
        // as the rest position makes an idle trigger look pressed.
        if (!dev.isCalibrated) {
            bool hasData = js.lX || js.lY || js.lZ || js.lRx || js.lRy || js.lRz;
            if (!hasData) return;

            if (dev.stableReads > 0 &&
                abs(js.lZ - dev.pendingLZ) < 1000 && abs(js.lRz - dev.pendingLRz) < 1000) {
                dev.stableReads++;
            } else {
                dev.pendingLZ = js.lZ;
                dev.pendingLRz = js.lRz;
                dev.stableReads = 1;
            }

            if (dev.stableReads >= DI_CALIBRATION_STABLE_READS) {
                dev.restLZ = js.lZ;
                dev.restLRz = js.lRz;
                dev.isCalibrated = true;
                LOG_INFO(L"[DirectInput: %s] Gatillos calibrados (reposo LZ=%d LRz=%d)", dev.name.c_str(), dev.restLZ, dev.restLRz);
            }
            return;
        }

        UpdateTriggerAxis(dev, js.lZ, dev.restLZ, dev.ltDown, dev.ltDownSince, nowMs, L"LT");
        UpdateTriggerAxis(dev, js.lRz, dev.restLRz, dev.rtDown, dev.rtDownSince, nowMs, L"RT");
    }

    static void UpdateTriggerAxis(const DIDeviceInfo& dev, LONG value, int& rest, bool& down,
                                  ULONGLONG& downSince, ULONGLONG nowMs, const wchar_t* label)
    {
        int delta = abs(value - rest);
        if (!down && delta > DI_TRIGGER_PRESS_DELTA) {
            down = true;
            downSince = nowMs;
        } else if (down && delta < DI_TRIGGER_RELEASE_DELTA) {
            down = false;
        } else if (down && nowMs - downSince > (ULONGLONG)DI_TRIGGER_STUCK_MS) {
            LOG_WARN(L"[DirectInput: %s] Gatillo %s presionado %d ms sin soltar: recalibrando reposo (%d -> %d)",
                dev.name.c_str(), label, DI_TRIGGER_STUCK_MS, rest, value);
            rest = value;
            down = false;
        }
    }
};

// -------------------------------------------------------------------------
// XInput Multi-Module Engine
// -------------------------------------------------------------------------
typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD dwUserIndex, XINPUT_STATE *pState);
typedef void (WINAPI *PFN_XInputEnable)(BOOL enable);

struct XInputApiModule
{
    HMODULE hModule;
    PFN_XInputGetState pfnGetState;
    PFN_XInputGetState pfnGetStateEx;
    PFN_XInputEnable pfnEnable;
    const wchar_t* name;
};

static std::vector<XInputApiModule> s_xinputModules;
static std::once_flag s_xinputInitOnce;

static void InitXInputModules()
{
    std::call_once(s_xinputInitOnce, [] {
        const wchar_t* dllNames[] = {
            L"xinput1_4.dll",
            L"xinput1_3.dll",
            L"xinput9_1_0.dll"
        };

        for (const wchar_t* dllName : dllNames)
        {
            HMODULE hMod = LoadLibraryW(dllName);
            if (hMod)
            {
                XInputApiModule mod = {};
                mod.hModule = hMod;
                mod.name = dllName;
                mod.pfnGetState = (PFN_XInputGetState)GetProcAddress(hMod, "XInputGetState");
                mod.pfnEnable = (PFN_XInputEnable)GetProcAddress(hMod, "XInputEnable");

                if (wcscmp(dllName, L"xinput1_4.dll") == 0)
                {
                    mod.pfnGetStateEx = (PFN_XInputGetState)GetProcAddress(hMod, (LPCSTR)100);
                }
                else
                {
                    mod.pfnGetStateEx = nullptr;
                }

                s_xinputModules.push_back(mod);
                LOG_INFO(L"InitXInputModules: %s cargada", dllName);
            }
        }
    });
}

static DWORD MultiXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState)
{
    InitXInputModules();

    for (const auto& mod : s_xinputModules)
    {
        if (mod.pfnGetStateEx)
        {
            DWORD res = mod.pfnGetStateEx(dwUserIndex, pState);
            if (res == ERROR_SUCCESS) return ERROR_SUCCESS;
        }
        if (mod.pfnGetState)
        {
            DWORD res = mod.pfnGetState(dwUserIndex, pState);
            if (res == ERROR_SUCCESS) return ERROR_SUCCESS;
        }
    }
    return ERROR_DEVICE_NOT_CONNECTED;
}

static void MultiXInputEnable(BOOL enable)
{
    InitXInputModules();
    for (const auto& mod : s_xinputModules)
    {
        if (mod.pfnEnable) mod.pfnEnable(enable);
    }
}

static const struct { WORD button; int action; } s_xinputButtonActions[] = {
    { XINPUT_GAMEPAD_A,              ACTION_ENTER },
    { XINPUT_GAMEPAD_B,              ACTION_BACK },
    { XINPUT_GAMEPAD_DPAD_UP,        1 },
    { XINPUT_GAMEPAD_DPAD_LEFT,      2 },
    { XINPUT_GAMEPAD_DPAD_DOWN,      3 },
    { XINPUT_GAMEPAD_DPAD_RIGHT,     4 },
    { XINPUT_GAMEPAD_LEFT_SHOULDER,  7 },
    { XINPUT_GAMEPAD_RIGHT_SHOULDER, 8 },
    { XINPUT_GAMEPAD_X,              0 },
    { XINPUT_GAMEPAD_Y,              9 },
    { XINPUT_GAMEPAD_START,          0 },
    { XINPUT_GAMEPAD_LEFT_THUMB,     0 },
    { XINPUT_GAMEPAD_RIGHT_THUMB,    9 },
};

struct XInputSlotState
{
    bool connected;
    ULONGLONG lastProbeMs;
    bool ltDown;
    bool rtDown;
};

class XInputEngine
{
public:
    int ConnectedCount() const
    {
        int count = 0;
        for (const auto& slot : _slots) if (slot.connected) ++count;
        return count;
    }

    UINT32 Poll(ULONGLONG nowMs)
    {
        UINT32 merged = 0;
        for (DWORD idx = 0; idx < XUSER_MAX_COUNT; ++idx) {
            XInputSlotState& slot = _slots[idx];
            if (!slot.connected && slot.lastProbeMs != 0 &&
                nowMs - slot.lastProbeMs < (ULONGLONG)XINPUT_PROBE_INTERVAL_MS) {
                continue;
            }
            slot.lastProbeMs = nowMs;

            XINPUT_STATE state = {};
            if (MultiXInputGetState(idx, &state) != ERROR_SUCCESS) {
                if (slot.connected) {
                    LOG_INFO(L"[XInput Engine] [Slot %lu] Mando Xbox DESCONECTADO", idx);
                    slot = {};
                    slot.lastProbeMs = nowMs;
                }
                continue;
            }

            if (!slot.connected) {
                slot.connected = true;
                LOG_INFO(L"[XInput Engine] [Slot %lu] Mando Xbox CONECTADO (dwPacketNumber: %lu)", idx, state.dwPacketNumber);
            }

            UINT32 actions = 0;
            for (const auto& map : s_xinputButtonActions) {
                if (state.Gamepad.wButtons & map.button) actions |= ActionBit(map.action);
            }

            BYTE lt = state.Gamepad.bLeftTrigger;
            BYTE rt = state.Gamepad.bRightTrigger;
            if (lt > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) slot.ltDown = true;
            else if (lt < XINPUT_TRIGGER_RELEASE) slot.ltDown = false;
            if (rt > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) slot.rtDown = true;
            else if (rt < XINPUT_TRIGGER_RELEASE) slot.rtDown = false;
            if (slot.ltDown) actions |= ActionBit(5);
            if (slot.rtDown) actions |= ActionBit(6);

            merged |= actions;
        }
        return merged;
    }

private:
    XInputSlotState _slots[XUSER_MAX_COUNT] = {};
};

// -------------------------------------------------------------------------
// Windows.Gaming.Input Engine (Modern Handheld & Windows 11)
// -------------------------------------------------------------------------
static const struct { GamepadButtons button; int action; } s_wgiButtonActions[] = {
    { GamepadButtons_A,             ACTION_ENTER },
    { GamepadButtons_B,             ACTION_BACK },
    { GamepadButtons_DPadUp,        1 },
    { GamepadButtons_DPadLeft,      2 },
    { GamepadButtons_DPadDown,      3 },
    { GamepadButtons_DPadRight,     4 },
    { GamepadButtons_LeftShoulder,  7 },
    { GamepadButtons_RightShoulder, 8 },
    { GamepadButtons_X,             0 },
    { GamepadButtons_Y,             9 },
};

class WgiEngine
{
public:
    explicit WgiEngine(IGamepadStatics* statics) : _statics(statics) {}

    unsigned int GamepadCount() const { return _count; }

    UINT32 Poll()
    {
        if (!_statics) return 0;

        ComPtr<IVectorView<Gamepad*>> gamepads;
        if (FAILED(_statics->get_Gamepads(&gamepads)) || !gamepads) return 0;

        unsigned int count = 0;
        gamepads->get_Size(&count);
        if (count != _count) {
            LOG_INFO(L"[WGI Engine] %u mandos detectados via Windows.Gaming.Input", count);
            _count = count;
        }

        UINT32 merged = 0;
        for (unsigned int i = 0; i < count && i < ARRAYSIZE(_ltDown); ++i) {
            ComPtr<IGamepad> gamepad;
            if (FAILED(gamepads->GetAt(i, &gamepad)) || !gamepad) continue;

            GamepadReading reading = {};
            if (FAILED(gamepad->GetCurrentReading(&reading))) continue;

            UINT32 actions = 0;
            for (const auto& map : s_wgiButtonActions) {
                if ((reading.Buttons & map.button) != 0) actions |= ActionBit(map.action);
            }

            if (reading.LeftTrigger > WGI_TRIGGER_PRESS) _ltDown[i] = true;
            else if (reading.LeftTrigger < WGI_TRIGGER_RELEASE) _ltDown[i] = false;
            if (reading.RightTrigger > WGI_TRIGGER_PRESS) _rtDown[i] = true;
            else if (reading.RightTrigger < WGI_TRIGGER_RELEASE) _rtDown[i] = false;
            if (_ltDown[i]) actions |= ActionBit(5);
            if (_rtDown[i]) actions |= ActionBit(6);

            merged |= actions;
        }
        return merged;
    }

private:
    IGamepadStatics* _statics;
    unsigned int _count = 0;
    bool _ltDown[16] = {};
    bool _rtDown[16] = {};
};

// -------------------------------------------------------------------------
// XInputController class implementation
// -------------------------------------------------------------------------
XInputController::XInputController() : capturing_(false) {
    InitXInputModules();
}

XInputController::~XInputController() {
    StopCapturing();
}

bool XInputController::IsControllerConnected(DWORD dwUserIndex) {
    if (dwUserIndex >= XUSER_MAX_COUNT) return false;
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));
    DWORD result = MultiXInputGetState(dwUserIndex, &state);
    return (result == ERROR_SUCCESS);
}

XINPUT_STATE XInputController::GetControllerState(DWORD dwUserIndex) {
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));
    if (dwUserIndex < XUSER_MAX_COUNT) {
        MultiXInputGetState(dwUserIndex, &state);
    }
    return state;
}

void XInputController::StartCapturing(DWORD /*dwUserIndex*/) {
    if (!capturing_) {
        LOG_INFO(L"XInputController::StartCapturing: Iniciando motor triple (DirectInput + WGI + XInput)");
        XInputController* previous = RegisterController(this);
        if (previous && previous != this) {
            LOG_WARN(L"XInputController::StartCapturing: Otro hilo de captura seguia activo; deja de inyectar teclas");
        }
        capturing_ = true;
        captureThread_ = std::thread(&XInputController::CaptureInput, this);
    }
}

void XInputController::StopCapturing() {
    if (capturing_) {
        LOG_INFO(L"XInputController::StopCapturing: Deteniendo hilo de captura");
        capturing_ = false;
        if (captureThread_.joinable()) {
            captureThread_.join();
        }
        UnregisterController(this);
        LOG_INFO(L"XInputController::StopCapturing: Hilo de captura finalizado correctamente");
    }
}

void XInputController::CaptureInput() {
    LOG_INFO(L"XInputController::CaptureInput: Hilo activo ejecutandose (TID: %lu)", GetCurrentThreadId());

    {
        wchar_t exe[MAX_PATH] = L"?";
        GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe));
        wchar_t desktop[64] = L"?";
        DWORD needed = 0;
        GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, desktop, sizeof(desktop), &needed);
        DWORD session = (DWORD)-1;
        ProcessIdToSessionId(GetCurrentProcessId(), &session);
        LOG_INFO(L"XInputController::CaptureInput: Proceso='%s' Sesion=%lu Escritorio='%s'", exe, session, desktop);
    }

    // All engine state lives on this thread, so overlapping credentials cannot corrupt each other.
    // 1. DirectInput Engine (re-enumerated periodically below)
    DirectInputEngine diEngine;
    diEngine.Enumerate();

    // 2. Immediately enable XInput at startup
    MultiXInputEnable(TRUE);
    XInputEngine xinputEngine;

    // 3. Initialize WinRT for Windows.Gaming.Input
    HRESULT hrRo = RoInitialize(RO_INIT_MULTITHREADED);
    LOG_INFO(L"XInputController::CaptureInput: RoInitialize retorno 0x%08X", hrRo);

    UINT32 prevActions = 0;
    bool firstTick = true;
    ULONGLONG lastFireMs[ACTION_COUNT] = {};
    ULONGLONG nowMs = NowMs();
    ULONGLONG lastEnableMs = nowMs;
    ULONGLONG lastEnumMs = nowMs;
    size_t lastDiCount = (size_t)-1;
    int lastXiCount = -1;
    unsigned int lastWgiCount = (unsigned int)-1;

    {
        ComPtr<IGamepadStatics> wgiStatics;
        HRESULT hrStatics = RoGetActivationFactory(
            HStringReference(RuntimeClass_Windows_Gaming_Input_Gamepad).Get(),
            IID_PPV_ARGS(&wgiStatics)
        );
        if (SUCCEEDED(hrStatics))
        {
            LOG_INFO(L"XInputController::CaptureInput: Windows.Gaming.Input (WGI) inicializado exitosamente");
        }
        else
        {
            LOG_WARN(L"XInputController::CaptureInput: No se pudo inicializar WGI (0x%08X)", hrStatics);
        }
        WgiEngine wgiEngine(wgiStatics.Get());

        while (capturing_) {
            nowMs = NowMs();

            // Periodically refresh XInput enable state
            if (nowMs - lastEnableMs >= (ULONGLONG)XINPUT_ENABLE_INTERVAL_MS) {
                MultiXInputEnable(TRUE);
                lastEnableMs = nowMs;
            }

            // Pick up controllers that were not ready when LogonUI started
            if (nowMs - lastEnumMs >= (ULONGLONG)DI_REENUM_INTERVAL_MS) {
                diEngine.Enumerate();
                lastEnumMs = nowMs;
            }

            UINT32 diActions = diEngine.Poll(nowMs);
            UINT32 wgiActions = wgiEngine.Poll();
            UINT32 xiActions = xinputEngine.Poll(nowMs);
            UINT32 allActions = diActions | wgiActions | xiActions;

            // Type only on the rising edge of the merged state: one key per physical press,
            // no auto-repeat while held, no duplicates across engines/devices.
            UINT32 newlyPressed = firstTick ? 0 : (allActions & ~prevActions);
            prevActions = allActions;
            firstTick = false;

            if (newlyPressed) {
                bool isActive = IsActiveController(this);
                for (int a = 0; a < ACTION_COUNT; ++a) {
                    if (!(newlyPressed & ActionBit(a))) continue;
                    if (nowMs - lastFireMs[a] < (ULONGLONG)ACTION_REFIRE_GUARD_MS) continue;
                    lastFireMs[a] = nowMs;

                    if (!isActive) {
                        LOG_DEBUG(L"XInputController::CaptureInput: %s ignorado (hilo de captura inactivo)", GetActionName(a));
                        continue;
                    }

                    LOG_INFO(L"Accion %s [DirectInput=%d WGI=%d XInput=%d]", GetActionName(a),
                        (diActions & ActionBit(a)) ? 1 : 0,
                        (wgiActions & ActionBit(a)) ? 1 : 0,
                        (xiActions & ActionBit(a)) ? 1 : 0);
                    if (LogKeysEnabled()) LogInjectionTarget();
                    SendKeyStroke(GetVkForAction(a));
                }
            }

            // Summary whenever the set of visible controllers changes (diagnoses "loads but types nothing")
            size_t diCount = diEngine.DeviceCount();
            int xiCount = xinputEngine.ConnectedCount();
            unsigned int wgiCount = wgiEngine.GamepadCount();
            if (diCount != lastDiCount || xiCount != lastXiCount || wgiCount != lastWgiCount) {
                LOG_INFO(L"XInputController::CaptureInput: Mandos visibles -> DirectInput=%zu XInput=%d WGI=%u",
                    diCount, xiCount, wgiCount);
                if (diCount == 0 && xiCount == 0 && wgiCount == 0) {
                    LOG_WARN(L"XInputController::CaptureInput: Ningun mando visible para LogonUI (HidHide oculto / mando aun no inicializado?)");
                }
                lastDiCount = diCount;
                lastXiCount = xiCount;
                lastWgiCount = wgiCount;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(CAPTURE_TICK_MS));
        }
    }

    if (SUCCEEDED(hrRo)) {
        RoUninitialize();
    }

    LOG_INFO(L"XInputController::CaptureInput: Saliendo del bucle de captura");
}
