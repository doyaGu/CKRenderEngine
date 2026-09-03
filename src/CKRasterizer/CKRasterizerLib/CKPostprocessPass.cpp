#include "CKPostprocessPass.h"
#include "CKRasterizerDevice.h"

#include "shaders/generated/dx11/vs_postprocess.bin.h"
#include "shaders/generated/dx11/fs_postprocess.bin.h"
#include "shaders/generated/dx12/vs_postprocess.bin.h"
#include "shaders/generated/dx12/fs_postprocess.bin.h"
#include "shaders/generated/spirv/vs_postprocess.bin.h"
#include "shaders/generated/spirv/fs_postprocess.bin.h"
#include "shaders/generated/glsl/vs_postprocess.bin.h"
#include "shaders/generated/glsl/fs_postprocess.bin.h"
#include "shaders/generated/essl/vs_postprocess.bin.h"
#include "shaders/generated/essl/fs_postprocess.bin.h"
#include "shaders/generated/metal/vs_postprocess.bin.h"
#include "shaders/generated/metal/fs_postprocess.bin.h"

#include <math.h>
#include <string.h>

namespace {

struct CKPostprocessShaderBlobSet {
    CK_SHADER_PROFILE Profile;
    const unsigned char *VS;
    unsigned int VSSize;
    const unsigned char *FS;
    unsigned int FSSize;
};

const CKPostprocessShaderBlobSet g_PostprocessShaderBlobSets[] = {
    {CKRST_SHADER_PROFILE_DX11, s_dx11_vs_postprocess, sizeof(s_dx11_vs_postprocess),
     s_dx11_fs_postprocess, sizeof(s_dx11_fs_postprocess)},
    {CKRST_SHADER_PROFILE_DX12, s_dx12_vs_postprocess, sizeof(s_dx12_vs_postprocess),
     s_dx12_fs_postprocess, sizeof(s_dx12_fs_postprocess)},
    {CKRST_SHADER_PROFILE_SPIRV, s_spirv_vs_postprocess, sizeof(s_spirv_vs_postprocess),
     s_spirv_fs_postprocess, sizeof(s_spirv_fs_postprocess)},
    {CKRST_SHADER_PROFILE_GLSL, s_glsl_vs_postprocess, sizeof(s_glsl_vs_postprocess),
     s_glsl_fs_postprocess, sizeof(s_glsl_fs_postprocess)},
    {CKRST_SHADER_PROFILE_ESSL, s_essl_vs_postprocess, sizeof(s_essl_vs_postprocess),
     s_essl_fs_postprocess, sizeof(s_essl_fs_postprocess)},
    {CKRST_SHADER_PROFILE_MSL, s_metal_vs_postprocess, sizeof(s_metal_vs_postprocess),
     s_metal_fs_postprocess, sizeof(s_metal_fs_postprocess)},
};

const CKPostprocessShaderBlobSet *FindPostprocessShaderBlobSet(CK_SHADER_PROFILE profile)
{
    for (const CKPostprocessShaderBlobSet &set : g_PostprocessShaderBlobSets) {
        if (set.Profile == profile)
            return &set;
    }
    return nullptr;
}

} // namespace

CKDWORD CKPostprocessPass::ScaledDimension(CKDWORD value, float scale, CKDWORD maximum)
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

float CKPostprocessPass::ClampRenderScale(float scale)
{
    if (!(scale > 0.0f) || !isfinite(scale))
        return 1.0f;
    if (scale < 0.5f)
        return 0.5f;
    if (scale > 2.0f)
        return 2.0f;
    return scale;
}

float CKPostprocessPass::ClampSharpness(float sharpness)
{
    if (!(sharpness > 0.0f) || !isfinite(sharpness))
        return 0.0f;
    if (sharpness > 1.0f)
        return 1.0f;
    return sharpness;
}

CKPostprocessPass::CKPostprocessPass()
    : m_Device(nullptr), m_ReadbackTexture(0), m_PostVertexShaderProfile(CKRST_SHADER_PROFILE_UNKNOWN) {}

CKPostprocessPass::~CKPostprocessPass()
{
    Shutdown();
}

void CKPostprocessPass::Init(CKRasterizerDevice *device)
{
    Shutdown();
    m_Device = device;
}

void CKPostprocessPass::Shutdown()
{
    DestroyTargets();
    DestroyResources();
    m_Device = nullptr;
    m_ResourceIds = CKPostprocessResourceIds();
}

