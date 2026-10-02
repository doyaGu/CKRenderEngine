#include "CKBgfxRasterizer.h"

#include "CKBgfxInternal.h"
#include "CKBgfxRasterizerContext.h"
#include "CKRasterizerCapsBaseline.h"
#include "CKRasterizerDriverCaps.h"

#include <new>

CKBOOL CKBgfxRasterizer::Start(WIN_HANDLE appWindow)
{
    if (GetDriverCount() != 0)
        return TRUE;

    CKRasterizer::Start(appWindow);

    CKBgfxRasterizerDriver *driver = new (std::nothrow) CKBgfxRasterizerDriver(this, 0);
    if (!driver)
        return FALSE;

    AddDriver(driver);
    return TRUE;
}

CKBgfxRasterizerDriver::CKBgfxRasterizerDriver(CKBgfxRasterizer *owner, CKDWORD index)
    : CKRasterizerDriver(owner, index, "bgfx Driver", TRUE)
{
    CKRSTInitializeDriverCaps(m_DisplayModes, m_TextureFormats, m_NativeCaps);
    BuildShaderLibrary();
}

CKBgfxRasterizerDriver::~CKBgfxRasterizerDriver()
{
    DestroyContexts();
}

void CKBgfxRasterizerDriver::BuildShaderLibrary()
{
    for (CKDWORD index = 0; index < CKBgfxRasterizerShaderProfileCount(); ++index) {
        CKRasterizerDeviceCaps caps;
        caps.ShaderFormat = CKRST_SHADER_FORMAT_BGFX;
        caps.ShaderProfile = CKBgfxRasterizerShaderProfile(index);
        CKFFShaderSet shaderSet;
        if (CKBgfxRasterizerShaderSet(caps, shaderSet))
            m_Shaders.Add(shaderSet);
    }
}

void CKBgfxRasterizerDriver::GetShaderTargets(
    XClassArray<CKFFShaderTarget> &targets) const
{
    m_Shaders.GetTargets(targets);
}

CKBOOL CKBgfxRasterizerDriver::GetShaderSet(
    const CKRasterizerDeviceCaps &caps, CKFFShaderSet &shaderSet) const
{
    return m_Shaders.Find(caps.ShaderFormat, caps.ShaderProfile, shaderSet);
}

CKRasterizerContext *CKBgfxRasterizerDriver::CreateContext()
{
    if (m_Contexts.Size() != 0 || m_Shaders.Empty()) {
        if (m_Contexts.Size() != 0)
            CKBgfxLogf("Init", "multiple bgfx contexts are unsupported");
        return NULL;
    }

    CKBgfxRasterizerContext *context =
        new (std::nothrow) CKBgfxRasterizerContext(this, &m_Shaders);
    if (!context)
        return NULL;

    AddContext(context);
    m_CapsFinal = FALSE;
    return context;
}

void CKBgfxRasterizerDriver::RefreshCaps(CKBgfxRasterizerContext &context)
{
    if (context.GetDeviceStatus() != CK_OK)
        return;

    m_Description.Format("bgfx %s Driver", context.GetRendererName());
    CKFFUpdateDriverCaps(context.GetCaps(), m_NativeCaps);

    m_TextureFormats.Clear();
    for (int value = _32_ARGB8888; value <= _32_X8L8V8U8; ++value) {
        const VX_PIXELFORMAT format = static_cast<VX_PIXELFORMAT>(value);
        CKBgfxTextureFormatCaps formatCaps;
        if (context.GetTextureFormatCaps(format, &formatCaps) != CK_OK ||
            (formatCaps.Caps & CKRST_FORMAT_CAPS_TEXTURE_2D) == 0)
            continue;

        CKTextureDesc textureDesc;
        textureDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB;
        switch (format) {
        case _32_ARGB8888:
        case _16_ARGB1555:
        case _16_ARGB4444:
        case _32_ABGR8888:
        case _32_RGBA8888:
        case _32_BGRA8888:
        case _16_ABGR1555:
        case _16_ABGR4444:
        case _DXT1:
        case _DXT3:
        case _DXT5:
            textureDesc.Flags |= CKRST_TEXTURE_ALPHA;
            break;
        default:
            break;
        }
        VxPixelFormat2ImageDesc(format, textureDesc.Format);
        m_TextureFormats.PushBack(textureDesc);
        if (format == _DXT1 || format == _DXT3 || format == _DXT5)
            m_NativeCaps.Features |= CKRST_CAPS_TEXTURE_DXT;
    }
    m_CapsFinal = TRUE;
}
