#include "CKNullRasterizer.h"
#include "CKRasterizerCapsBaseline.h"

#include <new>
#include <string.h>

// ===========================================================================
// CKNullBackendDriver
// ===========================================================================

CKNullBackendDriver::CKNullBackendDriver()
    : Format(CKRST_SHADER_FORMAT_BGFX), Profile(CKRST_SHADER_PROFILE_DX11),
      OriginBottomLeft(FALSE), HomogeneousDepth(FALSE)
{
    m_Desc = "NULL Rasterizer";
    m_Hardware = FALSE;
    m_CapsUpToDate = TRUE;
    m_DriverIndex = 0;

    static const int resolutions[][2] = {
        {640, 480},
        {800, 600},
        {1024, 768},
        {1280, 720},
        {1280, 960},
        {1280, 1024},
        {1366, 768},
        {1600, 900},
        {1920, 1080},
    };
    static const int bpps[] = {16, 32};
    for (int i = 0; i < (int)(sizeof(resolutions) / sizeof(resolutions[0])); ++i) {
        for (int j = 0; j < (int)(sizeof(bpps) / sizeof(bpps[0])); ++j) {
            VxDisplayMode displayMode;
            displayMode.Width = resolutions[i][0];
            displayMode.Height = resolutions[i][1];
            displayMode.Bpp = bpps[j];
            displayMode.RefreshRate = 60;
            m_DisplayModes.PushBack(displayMode);
        }
    }

    CKTextureDesc textureDesc;
    textureDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
    VxPixelFormat2ImageDesc(_32_ARGB8888, textureDesc.Format);
    m_TextureFormats.PushBack(textureDesc);

    // Capability baseline (spec 4.9.2). The NULL backend neither transforms
    // nor rasterizes in hardware, so only the hardware / software bits differ.
    memset(&m_3DCaps, 0, sizeof(m_3DCaps));
    memset(&m_2DCaps, 0, sizeof(m_2DCaps));
    if (!CKRSTGetCapsBaseline(&m_3DCaps, &m_2DCaps)) {
        m_3DCaps.MinTextureWidth = m_3DCaps.MinTextureHeight = 1;
        m_3DCaps.MaxTextureWidth = m_3DCaps.MaxTextureHeight = 4096;
        m_3DCaps.MaxClipPlanes = CKRST_MAX_USER_CLIP_PLANES;
        m_3DCaps.MaxActiveLights = CKRST_MAX_LIGHTS;
        m_3DCaps.MaxNumberBlendStage = CKRST_MAX_TEXTURE_STAGES;
        m_3DCaps.MaxNumberTextureStage = CKRST_MAX_TEXTURE_STAGES;
        m_2DCaps.Caps = CKRST_2DCAPS_WINDOWED | CKRST_2DCAPS_3D | CKRST_2DCAPS_GDI;
    }
    m_3DCaps.CKRasterizerSpecificCaps &= ~(CKRST_SPECIFICCAPS_HARDWARE | CKRST_SPECIFICCAPS_HARDWARETL);
    m_3DCaps.CKRasterizerSpecificCaps |= CKRST_SPECIFICCAPS_SOFTWARE;
}

CKNullBackendDriver::~CKNullBackendDriver()
{
    for (int i = 0; i < m_Backends.Size(); ++i) {
        m_Backends[i]->Shutdown();
        delete m_Backends[i];
    }
    m_Backends.Clear();
}

CKNullBackend *CKNullBackendDriver::NewBackend()
{
    return new (std::nothrow) CKNullBackend(GetBackendConventions());
}

CKBackendCaps CKNullBackendDriver::GetBackendConventions() const
{
    CKBackendCaps caps;
    caps.ShaderFormat = Format;
    caps.ShaderProfile = Profile;
    caps.OriginBottomLeft = OriginBottomLeft;
    caps.HomogeneousDepth = HomogeneousDepth;
    return caps;
}

void CKNullBackendDriver::GetShaderTargets(std::vector<CKBackendShaderTarget> &Out) const
{
    Out = {{Format, Profile}};
}

CKBOOL CKNullBackendDriver::GetShaderSet(const CKBackendCaps &Caps, CKBackendShaderSet &Out) const
{
    return CKNullRasterizerShaderSet(Caps, Out);
}

