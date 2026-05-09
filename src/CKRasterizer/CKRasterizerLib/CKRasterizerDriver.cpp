#include "CKRasterizer.h"

#include <string.h>

CKRasterizerDriver::CKRasterizerDriver()
    : m_Hardware(FALSE),
      m_CapsUpToDate(FALSE),
      m_Owner(NULL),
      m_DriverIndex(0)
{
}

CKRasterizerDriver::~CKRasterizerDriver() = default;

CKRasterizerContext *CKRasterizerDriver::CreateContext()
{
    return NULL;
}

CKBOOL CKRasterizerDriver::DestroyContext(CKRasterizerContext *)
{
    return FALSE;
}

CKERROR CKRasterizerDriver::GetShaderTarget(CKShaderTargetDesc *Target) const
{
    if (Target)
        *Target = CKShaderTargetDesc();
    return CKERR_NOTIMPLEMENTED;
}

CKERROR CKRasterizerDriver::GetProgrammableCaps(VxProgCapsDesc &Caps)
{
    memset(&Caps, 0, sizeof(Caps));
    return CKERR_NOTIMPLEMENTED;
}
