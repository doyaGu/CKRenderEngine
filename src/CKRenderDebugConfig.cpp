#include "CKRenderDebugConfig.h"
#include "VxConfiguration.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <climits>
#include <cstdlib>
#include <cstring>

namespace {

const char *kRenderDebugConfigFile = "CK2_3D.ini";
const char *kRenderDebugConfigSection = "CK2_3D";
const int kRenderDebugOverrideCount = 64;
const int kRenderDebugOverrideNameSize = 128;
const int kRenderDebugOverrideValueSize = 256;

struct CKRenderDebugConfigOverride {
    bool Used;
    char Name[kRenderDebugOverrideNameSize];
    char Value[kRenderDebugOverrideValueSize];
};

CKRenderDebugConfigOverride g_RenderDebugOverrides[kRenderDebugOverrideCount] = {};

int CKRenderDebugConfigStricmp(const char *a, const char *b) {
    return _stricmp(a, b);
}

bool CKRenderDebugConfigCopyString(char *buffer, CKDWORD bufferSize, const char *value) {
    if (!buffer || bufferSize == 0)
        return false;

    buffer[0] = '\0';
    if (!value || value[0] == '\0')
        return false;

    strncpy_s(buffer, bufferSize, value, _TRUNCATE);
    return buffer[0] != '\0';
}

bool CKRenderDebugConfigLoadFile(VxConfiguration &config, const char *path) {
    if (!path || path[0] == '\0')
        return false;

    int line = 0;
    XString error;
    return config.BuildFromFile(path, line, error) != FALSE;
}

void CKRenderDebugConfigLoad(VxConfiguration &config) {
    char path[MAX_PATH] = {0};
    HMODULE hMod = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&CKRenderDebugConfigLoad, &hMod)) {
        GetModuleFileNameA(hMod, path, MAX_PATH);
        char *last = strrchr(path, '\\');
        if (last) {
            strcpy_s(last + 1, MAX_PATH - (last + 1 - path), kRenderDebugConfigFile);
            if (CKRenderDebugConfigLoadFile(config, path))
                return;
        }
    }

    CKRenderDebugConfigLoadFile(config, kRenderDebugConfigFile);
}

VxConfiguration *CKRenderDebugConfigGetConfig() {
    static VxConfiguration s_Config;
    static bool s_Loaded = false;
    if (!s_Loaded) {
        s_Loaded = true;
        CKRenderDebugConfigLoad(s_Config);
    }
    return &s_Config;
}

int CKRenderDebugConfigFindOverride(const char *name) {
    if (!name || name[0] == '\0')
        return -1;

    for (int i = 0; i < kRenderDebugOverrideCount; ++i) {
        if (g_RenderDebugOverrides[i].Used &&
            CKRenderDebugConfigStricmp(g_RenderDebugOverrides[i].Name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int CKRenderDebugConfigFindFreeOverride() {
    for (int i = 0; i < kRenderDebugOverrideCount; ++i) {
        if (!g_RenderDebugOverrides[i].Used)
            return i;
    }
    return -1;
}

bool CKRenderDebugConfigOverrideString(const char *name, char *buffer, CKDWORD bufferSize) {
    int index = CKRenderDebugConfigFindOverride(name);
    if (index < 0)
        return false;
    return CKRenderDebugConfigCopyString(buffer, bufferSize, g_RenderDebugOverrides[index].Value);
}

} // namespace

bool CKRenderDebugConfigParseBool(const char *value, bool fallback) {
    if (!value || value[0] == '\0')
        return fallback;

    if (CKRenderDebugConfigStricmp(value, "1") == 0 ||
        CKRenderDebugConfigStricmp(value, "true") == 0 ||
        CKRenderDebugConfigStricmp(value, "on") == 0 ||
        CKRenderDebugConfigStricmp(value, "yes") == 0) {
        return true;
    }

    if (CKRenderDebugConfigStricmp(value, "0") == 0 ||
        CKRenderDebugConfigStricmp(value, "false") == 0 ||
        CKRenderDebugConfigStricmp(value, "off") == 0 ||
        CKRenderDebugConfigStricmp(value, "no") == 0) {
        return false;
    }

    return fallback;
}

bool CKRenderDebugConfigString(const char *name, char *buffer, CKDWORD bufferSize) {
    if (!name || !buffer || bufferSize == 0)
        return false;

    buffer[0] = '\0';
    if (CKRenderDebugConfigOverrideString(name, buffer, bufferSize))
        return true;

    char entryName[256] = {0};
    strcpy_s(entryName, kRenderDebugConfigSection);
    strcat_s(entryName, ".");
    strcat_s(entryName, name);

    VxConfigurationEntry *entry = CKRenderDebugConfigGetConfig()->GetEntry(entryName, TRUE);
    if (!entry)
        return false;

    return CKRenderDebugConfigCopyString(buffer, bufferSize, entry->GetValue());
}

bool CKRenderDebugConfigBool(const char *name, bool fallback) {
    char value[32] = {0};
    if (!CKRenderDebugConfigString(name, value, (CKDWORD)sizeof(value)))
        return fallback;
    return CKRenderDebugConfigParseBool(value, fallback);
}

int CKRenderDebugConfigInt(const char *name, int fallback) {
    char value[64] = {0};
    if (!CKRenderDebugConfigString(name, value, (CKDWORD)sizeof(value)))
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

CKDWORD CKRenderDebugConfigDword(const char *name, CKDWORD fallback) {
    char value[64] = {0};
    if (!CKRenderDebugConfigString(name, value, (CKDWORD)sizeof(value)))
        return fallback;

    char *end = nullptr;
    unsigned long parsed = std::strtoul(value, &end, 10);
    return (end != value) ? (CKDWORD)parsed : fallback;
}

void CKRenderDebugConfigSetOverrideForTests(const char *name, const char *value) {
    if (!name || name[0] == '\0')
        return;

    int index = CKRenderDebugConfigFindOverride(name);
    if (!value) {
        if (index >= 0) {
            g_RenderDebugOverrides[index].Used = false;
            g_RenderDebugOverrides[index].Name[0] = '\0';
            g_RenderDebugOverrides[index].Value[0] = '\0';
        }
        return;
    }

    if (index < 0)
        index = CKRenderDebugConfigFindFreeOverride();
    if (index < 0)
        return;

    g_RenderDebugOverrides[index].Used = true;
    strncpy_s(g_RenderDebugOverrides[index].Name, name, _TRUNCATE);
    strncpy_s(g_RenderDebugOverrides[index].Value, value, _TRUNCATE);
}

bool CKRenderDebugConfigGetOverrideForTests(const char *name, char *buffer, CKDWORD bufferSize) {
    if (!buffer || bufferSize == 0)
        return false;

    buffer[0] = '\0';
    return CKRenderDebugConfigOverrideString(name, buffer, bufferSize);
}

void CKRenderDebugConfigClearOverridesForTests() {
    for (int i = 0; i < kRenderDebugOverrideCount; ++i) {
        g_RenderDebugOverrides[i].Used = false;
        g_RenderDebugOverrides[i].Name[0] = '\0';
        g_RenderDebugOverrides[i].Value[0] = '\0';
    }
}
