#ifndef CK_RENDER_SETTINGS_H
#define CK_RENDER_SETTINGS_H

#include "CKRenderConfig.h"
#include "CKTypes.h"
#include "XString.h"
#include "VxConfiguration.h"

struct CKRenderSettingsState;

enum class CKRenderSettingsSection {
    Root,
    Debug,
    DebugFrameLog,
    DebugDrawMap,
    DebugRenderStats,
    DebugFrameCostStats,
    DebugFFPStats,
    DebugFFPLog,
    DebugMeshLog
};

// Path of file next to the module that contains address, or "" when the module is unknown.
XString CKRenderModuleSiblingFile(const void *address, const char *file);

bool CKRenderSettingsParseBool(const char *value, bool fallback);

bool CKRenderSettingsGetString(CKRenderSettingsSection section, const char *name, char *buffer, CKDWORD bufferSize, VxConfiguration *configuration = nullptr);
bool CKRenderSettingsGetString(CKRenderSettingsSection section, const char *name, XString &value, VxConfiguration *configuration = nullptr);
bool CKRenderSettingsGetBool(CKRenderSettingsSection section, const char *name, bool fallback, VxConfiguration *configuration = nullptr);
int CKRenderSettingsGetInt(CKRenderSettingsSection section, const char *name, int fallback, VxConfiguration *configuration = nullptr);
CKDWORD CKRenderSettingsGetDword(CKRenderSettingsSection section, const char *name, CKDWORD fallback, VxConfiguration *configuration = nullptr);
CKDWORD CKRenderSettingsGetPixelFormat(CKRenderSettingsSection section, const char *name, CKDWORD fallback, VxConfiguration *configuration = nullptr);

class CKRenderSettingsView {
public:
    explicit CKRenderSettingsView(CKRenderSettingsSection section, VxConfiguration *configuration = nullptr)
        : m_Section(section), m_Configuration(configuration) {}

    bool GetString(const char *name, char *buffer, CKDWORD bufferSize) const {
        return CKRenderSettingsGetString(m_Section, name, buffer, bufferSize, m_Configuration);
    }

    bool GetString(const char *name, XString &value) const {
        return CKRenderSettingsGetString(m_Section, name, value, m_Configuration);
    }

    bool GetBool(const char *name, bool fallback) const {
        return CKRenderSettingsGetBool(m_Section, name, fallback, m_Configuration);
    }

    int GetInt(const char *name, int fallback) const {
        return CKRenderSettingsGetInt(m_Section, name, fallback, m_Configuration);
    }

    CKDWORD GetDword(const char *name, CKDWORD fallback) const {
        return CKRenderSettingsGetDword(m_Section, name, fallback, m_Configuration);
    }

    CKDWORD GetPixelFormat(const char *name, CKDWORD fallback) const {
        return CKRenderSettingsGetPixelFormat(m_Section, name, fallback, m_Configuration);
    }

private:
    CKRenderSettingsSection m_Section;
    VxConfiguration *m_Configuration;
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

inline CKRenderSettingsView CKRenderDrawMapSettings() {
    return CKRenderSettings(CKRenderSettingsSection::DebugDrawMap);
}

inline CKRenderSettingsView CKRenderRenderStatsSettings() {
    return CKRenderSettings(CKRenderSettingsSection::DebugRenderStats);
}

inline CKRenderSettingsView CKRenderFrameCostStatsSettings() {
    return CKRenderSettings(CKRenderSettingsSection::DebugFrameCostStats);
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

struct CKRenderFrameCostStatsConfig {
    bool Enabled;
    int WarmupFrames;
    int SampleFrames;
    char Output[32];

    bool Any() const {
        return Enabled;
    }
};

struct CKRenderDrawMapConfig {
    bool Enabled;
    bool Submits;
    bool Resources;
    bool Passes;
    bool Markers;
    bool Frame;
    bool Summary;

    bool Any() const {
        return Enabled || Submits || Resources || Passes ||
               Markers || Frame || Summary;
    }
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
    int Draw3DLimit;
    int PositionTLimit;
    bool DrawSerialPerFrame;

    bool Any() const {
        return DrawLimit > 0 || Real3DLimit > 0 || Draw3DLimit > 0 ||
               PositionTLimit > 0 || DrawSerialPerFrame;
    }
};

struct CKRenderMeshLogConfig {
    int DrawLimit;
    bool HardwareIndexRanges;

    bool Any() const {
        return DrawLimit > 0 || HardwareIndexRanges;
    }
};

struct CKRenderDiagnosticsConfig {
    CKRenderFrameLogConfig FrameLog;
    CKRenderDrawMapConfig DrawMap;
    CKRenderRenderStatsConfig RenderStats;
    CKRenderFrameCostStatsConfig FrameCostStats;
    CKRenderFFPStatsConfig FFPStats;
    CKRenderFFPLogConfig FFPLog;
    CKRenderMeshLogConfig MeshLog;
};

const CKRenderDiagnosticsConfig &CKRenderDiagnosticsSettings();

void CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection section, const char *name, const char *value,
                                       CKRenderSettingsState *state = nullptr);
bool CKRenderSettingsGetOverrideForTests(CKRenderSettingsSection section, const char *name, char *buffer, CKDWORD bufferSize);
void CKRenderSettingsClearOverridesForTests();

struct CKRenderSettingsState {
    VxConfiguration Configuration;
    CKRenderDiagnosticsConfig Diagnostics = {};
    bool Load(const char *filename);
    void LoadDefaults();
};

// Implemented by the engine; the rasterizer library keeps its standalone default.
CKRenderSettingsState *CKRenderSettingsForContext(CKContext *context);
inline CKRenderSettingsView CKRenderRootSettings(CKContext *context) {
    CKRenderSettingsState *state = CKRenderSettingsForContext(context);
    return CKRenderSettingsView(CKRenderSettingsSection::Root, state ? &state->Configuration : nullptr);
}
inline const CKRenderDiagnosticsConfig &CKRenderDiagnosticsSettings(CKContext *context) {
    CKRenderSettingsState *state = CKRenderSettingsForContext(context);
    return state ? state->Diagnostics : CKRenderDiagnosticsSettings();
}

#endif
