#include "CKRecordingRasterizer.h"
#include "CKFFShaderInterface.h"
#include "FFPRecordingContext.h"

#include <new>
#include <string.h>

CKRecordingRasterizerDriver::CKRecordingRasterizerDriver(
    CKRasterizer *owner, CKDWORD index)
    : CKRasterizerDriver(owner, index, "Recording Rasterizer", FALSE),
      Format(CKRST_SHADER_FORMAT_DXIL), Profile(CKRST_SHADER_PROFILE_DX12),
      OriginBottomLeft(FALSE), HomogeneousDepth(FALSE)
{
    m_CapsFinal = TRUE;
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

    m_NativeCaps.MaxTextureSize = 4096;
    m_NativeCaps.MaxTextureStages = CKRST_MAX_TEXTURE_STAGES;
    m_NativeCaps.MaxAnisotropy = 1;
    m_NativeCaps.MaxUserClipPlanes = CKRST_MAX_USER_CLIP_PLANES;
    m_NativeCaps.MaxVertexBlendMatrices = CKRST_MAX_WORLD_MATRICES;
    m_NativeCaps.MaxMSAASamples = 1;
    m_NativeCaps.MaxPointSize = 1.0f;
    m_NativeCaps.MaxLights = CKRST_MAX_LIGHTS;
}
CKRecordingRasterizerDriver::~CKRecordingRasterizerDriver()
{
    while (m_Contexts.Size() > 0) {
        const int index = m_Contexts.Size() - 1;
        FFPRecordingContext *context =
            static_cast<FFPRecordingContext *>(m_Contexts[index]);
        if (!DestroyContext(context)) {
            context->BeginShutdown();
            delete context;
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

CKRasterizerDeviceCaps CKRecordingRasterizerDriver::GetBackendConventions() const
{
    CKRasterizerDeviceCaps caps;
    caps.ShaderFormat = Format;
    caps.ShaderProfile = Profile;
    caps.OriginBottomLeft = OriginBottomLeft;
    caps.HomogeneousDepth = HomogeneousDepth;
    return caps;
}

void CKRecordingRasterizerDriver::GetShaderTargets(
    XClassArray<CKFFShaderTarget> &out) const
{
    out.Clear();
    CKFFShaderTarget target;
    target.Format = Format;
    target.Profile = Profile;
    out.PushBack(target);
}

CKBOOL CKRecordingRasterizerDriver::GetShaderSet(
    const CKRasterizerDeviceCaps &caps, CKFFShaderSet &out) const
{
    return CKRecordingShaderSet(caps, out);
}

CKBOOL CKRecordingRasterizerDriver::BuildShaderLibrary(
    CKFFShaderLibrary &shaders) const
{
    XClassArray<CKFFShaderTarget> targets;
    GetShaderTargets(targets);
    for (int index = 0; index < targets.Size(); ++index) {
        CKRasterizerDeviceCaps caps;
        caps.ShaderFormat = targets[index].Format;
        caps.ShaderProfile = targets[index].Profile;
        CKFFShaderSet shaderSet;
        if (GetShaderSet(caps, shaderSet))
            shaders.Add(shaderSet);
    }
    return shaders.Empty() ? FALSE : TRUE;
}

CKRecordingBackend *CKRecordingRasterizerDriver::CreateBackend()
{
    CKRecordingBackend *backend = NewBackend();
    if (!backend)
        return NULL;
    m_Backends.PushBack(backend);
    return backend;
}

CKBOOL CKRecordingRasterizerDriver::DestroyBackend(CKRecordingBackend *backend)
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

    CKRecordingBackend *backend = CreateBackend();
    if (!backend)
        return NULL;

    FFPRecordingContextDesc desc;
    desc.Driver = this;
    desc.Backend = backend;
    desc.Shaders = &shaders;
    FFPRecordingContext *context = new (std::nothrow) FFPRecordingContext(desc);
    if (!context) {
        DestroyBackend(backend);
        return NULL;
    }

    AddContext(context);
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

        delete static_cast<FFPRecordingContext *>(context);
        m_Contexts.RemoveAt(index);
        m_ContextBackends.RemoveAt(index);
        return TRUE;
    }
    return FALSE;
}

CKRecordingRasterizerDriver *CKRecordingRasterizer::NewDriver()
{
    return new (std::nothrow) CKRecordingRasterizerDriver(this, 0);
}

CKBOOL CKRecordingRasterizer::Start(WIN_HANDLE appWindow)
{
    if (GetDriverCount() != 0)
        return TRUE;

    CKRasterizer::Start(appWindow);
    CKRecordingRasterizerDriver *driver = NewDriver();
    if (!driver)
        return FALSE;

    AddDriver(driver);
    return TRUE;
}

void CKRecordingShaderTargets(XClassArray<CKFFShaderTarget> &out)
{
    out.Clear();
    CKFFShaderTarget target;
    target.Format = CKRST_SHADER_FORMAT_DXIL;
    target.Profile = CKRST_SHADER_PROFILE_DX12;
    out.PushBack(target);
}

// The uniform buffers one stage of the native fixed-function interface declares.
static CKDWORD StageBufferCount(const CKFFProgramDesc &program, CK_SHADER_STAGE stage)
{
    CKDWORD count = 0;
    for (int i = 0; i < program.UniformBuffers.Size(); ++i)
        count += program.UniformBuffers[i].Stage == stage ? 1u : 0u;
    return count;
}

CKBOOL CKRecordingShaderSet(const CKRasterizerDeviceCaps &caps, CKFFShaderSet &out)
{
    static const CKBYTE token[CKRST_BUILTIN_SHADER_COUNT][4] = {
        {'N', 0}, {'N', 1}, {'N', 2}, {'N', 3}, {'N', 4}, {'N', 5}, {'N', 6}};
    out = CKFFShaderSet();
    if (caps.ShaderFormat == CKRST_SHADER_FORMAT_UNKNOWN ||
        caps.ShaderProfile == CKRST_SHADER_PROFILE_UNKNOWN)
        return FALSE;

    out.ABIVersion = CKFF_SHADER_ABI_VERSION;
    out.InterfaceHash = CKFF_SHADER_NATIVE_INTERFACE_HASH;
    const CKFFProgramDesc programs[3] = {CKFFBuildProgramInterface(0, 0),
        CKFFBuildProgramInterface(0, 0, FALSE, TRUE), CKFFBuildProgramInterface(0, 0, TRUE)};
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
        const CKFFProgramDesc &program =
            index == CKRST_SHADER_FF_POSITIONT || index == CKRST_SHADER_FF_POSITIONT_CLIP ? programs[1] :
            index == CKRST_SHADER_PRESENT_VERTEX || index == CKRST_SHADER_PRESENT_FRAGMENT ? programs[2] :
            programs[0];
        shader.UniformBufferCount = StageBufferCount(program, shader.Stage);
        shader.SamplerCount = index == CKRST_SHADER_FF_FRAGMENT
                                  ? CKFF_SHADER_SAMPLER_SLOT_COUNT
                                  : index == CKRST_SHADER_PRESENT_FRAGMENT ? 1 : 0;
    }
    return TRUE;
}
