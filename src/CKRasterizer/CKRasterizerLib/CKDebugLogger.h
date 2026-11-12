#pragma once

#include "CKRenderConfig.h"
#include "XString.h"

#include <cstdio>
#include <atomic>

#include "VxMutex.h"

class CKDebugLogger {
public:
    static CKDebugLogger &Instance();
    static bool OutputEnabled();
    static void WriteTagged(const char *tag, const char *message);
    static void WriteTaggedf(const char *tag, const char *format, ...);
    explicit CKDebugLogger(bool legacy);
    ~CKDebugLogger();
    bool IsOutputEnabled() const { return m_OutputEnabled; }
    void WriteFileLine(const char *message);

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

    CKDebugLogger(const CKDebugLogger &) = delete;
    CKDebugLogger &operator=(const CKDebugLogger &) = delete;

    void OpenFileIfNeeded();

    XString m_LogFilePath;
    std::atomic<bool> m_OutputEnabled;
    bool m_DebuggerEnabled;
    bool m_FileEnabled;
    FILE *m_File;
    bool m_Append;
    VxMutex m_Mutex;
};

#if CKRE_ENABLE_DEBUG_LOGGER
#define CK_LOG(category, msg)            do { if (CKDebugLogger::OutputEnabled()) CKDebugLogger::WriteTagged(category, msg); } while (0)
#define CK_LOG_FMT(category, fmt, ...)   do { if (CKDebugLogger::OutputEnabled()) CKDebugLogger::WriteTaggedf(category, fmt, __VA_ARGS__); } while (0)

#else

#define CK_LOG(category, msg)            ((void)0)
#define CK_LOG_FMT(category, fmt, ...)   ((void)0)

#endif
