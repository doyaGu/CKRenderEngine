#include "CKDebugLogger.h"

#include <cstdarg>
#include <cstdlib>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#ifndef CKRE_DEBUG_OUTPUT_DEFAULT
#define CKRE_DEBUG_OUTPUT_DEFAULT 1
#endif

static bool EnvDebugOutputEnabled() {
    const char *value = getenv("CK2_3D_DEBUG_OUTPUT");
    if (!value || !*value)
        return CKRE_DEBUG_OUTPUT_DEFAULT != 0;
    return value[0] != '0' &&
           _stricmp(value, "false") != 0 &&
           _stricmp(value, "off") != 0 &&
           _stricmp(value, "no") != 0;
}

CKDebugLogger &CKDebugLogger::Instance() {
    static CKDebugLogger instance;
    return instance;
}

CKDebugLogger::CKDebugLogger()
    : m_OutputEnabled(EnvDebugOutputEnabled()),
      m_DebuggerEnabled(true),
      m_FileEnabled(true),
      m_File(nullptr) {
    InitializeCriticalSection(&m_CriticalSection);
    m_LogFilePath[0] = '\0';

    char modulePath[MAX_PATH] = {0};
    HMODULE module = nullptr;

    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&CKDebugLogger::Instance), &module)) {
        if (GetModuleFileNameA(module, modulePath, MAX_PATH) > 0) {
            char drive[_MAX_DRIVE] = {0};
            char dir[_MAX_DIR] = {0};
            _splitpath_s(modulePath, drive, _MAX_DRIVE, dir, _MAX_DIR, nullptr, 0, nullptr, 0);

            char logPath[MAX_PATH] = {0};
            _makepath_s(logPath, MAX_PATH, drive, dir, "CK2_3D_Debug", "log");
            strncpy_s(m_LogFilePath, MAX_PATH, logPath, _TRUNCATE);
        }
    }

    if (m_LogFilePath[0] == '\0') {
        strncpy_s(m_LogFilePath, MAX_PATH, "CK2_3D_Debug.log", _TRUNCATE);
    }

    const char *envPath = getenv("CK2_3D_LOG");
    if (envPath && *envPath) {
        strncpy_s(m_LogFilePath, MAX_PATH, envPath, _TRUNCATE);
    }
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
    strncpy_s(m_LogFilePath, MAX_PATH, path, _TRUNCATE);
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

    fopen_s(&m_File, m_LogFilePath, "w");
}
