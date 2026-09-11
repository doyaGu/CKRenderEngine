#ifndef CKRECORDINGPROVIDER_H
#define CKRECORDINGPROVIDER_H

#include "CKRecordingBackend.h"
#include "CKBuiltinShaders.h"
#include "CKRasterizer.h"

void CKRecordingShaderTargets(std::vector<CKBackendShaderTarget> &out);
CKBOOL CKRecordingShaderSet(const CKBackendCaps &caps, CKBackendShaderSet &out);

class CKRecordingRasterizerDriver : public CKRasterizerDriver {
public:
    explicit CKRecordingRasterizerDriver(CKRasterizer *owner = NULL, CKDWORD index = 0);
    ~CKRecordingRasterizerDriver() override;

    CKRasterizerContext *CreateContext() override;
    CKBOOL DestroyContext(CKRasterizerContext *context) override;

    virtual CKRasterizerBackend *CreateBackend();
    virtual CKBOOL DestroyBackend(CKRasterizerBackend *backend);
    virtual void GetShaderTargets(std::vector<CKBackendShaderTarget> &out) const;
    virtual CKBOOL GetShaderSet(const CKBackendCaps &caps, CKBackendShaderSet &out) const;
    CKBackendCaps GetBackendConventions() const;

    CK_SHADER_FORMAT Format;
    CK_SHADER_PROFILE Profile;
    CKBOOL OriginBottomLeft;
    CKBOOL HomogeneousDepth;

protected:
    virtual CKRecordingBackend *NewBackend();
    CKBOOL BuildShaderLibrary(CKFFShaderLibrary &shaders) const;

private:
    XArray<CKRecordingBackend *> m_Backends;
    XArray<CKRecordingBackend *> m_ContextBackends;
};

class CKRecordingRasterizer : public CKRasterizer {
public:
    ~CKRecordingRasterizer() override;

    CKBOOL Start(WIN_HANDLE appWindow) override;
    void Close() override;

protected:
    virtual CKRecordingRasterizerDriver *NewDriver();
};

#endif
