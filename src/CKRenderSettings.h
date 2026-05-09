#ifndef CK_RENDER_SETTINGS_H
#define CK_RENDER_SETTINGS_H

#include "CKTypes.h"

bool CKRenderSettingsParseBool(const char *value, bool fallback);
bool CKRenderSettingsBool(const char *name, bool fallback);
int CKRenderSettingsInt(const char *name, int fallback);
CKDWORD CKRenderSettingsDword(const char *name, CKDWORD fallback);
bool CKRenderSettingsString(const char *name, char *buffer, CKDWORD bufferSize);

bool CKRenderSettingsDebugOutputEnabled(bool fallback);
bool CKRenderSettingsLogPath(char *buffer, CKDWORD bufferSize);
bool CKRenderSettingsFrameLogEnabled();
bool CKRenderSettingsCameraAttachLogEnabled();
bool CKRenderSettingsPresentSyncLogEnabled();
bool CKRenderSettingsRenderedSceneCameraLogEnabled();
bool CKRenderSettingsRenderStatsEnabled();
bool CKRenderSettingsFFPUberShaderEnabled();
bool CKRenderSettingsFFPStatsEnabled();
bool CKRenderSettingsFFPUniformHistEnabled();
int CKRenderSettingsFFPStatsInterval();
int CKRenderSettingsFFPDrawLogLimit();
int CKRenderSettingsFFPReal3DLogLimit();
int CKRenderSettingsFFPContract3DLogLimit();
int CKRenderSettingsFFPPositionTLogLimit();
bool CKRenderSettingsFFPDrawSerialPerFrame();
int CKRenderSettingsMeshContractLogLimit();
bool CKRenderSettingsMeshHardwareIndexRangesLogEnabled();

void CKRenderSettingsSetOverrideForTests(const char *name, const char *value);
bool CKRenderSettingsGetOverrideForTests(const char *name, char *buffer, CKDWORD bufferSize);
void CKRenderSettingsClearOverridesForTests();

#endif
