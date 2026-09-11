#include "CKRecordingProvider.h"

#include "CKFFRasterizerContext.h"
#include "CKRasterizerCapsBaseline.h"

#include <new>
#include <string.h>

CKRecordingRasterizerDriver::CKRecordingRasterizerDriver(
    CKRasterizer *owner, CKDWORD index)
    : Format(CKRST_SHADER_FORMAT_BGFX), Profile(CKRST_SHADER_PROFILE_DX11),
      OriginBottomLeft(FALSE), HomogeneousDepth(FALSE)
{
    m_Owner = owner;
    m_DriverIndex = index;
    m_Desc = "Recording Rasterizer";
    m_Hardware = FALSE;
    m_CapsUpToDate = TRUE;

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
    for (size_t resolution = 0;
         resolution < sizeof(resolutions) / sizeof(resolutions[0]);
         ++resolution) {
        for (size_t bpp = 0; bpp < sizeof(bpps) / sizeof(bpps[0]); ++bpp) {
            VxDisplayMode displayMode;
            displayMode.Width = resolutions[resolution][0];
            displayMode.Height = resolutions[resolution][1];
            displayMode.Bpp = bpps[bpp];
            displayMode.RefreshRate = 60;
            m_DisplayModes.PushBack(displayMode);
        }
    }

    CKTextureDesc textureDesc;
    textureDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
    VxPixelFormat2ImageDesc(_32_ARGB8888, textureDesc.Format);
    m_TextureFormats.PushBack(textureDesc);

    memset(&m_3DCaps, 0, sizeof(m_3DCaps));
    memset(&m_2DCaps, 0, sizeof(m_2DCaps));
    if (!CKRSTGetCapsBaseline(&m_3DCaps, &m_2DCaps)) {
        m_3DCaps.MinTextureWidth = 1;
        m_3DCaps.MinTextureHeight = 1;
        m_3DCaps.MaxTextureWidth = 4096;
        m_3DCaps.MaxTextureHeight = 4096;
        m_3DCaps.MaxClipPlanes = CKRST_MAX_USER_CLIP_PLANES;
        m_3DCaps.MaxActiveLights = CKRST_MAX_LIGHTS;
        m_3DCaps.MaxNumberBlendStage = CKRST_MAX_TEXTURE_STAGES;
        m_3DCaps.MaxNumberTextureStage = CKRST_MAX_TEXTURE_STAGES;
        m_2DCaps.Caps = CKRST_2DCAPS_WINDOWED | CKRST_2DCAPS_3D | CKRST_2DCAPS_GDI;
    }
    m_3DCaps.CKRasterizerSpecificCaps &=
        ~(CKRST_SPECIFICCAPS_HARDWARE | CKRST_SPECIFICCAPS_HARDWARETL);
    m_3DCaps.CKRasterizerSpecificCaps |= CKRST_SPECIFICCAPS_SOFTWARE;
}
CKRecordingRasterizerDriver::~CKRecordingRasterizerDriver()
{
    while (m_Contexts.Size() > 0) {
        const int index = m_Contexts.Size() - 1;
        CKRasterizerContext *context = m_Contexts[index];
        if (!DestroyContext(context)) {
            context->BeginShutdown();
            CKFFDeleteRasterizerContext(context);
            m_Contexts.PopBack();
            m_ContextBackends.PopBack();
        }
    }

    for (int index = 0; index < m_Backends.Size(); ++index) {
        m_Backends[index]->Shutdown();
        delete m_Backends[index];
    }
    m_Backends.Clear();
}

CKRecordingBackend *CKRecordingRasterizerDriver::NewBackend()
{
    return new (std::nothrow) CKRecordingBackend(GetBackendConventions());
}

CKBackendCaps CKRecordingRasterizerDriver::GetBackendConventions() const
{
    CKBackendCaps caps;
    caps.ShaderFormat = Format;
    caps.ShaderProfile = Profile;
    caps.OriginBottomLeft = OriginBottomLeft;
    caps.HomogeneousDepth = HomogeneousDepth;
    return caps;
}

void CKRecordingRasterizerDriver::GetShaderTargets(
    std::vector<CKBackendShaderTarget> &out) const
{
    out.clear();
    CKBackendShaderTarget target;
    target.Format = Format;
    target.Profile = Profile;
    out.push_back(target);
}

CKBOOL CKRecordingRasterizerDriver::GetShaderSet(
    const CKBackendCaps &caps, CKBackendShaderSet &out) const
{
    return CKRecordingShaderSet(caps, out);
}

CKBOOL CKRecordingRasterizerDriver::BuildShaderLibrary(
    CKFFShaderLibrary &shaders) const
{
    std::vector<CKBackendShaderTarget> targets;
    GetShaderTargets(targets);
    for (size_t index = 0; index < targets.size(); ++index) {
        CKBackendCaps caps;
        caps.ShaderFormat = targets[index].Format;
        caps.ShaderProfile = targets[index].Profile;
        CKBackendShaderSet shaderSet;
        if (GetShaderSet(caps, shaderSet))
            shaders.Add(shaderSet);
    }
    return shaders.Empty() ? FALSE : TRUE;
}

