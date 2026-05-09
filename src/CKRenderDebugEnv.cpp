#include "CKRenderDebugEnv.h"

#include <climits>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
#include <strings.h>
#endif

static int CKRenderDebugStricmp(const char *a, const char *b) {
#if defined(_WIN32)
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif
}

bool CKRenderDebugParseBool(const char *value, bool fallback) {
    if (!value || value[0] == '\0')
        return fallback;

    if (CKRenderDebugStricmp(value, "1") == 0 ||
        CKRenderDebugStricmp(value, "true") == 0 ||
        CKRenderDebugStricmp(value, "on") == 0 ||
        CKRenderDebugStricmp(value, "yes") == 0) {
        return true;
    }

    if (CKRenderDebugStricmp(value, "0") == 0 ||
        CKRenderDebugStricmp(value, "false") == 0 ||
        CKRenderDebugStricmp(value, "off") == 0 ||
        CKRenderDebugStricmp(value, "no") == 0) {
        return false;
    }

    return fallback;
}

bool CKRenderDebugEnvString(const char *name, char *buffer, CKDWORD bufferSize) {
    if (!name || !buffer || bufferSize == 0)
        return false;

    buffer[0] = '\0';
#if defined(_WIN32)
    DWORD n = GetEnvironmentVariableA(name, buffer, (DWORD)bufferSize);
    if (n == 0 || n >= bufferSize) {
        buffer[0] = '\0';
        return false;
    }
    return true;
#else
    const char *value = std::getenv(name);
    if (!value || value[0] == '\0')
        return false;
    std::strncpy(buffer, value, bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
    return true;
#endif
}

bool CKRenderDebugEnvBool(const char *name, bool fallback) {
    char value[32];
    if (!CKRenderDebugEnvString(name, value, (CKDWORD)sizeof(value)))
        return fallback;
    return CKRenderDebugParseBool(value, fallback);
}

int CKRenderDebugEnvInt(const char *name, int fallback) {
    char value[64];
    if (!CKRenderDebugEnvString(name, value, (CKDWORD)sizeof(value)))
        return fallback;

    char *end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (end == value)
        return fallback;
    if (parsed < INT_MIN)
        return INT_MIN;
    if (parsed > INT_MAX)
        return INT_MAX;
    return (int)parsed;
}

CKDWORD CKRenderDebugEnvDword(const char *name, CKDWORD fallback) {
    char value[64];
    if (!CKRenderDebugEnvString(name, value, (CKDWORD)sizeof(value)))
        return fallback;

    char *end = nullptr;
    unsigned long parsed = std::strtoul(value, &end, 10);
    return (end != value) ? (CKDWORD)parsed : fallback;
}
