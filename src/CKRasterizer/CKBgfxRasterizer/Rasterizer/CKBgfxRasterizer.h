#ifndef CKBGFXRASTERIZER_H
#define CKBGFXRASTERIZER_H

#include "CKBgfxBackend.h"

#include "CKBuiltinShaders.h"
#include "CKRasterizer.h"

class CKBgfxRasterizerDriver;

CKBOOL CKBgfxRasterizerShaderSet(const CKBackendCaps &caps, CKBackendShaderSet &out);

class CKBgfxRasterizer final : public CKRasterizer {
public:
    ~CKBgfxRasterizer() override;

    CKBOOL Start(WIN_HANDLE appWindow) override;
    void Close() override;
};

class CKBgfxRasterizerDriver final : public CKRasterizerDriver {
public:
    CKBgfxRasterizerDriver(CKBgfxRasterizer *owner, CKDWORD index);
    ~CKBgfxRasterizerDriver() override;

    CKRasterizerContext *CreateContext() override;
    CKBOOL DestroyContext(CKRasterizerContext *context) override;

    void GetShaderTargets(std::vector<CKBackendShaderTarget> &targets) const;
    CKBOOL GetShaderSet(const CKBackendCaps &caps, CKBackendShaderSet &shaderSet) const;
    int GetBackendCount() const { return m_Backends.Size(); }

private:
    static void OnBackendReady(void *user, CKRasterizerBackend *backend);
    void BuildShaderLibrary();
    void RefreshCaps(CKBgfxBackend &backend);

    CKFFShaderLibrary m_Shaders;
    XArray<CKBgfxBackend *> m_Backends;
};

#endif
