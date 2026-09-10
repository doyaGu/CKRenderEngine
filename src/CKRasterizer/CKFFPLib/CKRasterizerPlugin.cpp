#include "CKRasterizerPlugin.h"

#include <string.h>

CKRasterizerBackendDriver::CKRasterizerBackendDriver()
    : m_Hardware(FALSE), m_CapsUpToDate(FALSE), m_DriverIndex(0)
{
    memset(&m_3DCaps, 0, sizeof(m_3DCaps));
    memset(&m_2DCaps, 0, sizeof(m_2DCaps));
}
