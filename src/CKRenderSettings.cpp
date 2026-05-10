#include "CKRenderSettings.h"
#include "VxConfiguration.h"
#include "VxMath.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

const char *kRenderSettingsFile = "CK2_3D.ini";
const char *kRenderSettingsSection = "CK2_3D";
const int kRenderSettingsOverrideCount = 64;
const int kRenderSettingsOverrideSectionSize = 128;
const int kRenderSettingsOverrideNameSize = 128;
const int kRenderSettingsOverrideValueSize = 256;

struct CKRenderSettingsOverride {
    bool Used;
    char Section[kRenderSettingsOverrideSectionSize];
    char Name[kRenderSettingsOverrideNameSize];
    char Value[kRenderSettingsOverrideValueSize];
};

static CKRenderSettingsOverride g_RenderSettingsOverrides[kRenderSettingsOverrideCount] = {};
static CKDWORD g_RenderSettingsGeneration = 1;

static const char *CKRenderSettingsSectionName(CKRenderSettingsSection section) {
    switch (section) {
    case CKRenderSettingsSection::Root:
        return "";
    case CKRenderSettingsSection::Debug:
        return "Debug";
    case CKRenderSettingsSection::DebugFrameLog:
        return "Debug.FrameLog";
    case CKRenderSettingsSection::DebugRenderStats:
        return "Debug.RenderStats";
    case CKRenderSettingsSection::DebugFFPStats:
        return "Debug.FFPStats";
    case CKRenderSettingsSection::DebugFFPLog:
        return "Debug.FFPLog";
    case CKRenderSettingsSection::DebugMeshLog:
        return "Debug.MeshLog";
    case CKRenderSettingsSection::FFP:
        return "FFP";
    default:
        return "";
    }
}

static int CKRenderSettingsStricmp(const char *a, const char *b) {
    return _stricmp(a, b);
}

static bool CKRenderSettingsCopyString(char *buffer, CKDWORD bufferSize, const char *value) {
    if (!buffer || bufferSize == 0)
        return false;

    buffer[0] = '\0';
    if (!value || value[0] == '\0')
        return false;

    strncpy_s(buffer, bufferSize, value, _TRUNCATE);
    return buffer[0] != '\0';
}

static bool CKRenderSettingsLoadFile(VxConfiguration &config, const char *path) {
    if (!path || path[0] == '\0')
        return false;

    int line = 0;
    XString error;
    return config.BuildFromFile(path, line, error) != FALSE;
}

