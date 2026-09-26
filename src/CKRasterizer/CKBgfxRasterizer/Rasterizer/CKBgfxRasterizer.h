#ifndef CKBGFXRASTERIZER_H
#define CKBGFXRASTERIZER_H

#include "CKBuiltinShaders.h"
#include "CKFFConstants.h"
#include "CKRasterizer.h"

class CKBgfxRasterizerDriver;
class CKBgfxRasterizerContext;
struct CKRasterizerDeviceCaps;

CKBOOL CKBgfxRasterizerShaderSet(const CKRasterizerDeviceCaps &caps, CKFFShaderSet &out);
CKBOOL CKBgfxRasterizerFFFragmentShader(const CKRasterizerDeviceCaps &caps,
                                        CKFFSamplerLayout layout,
                                        CKShaderDesc &out);

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
