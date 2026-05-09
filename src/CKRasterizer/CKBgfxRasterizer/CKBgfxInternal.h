#ifndef CK_BGFX_INTERNAL_H
#define CK_BGFX_INTERNAL_H

#include "CKRasterizer.h"

#include <bgfx/bgfx.h>

void CKBgfxLogf(const char *tag, const char *fmt, ...);
void CKBgfxCloseLogFile();

const char *CKBgfxRendererTypeName(bgfx::RendererType::Enum type);
CK_SHADER_PROFILE CKBgfxShaderProfile(bgfx::RendererType::Enum type);
bgfx::RendererType::Enum CKBgfxParseRequestedRenderer();

#endif