CKBOOL CKPostprocessPass::EnsureSceneTarget(CKDWORD width, CKDWORD height, CKDWORD samples)
{
    if (!m_Device || width == 0 || height == 0)
        return FALSE;
    if (samples <= 1)
        samples = 0;
    if (m_Scene.IsActive() && m_Scene.Width == width && m_Scene.Height == height && m_Scene.Samples == samples)
        return TRUE;
    DestroyTarget(m_Scene);
    return CreateTarget(m_Scene, width, height, samples);
}

CKBOOL CKPostprocessPass::EnsureNativeTarget(CKDWORD width, CKDWORD height)
{
    if (!m_Device || width == 0 || height == 0)
        return FALSE;
    if (m_Native.IsActive() && m_Native.Width == width && m_Native.Height == height)
        return TRUE;
    DestroyTarget(m_Native);
    DestroyReadbackTexture();
    if (!CreateTarget(m_Native, width, height, 0))
        return FALSE;
    EnsureReadbackTexture(width, height);
    return TRUE;
}

void CKPostprocessPass::DestroyTargets()
{
    DestroyTarget(m_Scene);
    DestroyTarget(m_Native);
    DestroyReadbackTexture();
}

// Render targets cannot carry the readback flag; readbacks blit the native
// color into this plain texture first. Missing when the device has no blit
// or texture readback (the caller then falls back to the swap chain).
void CKPostprocessPass::EnsureReadbackTexture(CKDWORD width, CKDWORD height)
{
    if (m_ReadbackTexture || !m_Device)
        return;
    CKTextureDesc desc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
    desc.Format.Width = (int)width;
    desc.Format.Height = (int)height;
    desc.MipMapCount = 1;
    desc.Depth = 1;
    desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA |
                 CKRST_TEXTURE_BLIT_DST | CKRST_TEXTURE_READBACK;
    if (m_Device->CreateTexture(&desc, nullptr, &m_ReadbackTexture) != CK_OK)
        m_ReadbackTexture = 0;
}

void CKPostprocessPass::DestroyReadbackTexture()
{
    if (m_Device && m_ReadbackTexture)
        m_Device->DeleteObject(m_ReadbackTexture, CKRST_OBJ_TEXTURE);
    m_ReadbackTexture = 0;
}

CKBOOL CKPostprocessPass::CreateTarget(CKPostprocessTarget &target, CKDWORD width, CKDWORD height,
                                       CKDWORD samples)
{
    target = CKPostprocessTarget();
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
    if (m_Device->CreateTexture(&colorDesc, nullptr, &target.ColorTexture) != CK_OK)
        return FALSE;

    CKDepthTextureDesc depthDesc = {};
    depthDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_DEPTHSTENCIL | msaaFlag;
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipMapCount = 1;
    static const CK_DEPTH_FORMAT kDepthFormats[] = {CKRST_DEPTHFMT_D24S8, CKRST_DEPTHFMT_D24, CKRST_DEPTHFMT_D16};
    CKERROR depthErr = CKERR_NOTIMPLEMENTED;
    for (size_t i = 0; i < sizeof(kDepthFormats) / sizeof(kDepthFormats[0]) && depthErr != CK_OK; ++i) {
        depthDesc.DepthFormat = kDepthFormats[i];
        depthErr = m_Device->CreateDepthTexture(&depthDesc, &target.DepthTexture);
    }
    if (depthErr != CK_OK) {
        m_Device->DeleteObject(target.ColorTexture, CKRST_OBJ_TEXTURE);
        target.ColorTexture = 0;
        return FALSE;
    }

    CKFrameBufferAttachmentDesc colorAttachment;
    colorAttachment.Texture = target.ColorTexture;
    colorAttachment.Mip = 0;
    colorAttachment.Layer = 0;

    CKFrameBufferDesc fbDesc;
    fbDesc.Color = &colorAttachment;
    fbDesc.ColorCount = 1;
    fbDesc.DepthStencil.Texture = target.DepthTexture;
    fbDesc.DepthStencil.Mip = 0;
    fbDesc.DepthStencil.Layer = 0;

    if (m_Device->CreateFrameBuffer(&fbDesc, &target.FrameBuffer) != CK_OK) {
        m_Device->DeleteObject(target.DepthTexture, CKRST_OBJ_TEXTURE);
        m_Device->DeleteObject(target.ColorTexture, CKRST_OBJ_TEXTURE);
        target = CKPostprocessTarget();
        return FALSE;
    }

    target.Width = width;
    target.Height = height;
    target.Samples = samples;
    return TRUE;
}

