#include "CKBgfxRasterizer.h"

#include <new>

#if defined(_WIN32)
#define CK_BGFX_RASTERIZER_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define CK_BGFX_RASTERIZER_EXPORT __attribute__((visibility("default")))
#else
#define CK_BGFX_RASTERIZER_EXPORT
#endif

CKBgfxRasterizer::CKBgfxRasterizer() {}

CKBgfxRasterizer::~CKBgfxRasterizer()
{
    Close();
}

CKBOOL CKBgfxRasterizer::Start(WIN_HANDLE AppWnd)
{
    if (m_Drivers.Size() > 0)
        return TRUE;

    m_MainWindow = AppWnd;
    auto *driver = new (std::nothrow) CKBgfxRasterizerDriver(this);
    if (!driver)
        return FALSE;

    m_Drivers.PushBack(driver);
    return TRUE;
}

void CKBgfxRasterizer::Close()
{
    for (int i = 0; i < m_Drivers.Size(); ++i)
        delete m_Drivers[i];
    m_Drivers.Clear();
}

static CKRasterizer *CKBgfxRasterizerStart(WIN_HANDLE AppWnd)
{
    auto *rasterizer = new (std::nothrow) CKBgfxRasterizer();
    if (!rasterizer)
        return NULL;

    if (!rasterizer->Start(AppWnd)) {
        delete rasterizer;
        return NULL;
    }

    return rasterizer;
}

static void CKBgfxRasterizerClose(CKRasterizer *rst)
{
    if (!rst)
        return;

    rst->Close();
    delete rst;
}

#ifdef CK_LIB
void CKBgfxRasterizerGetInfo(CKRasterizerInfo *info)
#else
extern "C" CK_BGFX_RASTERIZER_EXPORT void CKRasterizerGetInfo(CKRasterizerInfo *info)
#endif
{
    if (!info)
        return;

    info->Desc = "bgfx Rasterizer";
    info->StartFct = CKBgfxRasterizerStart;
    info->CloseFct = CKBgfxRasterizerClose;
}
