#ifndef CK_RENDER_DEBUG_CONFIG_H
#define CK_RENDER_DEBUG_CONFIG_H

#include "CKTypes.h"

bool CKRenderDebugConfigParseBool(const char *value, bool fallback);
bool CKRenderDebugConfigBool(const char *name, bool fallback);
int CKRenderDebugConfigInt(const char *name, int fallback);
CKDWORD CKRenderDebugConfigDword(const char *name, CKDWORD fallback);
bool CKRenderDebugConfigString(const char *name, char *buffer, CKDWORD bufferSize);

void CKRenderDebugConfigSetOverrideForTests(const char *name, const char *value);
bool CKRenderDebugConfigGetOverrideForTests(const char *name, char *buffer, CKDWORD bufferSize);
void CKRenderDebugConfigClearOverridesForTests();

#endif
