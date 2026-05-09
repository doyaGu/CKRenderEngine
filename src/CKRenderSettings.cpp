#include "CKRenderSettings.h"
#include "VxConfiguration.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <climits>
#include <cstdlib>
#include <cstring>

namespace {

const char *kRenderSettingsFile = "CK2_3D.ini";
const char *kRenderSettingsSection = "CK2_3D";
const int kRenderSettingsOverrideCount = 64;
const int kRenderSettingsOverrideNameSize = 128;
const int kRenderSettingsOverrideValueSize = 256;

struct CKRenderSettingsOverride {
    bool Used;
    char Name[kRenderSettingsOverrideNameSize];
    char Value[kRenderSettingsOverrideValueSize];
};

CKRenderSettingsOverride g_RenderSettingsOverrides[kRenderSettingsOverrideCount] = {};

int CKRenderSettingsStricmp(const char *a, const char *b) {
    return _stricmp(a, b);
}

bool CKRenderSettingsCopyString(char *buffer, CKDWORD bufferSize, const char *value) {
    if (!buffer || bufferSize == 0)
        return false;

    buffer[0] = '\0';
    if (!value || value[0] == '\0')
        return false;

    strncpy_s(buffer, bufferSize, value, _TRUNCATE);
    return buffer[0] != '\0';
}

bool CKRenderSettingsLoadFile(VxConfiguration &config, const char *path) {
    if (!path || path[0] == '\0')
        return false;

    int line = 0;
    XString error;
    return config.BuildFromFile(path, line, error) != FALSE;
}

void CKRenderSettingsLoad(VxConfiguration &config) {
    char path[MAX_PATH] = {0};
    HMODULE hMod = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&CKRenderSettingsLoad, &hMod)) {
        GetModuleFileNameA(hMod, path, MAX_PATH);
        char *last = strrchr(path, '\\');
        if (last) {
            strcpy_s(last + 1, MAX_PATH - (last + 1 - path), kRenderSettingsFile);
            if (CKRenderSettingsLoadFile(config, path))
                return;
        }
    }

    CKRenderSettingsLoadFile(config, kRenderSettingsFile);
}

VxConfiguration *CKRenderSettingsGetConfig() {
    static VxConfiguration s_Config;
    static bool s_Loaded = false;
    if (!s_Loaded) {
        s_Loaded = true;
        CKRenderSettingsLoad(s_Config);
    }
    return &s_Config;
}

