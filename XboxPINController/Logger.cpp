#include "Logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <shlwapi.h>

CRITICAL_SECTION Logger::s_cs;
bool Logger::s_initialized = false;
bool Logger::s_enabled = false;
ULONGLONG Logger::s_lastCheckTick = 0;
LogLevel Logger::s_minLevel = LOG_LEVEL_DEBUG;
wchar_t Logger::s_logFilePath[MAX_PATH] = L"C:\\Windows\\Temp\\XboxPINController.log";

void Logger::Initialize()
{
    if (!s_initialized)
    {
        InitializeCriticalSection(&s_cs);
        s_initialized = true;
        s_lastCheckTick = 0;
        CheckEnabledStatus();
        if (s_enabled)
        {
            Log(LOG_LEVEL_INFO, L"==================================================");
            Log(LOG_LEVEL_INFO, L"XboxPINController Logger inicializado. (PID: %lu)", GetCurrentProcessId());
            Log(LOG_LEVEL_INFO, L"==================================================");
        }
    }
}

void Logger::Shutdown()
{
    if (s_initialized)
    {
        if (s_enabled)
        {
            Log(LOG_LEVEL_INFO, L"XboxPINController Logger finalizado.");
        }
        EnterCriticalSection(&s_cs);
        s_initialized = false;
        LeaveCriticalSection(&s_cs);
        DeleteCriticalSection(&s_cs);
    }
}

bool Logger::IsEnabled()
{
    if (!s_initialized)
    {
        Initialize();
    }
    CheckEnabledStatus();
    return s_enabled;
}

std::wstring Logger::GetLogFilePath()
{
    return std::wstring(s_logFilePath);
}

void Logger::CheckEnabledStatus()
{
    ULONGLONG currentTick = GetTickCount64();
    // Cache registry and file check for 1.5 seconds to minimize overhead
    if (currentTick - s_lastCheckTick < 1500 && s_lastCheckTick != 0)
    {
        return;
    }
    s_lastCheckTick = currentTick;

    bool enabled = false;
    LogLevel minLevel = LOG_LEVEL_DEBUG;
    wchar_t customPath[MAX_PATH] = { 0 };

    // 1. Check Registry: HKLM\SOFTWARE\XboxPINController
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\XboxPINController", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD dwEnable = 0;
        DWORD dwType = 0;
        DWORD dwSize = sizeof(dwEnable);
        if (RegQueryValueExW(hKey, L"EnableLogging", NULL, &dwType, (LPBYTE)&dwEnable, &dwSize) == ERROR_SUCCESS)
        {
            enabled = (dwEnable != 0);
        }

        DWORD dwLevel = 0;
        dwSize = sizeof(dwLevel);
        if (RegQueryValueExW(hKey, L"LogLevel", NULL, &dwType, (LPBYTE)&dwLevel, &dwSize) == ERROR_SUCCESS)
        {
            minLevel = static_cast<LogLevel>(dwLevel);
        }

        dwSize = sizeof(customPath);
        if (RegQueryValueExW(hKey, L"LogFilePath", NULL, &dwType, (LPBYTE)customPath, &dwSize) == ERROR_SUCCESS && dwSize > 0)
        {
            wcscpy_s(s_logFilePath, MAX_PATH, customPath);
        }

        RegCloseKey(hKey);
    }

    // 2. Fallback check: flag file in C:\Windows\Temp
    if (!enabled)
    {
        if (GetFileAttributesW(L"C:\\Windows\\Temp\\XboxPIN_EnableLogging.txt") != INVALID_FILE_ATTRIBUTES)
        {
            enabled = true;
        }
    }

    s_enabled = enabled;
    s_minLevel = minLevel;
}

void Logger::Log(LogLevel level, const wchar_t* format, ...)
{
    if (!IsEnabled())
    {
        return;
    }

    if (level < s_minLevel)
    {
        return;
    }

    wchar_t buffer[2048];
    va_list args;
    va_start(args, format);
    vswprintf_s(buffer, ARRAYSIZE(buffer), format, args);
    va_end(args);

    WriteLogInternal(level, buffer);
}

void Logger::LogA(LogLevel level, const char* format, ...)
{
    if (!IsEnabled())
    {
        return;
    }

    if (level < s_minLevel)
    {
        return;
    }

    char bufferA[2048];
    va_list args;
    va_start(args, format);
    vsprintf_s(bufferA, ARRAYSIZE(bufferA), format, args);
    va_end(args);

    wchar_t bufferW[2048];
    MultiByteToWideChar(CP_UTF8, 0, bufferA, -1, bufferW, ARRAYSIZE(bufferW));

    WriteLogInternal(level, bufferW);
}

void Logger::WriteLogInternal(LogLevel level, const wchar_t* message)
{
    if (!s_initialized)
    {
        return;
    }

    EnterCriticalSection(&s_cs);

    // Get current system time with milliseconds
    SYSTEMTIME st;
    GetLocalTime(&st);

    const wchar_t* levelStr = L"INFO";
    switch (level)
    {
    case LOG_LEVEL_DEBUG: levelStr = L"DEBUG"; break;
    case LOG_LEVEL_INFO:  levelStr = L"INFO "; break;
    case LOG_LEVEL_WARN:  levelStr = L"WARN "; break;
    case LOG_LEVEL_ERROR: levelStr = L"ERROR"; break;
    }

    DWORD pid = GetCurrentProcessId();
    DWORD tid = GetCurrentThreadId();

    wchar_t formattedLine[2560];
    swprintf_s(
        formattedLine,
        ARRAYSIZE(formattedLine),
        L"[%04d-%02d-%02d %02d:%02d:%02d.%03d] [PID:%05lu TID:%05lu] [%s] %s\r\n",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        pid, tid,
        levelStr,
        message
    );

    // 1. Output to OutputDebugString for live debugging (DebugView / WinDbg)
    OutputDebugStringW(formattedLine);

    // 2. Output to log file
    FILE* pFile = nullptr;
    if (_wfopen_s(&pFile, s_logFilePath, L"a, ccs=UTF-8") == 0 && pFile != nullptr)
    {
        fputws(formattedLine, pFile);
        fflush(pFile);
        fclose(pFile);
    }

    LeaveCriticalSection(&s_cs);
}
