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
int CKBgfxConfigPositiveInt(const char *section, const char *name, int fallback);

const char *CKBgfxRendererTypeName(bgfx::RendererType::Enum type);
CK_SHADER_PROFILE CKBgfxShaderProfile(bgfx::RendererType::Enum type);
bgfx::RendererType::Enum CKBgfxParseRequestedRenderer();
uint32_t CKBgfxBuildResetFlags(CKBOOL VSync, CKDWORD Samples);

bgfx::UniformType::Enum CKBgfxUniformType(CK_UNIFORM_TYPE Type);
bgfx::Attrib::Enum CKBgfxAttrib(CK_VERTEX_ATTRIB Attrib);
bgfx::AttribType::Enum CKBgfxAttribType(CK_VERTEX_ATTRIB_TYPE Type);
bgfx::TextureFormat::Enum CKBgfxTextureFormat(VX_PIXELFORMAT Format);
bgfx::TextureFormat::Enum CKBgfxDepthFormat(CK_DEPTH_FORMAT Format);
uint32_t CKBgfxSamplerFlags(const CKSamplerDesc *Sampler);
uint64_t CKBgfxState(CKDrawState State);

uint32_t CKBgfxBuildFrontStencil(CKDrawState State, CKDWORD Ref,
                                 CKDWORD ReadMask, CKDWORD WriteMask);
uint32_t CKBgfxBuildBackStencil(CKDrawState State, CKDWORD Ref,
                                CKDWORD ReadMask, CKDWORD WriteMask);

#endif
