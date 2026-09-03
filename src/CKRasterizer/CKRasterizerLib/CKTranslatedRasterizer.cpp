#include "CKTranslatedRasterizer.h"
#include "CKNullBackend.h"

#include <new>

// ===========================================================================
// CKTranslatedRasterizer
// ===========================================================================

CKTranslatedRasterizer::CKTranslatedRasterizer(CKRasterizerBackendLibrary *Library,
                                               CKTranslatedLibraryCloseFunction CloseLibrary)
    : m_Library(Library), m_CloseLibrary(CloseLibrary) {}

CKTranslatedRasterizer::~CKTranslatedRasterizer()
{
    Close();
    if (m_Library) {
        if (m_CloseLibrary)
            m_CloseLibrary(m_Library);
        else
            delete m_Library;
        m_Library = NULL;
    }
}

CKBOOL CKTranslatedRasterizer::Start(WIN_HANDLE AppWnd)
{
    m_MainWindow = AppWnd;
    if (!m_Library)
        return FALSE;
    if (m_Drivers.Size() > 0)
        return TRUE;
    if (m_Library->GetDriverCount() == 0 && !m_Library->Start(AppWnd))
        return FALSE;
    for (int i = 0; i < m_Library->GetDriverCount(); ++i) {
        CKRasterizerBackendDriver *backendDriver = m_Library->GetDriver((CKDWORD)i);
        if (!backendDriver)
            continue;
        CKTranslatedDriver *driver = new (std::nothrow) CKTranslatedDriver(this, backendDriver, (CKDWORD)m_Drivers.Size());
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
    if (m_Library)
        m_Library->Close();
}

// ===========================================================================
// CKTranslatedDriver
// ===========================================================================

CKTranslatedDriver::CKTranslatedDriver(CKTranslatedRasterizer *Owner, CKRasterizerBackendDriver *Backend, CKDWORD Index)
    : m_Backend(Backend)
{
    m_Owner = Owner;
    m_DriverIndex = Index;
    SyncCapsFromBackend();
}

CKTranslatedDriver::~CKTranslatedDriver()
{
    while (m_Contexts.Size() > 0) {
        CKRasterizerContext *context = m_Contexts[m_Contexts.Size() - 1];
        if (!DestroyContext(context)) {
            // The backend refused (frame in flight); drop our wrapper anyway
            // so the driver can go away. The backend driver owns the backend.
            m_Contexts.PopBack();
            delete static_cast<CKTranslatedContext *>(context);
        }
    }
}

void CKTranslatedDriver::SyncCapsFromBackend()
{
    if (!m_Backend)
        return;
    m_Backend->RefreshCaps();
    m_Hardware = m_Backend->m_Hardware;
    m_CapsUpToDate = m_Backend->m_CapsUpToDate;
    m_DisplayModes = m_Backend->m_DisplayModes;
    m_TextureFormats = m_Backend->m_TextureFormats;
    m_3DCaps = m_Backend->m_3DCaps;
    m_2DCaps = m_Backend->m_2DCaps;
    m_Desc = m_Backend->m_Desc;
}

CKRasterizerContext *CKTranslatedDriver::CreateContext()
{
    if (!m_Backend)
        return NULL;
    CKRasterizerBackend *backend = m_Backend->CreateBackend();
    if (!backend)
        return NULL;
    CKTranslatedContext *context = new (std::nothrow) CKTranslatedContext(this, backend);
    if (!context) {
        m_Backend->DestroyBackend(backend);
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
        if (m_Backend && !m_Backend->DestroyBackend(translated->GetBackend()))
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

CKRasterizer *CKTranslatedRasterizerStart(CKRasterizerBackendLibrary *Library,
                                          CKTranslatedLibraryCloseFunction CloseLibrary)
{
    if (!Library)
        return NULL;
    CKTranslatedRasterizer *rasterizer = new (std::nothrow) CKTranslatedRasterizer(Library, CloseLibrary);
    if (!rasterizer) {
        if (CloseLibrary)
            CloseLibrary(Library);
        else
            delete Library;
        return NULL;
    }
    if (!rasterizer->Start(Library->GetMainWindow())) {
        delete rasterizer; // closes the library too
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
    CKNullBackendLibrary *library = new (std::nothrow) CKNullBackendLibrary();
    if (!library)
        return NULL;
    if (!library->Start(AppWnd)) {
        delete library;
        return NULL;
    }
    return CKTranslatedRasterizerStart(library, NULL);
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
