#pragma once

#include "CKRenderConfig.h"

#if CKRE_ENABLE_DEBUG_LOGGER

#include <cstdio>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

class CKDebugLogger {
public:
    static CKDebugLogger &Instance();
    static bool OutputEnabled();

    void EnableOutput(bool enable);
    void EnableDebuggerOutput(bool enable);
    void EnableFileOutput(bool enable);
    void SetLogFilePath(const char *path);

    void Log(const char *msg);
    void Logf(const char *fmt, ...);
    void LogTagged(const char *tag, const char *msg);
    void LogTaggedf(const char *tag, const char *fmt, ...);
    void Flush();

private:
    CKDebugLogger();
    ~CKDebugLogger();

    CKDebugLogger(const CKDebugLogger &) = delete;
    CKDebugLogger &operator=(const CKDebugLogger &) = delete;

    void OpenFileIfNeeded();
    bool IsOutputEnabled() const { return m_OutputEnabled; }

    char m_LogFilePath[MAX_PATH];
    bool m_OutputEnabled;
    bool m_DebuggerEnabled;
    bool m_FileEnabled;
    FILE *m_File;
    CRITICAL_SECTION m_CriticalSection;
};

#define CK_LOG_RAW(msg)                  do { if (CKDebugLogger::OutputEnabled()) CKDebugLogger::Instance().Log(msg); } while (0)
#define CK_LOG_RAW_FMT(fmt, ...)         do { if (CKDebugLogger::OutputEnabled()) CKDebugLogger::Instance().Logf(fmt, __VA_ARGS__); } while (0)
#define CK_LOG(category, msg)            do { if (CKDebugLogger::OutputEnabled()) CKDebugLogger::Instance().LogTagged(category, msg); } while (0)
#define CK_LOG_FMT(category, fmt, ...)   do { if (CKDebugLogger::OutputEnabled()) CKDebugLogger::Instance().LogTaggedf(category, fmt, __VA_ARGS__); } while (0)

#else

#define CK_LOG_RAW(msg)                  ((void)0)
#define CK_LOG_RAW_FMT(fmt, ...)         ((void)0)
#define CK_LOG(category, msg)            ((void)0)
#define CK_LOG_FMT(category, fmt, ...)   ((void)0)

#endif
