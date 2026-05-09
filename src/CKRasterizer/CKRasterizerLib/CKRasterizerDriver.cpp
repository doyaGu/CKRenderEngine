#include "CKRasterizer.h"

#include <string.h>

CKRasterizerDriver::CKRasterizerDriver()
    : m_Hardware(FALSE),
      m_CapsUpToDate(FALSE),
      m_Owner(NULL),
      m_DriverIndex(0)
{
    memset(&m_3DCaps, 0, sizeof(m_3DCaps));
    memset(&m_2DCaps, 0, sizeof(m_2DCaps));
}

CKRasterizerDriver::~CKRasterizerDriver()
{
    for (auto it = m_Contexts.Begin(); it != m_Contexts.End(); ++it)
        delete *it;
    m_Contexts.Clear();
}

CKRasterizerContext *CKRasterizerDriver::CreateContext()
{
    CKRasterizerContext *context = new CKRasterizerContext();
    context->m_Driver = this;
    m_Contexts.PushBack(context);
    return context;
}

CKBOOL CKRasterizerDriver::DestroyContext(CKRasterizerContext *Context)
{
    if (!Context)
        return FALSE;

    for (int i = 0; i < m_Contexts.Size(); ++i) {
        if (m_Contexts[i] == Context) {
            delete m_Contexts[i];
            m_Contexts.RemoveAt(i);
            return TRUE;
        }
    }

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
    return CK_OK;
}

void CKRasterizerDriver::InitNULLRasterizerCaps(CKRasterizer *Owner)
{
    m_Owner = Owner;
    m_Desc = "NULL Rasterizer";
    m_Hardware = FALSE;
    m_CapsUpToDate = TRUE;
    m_DriverIndex = 0;

    m_DisplayModes.Clear();
    VxDisplayMode displayMode;
    displayMode.Width = 640;
    displayMode.Height = 480;
    displayMode.Bpp = 32;
    displayMode.RefreshRate = 0;
    m_DisplayModes.PushBack(displayMode);

    m_TextureFormats.Clear();
    CKTextureDesc textureDesc;
    memset(&textureDesc, 0, sizeof(textureDesc));
    textureDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
    VxPixelFormat2ImageDesc(_32_ARGB8888, textureDesc.Format);
    m_TextureFormats.PushBack(textureDesc);

    memset(&m_3DCaps, 0, sizeof(m_3DCaps));
    memset(&m_2DCaps, 0, sizeof(m_2DCaps));
    m_2DCaps.Caps = CKRST_2DCAPS_WINDOWED | CKRST_2DCAPS_3D | CKRST_2DCAPS_GDI;
}
