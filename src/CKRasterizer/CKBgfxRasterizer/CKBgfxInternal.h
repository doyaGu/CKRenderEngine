#ifndef CK_BGFX_INTERNAL_H
#define CK_BGFX_INTERNAL_H

#include "CKRasterizer.h"

#include <bgfx/bgfx.h>

struct CKBgfxLogConfig {
    bool File;
    bool Trace;
    bool Config;
    bool Textures;
    bool TextureBindings;
    bool Uniforms;
    bool PresentSync;
};

struct CKBgfxDebugConfig {
    uint32_t BgfxFlags;
    bool Overlay;
    CKBgfxLogConfig Log;
};

const CKBgfxDebugConfig &CKBgfxDebugSettings();
void CKBgfxLogf(const char *tag, const char *fmt, ...);
bool CKBgfxLogEnabled(const char *name, bool fallback);
void CKBgfxCloseLogFile();

const char *CKBgfxRendererTypeName(bgfx::RendererType::Enum type);
CK_SHADER_PROFILE CKBgfxShaderProfile(bgfx::RendererType::Enum type);
bgfx::RendererType::Enum CKBgfxParseRequestedRenderer();

#endif
