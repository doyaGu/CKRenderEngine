#include "CKBgfxPresentStage.h"
#include "CKBgfxRasterizerContext.h"

#include <string.h>

CKBgfxPresentStage::CKBgfxPresentStage()
    : m_Context(nullptr), m_ReadbackTexture(0), m_ReadbackWidth(0), m_ReadbackHeight(0),
      m_ShaderFormat(CKRST_SHADER_FORMAT_UNKNOWN),
      m_ShaderProfile(CKRST_SHADER_PROFILE_UNKNOWN) {}

CKBgfxPresentStage::~CKBgfxPresentStage()
{
    Shutdown();
}

void CKBgfxPresentStage::Init(CKBgfxRasterizerContext *Context,
                              const CKFFShaderSet &Shaders)
{
    Shutdown();
    m_Context = Context;
    m_Shaders = Shaders;
}

void CKBgfxPresentStage::Shutdown()
{
    DestroyTargets();
    DestroyResources();
    m_Context = nullptr;
    m_Shaders = CKFFShaderSet();
    m_ResourceIds = CKBgfxPresentResources();
}

CKBOOL CKBgfxPresentStage::EnsureSceneTarget(CKDWORD width, CKDWORD height, CKDWORD samples)
{
    if (!m_Context || width == 0 || height == 0)
        return FALSE;
    if (samples <= 1)
        samples = 0;
    if (m_Scene.IsActive() && m_Scene.Width == width && m_Scene.Height == height && m_Scene.Samples == samples)
        return TRUE;
    DestroyTarget(m_Scene);
    return CreateTarget(m_Scene, width, height, samples);
}

CKBOOL CKBgfxPresentStage::EnsureNativeTarget(CKDWORD width, CKDWORD height)
{
    if (!m_Context || width == 0 || height == 0)
        return FALSE;
    if (m_Native.IsActive() && m_Native.Width == width && m_Native.Height == height)
        return TRUE;
    DestroyTarget(m_Native);
    return CreateTarget(m_Native, width, height, 0);
}

void CKBgfxPresentStage::DestroyTargets()
{
    DestroyTarget(m_Scene);
    DestroyTarget(m_Native);
    DestroyReadbackTexture();
}

CKDWORD CKBgfxPresentStage::AcquireReadbackTexture(CKDWORD width, CKDWORD height)
{
    if (!m_Context || width == 0 || height == 0)
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
    if (m_Context->CreateTexture(&desc, nullptr, &m_ReadbackTexture) != CK_OK) {
        m_ReadbackTexture = 0;
        return 0;
    }
    m_ReadbackWidth = width;
    m_ReadbackHeight = height;
    return m_ReadbackTexture;
}

void CKBgfxPresentStage::DestroyReadbackTexture()
{
    if (m_Context && m_ReadbackTexture)
        m_Context->DestroyObject(m_ReadbackTexture, CKRST_OBJ_TEXTURE);
    m_ReadbackTexture = 0;
    m_ReadbackWidth = m_ReadbackHeight = 0;
}

CKBOOL CKBgfxPresentStage::CreateTarget(CKBgfxPresentTarget &target, CKDWORD width, CKDWORD height,
                                       CKDWORD samples)
{
    target = CKBgfxPresentTarget();
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
    if (m_Context->CreateTexture(&colorDesc, nullptr, &target.ColorTexture) != CK_OK)
        return FALSE;

    CKDepthTextureDesc depthDesc;
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.Samples = samples;
    static const CK_DEPTH_FORMAT kDepthFormats[] = {CKRST_DEPTHFMT_D24S8, CKRST_DEPTHFMT_D24, CKRST_DEPTHFMT_D16};
    CKERROR depthErr = CKERR_NOTIMPLEMENTED;
    for (size_t i = 0; i < sizeof(kDepthFormats) / sizeof(kDepthFormats[0]) && depthErr != CK_OK; ++i) {
        depthDesc.Format = kDepthFormats[i];
        depthErr = m_Context->CreateDepthTexture(&depthDesc, &target.DepthTexture);
    }
    if (depthErr != CK_OK) {
        m_Context->DestroyObject(target.ColorTexture, CKRST_OBJ_TEXTURE);
        target.ColorTexture = 0;
        return FALSE;
    }

    CKRenderTargetDesc rtDesc;
    rtDesc.ColorTexture = target.ColorTexture;
    rtDesc.DepthTexture = target.DepthTexture;
    if (m_Context->CreateRenderTarget(&rtDesc, &target.FrameBuffer) != CK_OK) {
        m_Context->DestroyObject(target.DepthTexture, CKRST_OBJ_TEXTURE);
        m_Context->DestroyObject(target.ColorTexture, CKRST_OBJ_TEXTURE);
        target = CKBgfxPresentTarget();
        return FALSE;
    }

    target.Width = width;
    target.Height = height;
    target.Samples = samples;
    target.DepthFormat = depthDesc.Format;
    return TRUE;
}

void CKBgfxPresentStage::DestroyTarget(CKBgfxPresentTarget &target)
{
    if (m_Context) {
        if (target.FrameBuffer)
            m_Context->DestroyObject(target.FrameBuffer, CKRST_OBJ_RENDERTARGET);
        if (target.DepthTexture)
            m_Context->DestroyObject(target.DepthTexture, CKRST_OBJ_TEXTURE);
        if (target.ColorTexture)
            m_Context->DestroyObject(target.ColorTexture, CKRST_OBJ_TEXTURE);
    }
    target = CKBgfxPresentTarget();
}

