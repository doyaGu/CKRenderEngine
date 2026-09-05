#include "CKPresentStage.h"

#include <math.h>
#include <string.h>

CKDWORD CKPresentStage::ScaledDimension(CKDWORD value, float scale, CKDWORD maximum)
{
    if (maximum == 0)
        return 0;
    if (value == 0)
        value = 1;
    const float scaled = (float)value * scale;
    if (scaled <= 1.0f)
        return 1;
    if (scaled >= (float)maximum)
        return maximum;
    return (CKDWORD)(scaled + 0.5f);
}

float CKPresentStage::ClampRenderScale(float scale)
{
    if (!(scale > 0.0f) || !isfinite(scale))
        return 1.0f;
    if (scale < 0.5f)
        return 0.5f;
    if (scale > 2.0f)
        return 2.0f;
    return scale;
}

float CKPresentStage::ClampSharpness(float sharpness)
{
    if (!(sharpness > 0.0f) || !isfinite(sharpness))
        return 0.0f;
    if (sharpness > 1.0f)
        return 1.0f;
    return sharpness;
}

CKPresentStage::CKPresentStage()
    : m_Backend(nullptr), m_ReadbackTexture(0), m_ReadbackWidth(0), m_ReadbackHeight(0),
      m_ShaderFormat(CKRST_SHADER_FORMAT_UNKNOWN),
      m_ShaderProfile(CKRST_SHADER_PROFILE_UNKNOWN) {}

CKPresentStage::~CKPresentStage()
{
    Shutdown();
}

void CKPresentStage::Init(CKRasterizerBackend *backend, const CKBackendShaderSet &shaders)
{
    Shutdown();
    m_Backend = backend;
    m_Shaders = shaders;
}

void CKPresentStage::Shutdown()
{
    DestroyTargets();
    DestroyResources();
    m_Backend = nullptr;
    m_Shaders = CKBackendShaderSet();
    m_ResourceIds = CKPresentResources();
}

CKBOOL CKPresentStage::EnsureSceneTarget(CKDWORD width, CKDWORD height, CKDWORD samples)
{
    if (!m_Backend || width == 0 || height == 0)
        return FALSE;
    if (samples <= 1)
        samples = 0;
    if (m_Scene.IsActive() && m_Scene.Width == width && m_Scene.Height == height && m_Scene.Samples == samples)
        return TRUE;
    DestroyTarget(m_Scene);
    return CreateTarget(m_Scene, width, height, samples);
}

CKBOOL CKPresentStage::EnsureNativeTarget(CKDWORD width, CKDWORD height)
{
    if (!m_Backend || width == 0 || height == 0)
        return FALSE;
    if (m_Native.IsActive() && m_Native.Width == width && m_Native.Height == height)
        return TRUE;
    DestroyTarget(m_Native);
    return CreateTarget(m_Native, width, height, 0);
}

void CKPresentStage::DestroyTargets()
{
    DestroyTarget(m_Scene);
    DestroyTarget(m_Native);
    DestroyReadbackTexture();
}

CKDWORD CKPresentStage::AcquireReadbackTexture(CKDWORD width, CKDWORD height)
{
    if (!m_Backend || width == 0 || height == 0)
        return 0;
    if (m_ReadbackTexture && m_ReadbackWidth == width && m_ReadbackHeight == height)
        return m_ReadbackTexture;
    DestroyReadbackTexture();
    CKTextureDesc desc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
    desc.Format.Width = (int)width;
    desc.Format.Height = (int)height;
    desc.MipMapCount = 1;
    desc.Depth = 1;
    desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA |
                 CKRST_TEXTURE_BLIT_DST | CKRST_TEXTURE_READBACK;
    if (m_Backend->CreateTexture(&desc, nullptr, &m_ReadbackTexture) != CK_OK) {
        m_ReadbackTexture = 0;
        return 0;
    }
    m_ReadbackWidth = width;
    m_ReadbackHeight = height;
    return m_ReadbackTexture;
}

void CKPresentStage::DestroyReadbackTexture()
{
    if (m_Backend && m_ReadbackTexture)
        m_Backend->DestroyObject(m_ReadbackTexture, CKRST_OBJ_TEXTURE);
    m_ReadbackTexture = 0;
    m_ReadbackWidth = m_ReadbackHeight = 0;
}

