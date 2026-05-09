#ifndef CK_BGFX_CONFIG_H
#define CK_BGFX_CONFIG_H

#include "CKTypes.h"

bool CKBgfxConfigBool(const char *name, bool fallback);
bool CKBgfxConfigString(const char *name, char *buffer, CKDWORD bufferSize);

#endif
