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
    CKRasterizerContext *context = new CKRasterizerContext();
    context->m_Driver = this;
    m_Contexts.PushBack(context);
    return context;
}

CKBOOL CKRasterizerDriver::DestroyContext(CKRasterizerContext *Context)
{
    m_Contexts.Remove(Context);
    delete Context;
    return TRUE;
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
    return CK_OK;
}
