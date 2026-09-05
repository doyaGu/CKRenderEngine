#ifndef CKNULLRASTERIZER_H
#define CKNULLRASTERIZER_H

#include "CKNullBackend.h"
#include "CKRasterizerPlugin.h"
#include "CKBuiltinShaders.h"

// Fallback and test composition. Display enumeration and built-in shader
// artifacts belong here; the underlying NULL device has no shader roles.
class CKNullBackendDriver : public CKRasterizerBackendDriver {
public:
    CKNullBackendDriver();
    ~CKNullBackendDriver() override;

    CKRasterizerBackend *CreateBackend() override;
    CKBOOL DestroyBackend(CKRasterizerBackend *Backend) override;
    void GetShaderTargets(std::vector<CKBackendShaderTarget> &Out) const override;
    CKBOOL GetShaderSet(const CKBackendCaps &Caps, CKBackendShaderSet &Out) const override;
    CKBackendCaps GetBackendConventions() const;

    // Tests configure these nominal conventions before creating a backend.
    CK_SHADER_FORMAT Format;
    CK_SHADER_PROFILE Profile;
    CKBOOL OriginBottomLeft;
    CKBOOL HomogeneousDepth;

protected:
    virtual CKNullBackend *NewBackend();

private:
    XArray<CKNullBackend *> m_Backends;
};

class CKNullBackendLibrary : public CKRasterizerBackendLibrary {
public:
    CKNullBackendLibrary();
    ~CKNullBackendLibrary() override;

    CKBOOL Start(WIN_HANDLE AppWnd) override;
    void Close() override;
    int GetDriverCount() const override { return m_Drivers.Size(); }
    CKRasterizerBackendDriver *GetDriver(CKDWORD Index) const override;
    WIN_HANDLE GetMainWindow() const override { return m_MainWindow; }

protected:
    virtual CKNullBackendDriver *NewDriver();

private:
    WIN_HANDLE m_MainWindow;
    XArray<CKNullBackendDriver *> m_Drivers;
};

#endif
