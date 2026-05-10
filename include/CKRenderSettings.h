#ifndef CK_RENDER_SETTINGS_H
#define CK_RENDER_SETTINGS_H

#include "CKTypes.h"

enum class CKRenderSettingsSection {
    Root
};

bool CKRenderSettingsGetString(CKRenderSettingsSection section, const char *name, char *buffer, CKDWORD bufferSize);
CKDWORD CKRenderSettingsGetDword(CKRenderSettingsSection section, const char *name, CKDWORD fallback);
CKDWORD CKRenderSettingsGetPixelFormat(CKRenderSettingsSection section, const char *name, CKDWORD fallback);

void CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection section, const char *name, const char *value);
void CKRenderSettingsClearOverridesForTests();

#endif // CK_RENDER_SETTINGS_H
