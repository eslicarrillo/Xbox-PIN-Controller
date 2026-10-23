#pragma once
#include <windows.h>
#include <xinput.h>
#include <thread>
#include <atomic>
#pragma comment(lib, "xinput.lib")

// Define XInput controller manager with full multi-controller support
class XInputController {
public:
    XInputController();
    ~XInputController();

    bool IsControllerConnected(DWORD dwUserIndex);
    XINPUT_STATE GetControllerState(DWORD dwUserIndex);

    void StartCapturing(DWORD dwUserIndex = 0);
    void StopCapturing();

private:
    std::atomic<bool> capturing_;
    std::thread captureThread_;

    void CaptureInput();
};
