#ifndef CKBGFXRASTERIZER_H
#define CKBGFXRASTERIZER_H

#include "CKRasterizerPlugin.h"
#include "CKBgfxBackend.h"

class CKBgfxBackendDriver;
class CKBgfxBackendLibrary;

// Immutable FFP artifacts are a rasterizer/plugin facility. The native
// backend accepts the supplied descriptors without interpreting their role.
CKBOOL CKBgfxRasterizerShaderSet(const CKBackendCaps &caps, CKBackendShaderSet &out);

// ===========================================================================
// CKBgfxBackendLibrary / CKBgfxBackendDriver
// ===========================================================================

class CKBgfxBackendLibrary : public CKRasterizerBackendLibrary {
public:
    CKBgfxBackendLibrary();
    ~CKBgfxBackendLibrary() override;

    CKBOOL Start(WIN_HANDLE AppWnd) override;
    void Close() override;
    int GetDriverCount() const override { return m_Drivers.Size(); }
    CKRasterizerBackendDriver *GetDriver(CKDWORD Index) const override;
    WIN_HANDLE GetMainWindow() const override { return m_MainWindow; }

private:
    WIN_HANDLE m_MainWindow;
    XArray<CKBgfxBackendDriver *> m_Drivers;
};

// The single bgfx adapter: display modes from SDL, caps from the baseline
// (spec 4.9.2) lowered to the bgfx limits once a backend is initialised.
class CKBgfxBackendDriver : public CKRasterizerBackendDriver {
public:
    explicit CKBgfxBackendDriver(CKBgfxBackendLibrary *owner);
    ~CKBgfxBackendDriver() override;

    CKRasterizerBackend *CreateBackend() override;
    CKBOOL DestroyBackend(CKRasterizerBackend *Backend) override;
    void RefreshCaps() override;
    void GetShaderTargets(std::vector<CKBackendShaderTarget> &out) const override;
    CKBOOL GetShaderSet(const CKBackendCaps &caps, CKBackendShaderSet &out) const override;

    CKBgfxBackendLibrary *GetOwner() const { return m_Owner; }
    int GetBackendCount() const { return m_Backends.Size(); }

private:
    CKBgfxBackendLibrary *m_Owner;
    XArray<CKBgfxBackend *> m_Backends;
};

#endif // CKBGFXRASTERIZER_H
