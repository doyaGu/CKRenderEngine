#pragma once

#include "CKRenderConfig.h"
#include "XString.h"

#if CKRE_ENABLE_DEBUG_LOGGER

#include <cstdio>

#include "VxMutex.h"

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

    XString m_LogFilePath;
    bool m_OutputEnabled;
    bool m_DebuggerEnabled;
    bool m_FileEnabled;
    FILE *m_File;
    VxMutex m_Mutex;
};

#define CK_LOG(category, msg)            do { if (CKDebugLogger::OutputEnabled()) CKDebugLogger::Instance().LogTagged(category, msg); } while (0)
#define CK_LOG_FMT(category, fmt, ...)   do { if (CKDebugLogger::OutputEnabled()) CKDebugLogger::Instance().LogTaggedf(category, fmt, __VA_ARGS__); } while (0)

#else

#define CK_LOG(category, msg)            ((void)0)
#define CK_LOG_FMT(category, fmt, ...)   ((void)0)

#endif