static void CKRenderSettingsLoad(VxConfiguration &config) {
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

static VxConfiguration *CKRenderSettingsGetConfig() {
    static VxConfiguration s_Config;
    static bool s_Loaded = false;
    if (!s_Loaded) {
        s_Loaded = true;
        CKRenderSettingsLoad(s_Config);
    }
    return &s_Config;
}

static int CKRenderSettingsFindOverride(const char *section, const char *name) {
    if (!name || name[0] == '\0')
        return -1;

    const char *sectionName = section ? section : "";
    for (int i = 0; i < kRenderSettingsOverrideCount; ++i) {
        if (g_RenderSettingsOverrides[i].Used &&
            CKRenderSettingsStricmp(g_RenderSettingsOverrides[i].Section, sectionName) == 0 &&
            CKRenderSettingsStricmp(g_RenderSettingsOverrides[i].Name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int CKRenderSettingsFindFreeOverride() {
    for (int i = 0; i < kRenderSettingsOverrideCount; ++i) {
        if (!g_RenderSettingsOverrides[i].Used)
            return i;
    }
    return -1;
}

static bool CKRenderSettingsOverrideString(const char *section, const char *name, char *buffer, CKDWORD bufferSize) {
    int index = CKRenderSettingsFindOverride(section, name);
    if (index < 0)
        return false;
    return CKRenderSettingsCopyString(buffer, bufferSize, g_RenderSettingsOverrides[index].Value);
}

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

static bool CKRenderSettingsStringByName(const char *section, const char *name, char *buffer, CKDWORD bufferSize) {
    if (!name || !buffer || bufferSize == 0)
        return false;

    buffer[0] = '\0';
    if (CKRenderSettingsOverrideString(section, name, buffer, bufferSize))
        return true;

    VxConfigurationSection *configSection = nullptr;
    if (!section || section[0] == '\0') {
        configSection = CKRenderSettingsGetConfig()->GetSubSection(kRenderSettingsSection, TRUE);
    } else {
        char sectionPath[256] = {0};
        if (sprintf_s(sectionPath, "%s.%s", kRenderSettingsSection, section) < 0)
            return false;
        configSection = CKRenderSettingsGetConfig()->GetSubSection(sectionPath, TRUE);
    }
    if (!configSection)
        return false;

    VxConfigurationEntry *entry = configSection->GetEntry(name);
    if (!entry)
        return false;

    return CKRenderSettingsCopyString(buffer, bufferSize, entry->GetValue());
}

static bool CKRenderSettingsBoolByName(const char *section, const char *name, bool fallback) {
    char value[32] = {0};
    if (!CKRenderSettingsStringByName(section, name, value, (CKDWORD)sizeof(value)))
        return fallback;
    return CKRenderSettingsParseBool(value, fallback);
}

static int CKRenderSettingsIntByName(const char *section, const char *name, int fallback) {
    char value[64] = {0};
    if (!CKRenderSettingsStringByName(section, name, value, (CKDWORD)sizeof(value)))
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

static CKDWORD CKRenderSettingsDwordByName(const char *section, const char *name, CKDWORD fallback) {
    char value[64] = {0};
    if (!CKRenderSettingsStringByName(section, name, value, (CKDWORD)sizeof(value)))
        return fallback;

    char *end = nullptr;
    unsigned long parsed = std::strtoul(value, &end, 10);
    return (end != value) ? (CKDWORD)parsed : fallback;
}

bool CKRenderSettingsGetString(CKRenderSettingsSection section, const char *name, char *buffer, CKDWORD bufferSize) {
    return CKRenderSettingsStringByName(CKRenderSettingsSectionName(section), name, buffer, bufferSize);
}

bool CKRenderSettingsGetBool(CKRenderSettingsSection section, const char *name, bool fallback) {
    return CKRenderSettingsBoolByName(CKRenderSettingsSectionName(section), name, fallback);
}

int CKRenderSettingsGetInt(CKRenderSettingsSection section, const char *name, int fallback) {
    return CKRenderSettingsIntByName(CKRenderSettingsSectionName(section), name, fallback);
}

CKDWORD CKRenderSettingsGetDword(CKRenderSettingsSection section, const char *name, CKDWORD fallback) {
    return CKRenderSettingsDwordByName(CKRenderSettingsSectionName(section), name, fallback);
}

CKDWORD CKRenderSettingsGetPixelFormat(CKRenderSettingsSection section, const char *name, CKDWORD fallback) {
    char value[64] = {0};
    CKDWORD format = fallback;
    if (!CKRenderSettingsGetString(section, name, value, (CKDWORD)sizeof(value)))
        return fallback;

    format = VxString2PixelFormat(value);
    return format != UNKNOWN_PF ? format : fallback;
}

static CKRenderDiagnosticsConfig CKRenderSettingsReadDiagnostics() {
    CKRenderDiagnosticsConfig config = {};

    const CKRenderSettingsView frameLog = CKRenderFrameLogSettings();
    config.FrameLog.Enabled = frameLog.GetBool("Enabled", false);
    config.FrameLog.CameraAttach = frameLog.GetBool("CameraAttach", false);
    config.FrameLog.PresentSync = frameLog.GetBool("PresentSync", false);
    config.FrameLog.RenderedSceneCamera = frameLog.GetBool("RenderedSceneCamera", false);

    config.RenderStats.Enabled = CKRenderRenderStatsSettings().GetBool("Enabled", false);

    const CKRenderSettingsView ffpStats = CKRenderFFPStatsSettings();
    config.FFPStats.Enabled = ffpStats.GetBool("Enabled", false);
    config.FFPStats.UniformHistogram = ffpStats.GetBool("UniformHistogram", false);
    config.FFPStats.Interval = ffpStats.GetInt("Interval", 60);
    if (config.FFPStats.Interval <= 0)
        config.FFPStats.Interval = 60;

    const CKRenderSettingsView ffpLog = CKRenderFFPLogSettings();
    config.FFPLog.DrawLimit = ffpLog.GetInt("DrawLimit", 0);
    config.FFPLog.Real3DLimit = ffpLog.GetInt("Real3DLimit", 0);
    config.FFPLog.Contract3DLimit = ffpLog.GetInt("Contract3DLimit", 0);
    config.FFPLog.PositionTLimit = ffpLog.GetInt("PositionTLimit", 0);
    config.FFPLog.DrawSerialPerFrame = ffpLog.GetBool("DrawSerialPerFrame", false);

    const CKRenderSettingsView meshLog = CKRenderMeshLogSettings();
    config.MeshLog.ContractLimit = meshLog.GetInt("ContractLimit", 0);
    config.MeshLog.HardwareIndexRanges = meshLog.GetBool("HardwareIndexRanges", false);

    return config;
}

const CKRenderDiagnosticsConfig &CKRenderDiagnosticsSettings() {
    static CKRenderDiagnosticsConfig s_Config = {};
    static CKDWORD s_Generation = 0;

    if (s_Generation != g_RenderSettingsGeneration) {
        s_Config = CKRenderSettingsReadDiagnostics();
        s_Generation = g_RenderSettingsGeneration;
    }
    return s_Config;
}

void CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection section, const char *name, const char *value) {
    if (!name || name[0] == '\0')
        return;

    const char *sectionName = CKRenderSettingsSectionName(section);
    int index = CKRenderSettingsFindOverride(sectionName, name);
    if (!value) {
        if (index >= 0) {
            g_RenderSettingsOverrides[index].Used = false;
            g_RenderSettingsOverrides[index].Section[0] = '\0';
            g_RenderSettingsOverrides[index].Name[0] = '\0';
            g_RenderSettingsOverrides[index].Value[0] = '\0';
            ++g_RenderSettingsGeneration;
        }
        return;
    }

    if (index < 0)
        index = CKRenderSettingsFindFreeOverride();
    if (index < 0)
        return;

    g_RenderSettingsOverrides[index].Used = true;
    strncpy_s(g_RenderSettingsOverrides[index].Section, sectionName, _TRUNCATE);
    strncpy_s(g_RenderSettingsOverrides[index].Name, name, _TRUNCATE);
    strncpy_s(g_RenderSettingsOverrides[index].Value, value, _TRUNCATE);
    ++g_RenderSettingsGeneration;
}

bool CKRenderSettingsGetOverrideForTests(CKRenderSettingsSection section, const char *name, char *buffer, CKDWORD bufferSize) {
    if (!buffer || bufferSize == 0)
        return false;

    buffer[0] = '\0';
    return CKRenderSettingsOverrideString(CKRenderSettingsSectionName(section), name, buffer, bufferSize);
}

void CKRenderSettingsClearOverridesForTests() {
    for (int i = 0; i < kRenderSettingsOverrideCount; ++i) {
        g_RenderSettingsOverrides[i].Used = false;
        g_RenderSettingsOverrides[i].Section[0] = '\0';
        g_RenderSettingsOverrides[i].Name[0] = '\0';
        g_RenderSettingsOverrides[i].Value[0] = '\0';
    }
    ++g_RenderSettingsGeneration;
}
