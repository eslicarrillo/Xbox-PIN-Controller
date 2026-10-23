#pragma once

#include <windows.h>
#include <string>

enum LogLevel
{
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_ERROR = 3
};

class Logger
{
public:
    static void Initialize();
    static void Shutdown();

    // Check if logging is currently enabled (via registry or flag file)
    static bool IsEnabled();

    // Log formatted wide-string message
    static void Log(LogLevel level, const wchar_t* format, ...);

    // Log formatted ANSI-string message
    static void LogA(LogLevel level, const char* format, ...);

    // Get current log file path
    static std::wstring GetLogFilePath();

private:
    static void WriteLogInternal(LogLevel level, const wchar_t* message);
    static void CheckEnabledStatus();

    static CRITICAL_SECTION s_cs;
    static bool s_initialized;
    static bool s_enabled;
    static ULONGLONG s_lastCheckTick;
    static LogLevel s_minLevel;
    static wchar_t s_logFilePath[MAX_PATH];
};

#define LOG_DEBUG(fmt, ...) Logger::Log(LOG_LEVEL_DEBUG, fmt, __VA_ARGS__)
#define LOG_INFO(fmt, ...)  Logger::Log(LOG_LEVEL_INFO,  fmt, __VA_ARGS__)
#define LOG_WARN(fmt, ...)  Logger::Log(LOG_LEVEL_WARN,  fmt, __VA_ARGS__)
#define LOG_ERROR(fmt, ...) Logger::Log(LOG_LEVEL_ERROR, fmt, __VA_ARGS__)