void CKPostprocessPass::DestroyTarget(CKPostprocessTarget &target)
{
    if (m_Device) {
        if (target.FrameBuffer)
            m_Device->DeleteObject(target.FrameBuffer, CKRST_OBJ_FRAMEBUFFER);
        if (target.DepthTexture)
            m_Device->DeleteObject(target.DepthTexture, CKRST_OBJ_TEXTURE);
        if (target.ColorTexture)
            m_Device->DeleteObject(target.ColorTexture, CKRST_OBJ_TEXTURE);
    }
    target = CKPostprocessTarget();
}

CKBOOL CKPostprocessPass::EnsureResources()
{
    if (!m_Device || !m_Device->m_Driver)
        return FALSE;
    CKRasterizerTargetDesc target;
    if (m_Device->GetTargetDesc(&target) != CK_OK)
        return FALSE;
    if (target.ShaderProfile == CKRST_SHADER_PROFILE_UNKNOWN)
        return FALSE;
    if (m_PostVertexShaderProfile == target.ShaderProfile &&
        m_Device->IsObjectAlive(m_ResourceIds.PostProgram, CKRST_OBJ_PROGRAM) &&
        m_Device->IsObjectAlive(m_ResourceIds.PostVertexShader, CKRST_OBJ_SHADER) &&
        m_Device->IsObjectAlive(m_ResourceIds.PostPixelShader, CKRST_OBJ_SHADER) &&
        m_Device->IsObjectAlive(m_ResourceIds.PostSamplerUniform, CKRST_OBJ_UNIFORM) &&
        m_Device->IsObjectAlive(m_ResourceIds.PostParamsUniform, CKRST_OBJ_UNIFORM) &&
        m_Device->IsObjectAlive(m_ResourceIds.PostVertexLayout, CKRST_OBJ_VERTEXLAYOUT))
        return TRUE;

    DestroyResources();

    const CKPostprocessShaderBlobSet *blobs = FindPostprocessShaderBlobSet(target.ShaderProfile);
    if (!blobs)
        return FALSE;

    CKUniformDesc uniformDesc;
    uniformDesc.Name = (CKSTRING)"s_sceneColor";
    uniformDesc.Type = CKRST_UNIFORM_SAMPLER;
    uniformDesc.Count = 1;
    if (m_Device->CreateUniform(&uniformDesc, &m_ResourceIds.PostSamplerUniform) != CK_OK) {
        DestroyResources();
        return FALSE;
    }

    uniformDesc.Name = (CKSTRING)"u_postParams";
    uniformDesc.Type = CKRST_UNIFORM_VEC4;
    uniformDesc.Count = 1;
    if (m_Device->CreateUniform(&uniformDesc, &m_ResourceIds.PostParamsUniform) != CK_OK) {
        DestroyResources();
        return FALSE;
    }

    CKShaderDesc shaderDesc;
    shaderDesc.Format = CKRST_SHADER_FORMAT_NATIVE;
    shaderDesc.Profile = target.ShaderProfile;

    shaderDesc.Stage = CKRST_SHADER_VERTEX;
    shaderDesc.Code = blobs->VS;
    shaderDesc.CodeSize = blobs->VSSize;
    if (m_Device->CreateShader(&shaderDesc, &m_ResourceIds.PostVertexShader) != CK_OK) {
        DestroyResources();
        return FALSE;
    }

    shaderDesc.Stage = CKRST_SHADER_PIXEL;
    shaderDesc.Code = blobs->FS;
    shaderDesc.CodeSize = blobs->FSSize;
    if (m_Device->CreateShader(&shaderDesc, &m_ResourceIds.PostPixelShader) != CK_OK) {
        DestroyResources();
        return FALSE;
    }

    CKProgramDesc programDesc;
    programDesc.VertexShader = m_ResourceIds.PostVertexShader;
    programDesc.PixelShader = m_ResourceIds.PostPixelShader;
    programDesc.ConsumeShaders = FALSE;
    if (m_Device->CreateProgram(&programDesc, &m_ResourceIds.PostProgram) != CK_OK) {
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
    if (m_Device->CreateVertexLayout(&layoutDesc, &m_ResourceIds.PostVertexLayout) != CK_OK) {
        DestroyResources();
        return FALSE;
    }

    m_PostVertexShaderProfile = target.ShaderProfile;
    return TRUE;
}

void CKPostprocessPass::DestroyResources()
{
    if (m_Device) {
        if (m_ResourceIds.PostProgram)
            m_Device->DeleteObject(m_ResourceIds.PostProgram, CKRST_OBJ_PROGRAM);
        if (m_ResourceIds.PostVertexShader)
            m_Device->DeleteObject(m_ResourceIds.PostVertexShader, CKRST_OBJ_SHADER);
        if (m_ResourceIds.PostPixelShader)
            m_Device->DeleteObject(m_ResourceIds.PostPixelShader, CKRST_OBJ_SHADER);
        if (m_ResourceIds.PostSamplerUniform)
            m_Device->DeleteObject(m_ResourceIds.PostSamplerUniform, CKRST_OBJ_UNIFORM);
        if (m_ResourceIds.PostParamsUniform)
            m_Device->DeleteObject(m_ResourceIds.PostParamsUniform, CKRST_OBJ_UNIFORM);
        if (m_ResourceIds.PostVertexLayout)
            m_Device->DeleteObject(m_ResourceIds.PostVertexLayout, CKRST_OBJ_VERTEXLAYOUT);
    }
    m_ResourceIds.PostProgram = 0;
    m_ResourceIds.PostVertexShader = 0;
    m_ResourceIds.PostPixelShader = 0;
    m_ResourceIds.PostSamplerUniform = 0;
    m_ResourceIds.PostParamsUniform = 0;
    m_ResourceIds.PostVertexLayout = 0;
    m_PostVertexShaderProfile = CKRST_SHADER_PROFILE_UNKNOWN;
}

CKERROR CKPostprocessPass::SubmitResolve(CKRasterizerEncoder *encoder, CKRenderView view, CKBOOL fxaa,
                                         float sharpness)
{
    return Submit(encoder, view, m_Scene, fxaa, sharpness);
}

CKERROR CKPostprocessPass::SubmitBlit(CKRasterizerEncoder *encoder, CKRenderView view)
{
    return Submit(encoder, view, m_Native, FALSE, 0.0f);
}

CKERROR CKPostprocessPass::Submit(CKRasterizerEncoder *encoder, CKRenderView view,
                                  const CKPostprocessTarget &source, CKBOOL fxaa, float sharpness)
{
    if (!m_Device || !encoder || !source.IsActive() || !EnsureResources())
        return CKERR_NOTIMPLEMENTED;

    struct PostVertex {
        float X, Y, Z;
        float U, V;
    };

    CKTransientVertexBuffer tvb;
    memset(&tvb, 0, sizeof(tvb));
    if (!m_Device->AllocTransientVertexBuffer(&tvb, 3, m_ResourceIds.PostVertexLayout))
        return CKERR_OUTOFMEMORY;

    PostVertex *vertices = (PostVertex *)tvb.Data;
    CKRasterizerTargetDesc target;
    const CKBOOL originBottomLeft =
        m_Device->GetTargetDesc(&target) == CK_OK && target.OriginBottomLeft;
    const float bottomV = originBottomLeft ? 0.0f : 1.0f;
    const float extendedTopV = originBottomLeft ? 2.0f : -1.0f;
    vertices[0] = {-1.0f, -1.0f, 0.0f, 0.0f, bottomV};
    vertices[1] = { 3.0f, -1.0f, 0.0f, 2.0f, bottomV};
    vertices[2] = {-1.0f,  3.0f, 0.0f, 0.0f, extendedTopV};

    CKSamplerDesc sampler;
    memset(&sampler, 0, sizeof(sampler));
    sampler.MinFilter = CKRST_FILTER_LINEAR;
    sampler.MagFilter = CKRST_FILTER_LINEAR;
    sampler.MipFilter = CKRST_FILTER_NONE;
    sampler.AddressU = CKRST_ADDRESS_CLAMP;
    sampler.AddressV = CKRST_ADDRESS_CLAMP;
    sampler.AddressW = CKRST_ADDRESS_CLAMP;
    sampler.CompareFunc = CKRST_COMPARE_NONE;

    const float params[4] = {
        source.Width > 0 ? 1.0f / (float)source.Width : 1.0f,
        source.Height > 0 ? 1.0f / (float)source.Height : 1.0f,
        fxaa ? 1.0f : 0.0f,
        sharpness
    };

    CKDrawState state = CKDrawStateBuilder()
        .Depth(FALSE, FALSE, VXCMP_ALWAYS)
        .Cull(VXCULL_NONE)
        .Build();

    encoder->SetState(state);
    encoder->SetStencilRef(0);
    encoder->SetStencilMask(0xFF, 0xFF);
    encoder->SetScissor(nullptr);
    encoder->SetPointSize(1.0f);
    encoder->SetTransientVertexBuffer(0, &tvb);
    encoder->SetTexture(0, m_ResourceIds.PostSamplerUniform, source.ColorTexture, &sampler);
    encoder->SetUniform(m_ResourceIds.PostParamsUniform, params, 1);
    encoder->Submit(view, m_ResourceIds.PostProgram, 0, CKRST_DISCARD_ALL);
    return encoder->GetStatus();
}
