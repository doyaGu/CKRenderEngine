#ifndef CK_RENDER_SETTINGS_H
#define CK_RENDER_SETTINGS_H

#include "CKRenderConfig.h"
#include "CKTypes.h"

enum class CKRenderSettingsSection {
    Root,
    Debug,
    DebugFrameLog,
    DebugRenderStats,
    DebugFFPStats,
    DebugFFPLog,
    DebugMeshLog,
    FFP
};

bool CKRenderSettingsParseBool(const char *value, bool fallback);

bool CKRenderSettingsGetString(CKRenderSettingsSection section, const char *name, char *buffer, CKDWORD bufferSize);
bool CKRenderSettingsGetBool(CKRenderSettingsSection section, const char *name, bool fallback);
int CKRenderSettingsGetInt(CKRenderSettingsSection section, const char *name, int fallback);
CKDWORD CKRenderSettingsGetDword(CKRenderSettingsSection section, const char *name, CKDWORD fallback);
CKDWORD CKRenderSettingsGetPixelFormat(CKRenderSettingsSection section, const char *name, CKDWORD fallback);

class CKRenderSettingsView {
public:
    explicit CKRenderSettingsView(CKRenderSettingsSection section) : m_Section(section) {}

    bool GetString(const char *name, char *buffer, CKDWORD bufferSize) const {
        return CKRenderSettingsGetString(m_Section, name, buffer, bufferSize);
    }

    bool GetBool(const char *name, bool fallback) const {
        return CKRenderSettingsGetBool(m_Section, name, fallback);
    }

    int GetInt(const char *name, int fallback) const {
        return CKRenderSettingsGetInt(m_Section, name, fallback);
    }

    CKDWORD GetDword(const char *name, CKDWORD fallback) const {
        return CKRenderSettingsGetDword(m_Section, name, fallback);
    }

    CKDWORD GetPixelFormat(const char *name, CKDWORD fallback) const {
        return CKRenderSettingsGetPixelFormat(m_Section, name, fallback);
    }

private:
    CKRenderSettingsSection m_Section;
};

inline CKRenderSettingsView CKRenderSettings(CKRenderSettingsSection section) {
    return CKRenderSettingsView(section);
}

inline CKRenderSettingsView CKRenderRootSettings() {
    return CKRenderSettings(CKRenderSettingsSection::Root);
}

inline CKRenderSettingsView CKRenderDebugSettings() {
    return CKRenderSettings(CKRenderSettingsSection::Debug);
}

inline CKRenderSettingsView CKRenderFrameLogSettings() {
    return CKRenderSettings(CKRenderSettingsSection::DebugFrameLog);
}

inline CKRenderSettingsView CKRenderRenderStatsSettings() {
    return CKRenderSettings(CKRenderSettingsSection::DebugRenderStats);
}

inline CKRenderSettingsView CKRenderFFPStatsSettings() {
    return CKRenderSettings(CKRenderSettingsSection::DebugFFPStats);
}

inline CKRenderSettingsView CKRenderFFPLogSettings() {
    return CKRenderSettings(CKRenderSettingsSection::DebugFFPLog);
}

inline CKRenderSettingsView CKRenderMeshLogSettings() {
    return CKRenderSettings(CKRenderSettingsSection::DebugMeshLog);
}

inline CKRenderSettingsView CKRenderFFPSettings() {
    return CKRenderSettings(CKRenderSettingsSection::FFP);
}

struct CKRenderFrameLogConfig {
    bool Enabled;
    bool CameraAttach;
    bool PresentSync;
    bool RenderedSceneCamera;

    bool Any() const {
        return Enabled || CameraAttach || PresentSync || RenderedSceneCamera;
    }
};

struct CKRenderRenderStatsConfig {
    bool Enabled;
};

struct CKRenderFFPStatsConfig {
    bool Enabled;
    bool UniformHistogram;
    int Interval;

    bool Any() const {
        return Enabled || UniformHistogram;
    }
};

struct CKRenderFFPLogConfig {
    int DrawLimit;
    int Real3DLimit;
    int Contract3DLimit;
    int PositionTLimit;
    bool DrawSerialPerFrame;

    bool Any() const {
        return DrawLimit > 0 || Real3DLimit > 0 || Contract3DLimit > 0 ||
               PositionTLimit > 0 || DrawSerialPerFrame;
    }
};

struct CKRenderMeshLogConfig {
    int ContractLimit;
    bool HardwareIndexRanges;

    bool Any() const {
        return ContractLimit > 0 || HardwareIndexRanges;
    }
};

struct CKRenderDiagnosticsConfig {
    CKRenderFrameLogConfig FrameLog;
    CKRenderRenderStatsConfig RenderStats;
    CKRenderFFPStatsConfig FFPStats;
    CKRenderFFPLogConfig FFPLog;
    CKRenderMeshLogConfig MeshLog;
};

const CKRenderDiagnosticsConfig &CKRenderDiagnosticsSettings();

void CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection section, const char *name, const char *value);
bool CKRenderSettingsGetOverrideForTests(CKRenderSettingsSection section, const char *name, char *buffer, CKDWORD bufferSize);
void CKRenderSettingsClearOverridesForTests();

#endif
