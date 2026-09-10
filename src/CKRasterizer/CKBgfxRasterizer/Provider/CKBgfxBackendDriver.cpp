// CKBgfxBackendLibrary / CKBgfxBackendDriver: the single bgfx adapter with
// its display modes (SDL) and capability baseline (spec 4.9.2).

#include "CKBgfxRasterizer.h"
#include "CKBgfxInternal.h"
#include "CKRasterizerCapsBaseline.h"

#include <SDL3/SDL.h>

#include <new>

// ===========================================================================
// CKBgfxBackendLibrary
// ===========================================================================

CKBgfxBackendLibrary::CKBgfxBackendLibrary() : m_MainWindow(NULL) {}

CKBgfxBackendLibrary::~CKBgfxBackendLibrary()
{
    Close();
}

CKBOOL CKBgfxBackendLibrary::Start(WIN_HANDLE AppWnd)
{
    if (m_Drivers.Size() > 0)
        return TRUE;

    m_MainWindow = AppWnd;
    auto *driver = new (std::nothrow) CKBgfxBackendDriver(this);
    if (!driver)
        return FALSE;

    m_Drivers.PushBack(driver);
    return TRUE;
}

void CKBgfxBackendLibrary::Close()
{
    for (int i = 0; i < m_Drivers.Size(); ++i)
        delete m_Drivers[i];
    m_Drivers.Clear();
}

CKRasterizerBackendDriver *CKBgfxBackendLibrary::GetDriver(CKDWORD Index) const
{
    return (int)Index < m_Drivers.Size() ? m_Drivers[(int)Index] : NULL;
}

// ===========================================================================
// CKBgfxBackendDriver
// ===========================================================================

CKBgfxBackendDriver::CKBgfxBackendDriver(CKBgfxBackendLibrary *owner)
    : m_Owner(owner)
{
    m_Desc = "bgfx Driver";
    InitializeDisplayCaps();
}

CKBgfxBackendDriver::~CKBgfxBackendDriver()
{
    for (int i = 0; i < m_Backends.Size(); ++i) {
        m_Backends[i]->Shutdown();
        delete m_Backends[i];
    }
    m_Backends.Clear();
}

void CKBgfxBackendDriver::GetShaderTargets(std::vector<CKBackendShaderTarget> &out) const
{
    out.clear();
    const CK_SHADER_PROFILE profiles[] = {
        CKRST_SHADER_PROFILE_DX11, CKRST_SHADER_PROFILE_DX12, CKRST_SHADER_PROFILE_SPIRV,
        CKRST_SHADER_PROFILE_GLSL, CKRST_SHADER_PROFILE_ESSL, CKRST_SHADER_PROFILE_MSL,
    };
    for (CK_SHADER_PROFILE profile : profiles) {
        CKBackendCaps caps;
        caps.ShaderFormat = CKRST_SHADER_FORMAT_BGFX;
        caps.ShaderProfile = profile;
        CKBackendShaderSet shaders;
        if (CKBgfxRasterizerShaderSet(caps, shaders))
            out.push_back({caps.ShaderFormat, caps.ShaderProfile});
    }
}

CKBOOL CKBgfxBackendDriver::GetShaderSet(const CKBackendCaps &caps, CKBackendShaderSet &out) const
{
    return CKBgfxRasterizerShaderSet(caps, out);
}

CKRasterizerBackend *CKBgfxBackendDriver::CreateBackend()
{
    if (m_Backends.Size() != 0) {
        CKBgfxLogf("Init", "multiple bgfx backends are unsupported");
        return NULL;
    }
    auto *backend = new (std::nothrow) CKBgfxBackend();
    if (!backend)
        return NULL;

    m_Backends.PushBack(backend);
    m_CapsUpToDate = FALSE;
    return backend;
}

void CKBgfxBackendDriver::RefreshCaps()
{
    if (m_CapsUpToDate || m_Backends.Size() == 0) return;
    const CKBgfxBackend &backend = *m_Backends[0];
    if (backend.GetDeviceStatus() != CK_OK) return;
    const CKBackendDeviceLimits &native = backend.GetDeviceLimits();
    m_Desc.Format("bgfx %s Driver", backend.GetRendererName());
    // The FFP-facing capability policy belongs to the rasterizer provider.
    // The native backend only reports device limits and format support.
    Vx3DCapsDesc limits = {};
    limits.MaxTextureWidth = limits.MaxTextureHeight = limits.MaxTextureRatio = native.MaxTextureSize;
    limits.MaxNumberTextureStage = limits.MaxNumberBlendStage =
        XMin(native.MaxTextureBindings, (CKDWORD)CKRST_MAX_TEXTURE_STAGES);
    CKRSTLowerCapsToLimits(&m_3DCaps, &limits);
    m_TextureFormats.Clear();
    for (int format = _32_ARGB8888; format <= _32_X8L8V8U8; ++format) {
        CKTextureFormatCaps formatCaps;
        if (backend.GetTextureFormatCaps((VX_PIXELFORMAT)format, &formatCaps) != CK_OK ||
            (formatCaps.Caps & CKRST_FORMAT_CAPS_TEXTURE_2D) == 0) continue;
        CKTextureDesc textureDesc;
        textureDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB;
        switch ((VX_PIXELFORMAT)format) {
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
        default: break;
        }
        VxPixelFormat2ImageDesc((VX_PIXELFORMAT)format, textureDesc.Format);
        m_TextureFormats.PushBack(textureDesc);
    }
    m_CapsUpToDate = TRUE;
}

CKBOOL CKBgfxBackendDriver::DestroyBackend(CKRasterizerBackend *Backend)
{
    if (!Backend)
        return FALSE;

    for (int i = 0; i < m_Backends.Size(); ++i)
    {
        if (m_Backends[i] == Backend)
        {
            if (!m_Backends[i]->IsIdle())
                return FALSE;
            m_Backends[i]->Shutdown();
            delete m_Backends[i];
            m_Backends.RemoveAt(i);
            return TRUE;
        }
    }
    return FALSE;
}
