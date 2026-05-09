#ifndef CK_RENDER_DEBUG_ENV_H
#define CK_RENDER_DEBUG_ENV_H

#include "CKTypes.h"

bool CKRenderDebugParseBool(const char *value, bool fallback);
bool CKRenderDebugEnvBool(const char *name, bool fallback);
int CKRenderDebugEnvInt(const char *name, int fallback);
CKDWORD CKRenderDebugEnvDword(const char *name, CKDWORD fallback);
bool CKRenderDebugEnvString(const char *name, char *buffer, CKDWORD bufferSize);

#endif
