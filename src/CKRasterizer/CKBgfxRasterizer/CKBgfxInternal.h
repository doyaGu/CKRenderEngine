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
XString CKBgfxModuleSiblingFile(const void *address, const char *file);

const char *CKBgfxRendererTypeName(bgfx::RendererType::Enum type);
CK_SHADER_PROFILE CKBgfxShaderProfile(bgfx::RendererType::Enum type);
bgfx::RendererType::Enum CKBgfxParseRequestedRenderer();
uint32_t CKBgfxBuildResetFlags(CKBOOL VSync, CKDWORD Samples);

bgfx::UniformType::Enum CKBgfxUniformType(CK_UNIFORM_TYPE Type);
bgfx::Attrib::Enum CKBgfxAttrib(CK_VERTEX_ATTRIB Attrib);
bgfx::AttribType::Enum CKBgfxAttribType(CK_VERTEX_ATTRIB_TYPE Type);
bgfx::TextureFormat::Enum CKBgfxTextureFormat(VX_PIXELFORMAT Format);
bgfx::TextureFormat::Enum CKBgfxDepthFormat(CK_DEPTH_FORMAT Format);
CKDWORD CKBgfxImageRowBytes(CKDWORD Width, CKDWORD BitsPerPixel);
CKDWORD CKBgfxResolveImagePitch(CKDWORD Width, CKDWORD Height,
                                CKDWORD BitsPerPixel, CKDWORD PitchOrImageSize);
CKDWORD CKBgfxTextureMipCount(CKDWORD Width, CKDWORD Height, CKDWORD Depth);
CKBOOL CKBgfxIsAutoMipRequest(CKDWORD RequestedMipCount, CKDWORD FullMipCount);
CKBOOL CKBgfxShouldCreateTextureMipChain(CKDWORD RequestedMipCount,
                                         CKDWORD FullMipCount,
                                         CKBOOL OpenGL,
                                         CKBOOL AutoMipDataAvailable);

typedef enum CKBgfxAutoMipUpdateAction {
    CKBGFX_AUTOMIP_UPDATE_NONE = 0,
    CKBGFX_AUTOMIP_UPDATE_KEEP,
    CKBGFX_AUTOMIP_UPDATE_PROMOTE,
    CKBGFX_AUTOMIP_UPDATE_DEMOTE,
} CKBgfxAutoMipUpdateAction;

CKBgfxAutoMipUpdateAction CKBgfxResolveAutoMipUpdateAction(CKBOOL RequestedAutoMips,
                                                           CKDWORD CurrentMipCount,
                                                           CKBOOL FullBaseUpdate,
                                                           CKBOOL CanGenerateFullMipChain);
CKBOOL CKBgfxSamplerWantsMipMaps(const CKSamplerDesc *Sampler);
uint32_t CKBgfxSamplerFlags(const CKSamplerDesc *Sampler);
uint64_t CKBgfxState(CKDrawState State);

uint32_t CKBgfxBuildFrontStencil(CKDrawState State, CKDWORD Ref,
                                 CKDWORD ReadMask, CKDWORD WriteMask);
uint32_t CKBgfxBuildBackStencil(CKDrawState State, CKDWORD Ref,
                                CKDWORD ReadMask, CKDWORD WriteMask);

#endif
