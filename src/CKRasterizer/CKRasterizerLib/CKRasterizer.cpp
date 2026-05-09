#include "CKRasterizer.h"

CKRasterizer::CKRasterizer()
    : m_MainWindow(NULL)
{
}

CKRasterizer::~CKRasterizer() = default;

CKBOOL CKRasterizer::Start(WIN_HANDLE AppWnd)
{
    m_MainWindow = AppWnd;
    return FALSE;
}

void CKRasterizer::Close()
{
}

int CKRasterizer::GetDriverCount()
{
    return m_Drivers.Size();
}

CKRasterizerDriver *CKRasterizer::GetDriver(CKDWORD Index)
{
    return m_Drivers[Index];
}