CKRasterizerBackend *CKRecordingRasterizerDriver::CreateBackend()
{
    CKRecordingBackend *backend = NewBackend();
    if (!backend)
        return NULL;
    m_Backends.PushBack(backend);
    return backend;
}

CKBOOL CKRecordingRasterizerDriver::DestroyBackend(CKRasterizerBackend *backend)
{
    if (!backend)
        return FALSE;

    for (int index = 0; index < m_Backends.Size(); ++index) {
        if (m_Backends[index] != backend)
            continue;
        if (!m_Backends[index]->IsIdle())
            return FALSE;

        m_Backends[index]->Shutdown();
        delete m_Backends[index];
        m_Backends.RemoveAt(index);
        return TRUE;
    }
    return FALSE;
}

CKRasterizerContext *CKRecordingRasterizerDriver::CreateContext()
{
    CKFFShaderLibrary shaders;
    if (!BuildShaderLibrary(shaders))
        return NULL;

    CKRecordingBackend *backend =
        static_cast<CKRecordingBackend *>(CreateBackend());
    if (!backend)
        return NULL;

    CKFFRasterizerContextDesc desc;
    desc.Driver = this;
    desc.Backend = backend;
    desc.Shaders = &shaders;
    CKRasterizerContext *context = CKFFCreateRasterizerContext(desc);
    if (!context) {
        DestroyBackend(backend);
        return NULL;
    }

    m_Contexts.PushBack(context);
    m_ContextBackends.PushBack(backend);
    return context;
}

CKBOOL CKRecordingRasterizerDriver::DestroyContext(CKRasterizerContext *context)
{
    if (!context)
        return FALSE;

    for (int index = 0; index < m_Contexts.Size(); ++index) {
        if (m_Contexts[index] != context)
            continue;

        CKRecordingBackend *backend = m_ContextBackends[index];
        if (!context->BeginShutdown() || !DestroyBackend(backend))
            return FALSE;

        CKFFDeleteRasterizerContext(context);
        m_Contexts.RemoveAt(index);
        m_ContextBackends.RemoveAt(index);
        return TRUE;
    }
    return FALSE;
}

CKRecordingRasterizer::~CKRecordingRasterizer()
{
    Close();
}

CKRecordingRasterizerDriver *CKRecordingRasterizer::NewDriver()
{
    return new (std::nothrow) CKRecordingRasterizerDriver(this, 0);
}

CKBOOL CKRecordingRasterizer::Start(WIN_HANDLE appWindow)
{
    if (m_Drivers.Size() > 0)
        return TRUE;

    m_MainWindow = appWindow;
    CKRecordingRasterizerDriver *driver = NewDriver();
    if (!driver)
        return FALSE;

    driver->m_Owner = this;
    driver->m_DriverIndex = 0;
    m_Drivers.PushBack(driver);
    return TRUE;
}

void CKRecordingRasterizer::Close()
{
    for (int index = 0; index < m_Drivers.Size(); ++index)
        delete m_Drivers[index];
    m_Drivers.Clear();
}

void CKRecordingShaderTargets(std::vector<CKBackendShaderTarget> &out)
{
    out.clear();
    CKBackendShaderTarget target;
    target.Format = CKRST_SHADER_FORMAT_BGFX;
    target.Profile = CKRST_SHADER_PROFILE_DX11;
    out.push_back(target);
}

CKBOOL CKRecordingShaderSet(const CKBackendCaps &caps, CKBackendShaderSet &out)
{
    static const CKBYTE token[CKRST_BUILTIN_SHADER_COUNT][4] = {
        {'N', 0}, {'N', 1}, {'N', 2}, {'N', 3}, {'N', 4}, {'N', 5}, {'N', 6}};
    out = CKBackendShaderSet();
    if (caps.ShaderFormat == CKRST_SHADER_FORMAT_UNKNOWN ||
        caps.ShaderProfile == CKRST_SHADER_PROFILE_UNKNOWN)
        return FALSE;

    out.ABIVersion = CKFF_SHADER_ABI_VERSION;
    out.InterfaceHash = CKFFShaderInterfaceHash(caps.ShaderFormat);
    for (unsigned index = 0; index < CKRST_BUILTIN_SHADER_COUNT; ++index) {
        CKShaderDesc &shader = out.Shaders[index];
        shader.Code = token[index];
        shader.CodeSize = sizeof(token[index]);
        shader.Format = caps.ShaderFormat;
        shader.Profile = caps.ShaderProfile;
        shader.Stage =
            index == CKRST_SHADER_FF_FRAGMENT || index == CKRST_SHADER_PRESENT_FRAGMENT
                ? CKRST_SHADER_PIXEL
                : CKRST_SHADER_VERTEX;
        if (caps.ShaderFormat != CKRST_SHADER_FORMAT_BGFX) {
            shader.UniformBufferCount = index == CKRST_SHADER_PRESENT_VERTEX ? 0 : 1;
            shader.SamplerCount = index == CKRST_SHADER_FF_FRAGMENT
                                      ? CKFF_SHADER_SAMPLER_SLOT_COUNT
                                      : index == CKRST_SHADER_PRESENT_FRAGMENT ? 1 : 0;
        }
    }
    return TRUE;
}