CKRasterizerBackend *CKNullBackendDriver::CreateBackend()
{
    CKNullBackend *backend = NewBackend();
    if (!backend)
        return NULL;
    m_Backends.PushBack(backend);
    return backend;
}

CKBOOL CKNullBackendDriver::DestroyBackend(CKRasterizerBackend *Backend)
{
    if (!Backend)
        return FALSE;
    for (int i = 0; i < m_Backends.Size(); ++i) {
        if (m_Backends[i] != Backend)
            continue;
        if (!m_Backends[i]->IsIdle())
            return FALSE;
        m_Backends[i]->Shutdown();
        delete m_Backends[i];
        m_Backends.RemoveAt(i);
        return TRUE;
    }
    return FALSE;
}

// ===========================================================================
// CKNullBackendLibrary
// ===========================================================================

CKNullBackendLibrary::CKNullBackendLibrary() : m_MainWindow(NULL) {}

CKNullBackendLibrary::~CKNullBackendLibrary()
{
    Close();
}

CKNullBackendDriver *CKNullBackendLibrary::NewDriver()
{
    return new (std::nothrow) CKNullBackendDriver();
}

CKBOOL CKNullBackendLibrary::Start(WIN_HANDLE AppWnd)
{
    m_MainWindow = AppWnd;
    if (m_Drivers.Size() > 0)
        return TRUE;
    CKNullBackendDriver *driver = NewDriver();
    if (!driver)
        return FALSE;
    driver->m_DriverIndex = 0;
    m_Drivers.PushBack(driver);
    return TRUE;
}

void CKNullBackendLibrary::Close()
{
    for (int i = 0; i < m_Drivers.Size(); ++i)
        delete m_Drivers[i];
    m_Drivers.Clear();
}

CKRasterizerBackendDriver *CKNullBackendLibrary::GetDriver(CKDWORD Index) const
{
    return Index < static_cast<CKDWORD>(m_Drivers.Size()) ? m_Drivers[static_cast<int>(Index)] : NULL;
}

void CKNullRasterizerShaderTargets(std::vector<CKBackendShaderTarget> &Out)
{
    Out = {{CKRST_SHADER_FORMAT_BGFX, CKRST_SHADER_PROFILE_DX11}};
}

CKBOOL CKNullRasterizerShaderSet(const CKBackendCaps &Caps, CKBackendShaderSet &Out)
{
    static const CKBYTE token[CKRST_BUILTIN_SHADER_COUNT][4] = {
        {'N', 0}, {'N', 1}, {'N', 2}, {'N', 3}, {'N', 4}, {'N', 5}, {'N', 6}};
    Out = CKBackendShaderSet();
    if (Caps.ShaderFormat == CKRST_SHADER_FORMAT_UNKNOWN || Caps.ShaderProfile == CKRST_SHADER_PROFILE_UNKNOWN)
        return FALSE;
    Out.ABIVersion = CKFF_SHADER_ABI_VERSION;
    Out.InterfaceHash = CKFFShaderInterfaceHash(Caps.ShaderFormat);
    for (unsigned i = 0; i < CKRST_BUILTIN_SHADER_COUNT; ++i) {
        CKShaderDesc &shader = Out.Shaders[i];
        shader.Code = token[i];
        shader.CodeSize = sizeof(token[i]);
        shader.Format = Caps.ShaderFormat;
        shader.Profile = Caps.ShaderProfile;
        shader.Stage = i == CKRST_SHADER_FF_FRAGMENT || i == CKRST_SHADER_PRESENT_FRAGMENT
            ? CKRST_SHADER_PIXEL : CKRST_SHADER_VERTEX;
        if (Caps.ShaderFormat != CKRST_SHADER_FORMAT_BGFX) {
            shader.UniformBufferCount = i == CKRST_SHADER_PRESENT_VERTEX ? 0 : 1;
            shader.SamplerCount = i == CKRST_SHADER_FF_FRAGMENT ? CKFF_SAMPLER_SLOT_COUNT :
                i == CKRST_SHADER_PRESENT_FRAGMENT ? 1 : 0;
        }
    }
    return TRUE;
}
