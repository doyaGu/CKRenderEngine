#ifndef CKRECORDINGPROVIDER_H
#define CKRECORDINGPROVIDER_H

#include "CKRecordingBackend.h"
#include "CKRasterizerPlugin.h"
#include "CKBuiltinShaders.h"

// Test-only provider composition. Display enumeration and nominal shader
// artifacts make the command recorder usable through the FFP translation.
void CKRecordingShaderTargets(std::vector<CKBackendShaderTarget> &Out);
CKBOOL CKRecordingShaderSet(const CKBackendCaps &Caps, CKBackendShaderSet &Out);

class CKRecordingBackendDriver : public CKRasterizerBackendDriver {
public:
    CKRecordingBackendDriver();
    ~CKRecordingBackendDriver() override;

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
    virtual CKRecordingBackend *NewBackend();

private:
    XArray<CKRecordingBackend *> m_Backends;
};

class CKRecordingBackendLibrary : public CKRasterizerBackendLibrary {
public:
    CKRecordingBackendLibrary();
    ~CKRecordingBackendLibrary() override;

    CKBOOL Start(WIN_HANDLE AppWnd) override;
    void Close() override;
    int GetDriverCount() const override { return m_Drivers.Size(); }
    CKRasterizerBackendDriver *GetDriver(CKDWORD Index) const override;
    WIN_HANDLE GetMainWindow() const override { return m_MainWindow; }

protected:
    virtual CKRecordingBackendDriver *NewDriver();

private:
    WIN_HANDLE m_MainWindow;
    XArray<CKRecordingBackendDriver *> m_Drivers;
};

#endif