CKBOOL CKBgfxPresentStage::EnsureResources()
{
    if (!m_Context)
        return FALSE;
    const CKRasterizerDeviceCaps &caps = m_Context->GetCaps();
    if (caps.ShaderFormat == CKRST_SHADER_FORMAT_UNKNOWN ||
        caps.ShaderProfile == CKRST_SHADER_PROFILE_UNKNOWN)
        return FALSE;
    if (m_ShaderFormat == caps.ShaderFormat &&
        m_ShaderProfile == caps.ShaderProfile &&
        m_Context->IsNativeObjectAlive(m_ResourceIds.Program, CKRST_OBJ_PROGRAM) &&
        m_Context->IsNativeObjectAlive(m_ResourceIds.VertexShader, CKRST_OBJ_SHADER) &&
        m_Context->IsNativeObjectAlive(m_ResourceIds.PixelShader, CKRST_OBJ_SHADER) &&
        m_Context->IsNativeObjectAlive(m_ResourceIds.VertexLayout, CKRST_OBJ_VERTEXLAYOUT))
        return TRUE;

    DestroyResources();

    if (!m_Shaders.Matches(caps.ShaderFormat, caps.ShaderProfile))
        return FALSE;
    if (m_Context->CreateShader(&m_Shaders.Shaders[CKRST_SHADER_PRESENT_VERTEX], &m_ResourceIds.VertexShader) != CK_OK ||
        m_Context->CreateShader(&m_Shaders.Shaders[CKRST_SHADER_PRESENT_FRAGMENT], &m_ResourceIds.PixelShader) != CK_OK) {
        DestroyResources();
        return FALSE;
    }

    const CKFFProgramDesc program = CKFFBuildProgramInterface(
        m_ResourceIds.VertexShader, m_ResourceIds.PixelShader, caps.ShaderFormat, TRUE);
    if (m_Context->CreateProgram(&program, &m_ResourceIds.Program) != CK_OK) {
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
    if (m_Context->CreateVertexLayout(&layoutDesc, &m_ResourceIds.VertexLayout) != CK_OK) {
        DestroyResources();
        return FALSE;
    }

    m_ShaderFormat = caps.ShaderFormat;
    m_ShaderProfile = caps.ShaderProfile;
    return TRUE;
}

void CKBgfxPresentStage::DestroyResources()
{
    if (m_Context) {
        if (m_ResourceIds.Program)
            m_Context->DestroyObject(m_ResourceIds.Program, CKRST_OBJ_PROGRAM);
        if (m_ResourceIds.VertexShader)
            m_Context->DestroyObject(m_ResourceIds.VertexShader, CKRST_OBJ_SHADER);
        if (m_ResourceIds.PixelShader)
            m_Context->DestroyObject(m_ResourceIds.PixelShader, CKRST_OBJ_SHADER);
        if (m_ResourceIds.VertexLayout)
            m_Context->DestroyObject(m_ResourceIds.VertexLayout, CKRST_OBJ_VERTEXLAYOUT);
    }
    m_ResourceIds = CKBgfxPresentResources();
    m_ShaderFormat = CKRST_SHADER_FORMAT_UNKNOWN;
    m_ShaderProfile = CKRST_SHADER_PROFILE_UNKNOWN;
}

CKERROR CKBgfxPresentStage::SubmitResolve(CKBOOL fxaa, float sharpness)
{
    return Submit(m_Scene, fxaa, sharpness);
}

CKERROR CKBgfxPresentStage::SubmitBlit()
{
    return Submit(m_Native, FALSE, 0.0f);
}

CKERROR CKBgfxPresentStage::Submit(const CKBgfxPresentTarget &source, CKBOOL fxaa, float sharpness)
{
    if (!source.IsActive())
        return CKERR_INVALIDPARAMETER;
    return SubmitTexture(source.ColorTexture, source.Width, source.Height, TRUE, fxaa, sharpness,
                         !m_Context->GetCaps().OriginBottomLeft);
}

CKERROR CKBgfxPresentStage::SubmitCopy(CKDWORD texture, CKDWORD width, CKDWORD height,
                                       CKBOOL flipV)
{
    return SubmitTexture(texture, width, height, FALSE, FALSE, 0.0f, flipV);
}

CKERROR CKBgfxPresentStage::SubmitTexture(CKDWORD texture, CKDWORD width, CKDWORD height,
                                     CKBOOL linear, CKBOOL fxaa, float sharpness,
                                     CKBOOL flipV)
{
    if (!m_Context || !texture || !width || !height || !EnsureResources())
        return CKERR_NOTIMPLEMENTED;

    CKTransientVertexData tvb;
    if (!m_Context->AllocTransientVertices(
            3, m_ResourceIds.VertexLayout, &tvb))
        return CKERR_OUTOFMEMORY;

    CKDrawCommand draw;
    const CKERROR prepared = m_Draw.Prepare(
        texture, width, height, linear, fxaa, sharpness, flipV,
        m_ResourceIds.Program, m_ResourceIds.VertexLayout, tvb, draw);
    return prepared == CK_OK ? m_Context->Draw(&draw) : prepared;
}
