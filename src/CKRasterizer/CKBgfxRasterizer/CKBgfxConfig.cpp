#include "CKBgfxConfig.h"
#include "VxConfiguration.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstring>

static int CKBgfxConfigStricmp(const char *a, const char *b)
{
    return _stricmp(a, b);
}

static bool CKBgfxConfigParseBool(const char *value, bool fallback)
{
    if (!value || value[0] == '\0')
        return fallback;

    if (CKBgfxConfigStricmp(value, "1") == 0 ||
        CKBgfxConfigStricmp(value, "true") == 0 ||
        CKBgfxConfigStricmp(value, "on") == 0 ||
        CKBgfxConfigStricmp(value, "yes") == 0)
        return true;

    if (CKBgfxConfigStricmp(value, "0") == 0 ||
        CKBgfxConfigStricmp(value, "false") == 0 ||
        CKBgfxConfigStricmp(value, "off") == 0 ||
        CKBgfxConfigStricmp(value, "no") == 0)
        return false;

    return fallback;
}

static bool CKBgfxLoadConfigFile(VxConfiguration &config, const char *path)
{
    if (!path || path[0] == '\0')
        return false;

    int line = 0;
    XString error;
    return config.BuildFromFile(path, line, error) != FALSE;
}

static void CKBgfxLoadConfig(VxConfiguration &config)
{
    char path[MAX_PATH] = {0};
    HMODULE hMod = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&CKBgfxLoadConfig, &hMod)) {
        GetModuleFileNameA(hMod, path, MAX_PATH);
        char *last = strrchr(path, '\\');
        if (last) {
            strcpy_s(last + 1, MAX_PATH - (last + 1 - path), "CKBgfxRasterizer.cfg");
            if (CKBgfxLoadConfigFile(config, path))
                return;
        }
    }

    CKBgfxLoadConfigFile(config, "CKBgfxRasterizer.cfg");
}

static VxConfiguration *CKBgfxGetConfig()
{
    static VxConfiguration s_Config;
    static bool s_Loaded = false;
    if (!s_Loaded) {
        s_Loaded = true;
        CKBgfxLoadConfig(s_Config);
    }
    return &s_Config;
}

bool CKBgfxConfigString(const char *name, char *buffer, CKDWORD bufferSize)
{
    if (!name || !buffer || bufferSize == 0)
        return false;

    buffer[0] = '\0';
    VxConfigurationEntry *entry = CKBgfxGetConfig()->GetEntry(name, TRUE);
    if (!entry)
        return false;

    const char *value = entry->GetValue();
    if (!value || value[0] == '\0')
        return false;

    strncpy_s(buffer, bufferSize, value, _TRUNCATE);
    return buffer[0] != '\0';
}

bool CKBgfxConfigBool(const char *name, bool fallback)
{
    char value[32] = {0};
    if (!CKBgfxConfigString(name, value, (CKDWORD)sizeof(value)))
        return fallback;

    return CKBgfxConfigParseBool(value, fallback);
}
