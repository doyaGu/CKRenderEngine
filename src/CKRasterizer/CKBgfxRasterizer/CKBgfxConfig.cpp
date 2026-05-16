#include "CKBgfxConfig.h"
#include "VxConfiguration.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
#include <dlfcn.h>
#include <strings.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

static const char *kCKBgfxConfigFile = "CKBgfxRasterizer.ini";
static const char *kCKBgfxConfigSection = "CKBgfxRasterizer";

static int CKBgfxConfigStricmp(const char *a, const char *b)
{
#ifdef _WIN32
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif
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
#ifdef _WIN32
    HMODULE hMod = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&CKBgfxLoadConfig, &hMod)) {
        GetModuleFileNameA(hMod, path, MAX_PATH);
        char *last = strrchr(path, '\\');
        if (last) {
            strcpy_s(last + 1, MAX_PATH - (last + 1 - path), kCKBgfxConfigFile);
            if (CKBgfxLoadConfigFile(config, path))
                return;
        }
    }
#else
    Dl_info info;
    if (dladdr((void *)&CKBgfxLoadConfig, &info) && info.dli_fname) {
        strncpy(path, info.dli_fname, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        char *last = strrchr(path, '/');
        if (last) {
            snprintf(last + 1, sizeof(path) - (size_t)(last + 1 - path), "%s", kCKBgfxConfigFile);
            if (CKBgfxLoadConfigFile(config, path))
                return;
        }
    }
#endif

    CKBgfxLoadConfigFile(config, kCKBgfxConfigFile);
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

bool CKBgfxConfigString(const char *section, const char *name, char *buffer, CKDWORD bufferSize)
{
    if (!section || !name || !buffer || bufferSize == 0)
        return false;

    buffer[0] = '\0';
    char sectionPath[256] = {0};
    if (snprintf(sectionPath, sizeof(sectionPath), "%s.%s", kCKBgfxConfigSection, section) < 0)
        return false;

    VxConfigurationSection *configSection = CKBgfxGetConfig()->GetSubSection(sectionPath, TRUE);
    if (!configSection)
        return false;

    VxConfigurationEntry *entry = configSection->GetEntry(name);
    if (!entry)
        return false;

    const char *value = entry->GetValue();
    if (!value || value[0] == '\0')
        return false;

    strncpy(buffer, value, bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
    return buffer[0] != '\0';
}

bool CKBgfxConfigBool(const char *section, const char *name, bool fallback)
{
    char value[32] = {0};
    if (!CKBgfxConfigString(section, name, value, (CKDWORD)sizeof(value)))
        return fallback;

    return CKBgfxConfigParseBool(value, fallback);
}

int CKBgfxConfigInt(const char *section, const char *name, int fallback)
{
    char value[64] = {0};
    if (!CKBgfxConfigString(section, name, value, (CKDWORD)sizeof(value)))
        return fallback;

    char *end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    return (end != value) ? (int)parsed : fallback;
}
