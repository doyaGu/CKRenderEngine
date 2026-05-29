#include "CKDebugLogger.h"
#include "CKRenderSettings.h"
#include "VxWindowFunctions.h"

#include <cstdarg>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

static XString CKDebugLoggerSiblingFile(const char *path, const char *file) {
    if (!path || !file)
        return "";

    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *last = slash;
    if (!last || (backslash && backslash > last))
        last = backslash;
    if (!last)
        return file;

    XString sibling(path, (int)(last - path + 1));
    sibling << file;
    return sibling;
}

static XString CKDebugLoggerModuleSiblingFile(const void *address, const char *file) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(address), &module))
        return "";

    XString modulePath = VxGetModuleFileName((INSTANCE_HANDLE)module);
    return CKDebugLoggerSiblingFile(modulePath.CStr(), file);
}

CKDebugLogger &CKDebugLogger::Instance() {
    static CKDebugLogger instance;
    return instance;
}

bool CKDebugLogger::OutputEnabled() {
    return Instance().IsOutputEnabled();
}

CKDebugLogger::CKDebugLogger()
    : m_OutputEnabled(CKRenderDebugSettings().GetBool("Output", false)),
      m_DebuggerEnabled(true),
      m_FileEnabled(true),
      m_File(nullptr) {
    InitializeCriticalSection(&m_CriticalSection);

    m_LogFilePath = CKDebugLoggerModuleSiblingFile((const void *)&CKDebugLogger::Instance, "CK2_3D_Debug.log");
    if (m_LogFilePath.Length() == 0)
        m_LogFilePath = "CK2_3D_Debug.log";

    XString configPath;
    if (CKRenderDebugSettings().GetString("LogPath", configPath))
        m_LogFilePath = configPath;
}

CKDebugLogger::~CKDebugLogger() {
    EnterCriticalSection(&m_CriticalSection);
    if (m_File) {
        fflush(m_File);
        fclose(m_File);
        m_File = nullptr;
    }
    LeaveCriticalSection(&m_CriticalSection);
    DeleteCriticalSection(&m_CriticalSection);
}

void CKDebugLogger::EnableOutput(bool enable) {
    EnterCriticalSection(&m_CriticalSection);
    m_OutputEnabled = enable;
    if (!m_OutputEnabled && m_File) {
        fclose(m_File);
        m_File = nullptr;
    }
    LeaveCriticalSection(&m_CriticalSection);
}

void CKDebugLogger::EnableDebuggerOutput(bool enable) {
    EnterCriticalSection(&m_CriticalSection);
    m_DebuggerEnabled = enable;
    LeaveCriticalSection(&m_CriticalSection);
}

void CKDebugLogger::EnableFileOutput(bool enable) {
    EnterCriticalSection(&m_CriticalSection);
    m_FileEnabled = enable;
    if (!m_FileEnabled && m_File) {
        fclose(m_File);
        m_File = nullptr;
    }
    LeaveCriticalSection(&m_CriticalSection);
}

void CKDebugLogger::SetLogFilePath(const char *path) {
    if (!path || !*path) {
        return;
    }

    EnterCriticalSection(&m_CriticalSection);
    m_LogFilePath = path;
    if (m_File) {
        fclose(m_File);
        m_File = nullptr;
    }
    LeaveCriticalSection(&m_CriticalSection);
}

void CKDebugLogger::Log(const char *msg) {
    if (!msg) {
        return;
    }

    EnterCriticalSection(&m_CriticalSection);

    if (!m_OutputEnabled) {
        LeaveCriticalSection(&m_CriticalSection);
        return;
    }

    if (m_DebuggerEnabled) {
        OutputDebugStringA(msg);
        OutputDebugStringA("\n");
    }

    if (m_FileEnabled) {
        OpenFileIfNeeded();
        if (m_File) {
            fprintf(m_File, "%s\n", msg);
            fflush(m_File);
        }
    }

    LeaveCriticalSection(&m_CriticalSection);
}

void CKDebugLogger::Logf(const char *fmt, ...) {
    if (!fmt) {
        return;
    }

    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, args);
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
    _vsnprintf_s(messageBuffer, sizeof(messageBuffer), _TRUNCATE, fmt, args);
    va_end(args);

    Logf("[CK2_3D] [%s] %s", safeTag, messageBuffer);
}

void CKDebugLogger::Flush() {
    EnterCriticalSection(&m_CriticalSection);
    if (m_File) {
        fflush(m_File);
    }
    LeaveCriticalSection(&m_CriticalSection);
}

void CKDebugLogger::OpenFileIfNeeded() {
    if (m_File || !m_FileEnabled) {
        return;
    }

    fopen_s(&m_File, m_LogFilePath.CStr(), "w");
}
