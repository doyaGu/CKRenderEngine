#ifndef CK_RENDER_SETTINGS_H
#define CK_RENDER_SETTINGS_H

#include "CKRenderConfig.h"
#include "CKTypes.h"

bool CKRenderSettingsParseBool(const char *value, bool fallback);
bool CKRenderSettingsBool(const char *name, bool fallback);
int CKRenderSettingsInt(const char *name, int fallback);
CKDWORD CKRenderSettingsDword(const char *name, CKDWORD fallback);
bool CKRenderSettingsString(const char *name, char *buffer, CKDWORD bufferSize);

bool CKRenderSettingsDebugOutputEnabled(bool fallback);
bool CKRenderSettingsLogPath(char *buffer, CKDWORD bufferSize);

#if CKRE_ENABLE_FRAME_DIAGNOSTICS
bool CKRenderSettingsFrameLogEnabled();
bool CKRenderSettingsCameraAttachLogEnabled();
bool CKRenderSettingsPresentSyncLogEnabled();
bool CKRenderSettingsRenderedSceneCameraLogEnabled();
#else
inline bool CKRenderSettingsFrameLogEnabled() { return false; }
inline bool CKRenderSettingsCameraAttachLogEnabled() { return false; }
inline bool CKRenderSettingsPresentSyncLogEnabled() { return false; }
inline bool CKRenderSettingsRenderedSceneCameraLogEnabled() { return false; }
#endif

#if CKRE_ENABLE_RENDER_STATS
bool CKRenderSettingsRenderStatsEnabled();
#else
inline bool CKRenderSettingsRenderStatsEnabled() { return false; }
#endif

bool CKRenderSettingsFFPUberShaderEnabled();

#if CKRE_ENABLE_FFP_DIAGNOSTICS
bool CKRenderSettingsFFPStatsEnabled();
bool CKRenderSettingsFFPUniformHistEnabled();
int CKRenderSettingsFFPStatsInterval();
int CKRenderSettingsFFPDrawLogLimit();
int CKRenderSettingsFFPReal3DLogLimit();
int CKRenderSettingsFFPContract3DLogLimit();
int CKRenderSettingsFFPPositionTLogLimit();
bool CKRenderSettingsFFPDrawSerialPerFrame();
#else
inline bool CKRenderSettingsFFPStatsEnabled() { return false; }
inline bool CKRenderSettingsFFPUniformHistEnabled() { return false; }
inline int CKRenderSettingsFFPStatsInterval() { return 60; }
inline int CKRenderSettingsFFPDrawLogLimit() { return 0; }
inline int CKRenderSettingsFFPReal3DLogLimit() { return 0; }
inline int CKRenderSettingsFFPContract3DLogLimit() { return 0; }
inline int CKRenderSettingsFFPPositionTLogLimit() { return 0; }
inline bool CKRenderSettingsFFPDrawSerialPerFrame() { return false; }
#endif

#if CKRE_ENABLE_MESH_DIAGNOSTICS
int CKRenderSettingsMeshContractLogLimit();
bool CKRenderSettingsMeshHardwareIndexRangesLogEnabled();
#else
inline int CKRenderSettingsMeshContractLogLimit() { return 0; }
inline bool CKRenderSettingsMeshHardwareIndexRangesLogEnabled() { return false; }
#endif

void CKRenderSettingsSetOverrideForTests(const char *name, const char *value);
bool CKRenderSettingsGetOverrideForTests(const char *name, char *buffer, CKDWORD bufferSize);
void CKRenderSettingsClearOverridesForTests();

#endif
