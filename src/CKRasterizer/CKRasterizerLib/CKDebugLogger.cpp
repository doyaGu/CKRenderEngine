#include "CKDebugLogger.h"
#include "CKRenderSettings.h"
#include "CKRenderDiagnosticsContext.h"

#include <cstdarg>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

static void CKDebugLoggerWriteDebugger(const char *msg) {
#ifdef _WIN32
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
#else
    fprintf(stderr, "%s\n", msg);
#endif
}

CKDebugLogger &CKDebugLogger::Instance() {
    if (const auto *context = CKRenderCurrentDiagnostics())
        return *context->Logger(context->Owner);
    static CKDebugLogger instance;
    return instance;
}

bool CKDebugLogger::OutputEnabled() {
    if (const auto *context = CKRenderCurrentDiagnostics())
        return context->OutputEnabled(context->Owner);
    return Instance().IsOutputEnabled();
}

CKDebugLogger::CKDebugLogger() : CKDebugLogger(true) {}

CKDebugLogger::CKDebugLogger(bool legacy)
    : m_OutputEnabled(CKRenderDebugSettings().GetBool("Output", false)),
      m_DebuggerEnabled(true),
      m_FileEnabled(true),
      m_File(nullptr), m_Append(!legacy) {
    m_LogFilePath = CKRenderModuleSiblingFile((const void *)&CKDebugLogger::Instance, "CK2_3D_Debug.log");
    if (m_LogFilePath.Length() == 0)
        m_LogFilePath = "CK2_3D_Debug.log";

    XString configPath;
    if (CKRenderDebugSettings().GetString("LogPath", configPath))
        m_LogFilePath = configPath;
}

CKDebugLogger::~CKDebugLogger() {
    VxMutexLock lock(m_Mutex);
    if (m_File) {
        fflush(m_File);
        fclose(m_File);
        m_File = nullptr;
    }
}

void CKDebugLogger::EnableOutput(bool enable) {
    VxMutexLock lock(m_Mutex);
    m_OutputEnabled = enable;
    if (!m_OutputEnabled && m_File) {
        fclose(m_File);
        m_File = nullptr;
    }
}

void CKDebugLogger::EnableDebuggerOutput(bool enable) {
    VxMutexLock lock(m_Mutex);
    m_DebuggerEnabled = enable;
}

void CKDebugLogger::EnableFileOutput(bool enable) {
    VxMutexLock lock(m_Mutex);
    m_FileEnabled = enable;
    if (!m_FileEnabled && m_File) {
        fclose(m_File);
        m_File = nullptr;
    }
}

void CKDebugLogger::SetLogFilePath(const char *path) {
    if (!path || !*path) {
        return;
    }

    VxMutexLock lock(m_Mutex);
    m_LogFilePath = path;
    if (m_File) {
        fclose(m_File);
        m_File = nullptr;
    }
}

void CKDebugLogger::Log(const char *msg) {
    if (!msg) {
        return;
    }

    VxMutexLock lock(m_Mutex);

    if (!m_OutputEnabled) {
        return;
    }

    if (m_DebuggerEnabled) {
        CKDebugLoggerWriteDebugger(msg);
    }

    if (m_FileEnabled) {
        OpenFileIfNeeded();
        if (m_File) {
            fprintf(m_File, "%s\n", msg);
            fflush(m_File);
        }
    }
}

void CKDebugLogger::Logf(const char *fmt, ...) {
    if (!fmt) {
        return;
    }

    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    Log(buffer);
}

void CKDebugLogger::LogTagged(const char *tag, const char *msg) {
    const char *safeTag = (tag && *tag) ? tag : "General";
    const char *safeMsg = msg ? msg : "";
    Logf("[CK2_3D] [%s] %s", safeTag, safeMsg);
}

void CKDebugLogger::LogTaggedf(const char *tag, const char *fmt, ...) {
    if (!fmt) {
        return;
    }

    const char *safeTag = (tag && *tag) ? tag : "General";

    char messageBuffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(messageBuffer, sizeof(messageBuffer), fmt, args);
    va_end(args);

    Logf("[CK2_3D] [%s] %s", safeTag, messageBuffer);
}

void CKDebugLogger::Flush() {
    VxMutexLock lock(m_Mutex);
    if (m_File) {
        fflush(m_File);
    }
}

void CKDebugLogger::OpenFileIfNeeded() {
    if (m_File || !m_FileEnabled) {
        return;
    }

    m_File = fopen(m_LogFilePath.CStr(), m_Append ? "a" : "w");
}

void CKDebugLogger::WriteTagged(const char *tag, const char *message) {
    if (const auto *context = CKRenderCurrentDiagnostics())
        context->Write(context->Owner, tag, message, false);
    else Instance().LogTagged(tag, message);
}

void CKDebugLogger::WriteTaggedf(const char *tag, const char *format, ...) {
    if (!format) return;
    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    WriteTagged(tag, message);
}

void CKDebugLogger::WriteFileLine(const char *message) {
    if (!message) return;
    VxMutexLock lock(m_Mutex);
    if (!m_File) m_File = fopen(m_LogFilePath.CStr(), "a");
    if (m_File) { fprintf(m_File, "%s\n", message); fflush(m_File); }
}