CKBOOL CKPresentStage::CreateTarget(CKPresentTarget &target, CKDWORD width, CKDWORD height,
                                       CKDWORD samples)
{
    target = CKPresentTarget();
    const CKDWORD msaaFlag = CKRSTTextureMSAAFlag(samples);
    if (samples > 1 && msaaFlag == 0)
        return FALSE;

    CKTextureDesc colorDesc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, colorDesc.Format);
    colorDesc.Format.Width = (int)width;
    colorDesc.Format.Height = (int)height;
    colorDesc.MipMapCount = 1;
    colorDesc.Depth = 1;
    colorDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB |
                      CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_RENDERTARGET | msaaFlag;
    if (m_Backend->CreateTexture(&colorDesc, nullptr, &target.ColorTexture) != CK_OK)
        return FALSE;

    CKBackendDepthDesc depthDesc;
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.Samples = samples;
    static const CK_DEPTH_FORMAT kDepthFormats[] = {CKRST_DEPTHFMT_D24S8, CKRST_DEPTHFMT_D24, CKRST_DEPTHFMT_D16};
    CKERROR depthErr = CKERR_NOTIMPLEMENTED;
    for (size_t i = 0; i < sizeof(kDepthFormats) / sizeof(kDepthFormats[0]) && depthErr != CK_OK; ++i) {
        depthDesc.Format = kDepthFormats[i];
        depthErr = m_Backend->CreateDepthTexture(&depthDesc, &target.DepthTexture);
    }
    if (depthErr != CK_OK) {
        m_Backend->DestroyObject(target.ColorTexture, CKRST_OBJ_TEXTURE);
        target.ColorTexture = 0;
        return FALSE;
    }

    CKBackendRenderTargetDesc rtDesc;
    rtDesc.ColorTexture = target.ColorTexture;
    rtDesc.DepthTexture = target.DepthTexture;
    if (m_Backend->CreateRenderTarget(&rtDesc, &target.FrameBuffer) != CK_OK) {
        m_Backend->DestroyObject(target.DepthTexture, CKRST_OBJ_TEXTURE);
        m_Backend->DestroyObject(target.ColorTexture, CKRST_OBJ_TEXTURE);
        target = CKPresentTarget();
        return FALSE;
    }

    target.Width = width;
    target.Height = height;
    target.Samples = samples;
    return TRUE;
}

void CKPresentStage::DestroyTarget(CKPresentTarget &target)
{
    if (m_Backend) {
        if (target.FrameBuffer)
            m_Backend->DestroyObject(target.FrameBuffer, CKRST_OBJ_RENDERTARGET);
        if (target.DepthTexture)
            m_Backend->DestroyObject(target.DepthTexture, CKRST_OBJ_TEXTURE);
        if (target.ColorTexture)
            m_Backend->DestroyObject(target.ColorTexture, CKRST_OBJ_TEXTURE);
    }
    target = CKPresentTarget();
}

CKBOOL CKPresentStage::EnsureResources()
{
    if (!m_Backend)
        return FALSE;
    const CKBackendCaps &caps = m_Backend->GetCaps();
    if (caps.ShaderFormat == CKRST_SHADER_FORMAT_UNKNOWN ||
        caps.ShaderProfile == CKRST_SHADER_PROFILE_UNKNOWN)
        return FALSE;
    if (m_ShaderFormat == caps.ShaderFormat &&
        m_ShaderProfile == caps.ShaderProfile &&
        m_Backend->IsObjectAlive(m_ResourceIds.Program, CKRST_OBJ_PROGRAM) &&
        m_Backend->IsObjectAlive(m_ResourceIds.VertexShader, CKRST_OBJ_SHADER) &&
        m_Backend->IsObjectAlive(m_ResourceIds.PixelShader, CKRST_OBJ_SHADER) &&
        m_Backend->IsObjectAlive(m_ResourceIds.VertexLayout, CKRST_OBJ_VERTEXLAYOUT))
        return TRUE;

    DestroyResources();

    if (!m_Shaders.Matches(caps.ShaderFormat, caps.ShaderProfile))
        return FALSE;
    if (m_Backend->CreateShader(&m_Shaders.Shaders[CKRST_SHADER_PRESENT_VERTEX], &m_ResourceIds.VertexShader) != CK_OK ||
        m_Backend->CreateShader(&m_Shaders.Shaders[CKRST_SHADER_PRESENT_FRAGMENT], &m_ResourceIds.PixelShader) != CK_OK) {
        DestroyResources();
        return FALSE;
    }

    const CKBackendProgramDesc program = CKFFBuildProgramInterface(
        m_ResourceIds.VertexShader, m_ResourceIds.PixelShader, caps.ShaderFormat, TRUE);
    if (m_Backend->CreateProgram(&program, &m_ResourceIds.Program) != CK_OK) {
        DestroyResources();
        return FALSE;
    }

    CKVertexElementDesc elements[2];
    memset(elements, 0, sizeof(elements));
    elements[0].Attrib = CKRST_ATTRIB_POSITION;
    elements[0].Type = CKRST_ATTRIBTYPE_FLOAT;
    elements[0].Count = 3;
    elements[0].Normalized = FALSE;
    elements[0].AsInt = FALSE;
    elements[0].Offset = 0;
    elements[1].Attrib = CKRST_ATTRIB_TEXCOORD0;
    elements[1].Type = CKRST_ATTRIBTYPE_FLOAT;
    elements[1].Count = 2;
    elements[1].Normalized = FALSE;
    elements[1].AsInt = FALSE;
    elements[1].Offset = 12;

    CKVertexLayoutDesc layoutDesc;
    layoutDesc.Elements = elements;
    layoutDesc.ElementCount = 2;
    layoutDesc.Stride = 20;
    if (m_Backend->CreateVertexLayout(&layoutDesc, &m_ResourceIds.VertexLayout) != CK_OK) {
        DestroyResources();
        return FALSE;
    }

    m_ShaderFormat = caps.ShaderFormat;
    m_ShaderProfile = caps.ShaderProfile;
    return TRUE;
}

