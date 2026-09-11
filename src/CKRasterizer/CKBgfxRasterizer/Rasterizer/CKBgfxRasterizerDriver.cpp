#include "CKBgfxRasterizer.h"

#include "CKBgfxInternal.h"
#include "CKFFRasterizerContext.h"
#include "CKRasterizerCapsBaseline.h"
#include "CKRasterizerDriverCaps.h"

#include <new>

CKBgfxRasterizer::~CKBgfxRasterizer()
{
    Close();
}
CKBOOL CKBgfxRasterizer::Start(WIN_HANDLE appWindow)
{
    if (m_Drivers.Size() > 0)
        return TRUE;

    m_MainWindow = appWindow;
    CKBgfxRasterizerDriver *driver = new (std::nothrow) CKBgfxRasterizerDriver(this, 0);
    if (!driver)
        return FALSE;

    m_Drivers.PushBack(driver);
    return TRUE;
}

void CKBgfxRasterizer::Close()
{
    for (int index = 0; index < m_Drivers.Size(); ++index)
        delete m_Drivers[index];
    m_Drivers.Clear();
}

CKBgfxRasterizerDriver::CKBgfxRasterizerDriver(CKBgfxRasterizer *owner, CKDWORD index)
{
    m_Owner = owner;
    m_DriverIndex = index;
    m_Desc = "bgfx Driver";
    CKRSTInitializeDriverCaps(this);
    BuildShaderLibrary();
}

CKBgfxRasterizerDriver::~CKBgfxRasterizerDriver()
{
    while (m_Contexts.Size() > 0) {
        const int index = m_Contexts.Size() - 1;
        CKRasterizerContext *context = m_Contexts[index];
        CKBgfxBackend *backend = m_Backends[index];
        context->BeginShutdown();
        CKFFDeleteRasterizerContext(context);
        delete backend;
        m_Contexts.PopBack();
        m_Backends.PopBack();
    }
}

void CKBgfxRasterizerDriver::BuildShaderLibrary()
{
    const CK_SHADER_PROFILE profiles[] = {
        CKRST_SHADER_PROFILE_DX11,
        CKRST_SHADER_PROFILE_DX12,
        CKRST_SHADER_PROFILE_SPIRV,
        CKRST_SHADER_PROFILE_GLSL,
        CKRST_SHADER_PROFILE_ESSL,
        CKRST_SHADER_PROFILE_MSL,
    };
    for (size_t index = 0; index < sizeof(profiles) / sizeof(profiles[0]); ++index) {
        CKBackendCaps caps;
        caps.ShaderFormat = CKRST_SHADER_FORMAT_BGFX;
        caps.ShaderProfile = profiles[index];
        CKBackendShaderSet shaderSet;
        if (CKBgfxRasterizerShaderSet(caps, shaderSet))
            m_Shaders.Add(shaderSet);
    }
}

void CKBgfxRasterizerDriver::GetShaderTargets(
    std::vector<CKBackendShaderTarget> &targets) const
{
    m_Shaders.GetTargets(targets);
}

CKBOOL CKBgfxRasterizerDriver::GetShaderSet(
    const CKBackendCaps &caps, CKBackendShaderSet &shaderSet) const
{
    return m_Shaders.Find(caps.ShaderFormat, caps.ShaderProfile, shaderSet);
}

CKRasterizerContext *CKBgfxRasterizerDriver::CreateContext()
{
    if (m_Backends.Size() != 0 || m_Shaders.Empty()) {
        if (m_Backends.Size() != 0)
            CKBgfxLogf("Init", "multiple bgfx contexts are unsupported");
        return NULL;
    }

    CKBgfxBackend *backend = new (std::nothrow) CKBgfxBackend();
    if (!backend)
        return NULL;

    CKFFRasterizerContextDesc desc;
    desc.Driver = this;
    desc.Backend = backend;
    desc.Shaders = &m_Shaders;
    desc.BackendReady = OnBackendReady;
    desc.BackendReadyUser = this;

    CKRasterizerContext *context = CKFFCreateRasterizerContext(desc);
    if (!context) {
        delete backend;
        return NULL;
    }

    m_Backends.PushBack(backend);
    m_Contexts.PushBack(context);
    m_CapsUpToDate = FALSE;
    return context;
}

CKBOOL CKBgfxRasterizerDriver::DestroyContext(CKRasterizerContext *context)
{
    if (!context)
        return FALSE;

    for (int index = 0; index < m_Contexts.Size(); ++index) {
        if (m_Contexts[index] != context)
            continue;

        CKBgfxBackend *backend = m_Backends[index];
        if (!context->BeginShutdown() || !backend->IsIdle())
            return FALSE;

        CKFFDeleteRasterizerContext(context);
        delete backend;
        m_Contexts.RemoveAt(index);
        m_Backends.RemoveAt(index);
        return TRUE;
    }
    return FALSE;
}

void CKBgfxRasterizerDriver::OnBackendReady(void *user, CKRasterizerBackend *backend)
{
    CKBgfxRasterizerDriver *driver = static_cast<CKBgfxRasterizerDriver *>(user);
    if (driver && backend)
        driver->RefreshCaps(*static_cast<CKBgfxBackend *>(backend));
}

void CKBgfxRasterizerDriver::RefreshCaps(CKBgfxBackend &backend)
{
    if (backend.GetDeviceStatus() != CK_OK)
        return;

    const CKBackendDeviceLimits &native = backend.GetDeviceLimits();
    m_Desc.Format("bgfx %s Driver", backend.GetRendererName());

    Vx3DCapsDesc limits = {};
    limits.MaxTextureWidth = native.MaxTextureSize;
    limits.MaxTextureHeight = native.MaxTextureSize;
    limits.MaxTextureRatio = native.MaxTextureSize;
    limits.MaxNumberTextureStage =
        XMin(native.MaxTextureBindings, static_cast<CKDWORD>(CKRST_MAX_TEXTURE_STAGES));
    limits.MaxNumberBlendStage = limits.MaxNumberTextureStage;
    CKRSTLowerCapsToLimits(&m_3DCaps, &limits);

    m_TextureFormats.Clear();
    for (int value = _32_ARGB8888; value <= _32_X8L8V8U8; ++value) {
        const VX_PIXELFORMAT format = static_cast<VX_PIXELFORMAT>(value);
        CKTextureFormatCaps formatCaps;
        if (backend.GetTextureFormatCaps(format, &formatCaps) != CK_OK ||
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
    }
    m_CapsUpToDate = TRUE;
}
