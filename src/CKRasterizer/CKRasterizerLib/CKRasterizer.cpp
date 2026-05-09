#include "CKRasterizer.h"

#include <string.h>

CKRasterizer::CKRasterizer()
    : m_MainWindow(NULL)
{
}

CKRasterizer::~CKRasterizer() = default;

CKBOOL CKRasterizer::Start(WIN_HANDLE AppWnd)
{
    m_MainWindow = AppWnd;
    CKRasterizerDriver *driver = new CKRasterizerDriver;
    m_Drivers.PushBack(driver);
    return TRUE;
}

void CKRasterizer::Close()
{
    for (auto it = m_Drivers.Begin(); it != m_Drivers.End(); ++it)
        delete *it;
    m_Drivers.Clear();
}

int CKRasterizer::GetDriverCount()
{
    return m_Drivers.Size();
}

CKRasterizerDriver *CKRasterizer::GetDriver(CKDWORD Index)
{
    return m_Drivers[Index];
}

CKRasterizer *CKNULLRasterizerStart(WIN_HANDLE AppWnd)
{
    CKRasterizer *rst = new CKRasterizer();
    if (!rst)
        return NULL;

    if (!rst->Start(AppWnd)) {
        delete rst;
        rst = NULL;
    }

    return rst;
}

void CKNULLRasterizerClose(CKRasterizer *rst)
{
    if (rst) {
        rst->Close();
        delete rst;
    }
}
