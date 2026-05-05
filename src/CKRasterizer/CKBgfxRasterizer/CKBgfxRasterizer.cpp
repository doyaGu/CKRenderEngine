#include "CKBgfxRasterizer.h"

CKBgfxRasterizer::CKBgfxRasterizer() {}

CKBgfxRasterizer::~CKBgfxRasterizer() {}

CKBOOL CKBgfxRasterizer::Start(WIN_HANDLE AppWnd)
{
    m_MainWindow = AppWnd;
    auto *driver = new CKBgfxRasterizerDriver(this);
    m_Drivers.PushBack(driver);
    return TRUE;
}

void CKBgfxRasterizer::Close()
{
    for (int i = 0; i < m_Drivers.Size(); ++i)
        delete m_Drivers[i];
    m_Drivers.Clear();
}

static CKBgfxRasterizer *g_Rasterizer = NULL;

static CKRasterizer *CKBgfxRasterizerStart(WIN_HANDLE AppWnd)
{
    g_Rasterizer = new CKBgfxRasterizer();
    if (!g_Rasterizer->Start(AppWnd))
    {
        delete g_Rasterizer;
        g_Rasterizer = NULL;
    }
    return g_Rasterizer;
}

static void CKBgfxRasterizerClose(CKRasterizer *rst)
{
    if (rst)
    {
        rst->Close();
        delete rst;
    }
    g_Rasterizer = NULL;
}

#ifdef CK_LIB
void CKBgfxRasterizerGetInfo(CKRasterizerInfo *info)
#else
extern "C" __declspec(dllexport) void CKRasterizerGetInfo(CKRasterizerInfo *info)
#endif
{
    info->Desc = "bgfx Rasterizer";
    info->StartFct = CKBgfxRasterizerStart;
    info->CloseFct = CKBgfxRasterizerClose;
}
