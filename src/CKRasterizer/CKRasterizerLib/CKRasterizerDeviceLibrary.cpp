#include "CKRasterizerDevice.h"

#include <string.h>

CKRasterizerDeviceLibrary::CKRasterizerDeviceLibrary()
    : m_MainWindow(NULL)
{
}

CKRasterizerDeviceLibrary::~CKRasterizerDeviceLibrary() = default;

CKBOOL CKRasterizerDeviceLibrary::Start(WIN_HANDLE AppWnd)
{
    m_MainWindow = AppWnd;
    CKRasterizerDeviceDriver *driver = new CKRasterizerDeviceDriver;
    driver->InitNULLRasterizerCaps(this);
    m_Drivers.PushBack(driver);
    return TRUE;
}

void CKRasterizerDeviceLibrary::Close()
{
    for (auto it = m_Drivers.Begin(); it != m_Drivers.End(); ++it)
        delete *it;
    m_Drivers.Clear();
}

int CKRasterizerDeviceLibrary::GetDriverCount()
{
    return m_Drivers.Size();
}

CKRasterizerDeviceDriver *CKRasterizerDeviceLibrary::GetDriver(CKDWORD Index)
{
    return m_Drivers[Index];
}

CKRasterizerDeviceLibrary *CKNULLRasterizerStart(WIN_HANDLE AppWnd)
{
    CKRasterizerDeviceLibrary *rst = new CKRasterizerDeviceLibrary();
    if (!rst)
        return NULL;

    if (!rst->Start(AppWnd)) {
        delete rst;
        rst = NULL;
    }

    return rst;
}

void CKNULLRasterizerClose(CKRasterizerDeviceLibrary *rst)
{
    if (rst) {
        rst->Close();
        delete rst;
    }
}