void CKPresentStage::DestroyResources()
{
    if (m_Backend) {
        if (m_ResourceIds.Program)
            m_Backend->DestroyObject(m_ResourceIds.Program, CKRST_OBJ_PROGRAM);
        if (m_ResourceIds.VertexShader)
            m_Backend->DestroyObject(m_ResourceIds.VertexShader, CKRST_OBJ_SHADER);
        if (m_ResourceIds.PixelShader)
            m_Backend->DestroyObject(m_ResourceIds.PixelShader, CKRST_OBJ_SHADER);
        if (m_ResourceIds.VertexLayout)
            m_Backend->DestroyObject(m_ResourceIds.VertexLayout, CKRST_OBJ_VERTEXLAYOUT);
    }
    m_ResourceIds = CKPresentResources();
    m_ShaderFormat = CKRST_SHADER_FORMAT_UNKNOWN;
    m_ShaderProfile = CKRST_SHADER_PROFILE_UNKNOWN;
}

CKERROR CKPresentStage::SubmitResolve(CKBOOL fxaa, float sharpness)
{
    return Submit(m_Scene, fxaa, sharpness);
}

CKERROR CKPresentStage::SubmitBlit()
{
    return Submit(m_Native, FALSE, 0.0f);
}

CKERROR CKPresentStage::Submit(const CKPresentTarget &source, CKBOOL fxaa, float sharpness)
{
    if (!source.IsActive())
        return CKERR_INVALIDPARAMETER;
    return SubmitTexture(source.ColorTexture, source.Width, source.Height, TRUE, fxaa, sharpness);
}

CKERROR CKPresentStage::SubmitCopy(CKDWORD texture, CKDWORD width, CKDWORD height)
{
    return SubmitTexture(texture, width, height, FALSE, FALSE, 0.0f);
}

CKERROR CKPresentStage::SubmitTexture(CKDWORD texture, CKDWORD width, CKDWORD height,
                                     CKBOOL linear, CKBOOL fxaa, float sharpness)
{
    if (!m_Backend || !texture || !width || !height || !EnsureResources())
        return CKERR_NOTIMPLEMENTED;

    struct PresentVertex {
        float X, Y, Z;
        float U, V;
    };

    CKBackendTransientVertices tvb;
    if (!m_Backend->AllocTransientVertices(3, m_ResourceIds.VertexLayout, &tvb) || tvb.Stride < sizeof(PresentVertex))
        return CKERR_OUTOFMEMORY;

    PresentVertex *vertices = (PresentVertex *)tvb.Data;
    const CKBOOL originBottomLeft = m_Backend->GetCaps().OriginBottomLeft;
    const float bottomV = originBottomLeft ? 0.0f : 1.0f;
    const float extendedTopV = originBottomLeft ? 2.0f : -1.0f;
    vertices[0] = {-1.0f, -1.0f, 0.0f, 0.0f, bottomV};
    vertices[1] = { 3.0f, -1.0f, 0.0f, 2.0f, bottomV};
    vertices[2] = {-1.0f,  3.0f, 0.0f, 0.0f, extendedTopV};

    CKSamplerDesc sampler;
    memset(&sampler, 0, sizeof(sampler));
    sampler.MinFilter = linear ? CKRST_FILTER_LINEAR : CKRST_FILTER_NEAREST;
    sampler.MagFilter = sampler.MinFilter;
    sampler.MipFilter = CKRST_FILTER_NONE;
    sampler.AddressU = CKRST_ADDRESS_CLAMP;
    sampler.AddressV = CKRST_ADDRESS_CLAMP;
    sampler.AddressW = CKRST_ADDRESS_CLAMP;
    sampler.CompareFunc = CKRST_COMPARE_NONE;

    const float params[4] = {
        1.0f / (float)width,
        1.0f / (float)height,
        fxaa ? 1.0f : 0.0f,
        sharpness
    };

    CKBackendPipelineState state;
    state.State = CKDrawStateBuilder()
        .Depth(FALSE, FALSE, VXCMP_ALWAYS)
        .Cull(VXCULL_NONE)
        .Build();
    m_Backend->SetPipelineState(&state);
    m_Backend->BindTexture(CKFF_SLOT_PRESENT, texture, &sampler);
    const CKERROR pushed = CKFFPushConstants(m_Backend, CKRST_BLOCK_PRESENT_PARAMS, params, 1);
    if (pushed != CK_OK) {
        m_Backend->BindTexture(CKFF_SLOT_PRESENT, 0, NULL);
        return pushed;
    }

    CKBackendDraw draw;
    draw.Program = m_ResourceIds.Program;
    draw.Layout = m_ResourceIds.VertexLayout;
    draw.TransientVertices = &tvb;
    draw.VertexCount = 3;
    const CKERROR drawn = m_Backend->Draw(&draw);
    // The present sampler is only used by these fullscreen draws.
    m_Backend->BindTexture(CKFF_SLOT_PRESENT, 0, NULL);
    return drawn;
}
