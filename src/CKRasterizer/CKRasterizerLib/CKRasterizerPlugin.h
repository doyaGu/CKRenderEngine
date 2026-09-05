#ifndef CKRASTERIZERPLUGIN_H
#define CKRASTERIZERPLUGIN_H

#include "CKRasterizerBackend.h"
#include "CKBuiltinShaders.h"

// Plugin composition belongs to the rasterizer layer: the adapter enumerates
// devices, supplies a complete FFP shader family and creates a generic backend.
class CKRasterizerBackendDriver {
public:
    CKRasterizerBackendDriver();
    virtual ~CKRasterizerBackendDriver() {}

    virtual CKRasterizerBackend *CreateBackend() = 0;
    virtual CKBOOL DestroyBackend(CKRasterizerBackend *backend) = 0;
    virtual void GetShaderTargets(std::vector<CKBackendShaderTarget> &out) const { out.clear(); }
    virtual CKBOOL GetShaderSet(const CKBackendCaps &, CKBackendShaderSet &out) const {
        out = CKBackendShaderSet();
        return FALSE;
    }
    virtual void RefreshCaps() {}
    void InitializeDisplayCaps();

    CKBOOL m_Hardware;
    CKBOOL m_CapsUpToDate;
    CKDWORD m_DriverIndex;
    XArray<VxDisplayMode> m_DisplayModes;
    XClassArray<CKTextureDesc> m_TextureFormats;
    Vx3DCapsDesc m_3DCaps;
    Vx2DCapsDesc m_2DCaps;
    XString m_Desc;
};

class CKRasterizerBackendLibrary {
public:
    virtual ~CKRasterizerBackendLibrary() {}
    virtual CKBOOL Start(WIN_HANDLE appWnd) = 0;
    virtual void Close() = 0;
    virtual int GetDriverCount() const = 0;
    virtual CKRasterizerBackendDriver *GetDriver(CKDWORD index) const = 0;
    virtual WIN_HANDLE GetMainWindow() const = 0;
};

#endif