int CKRenderSettingsFindOverride(const char *name) {
    if (!name || name[0] == '\0')
        return -1;

    for (int i = 0; i < kRenderSettingsOverrideCount; ++i) {
        if (g_RenderSettingsOverrides[i].Used &&
            CKRenderSettingsStricmp(g_RenderSettingsOverrides[i].Name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int CKRenderSettingsFindFreeOverride() {
    for (int i = 0; i < kRenderSettingsOverrideCount; ++i) {
        if (!g_RenderSettingsOverrides[i].Used)
            return i;
    }
    return -1;
}

bool CKRenderSettingsOverrideString(const char *name, char *buffer, CKDWORD bufferSize) {
    int index = CKRenderSettingsFindOverride(name);
    if (index < 0)
        return false;
    return CKRenderSettingsCopyString(buffer, bufferSize, g_RenderSettingsOverrides[index].Value);
}

} // namespace

bool CKRenderSettingsParseBool(const char *value, bool fallback) {
    if (!value || value[0] == '\0')
        return fallback;

    if (CKRenderSettingsStricmp(value, "1") == 0 ||
        CKRenderSettingsStricmp(value, "true") == 0 ||
        CKRenderSettingsStricmp(value, "on") == 0 ||
        CKRenderSettingsStricmp(value, "yes") == 0) {
        return true;
    }

    if (CKRenderSettingsStricmp(value, "0") == 0 ||
        CKRenderSettingsStricmp(value, "false") == 0 ||
        CKRenderSettingsStricmp(value, "off") == 0 ||
        CKRenderSettingsStricmp(value, "no") == 0) {
        return false;
    }

    return fallback;
}

bool CKRenderSettingsString(const char *name, char *buffer, CKDWORD bufferSize) {
    if (!name || !buffer || bufferSize == 0)
        return false;

    buffer[0] = '\0';
    if (CKRenderSettingsOverrideString(name, buffer, bufferSize))
        return true;

    char entryName[256] = {0};
    strcpy_s(entryName, kRenderSettingsSection);
    strcat_s(entryName, ".");
    strcat_s(entryName, name);

    VxConfigurationEntry *entry = CKRenderSettingsGetConfig()->GetEntry(entryName, TRUE);
    if (!entry)
        return false;

    return CKRenderSettingsCopyString(buffer, bufferSize, entry->GetValue());
}

bool CKRenderSettingsBool(const char *name, bool fallback) {
    char value[32] = {0};
    if (!CKRenderSettingsString(name, value, (CKDWORD)sizeof(value)))
        return fallback;
    return CKRenderSettingsParseBool(value, fallback);
}

int CKRenderSettingsInt(const char *name, int fallback) {
    char value[64] = {0};
    if (!CKRenderSettingsString(name, value, (CKDWORD)sizeof(value)))
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

CKDWORD CKRenderSettingsDword(const char *name, CKDWORD fallback) {
    char value[64] = {0};
    if (!CKRenderSettingsString(name, value, (CKDWORD)sizeof(value)))
        return fallback;

    char *end = nullptr;
    unsigned long parsed = std::strtoul(value, &end, 10);
    return (end != value) ? (CKDWORD)parsed : fallback;
}

bool CKRenderSettingsDebugOutputEnabled(bool fallback) {
    return CKRenderSettingsBool("DEBUG_OUTPUT", fallback);
}

bool CKRenderSettingsLogPath(char *buffer, CKDWORD bufferSize) {
    return CKRenderSettingsString("LOG", buffer, bufferSize);
}

bool CKRenderSettingsFrameLogEnabled() {
    return CKRenderSettingsBool("DEBUG_FRAME_LOG", false);
}

bool CKRenderSettingsCameraAttachLogEnabled() {
    return CKRenderSettingsBool("DEBUG_LOG_CAMERA_ATTACH", false);
}

bool CKRenderSettingsPresentSyncLogEnabled() {
    return CKRenderSettingsBool("DEBUG_LOG_PRESENT_SYNC", false);
}

bool CKRenderSettingsRenderedSceneCameraLogEnabled() {
    return CKRenderSettingsBool("DEBUG_LOG_RENDERED_SCENE_CAMERA", false);
}

bool CKRenderSettingsRenderStatsEnabled() {
    return CKRenderSettingsBool("DEBUG_RENDER_STATS", false);
}

bool CKRenderSettingsFFPUberShaderEnabled() {
    return CKRenderSettingsBool("FFP_UBERSHADER", false);
}

bool CKRenderSettingsFFPStatsEnabled() {
    return CKRenderSettingsBool("DEBUG_FFP_STATS", false);
}

bool CKRenderSettingsFFPUniformHistEnabled() {
    return CKRenderSettingsBool("DEBUG_FFP_UNIFORM_HIST", false);
}

int CKRenderSettingsFFPStatsInterval() {
    int interval = CKRenderSettingsInt("DEBUG_FFP_STATS_INTERVAL", 60);
    return interval > 0 ? interval : 60;
}

int CKRenderSettingsFFPDrawLogLimit() {
    return CKRenderSettingsInt("DEBUG_DRAW_LOG_LIMIT", 0);
}

int CKRenderSettingsFFPReal3DLogLimit() {
    return CKRenderSettingsInt("DEBUG_REAL3D_LOG_LIMIT", 0);
}

int CKRenderSettingsFFPContract3DLogLimit() {
    return CKRenderSettingsInt("DEBUG_3D_CONTRACT_LOG_LIMIT", 0);
}

int CKRenderSettingsFFPPositionTLogLimit() {
    return CKRenderSettingsInt("DEBUG_POSITIONT_LOG_LIMIT", 0);
}

bool CKRenderSettingsFFPDrawSerialPerFrame() {
    return CKRenderSettingsBool("DEBUG_DRAW_SERIAL_PER_FRAME", false);
}

int CKRenderSettingsMeshContractLogLimit() {
    return CKRenderSettingsInt("DEBUG_MESH_CONTRACT_LOG_LIMIT", 0);
}

bool CKRenderSettingsMeshHardwareIndexRangesLogEnabled() {
    return CKRenderSettingsBool("DEBUG_LOG_HW_INDEX_RANGES", false);
}

void CKRenderSettingsSetOverrideForTests(const char *name, const char *value) {
    if (!name || name[0] == '\0')
        return;

    int index = CKRenderSettingsFindOverride(name);
    if (!value) {
        if (index >= 0) {
            g_RenderSettingsOverrides[index].Used = false;
            g_RenderSettingsOverrides[index].Name[0] = '\0';
            g_RenderSettingsOverrides[index].Value[0] = '\0';
        }
        return;
    }

    if (index < 0)
        index = CKRenderSettingsFindFreeOverride();
    if (index < 0)
        return;

    g_RenderSettingsOverrides[index].Used = true;
    strncpy_s(g_RenderSettingsOverrides[index].Name, name, _TRUNCATE);
    strncpy_s(g_RenderSettingsOverrides[index].Value, value, _TRUNCATE);
}

bool CKRenderSettingsGetOverrideForTests(const char *name, char *buffer, CKDWORD bufferSize) {
    if (!buffer || bufferSize == 0)
        return false;

    buffer[0] = '\0';
    return CKRenderSettingsOverrideString(name, buffer, bufferSize);
}

void CKRenderSettingsClearOverridesForTests() {
    for (int i = 0; i < kRenderSettingsOverrideCount; ++i) {
        g_RenderSettingsOverrides[i].Used = false;
        g_RenderSettingsOverrides[i].Name[0] = '\0';
        g_RenderSettingsOverrides[i].Value[0] = '\0';
    }
}
