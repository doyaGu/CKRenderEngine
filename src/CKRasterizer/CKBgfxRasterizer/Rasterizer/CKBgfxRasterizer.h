#ifndef CKBGFXRASTERIZER_H
#define CKBGFXRASTERIZER_H

#include "CKBuiltinShaders.h"
#include "CKFFConstants.h"
#include "CKFFProgram.h"
#include "CKRasterizer.h"

class CKBgfxRasterizerDriver;
class CKBgfxRasterizerContext;
struct CKRasterizerDeviceCaps;

// The shader profiles the build embeds: those of its bgfx renderers. Indices
// out of range have CKRST_SHADER_PROFILE_UNKNOWN.
CKDWORD CKBgfxRasterizerShaderProfileCount();
CK_SHADER_PROFILE CKBgfxRasterizerShaderProfile(CKDWORD index);

CKBOOL CKBgfxRasterizerShaderSet(const CKRasterizerDeviceCaps &caps, CKFFShaderSet &out);
CKBOOL CKBgfxRasterizerFFFragmentShader(const CKRasterizerDeviceCaps &caps,
                                        CKFFSamplerLayout layout,
                                        CKBOOL requiresShaderSampling,
                                        CKShaderDesc &out);
CKBOOL CKBgfxRasterizerDitherFragmentShader(
    const CKRasterizerDeviceCaps &caps, CKShaderDesc &out);

class CKBgfxRasterizer final : public CKRasterizer {
public:
    CKBOOL Start(WIN_HANDLE appWindow) override;
};

class CKBgfxRasterizerDriver final : public CKRasterizerDriver {
public:
    CKBgfxRasterizerDriver(CKBgfxRasterizer *owner, CKDWORD index);
    ~CKBgfxRasterizerDriver() override;

    CKRasterizerContext *CreateContext() override;

    void GetShaderTargets(XClassArray<CKFFShaderTarget> &targets) const;
    CKBOOL GetShaderSet(const CKRasterizerDeviceCaps &caps, CKFFShaderSet &shaderSet) const;
    int GetContextCount() const { return m_Contexts.Size(); }
    void RefreshCaps(CKBgfxRasterizerContext &context);

private:
    void BuildShaderLibrary();

    CKFFShaderLibrary m_Shaders;
};

#endif
