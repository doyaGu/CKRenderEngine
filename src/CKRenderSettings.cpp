#include "CKRenderSettings.h"

#include "VxConfiguration.h"
#include "VxMath.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

const char *kRenderSettingsFile = "CK2_3D.ini";
const char *kRenderSettingsSection = "CK2_3D";
const int kOverrideCount = 32;
const int kOverrideNameSize = 128;
const int kOverrideValueSize = 256;

struct SettingOverride {
    bool Used;
    char Name[kOverrideNameSize];
    char Value[kOverrideValueSize];
};

SettingOverride g_Overrides[kOverrideCount] = {};

static bool CopyString(char *buffer, CKDWORD bufferSize, const char *value) {
    if (!buffer || bufferSize == 0)
        return false;

    buffer[0] = '\0';
    if (!value || value[0] == '\0')
        return false;

    strncpy(buffer, value, bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
    return buffer[0] != '\0';
}

static bool LoadConfigFile(VxConfiguration &config, const char *path) {
    if (!path || path[0] == '\0')
        return false;

    int line = 0;
    XString error;
    return config.BuildFromFile(path, line, error) != FALSE;
}

static void LoadConfig(VxConfiguration &config) {
    char path[MAX_PATH] = {0};
    HMODULE module = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&LoadConfig), &module)) {
        GetModuleFileNameA(module, path, MAX_PATH);
        char *lastSlash = strrchr(path, '\\');
        if (lastSlash) {
            CopyString(lastSlash + 1, (CKDWORD)(MAX_PATH - (lastSlash + 1 - path)), kRenderSettingsFile);
            if (LoadConfigFile(config, path))
                return;
        }
    }

    LoadConfigFile(config, kRenderSettingsFile);
}

static VxConfiguration *GetConfig() {
    static VxConfiguration config;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        LoadConfig(config);
    }
    return &config;
}

static int FindOverride(const char *name) {
    if (!name || name[0] == '\0')
        return -1;

    for (int i = 0; i < kOverrideCount; ++i) {
        if (g_Overrides[i].Used && stricmp(g_Overrides[i].Name, name) == 0)
            return i;
    }
    return -1;
}

static int FindFreeOverride() {
    for (int i = 0; i < kOverrideCount; ++i) {
        if (!g_Overrides[i].Used)
            return i;
    }
    return -1;
}

static bool GetOverrideString(const char *name, char *buffer, CKDWORD bufferSize) {
    const int index = FindOverride(name);
    if (index < 0)
        return false;
    return CopyString(buffer, bufferSize, g_Overrides[index].Value);
}

bool CKRenderSettingsGetString(CKRenderSettingsSection section, const char *name, char *buffer, CKDWORD bufferSize) {
    if (section != CKRenderSettingsSection::Root || !name || !buffer || bufferSize == 0)
        return false;

    buffer[0] = '\0';
    if (GetOverrideString(name, buffer, bufferSize))
        return true;

    VxConfigurationSection *configSection = GetConfig()->GetSubSection(kRenderSettingsSection, TRUE);
    if (!configSection)
        return false;

    VxConfigurationEntry *entry = configSection->GetEntry(name);
    if (!entry)
        return false;

    return CopyString(buffer, bufferSize, entry->GetValue());
}

CKDWORD CKRenderSettingsGetDword(CKRenderSettingsSection section, const char *name, CKDWORD fallback) {
    char value[64] = {0};
    if (!CKRenderSettingsGetString(section, name, value, (CKDWORD)sizeof(value)))
        return fallback;

    char *end = nullptr;
    unsigned long parsed = strtoul(value, &end, 10);
    return (end != value) ? (CKDWORD)parsed : fallback;
}

CKDWORD CKRenderSettingsGetPixelFormat(CKRenderSettingsSection section, const char *name, CKDWORD fallback) {
    char value[64] = {0};
    if (!CKRenderSettingsGetString(section, name, value, (CKDWORD)sizeof(value)))
        return fallback;

    const VX_PIXELFORMAT format = VxString2PixelFormat(value);
    return format != UNKNOWN_PF ? (CKDWORD)format : fallback;
}

void CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection section, const char *name, const char *value) {
    if (section != CKRenderSettingsSection::Root || !name || name[0] == '\0')
        return;

    int index = FindOverride(name);
    if (!value) {
        if (index >= 0) {
            g_Overrides[index].Used = false;
            g_Overrides[index].Name[0] = '\0';
            g_Overrides[index].Value[0] = '\0';
        }
        return;
    }

    if (index < 0)
        index = FindFreeOverride();
    if (index < 0)
        return;

    g_Overrides[index].Used = true;
    CopyString(g_Overrides[index].Name, (CKDWORD)sizeof(g_Overrides[index].Name), name);
    CopyString(g_Overrides[index].Value, (CKDWORD)sizeof(g_Overrides[index].Value), value);
}

void CKRenderSettingsClearOverridesForTests() {
    for (int i = 0; i < kOverrideCount; ++i) {
        g_Overrides[i].Used = false;
        g_Overrides[i].Name[0] = '\0';
        g_Overrides[i].Value[0] = '\0';
    }
}
