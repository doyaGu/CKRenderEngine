#include "CKTranslatedRasterizer.h"

#include <new>

// Built-in NULL device (CKRasterizerDeviceLibrary.cpp)
extern CKRasterizerDeviceLibrary *CKNULLRasterizerStart(WIN_HANDLE AppWnd);
extern void CKNULLRasterizerClose(CKRasterizerDeviceLibrary *rst);

// ===========================================================================
// CKTranslatedRasterizer
// ===========================================================================

CKTranslatedRasterizer::CKTranslatedRasterizer(CKRasterizerDeviceLibrary *Device,
                                               CKTranslatedDeviceCloseFunction CloseDevice)
    : m_Device(Device), m_CloseDevice(CloseDevice) {}

CKTranslatedRasterizer::~CKTranslatedRasterizer()
{
    Close();
    if (m_Device) {
        if (m_CloseDevice)
            m_CloseDevice(m_Device);
        else
            delete m_Device;
        m_Device = NULL;
    }
}

CKBOOL CKTranslatedRasterizer::Start(WIN_HANDLE AppWnd)
{
    m_MainWindow = AppWnd;
    if (!m_Device)
        return FALSE;
    if (m_Drivers.Size() > 0)
        return TRUE;
    // Plugins hand over a started device; the NULL fallback starts here.
    if (m_Device->GetDriverCount() == 0 && !m_Device->Start(AppWnd))
        return FALSE;
    for (int i = 0; i < m_Device->GetDriverCount(); ++i) {
        CKRasterizerDeviceDriver *deviceDriver = m_Device->GetDriver((CKDWORD)i);
        if (!deviceDriver)
            continue;
        CKTranslatedDriver *driver = new (std::nothrow) CKTranslatedDriver(this, deviceDriver, (CKDWORD)m_Drivers.Size());
        if (!driver)
            break;
        m_Drivers.PushBack(driver);
    }
    return m_Drivers.Size() > 0 ? TRUE : FALSE;
}

void CKTranslatedRasterizer::Close()
{
    for (int i = 0; i < m_Drivers.Size(); ++i)
        delete m_Drivers[i];
    m_Drivers.Clear();
    if (m_Device)
        m_Device->Close();
}

// ===========================================================================
// CKTranslatedDriver
// ===========================================================================

CKTranslatedDriver::CKTranslatedDriver(CKTranslatedRasterizer *Owner, CKRasterizerDeviceDriver *Device, CKDWORD Index)
    : m_Device(Device)
{
    m_Owner = Owner;
    m_DriverIndex = Index;
    SyncCapsFromDevice();
}

CKTranslatedDriver::~CKTranslatedDriver()
{
    while (m_Contexts.Size() > 0) {
        CKRasterizerContext *context = m_Contexts[m_Contexts.Size() - 1];
        if (!DestroyContext(context)) {
            // The device refused (frame in flight); drop our wrapper anyway so
            // the driver can go away. The device driver owns its context.
            m_Contexts.PopBack();
            delete static_cast<CKTranslatedContext *>(context);
        }
    }
}

void CKTranslatedDriver::SyncCapsFromDevice()
{
    if (!m_Device)
        return;
    m_Hardware = m_Device->m_Hardware;
    m_CapsUpToDate = m_Device->m_CapsUpToDate;
    m_DisplayModes = m_Device->m_DisplayModes;
    m_TextureFormats = m_Device->m_TextureFormats;
    m_3DCaps = m_Device->m_3DCaps;
    m_2DCaps = m_Device->m_2DCaps;
    m_Desc = m_Device->m_Desc;
}

CKRasterizerContext *CKTranslatedDriver::CreateContext()
{
    if (!m_Device)
        return NULL;
    CKRasterizerDevice *device = m_Device->CreateContext();
    if (!device)
        return NULL;
    CKTranslatedContext *context = new (std::nothrow) CKTranslatedContext(this, device);
    if (!context) {
        m_Device->DestroyContext(device);
        return NULL;
    }
    m_Contexts.PushBack(context);
    return context;
}

CKBOOL CKTranslatedDriver::DestroyContext(CKRasterizerContext *Context)
{
    if (!Context)
        return FALSE;
    for (int i = 0; i < m_Contexts.Size(); ++i) {
        if (m_Contexts[i] != Context)
            continue;
        CKTranslatedContext *translated = static_cast<CKTranslatedContext *>(Context);
        translated->BeginShutdown();
        if (m_Device && !m_Device->DestroyContext(translated->GetDeviceForMigration()))
            return FALSE;
        m_Contexts.RemoveAt(i);
        delete translated;
        return TRUE;
    }
    return FALSE;
}

// ===========================================================================
// Entry points
// ===========================================================================

CKRasterizer *CKTranslatedRasterizerStart(CKRasterizerDeviceLibrary *Device,
                                          CKTranslatedDeviceCloseFunction CloseDevice)
{
    if (!Device)
        return NULL;
    CKTranslatedRasterizer *rasterizer = new (std::nothrow) CKTranslatedRasterizer(Device, CloseDevice);
    if (!rasterizer) {
        if (CloseDevice)
            CloseDevice(Device);
        else
            delete Device;
        return NULL;
    }
    if (!rasterizer->Start(Device->m_MainWindow)) {
        delete rasterizer; // closes the device too
        return NULL;
    }
    return rasterizer;
}

void CKTranslatedRasterizerClose(CKRasterizer *Rasterizer)
{
    delete Rasterizer;
}

CKRasterizer *CKTranslatedNullRasterizerStart(WIN_HANDLE AppWnd)
{
    CKRasterizerDeviceLibrary *device = CKNULLRasterizerStart(AppWnd);
    if (!device)
        return NULL;
    return CKTranslatedRasterizerStart(device, CKNULLRasterizerClose);
}

void CKTranslatedNullRasterizerClose(CKRasterizer *Rasterizer)
{
    CKTranslatedRasterizerClose(Rasterizer);
}

void CKTranslatedNullRasterizerGetInfo(CKRasterizerInfo *Info)
{
    if (!Info)
        return;
    Info->DllName = "CK2_3D";
    Info->Desc = "NULL Rasterizer (translation core)";
    Info->DllInstance = NULL;
    Info->StartFct = CKTranslatedNullRasterizerStart;
    Info->CloseFct = CKTranslatedNullRasterizerClose;
    Info->InterfaceRevision = CKRST_INTERFACE_REVISION;
}
