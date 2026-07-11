#include "CKFixedFunctionPipeline.h"
#include "CKFFSpecializationInfo.h"
#include "CKFFSpecializedModuleTable.h"
#include "CKFFUniformState.h"
#include "CKRenderPipeline.h"
#include "CKRenderSettings.h"
#include "FFPDiagnosticHarness.h"
#include "TestTriangleMultiset.h"
#include "shaders/generated/CKFFSpecializedModuleTable.generated.h"

#include <math.h>
#include <string.h>

extern CKRasterizer *CKNULLRasterizerStart(WIN_HANDLE AppWnd);
extern void CKNULLRasterizerClose(CKRasterizer *Rasterizer);

namespace {

void NullRasterizerSupportsHeadlessFFP()
{
    CKRasterizer *rasterizer = CKNULLRasterizerStart(NULL);
    TestCheck(rasterizer != NULL && rasterizer->GetDriverCount() == 1,
              "Null rasterizer must expose its headless driver");
    CKRasterizerDriver *driver = rasterizer->GetDriver(0);
    CKRasterizerContext *first = driver->CreateContext();
    CKRasterizerContext *second = driver->CreateContext();
    TestCheck(first != NULL && second != NULL &&
                  first->Create(NULL, 0, 0, 64, 64, 32, FALSE, 0, 24, 8) == CK_OK &&
                  second->Create(NULL, 0, 0, 32, 32, 32, FALSE, 0, 16, 0) == CK_OK,
              "Null rasterizer must allow independent headless contexts");

    CKFixedFunctionPipeline ffp;
    TestCheck(ffp.Init(first),
              "FFP must initialize without shader programs on a headless backend");
    TestCheck(ffp.Shutdown() == CK_OK,
              "Headless FFP shutdown must release its resources cleanly");
    TestCheck(driver->DestroyContext(first) && driver->DestroyContext(second),
              "Idle headless contexts must be independently destroyable");
    CKNULLRasterizerClose(rasterizer);
}

void RenderPipelinePropagatesFrameFailure()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKRenderPipeline pipeline;
    pipeline.Init(&context);

    CKRECT viewport = {0, 0, 64, 64};
    VxMatrix identity;
    Vx3DMatrixIdentity(identity);
    pipeline.BeginFrame(viewport, 0, 0, 1.0f, identity, identity);
    context.FrameResult = CKERR_INVALIDOPERATION;
    TestCheck(pipeline.EndFrame(CKRST_FRAME_SYNC_IMMEDIATE) ==
                  CKERR_INVALIDOPERATION,
              "Render pipeline must propagate backend frame failures");
    TestCheck(pipeline.Shutdown() == CK_OK,
              "Pipeline must remain safely shutdownable after a frame failure");
}

CKDWORD FloatStageState(float value) {
    union {
        float F;
        CKDWORD D;
    } u;
    u.F = value;
    return u.D;
}

VXPRIMITIVETYPE DrawStateTopology(const CKDrawState &state) {
    return (VXPRIMITIVETYPE)((state.Mid >> 6) & 0x7u);
}

struct ShaderProfileCase {
    CK_SHADER_PROFILE Profile;
    const char *Name;
};

static const ShaderProfileCase kSamplerLayoutProfiles[] = {
    {CKRST_SHADER_PROFILE_DX11, "dx11"},
    {CKRST_SHADER_PROFILE_DX12, "dx12"},
    {CKRST_SHADER_PROFILE_SPIRV, "spirv"},
    {CKRST_SHADER_PROFILE_GLSL, "glsl"},
    {CKRST_SHADER_PROFILE_ESSL, "essl"},
    {CKRST_SHADER_PROFILE_MSL, "metal"},
};

CKFFSpecializationInfo CurrentDrawSpecialization(CKFixedFunctionPipeline &ffp,
                                                 const FFPDiagnosticContext &context) {
    CKFFSpecializationInfo info;
    const CKDWORD uniform = ffp.GetShaderCache().GetUniforms().u_ffSpec;
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator it =
        context.Encoder.FloatUniforms.find(uniform);
    if (it != context.Encoder.FloatUniforms.end() &&
        it->second.size() >= CKFF_SPEC_UNIFORM_VEC4_COUNT * 4) {
        CKDWORD dwords[CKFFSpecializationInfo::MaxSpecDwords] = {};
        for (CKDWORD i = 0; i < CKFF_SPEC_UNIFORM_VEC4_COUNT; ++i) {
            dwords[i] = ((CKDWORD)it->second[i * 4 + 0] & 0xFFu) |
                        (((CKDWORD)it->second[i * 4 + 1] & 0xFFu) << 8) |
                        (((CKDWORD)it->second[i * 4 + 2] & 0xFFu) << 16) |
                        (((CKDWORD)it->second[i * 4 + 3] & 0xFFu) << 24);
        }
        info.SetDwords(dwords, CKFFSpecializationInfo::MaxSpecDwords);
    } else {
        for (size_t i = 0; i < g_CKFFSpecializedModuleCount; ++i) {
            const CKFFSpecializedModule &module = g_CKFFSpecializedModules[i].Module;
            if (module.FSData == context.LastPixelShaderCode &&
                module.FSSize == context.LastPixelShaderCodeSize) {
                return module.Specialization;
            }
        }
    }
    return info;
}

void DrawVertexBufferRejectsPartialStencilWriteMask() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetRenderState(VXRENDERSTATE_STENCILENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_STENCILFUNC, VXCMP_EQUAL);
    ffp.SetRenderState(VXRENDERSTATE_STENCILPASS, VXSTENCILOP_REPLACE);
    ffp.SetRenderState(VXRENDERSTATE_STENCILREF, 0x12);
    ffp.SetRenderState(VXRENDERSTATE_STENCILMASK, 0xF0);
    ffp.SetRenderState(VXRENDERSTATE_STENCILWRITEMASK, 0x0F);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(!drawn,
              "A partial stencil write mask must be rejected explicitly");
    TestCheck(context.Encoder.SubmitCount == 0,
              "A rejected stencil write mask must not reach the backend");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_STENCIL_WRITE_MASK,
              "Partial stencil write mask rejection must report its reason");

    ffp.Shutdown();
}

void DrawVertexBufferSubmitsRepresentableStencilMasks() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetRenderState(VXRENDERSTATE_STENCILENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_STENCILFUNC, VXCMP_EQUAL);
    ffp.SetRenderState(VXRENDERSTATE_STENCILPASS, VXSTENCILOP_REPLACE);
    ffp.SetRenderState(VXRENDERSTATE_STENCILREF, 0x12);
    ffp.SetRenderState(VXRENDERSTATE_STENCILMASK, 0xF0);
    ffp.SetRenderState(VXRENDERSTATE_STENCILWRITEMASK, 0xFF);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(drawn && context.Encoder.SubmitCount == 1,
              "Representable stencil masks must submit once");
    TestCheck(context.Encoder.StencilRefSetCount == 1 &&
                  context.Encoder.StencilMaskSetCount == 1,
              "Representable stencil state must reach the backend");
    TestCheck(context.Encoder.LastStencilRef == 0x12 &&
                  context.Encoder.LastStencilReadMask == 0xF0 &&
                  context.Encoder.LastStencilWriteMask == 0xFF,
              "Representable stencil state must preserve ref and masks");

    ffp.Shutdown();
}

void DrawVertexBufferPropagatesEncoderFailure() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);
    context.Encoder.SubmitError = CKERR_INVALIDPARAMETER;

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(!drawn,
              "A backend submit failure must fail the originating FFP draw");
    TestCheck(context.Encoder.SubmitCount == 1,
              "The failing backend submit must be attempted exactly once");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_ENCODER_ERROR,
              "A backend submit failure must report the encoder-error reason");

    context.Encoder.Status = CK_OK;
    context.Encoder.SubmitError = CK_OK;
    ffp.Shutdown();
}

void UnsupportedRenderStatesRejectExplicitly() {
    struct UnsupportedStateCase {
        VXRENDERSTATETYPE State;
        CKDWORD Value;
        CKDWORD ResetValue;
        CKFFDrawRejectReason Reason;
    };
    const UnsupportedStateCase cases[] = {
        {VXRENDERSTATE_DITHERENABLE, TRUE, FALSE, CKFF_DRAW_REJECT_DITHER},
        {VXRENDERSTATE_ZBIAS, 1, 0, CKFF_DRAW_REJECT_ZBIAS},
        {VXRENDERSTATE_LINEPATTERN, 0xFFFFu, 0, CKFF_DRAW_REJECT_LINE_PATTERN},
        {VXRENDERSTATE_EDGEANTIALIAS, TRUE, FALSE, CKFF_DRAW_REJECT_EDGE_ANTIALIAS},
        {VXRENDERSTATE_CLIPPING, FALSE, TRUE, CKFF_DRAW_REJECT_CLIPPING_DISABLED},
    };

    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    CKBOOL allRejected = TRUE;
    for (CKDWORD i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        ffp.SetRenderState(cases[i].State, cases[i].Value);
        const CKBOOL drawn = ffp.DrawVertexBuffer(
            &context.Encoder, 1, VX_TRIANGLELIST,
            1, 0, 0, 3, 0, 0,
            CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
        allRejected = allRejected && !drawn &&
            ffp.GetLastDrawRejectReason() == cases[i].Reason;
        ffp.SetRenderState(cases[i].State, cases[i].ResetValue);
    }

    TestCheck(allRejected,
              "Unsupported output-affecting render states must report explicit rejection reasons");
    TestCheck(context.Encoder.SubmitCount == 0,
              "Unsupported render states must not reach backend submission");

    ffp.Shutdown();
}

void InvalidStateValuesRejectBeforeBackendEncoding() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetRenderState(VXRENDERSTATE_FILLMODE, 99);
    TestCheck(ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                                   1, 0, 0, 3, 0, 0,
                                   CKRST_DP_CL_V, CKRST_DP_CL_V, 1) == FALSE &&
                  ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_STATE_VALUE,
              "invalid raster state values must reject before backend encoding");
    ffp.SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);

    ffp.SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_PHONG);
    TestCheck(ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                                   1, 0, 0, 3, 0, 0,
                                   CKRST_DP_CL_V, CKRST_DP_CL_V, 1) == FALSE &&
                  ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_STATE_VALUE,
              "unsupported Phong shade mode must not silently become Gouraud");
    ffp.SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_GOURAUD);

    ffp.SetTexture(0, 1);
    ffp.SetTextureStageState(0, CKRST_TSS_MINFILTER, 99);
    TestCheck(ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                                   1, 0, 0, 3, 0, 0,
                                   CKRST_DP_CL_V, CKRST_DP_CL_V, 1) == FALSE &&
                  ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_STATE_VALUE,
              "invalid sampler state values must reject before backend encoding");
    TestCheck(context.Encoder.SubmitCount == 0,
              "invalid state values must not reach backend submission");

    ffp.Shutdown();
}

void UnsupportedTextureStageStatesRejectExplicitly() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);
    ffp.SetTexture(0, 101, CKRST_TEXTURE_VALID);

    ffp.SetTextureStageState(
        0, CKRST_TSS_STAGEBLEND, STAGEBLEND(VXBLEND_ONE, VXBLEND_ONE));
    CKBOOL drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(!drawn &&
                  ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_STAGE_BLEND,
              "Unsupported STAGEBLEND must reject instead of reusing prior texture ops");

    ffp.ResetTextureStage(0);
    ffp.SetTexture(0, 101, CKRST_TEXTURE_VALID);
    ffp.SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_MIPLINEAR);
    ffp.SetTextureStageState(0, CKRST_TSS_MIPMAPLODBIAS, FloatStageState(1.0f));
    drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(!drawn &&
                  ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_SAMPLER_LOD_CONTROL,
              "Unsupported mip LOD controls must reject instead of being ignored");

    ffp.SetTextureStageState(0, CKRST_TSS_MIPMAPLODBIAS, FloatStageState(0.0f));
    ffp.SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_ANISOTROPIC);
    ffp.SetTextureStageState(0, CKRST_TSS_MAXANISOTROPY, 4);
    drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(!drawn &&
                  ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_SAMPLER_ANISOTROPY_LIMIT,
              "Unrepresentable anisotropy limits must reject explicitly");

    ffp.SetTextureStageState(0, CKRST_TSS_MAXANISOTROPY, 1);
    drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(drawn &&
                  context.Encoder.LastTextureSampler.MinFilter == CKRST_FILTER_LINEAR &&
                  context.Encoder.LastTextureSampler.MipFilter == CKRST_FILTER_LINEAR,
              "MAXANISOTROPY one must reduce anisotropic filtering to linear filtering");

    ffp.Shutdown();
}

void SingleCubeVolumeLayoutUsesGenericMixedSamplerModule() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);
    ffp.SetTexture(0, 101, CKRST_TEXTURE_VALID | CKRST_TEXTURE_CUBEMAP);
    ffp.SetTexture(1, 102, CKRST_TEXTURE_VALID);
    ffp.SetTexture(2, 103, CKRST_TEXTURE_VALID | CKRST_TEXTURE_VOLUMEMAP);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(drawn && context.Encoder.SubmitCount == 1,
              "one cube and one volume texture must use the generic mixed sampler module");
    const CKFFUniformHandles &u = ffp.GetShaderCache().GetUniforms();
    bool sawCube = false;
    bool sawVolume = false;
    for (const FFPTextureBinding &binding : context.Encoder.TextureBindings) {
        if (binding.Stage == 9 && binding.Uniform == u.s_textureCube[0] &&
            binding.Texture == 101) {
            sawCube = true;
        }
        if (binding.Stage == 8 && binding.Uniform == u.s_textureVolume[0] &&
            binding.Texture == 103) {
            sawVolume = true;
        }
    }
    TestCheck(sawCube && sawVolume,
              "generic mixed sampler module must use stable cube and volume slots");
    ffp.Shutdown();
}

void DrawVertexBufferStopsBeforeSubmitAfterBindingFailure() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);
    context.Encoder.StateError = CKERR_INVALIDPARAMETER;

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(!drawn,
              "A backend state-binding failure must fail the originating FFP draw");
    TestCheck(context.Encoder.SubmitCount == 0,
              "A state-binding failure must stop before backend submit");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_ENCODER_ERROR,
              "A state-binding failure must report the encoder-error reason");

    context.Encoder.Status = CK_OK;
    context.Encoder.StateError = CK_OK;
    ffp.Shutdown();
}

void DrawVertexBufferStopsUniformUploadsAfterFailure() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);
    context.Encoder.UniformError = CKERR_INVALIDPARAMETER;

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(!drawn,
              "A backend uniform failure must fail the originating FFP draw");
    TestCheck(context.Encoder.UniformSetCount == 1,
              "Uniform upload must stop at the first backend failure");
    TestCheck(context.Encoder.StateSetCount == 0 && context.Encoder.SubmitCount == 0,
              "A uniform failure must stop before state binding and submit");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_ENCODER_ERROR,
              "A uniform failure must report the encoder-error reason");

    context.Encoder.Status = CK_OK;
    context.Encoder.UniformError = CK_OK;
    ffp.Shutdown();
}

void DrawVertexBufferUploadsAlphaPrecision() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_ALPHAFUNC, VXCMP_GREATER);
    ffp.SetRenderState(VXRENDERSTATE_ALPHAREF, 0x12345680);
    ffp.SetAlphaTestPrecision(0x2);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKDWORD uniform = ffp.GetShaderCache().GetUniforms().u_ffDrawParams;
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator it =
        context.Encoder.FloatUniforms.find(uniform);

    TestCheck(it != context.Encoder.FloatUniforms.end(),
              "FFP draw must upload draw params");
    TestCheck(it->second.size() >= 36,
              "FFP draw params must contain alpha-test slot");
    TestCheck(it->second[32] == 0x80,
              "FFP draw params must upload alpha ref low byte");
    const CKDWORD alphaFuncPrecision = (CKDWORD)it->second[33];
    TestCheck((alphaFuncPrecision & 0xFu) == VXCMP_GREATER,
              "FFP draw params must upload alpha-test compare function");
    TestCheck(((alphaFuncPrecision >> 4) & 0xFu) == 0x2,
              "FFP draw params must upload current alpha-test precision");

    ffp.Shutdown();
}

void DrawVertexBufferSetsFlatShadeSpecialization() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_GOURAUD);
    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKFFSpecializationInfo gouraudSpec = CurrentDrawSpecialization(ffp, context);
    TestCheck(gouraudSpec.Get(CKFF_SPEC_FLAT_SHADE) == 0,
              "Gouraud shade mode must not set flat shade specialization");

    ffp.SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_FLAT);
    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKFFSpecializationInfo flatSpec = CurrentDrawSpecialization(ffp, context);
    TestCheck(flatSpec.Get(CKFF_SPEC_FLAT_SHADE) == 1,
              "Flat shade mode must set flat shade specialization");

    ffp.Shutdown();
}

void DrawVertexBufferUploadsFogParams() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    const float fogStart = 10.0f;
    const float fogEnd = 30.0f;
    const float fogDensity = 0.125f;
    ffp.SetRenderState(VXRENDERSTATE_FOGENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_FOGVERTEXMODE, VXFOG_EXP);
    ffp.SetRenderState(VXRENDERSTATE_FOGPIXELMODE, VXFOG_NONE);
    ffp.SetRenderState(VXRENDERSTATE_FOGSTART, FloatStageState(fogStart));
    ffp.SetRenderState(VXRENDERSTATE_FOGEND, FloatStageState(fogEnd));
    ffp.SetRenderState(VXRENDERSTATE_FOGDENSITY, FloatStageState(fogDensity));

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKDWORD uniform = ffp.GetShaderCache().GetUniforms().u_ffDrawParams;
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator it =
        context.Encoder.FloatUniforms.find(uniform);

    TestCheck(it != context.Encoder.FloatUniforms.end(),
              "FFP fog draw must upload draw params");
    TestCheck(it->second.size() >= 44,
              "FFP fog draw params must contain fog slot");
    TestCheck(it->second[40] == fogStart,
              "FFP fog params must upload fog start");
    TestCheck(it->second[41] == fogEnd,
              "FFP fog params must upload fog end");
    TestCheck(it->second[42] == fogDensity,
              "FFP fog params must upload fog density");
    TestCheck(it->second[43] == (float)VXFOG_EXP,
              "FFP fog params must upload vertex fog mode");

    ffp.Shutdown();
}

void PositionTFogUsesPositionTShaderKey() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetRenderState(VXRENDERSTATE_FOGENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_FOGVERTEXMODE, VXFOG_LINEAR);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKFF_VF_POSITIONT | CKFF_VF_COLOR0 | CKFF_VF_COLOR1, 1);

    const CKDWORD uniform = ffp.GetShaderCache().GetUniforms().u_ffDrawParams;
    const CKDWORD matrixUniform = ffp.GetShaderCache().GetUniforms().u_ffMatrices;
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator it =
        context.Encoder.FloatUniforms.find(uniform);
    TestCheck(context.Encoder.FloatUniforms.find(matrixUniform) == context.Encoder.FloatUniforms.end(),
              "POSITIONT draws must not upload transformed 3D matrix uniforms");
    TestCheck(it != context.Encoder.FloatUniforms.end() && it->second.size() >= 44,
              "POSITIONT fog draw must upload fog params");
    TestCheck(it->second[43] == (float)VXFOG_LINEAR,
              "POSITIONT fog draw must upload vertex fog mode");

    ffp.Shutdown();
}

void RangeFogChangesSpecialization() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetRenderState(VXRENDERSTATE_FOGENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_FOGVERTEXMODE, VXFOG_LINEAR);
    ffp.SetRenderState(VXRENDERSTATE_RANGEFOGENABLE, FALSE);
    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKFFSpecializationInfo specNoRange = CurrentDrawSpecialization(ffp, context);
    TestCheck(specNoRange.Get(CKFF_SPEC_RANGE_FOG) == 0,
              "Range fog disabled must clear range fog specialization");

    ffp.SetRenderState(VXRENDERSTATE_RANGEFOGENABLE, TRUE);
    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKFFSpecializationInfo specRange = CurrentDrawSpecialization(ffp, context);
    TestCheck(specRange.Get(CKFF_SPEC_RANGE_FOG) == 1,
              "Range fog enabled must set range fog specialization");

    ffp.Shutdown();
}

void PixelFogOverridesVertexFogMode() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetRenderState(VXRENDERSTATE_FOGENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_FOGVERTEXMODE, VXFOG_EXP);
    ffp.SetRenderState(VXRENDERSTATE_FOGPIXELMODE, VXFOG_LINEAR);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKFF_VF_POSITION | CKFF_VF_NORMAL, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    const CKDWORD uniform = ffp.GetShaderCache().GetUniforms().u_ffDrawParams;
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator it =
        context.Encoder.FloatUniforms.find(uniform);

    TestCheck(spec.Get(CKFF_SPEC_VERTEX_FOG_MODE) == VXFOG_NONE,
              "Pixel fog must clear vertex fog specialization");
    TestCheck(spec.Get(CKFF_SPEC_PIXEL_FOG_MODE) == VXFOG_LINEAR,
              "Pixel fog must keep the pixel fog specialization");
    TestCheck(it != context.Encoder.FloatUniforms.end() &&
                  it->second.size() >= 44 &&
                  it->second[35] == (float)VXFOG_LINEAR &&
                  it->second[43] == (float)VXFOG_NONE,
              "Pixel fog draw params must clear vertex fog mode and preserve pixel fog mode");
    TestCheck(it != context.Encoder.FloatUniforms.end() &&
                  it->second.size() >= 32 &&
                  it->second[31] == 0.0f,
              "Pixel fog mode must not leak into the inline-light flag");

    ffp.Shutdown();
}

void DrawVertexBufferCompactsClipPlaneUniforms() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    VxPlane plane1;
    plane1.m_Normal = VxVector(1.0f, 2.0f, 3.0f);
    plane1.m_D = 4.0f;
    VxPlane plane3;
    plane3.m_Normal = VxVector(5.0f, 6.0f, 7.0f);
    plane3.m_D = 8.0f;
    ffp.SetUserClipPlane(1, plane1);
    ffp.SetUserClipPlane(3, plane3);
    ffp.SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, (1u << 1) | (1u << 3));

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKDWORD planesUniform = ffp.GetShaderCache().GetUniforms().u_clipPlanes;
    const CKDWORD paramsUniform = ffp.GetShaderCache().GetUniforms().u_clipParams;
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator planes =
        context.Encoder.FloatUniforms.find(planesUniform);
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator params =
        context.Encoder.FloatUniforms.find(paramsUniform);

    TestCheck(planes != context.Encoder.FloatUniforms.end(),
              "Enabled clip planes must upload compacted plane uniform");
    TestCheck(params != context.Encoder.FloatUniforms.end(),
              "Enabled clip planes must upload clip params uniform");
    TestCheck(params->second[0] == 2.0f,
              "Clip params must contain enabled clip plane count");
    TestCheck(planes->second[0] == 1.0f && planes->second[1] == 2.0f &&
                  planes->second[2] == 3.0f && planes->second[3] == 4.0f,
              "First uploaded clip plane must be the lowest enabled index");
    TestCheck(planes->second[4] == 5.0f && planes->second[5] == 6.0f &&
                  planes->second[6] == 7.0f && planes->second[7] == 8.0f,
              "Second uploaded clip plane must be the next enabled index");

    ffp.Shutdown();
}

void DrawVertexBufferSkipsClipUniformsWhenDisabled() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    VxPlane plane;
    plane.m_Normal = VxVector(1.0f, 0.0f, 0.0f);
    plane.m_D = 1.0f;
    ffp.SetUserClipPlane(0, plane);
    ffp.SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, 0);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKDWORD planesUniform = ffp.GetShaderCache().GetUniforms().u_clipPlanes;
    const CKDWORD paramsUniform = ffp.GetShaderCache().GetUniforms().u_clipParams;
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator params =
        context.Encoder.FloatUniforms.find(paramsUniform);
    TestCheck(context.Encoder.FloatUniforms.find(planesUniform) == context.Encoder.FloatUniforms.end(),
              "Disabled clip planes must not upload clip plane uniform");
    TestCheck(params == context.Encoder.FloatUniforms.end() || params->second[0] == 0.0f,
              "Disabled clip planes must either skip clip params or upload count zero");

    ffp.Shutdown();
}

void ClipPlanesUseDedicatedVertexShaderVariant() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    const void *defaultVertexShaderCode = context.LastVertexShaderCode;

    VxPlane plane;
    plane.m_Normal = VxVector(1.0f, 0.0f, 0.0f);
    plane.m_D = 1.0f;
    ffp.SetUserClipPlane(0, plane);
    ffp.SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, 1u);
    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(defaultVertexShaderCode != nullptr,
              "Default draw must create a vertex shader");
    TestCheck(context.LastVertexShaderCode != nullptr &&
                  context.LastVertexShaderCode != defaultVertexShaderCode,
              "Clip plane draw must use a dedicated vertex shader variant");

    ffp.Shutdown();
}

void ResultArgTempClearsOnlyLastActiveStage() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_RESULTARG0, CKRST_TA_TEMP);
    ffp.SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_TEMP);
    ffp.SetTextureStageState(1, CKRST_TSS_RESULTARG0, CKRST_TA_TEMP);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    TestCheck(spec.Get(CKFF_SPEC_STAGE0_RESULT_IS_TEMP) == 1,
              "Non-final active stage must preserve RESULTARG=TEMP");
    TestCheck(spec.Get(CKFF_SPEC_STAGE1_RESULT_IS_TEMP) == 0,
              "Final active stage must write to current even when RESULTARG=TEMP");

    ffp.Shutdown();
}

void Modulate4XStaysInTextureStageSpecialization() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE4X);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_CURRENT);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    TestCheck(spec.Get(CKFF_SPEC_STAGE0_COLOR_OP) == CKRST_TOP_MODULATE4X,
              "MODULATE4X must remain a normal texture-stage specialization op");

    ffp.Shutdown();
}

void PremodulateStaysInTextureStageSpecialization() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_PREMODULATE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_CURRENT);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    TestCheck(spec.Get(CKFF_SPEC_STAGE0_COLOR_OP) == CKRST_TOP_PREMODULATE,
              "PREMODULATE must remain encoded as the stage color op");
    TestCheck(spec.Get(CKFF_SPEC_LAST_ACTIVE_TEXTURE_STAGE) == 0,
              "Single-stage PREMODULATE draw must keep last active stage at zero");

    ffp.Shutdown();
}

void TextureArgModifiersStayInSpecialization() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE | CKRST_TA_COMPLEMENT);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE | CKRST_TA_ALPHAREPLICATE);
    ffp.SetTexture(0, 7);
    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V | CKRST_DP_STAGE(0), CKFF_VF_POSITION | CKFF_VF_TEXCOORD0, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    TestCheck(spec.Get(CKFF_SPEC_STAGE0_COLOR_ARG1) ==
                  CKFFSpecializationInfo::RepackArg(CKRST_TA_TEXTURE | CKRST_TA_COMPLEMENT),
              "COMPLEMENT must remain in stage specialization");
    TestCheck(spec.Get(CKFF_SPEC_STAGE0_ALPHA_ARG1) ==
                  CKFFSpecializationInfo::RepackArg(CKRST_TA_TEXTURE | CKRST_TA_ALPHAREPLICATE),
              "ALPHAREPLICATE must remain in stage specialization");

    ffp.Shutdown();
}

void NullTextureStagePreservesSpecialization() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTexture(0, 0);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_CURRENT);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    TestCheck(spec.Get(CKFF_SPEC_STAGE0_COLOR_OP) == CKRST_TOP_SELECTARG1,
              "Unbound texture stage must keep its original color op");
    TestCheck(spec.Get(CKFF_SPEC_STAGE0_COLOR_ARG1) == CKRST_TA_TEXTURE,
              "Unbound texture stage must keep TEXTURE as its color arg");
    TestCheck(spec.Get(CKFF_SPEC_LAST_ACTIVE_TEXTURE_STAGE) == 1,
              "Unbound texture stage must not truncate later active stages");

    ffp.Shutdown();
}

void StageConstantDoesNotCreateTextureDependency() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTexture(0, 0);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_CONSTANT);
    ffp.SetTextureStageState(0, CKRST_TSS_CONSTANT, 0x80402010u);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    const CKDWORD stageParamsUniform = ffp.GetShaderCache().GetUniforms().u_stageParams;
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator stageParams =
        context.Encoder.FloatUniforms.find(stageParamsUniform);

    TestCheck(spec.Get(CKFF_SPEC_STAGE0_COLOR_OP) == CKRST_TOP_SELECTARG1,
              "D3DTA_CONSTANT must not disable the stage when no texture is bound");
    TestCheck(spec.Get(CKFF_SPEC_STAGE0_COLOR_ARG1) == CKRST_TA_CONSTANT,
              "D3DTA_CONSTANT must remain encoded in specialization");
    TestCheck(stageParams != context.Encoder.FloatUniforms.end() &&
                  stageParams->second.size() >= 16 &&
                  stageParams->second[11] == 0x40 / 255.0f &&
                  stageParams->second[13] == 0x20 / 255.0f &&
                  stageParams->second[14] == 0x10 / 255.0f &&
                  stageParams->second[15] == 0x80 / 255.0f,
              "Stage constant must upload in stage params");

    ffp.Shutdown();
}

void CubeTextureUsesCubeSamplerSpecializationAndBinding() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID | CKRST_TEXTURE_CUBEMAP);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);
    TestCheck((spec.Get(CKFF_SPEC_SAMPLER_TYPE_MASK) & 0x3u) == CKFF_SAMPLER_CUBE,
              "Cubemap texture must mark stage 0 as cube sampler");
    TestCheck(context.Encoder.TextureBindCount == 1,
              "Cubemap draw must bind one texture");
    TestCheck(context.Encoder.LastTextureUniform == ffp.GetShaderCache().GetUniforms().s_textureCube[0],
              "Cubemap draw must bind the cube sampler uniform");
    TestCheck(context.Encoder.LastTextureHandle == 77,
              "Cubemap draw must bind the requested texture handle");

    ffp.Shutdown();
}

void VolumeTextureModulateCacheMissUsesUberShader() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTexture(0, 91, CKRST_TEXTURE_VALID | CKRST_TEXTURE_VOLUMEMAP);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_MODULATE);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG2, CKRST_TA_DIFFUSE);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);
    TestCheck(context.Encoder.SubmitCount == 1,
              "Volume texture cache miss must draw through the runtime shader");
    TestCheck((spec.Get(CKFF_SPEC_STAGE0_COLOR_OP) == CKRST_TOP_MODULATE) &&
              ((spec.Get(CKFF_SPEC_SAMPLER_TYPE_MASK) & 0x3u) == CKFF_SAMPLER_VOLUME),
              "Volume runtime shader must keep runtime stage op and volume sampler specialization");
    TestCheck(context.Encoder.TextureBindCount == 1,
              "Volume runtime draw must bind one texture");
    TestCheck(context.Encoder.LastTextureStage == 8 &&
              context.Encoder.LastTextureUniform == ffp.GetShaderCache().GetUniforms().s_textureVolume[0],
              "Volume stage 0 must bind slot 8 and s_textureVolume0");

    ffp.Shutdown();
}

void VolumeTextureStageSevenBindsVolumeSampler() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    for (CKDWORD stage = 0; stage < 7; ++stage) {
        ffp.SetTextureStageState(stage, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
        ffp.SetTextureStageState(stage, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
        ffp.SetTextureStageState(stage, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
        ffp.SetTextureStageState(stage, CKRST_TSS_AARG1, CKRST_TA_CURRENT);
    }
    ffp.SetTexture(7, 107, CKRST_TEXTURE_VALID | CKRST_TEXTURE_VOLUMEMAP);
    ffp.SetTextureStageState(7, CKRST_TSS_OP, CKRST_TOP_MODULATE);
    ffp.SetTextureStageState(7, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(7, CKRST_TSS_ARG2, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(7, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(7, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);

    TestCheck(context.Encoder.SubmitCount == 1,
              "Volume stage 7 must submit through the runtime shader");
    TestCheck(context.Encoder.TextureBindCount == 1,
              "Volume stage 7 draw must bind one texture");
    TestCheck(context.Encoder.LastTextureStage == 15 &&
              context.Encoder.LastTextureUniform == ffp.GetShaderCache().GetUniforms().s_textureVolume[7],
              "Volume stage 7 must bind slot 15 and s_textureVolume7");

    ffp.Shutdown();
}

void RunVolumeAndCubeCacheMissUsesStaticSamplerLayoutFallback(CK_SHADER_PROFILE profile) {
    FFPDiagnosticDriver driver(profile);
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTexture(0, 201, CKRST_TEXTURE_VALID | CKRST_TEXTURE_VOLUMEMAP);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);

    ffp.SetTexture(1, 202, CKRST_TEXTURE_VALID | CKRST_TEXTURE_CUBEMAP);
    ffp.SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_ADD);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG2, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(1, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_AARG1, CKRST_TA_CURRENT);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    TestCheck(context.Encoder.SubmitCount == 1,
              "Volume + cube cache miss must draw through the static sampler layout fallback");
    TestCheck((spec.Get(CKFF_SPEC_SAMPLER_TYPE_MASK) & 0x3u) == CKFF_SAMPLER_VOLUME &&
                  (((spec.Get(CKFF_SPEC_SAMPLER_TYPE_MASK) >> 2) & 0x3u) == CKFF_SAMPLER_CUBE),
              "Volume + cube fallback must preserve sampler types in runtime specialization data");
    TestCheck(spec.Get(CKFF_SPEC_STAGE0_COLOR_OP) == CKRST_TOP_MODULATE &&
                  spec.Get(CKFF_SPEC_STAGE1_COLOR_OP) == CKRST_TOP_ADD,
              "Volume + cube fallback must keep texture stage ops runtime-specialized");
    TestCheck(context.Encoder.TextureBindCount == 2,
              "Volume + cube fallback must bind both textures");

    const CKFFUniformHandles &u = ffp.GetShaderCache().GetUniforms();
    bool sawVolume = false;
    bool sawCube = false;
    for (const FFPTextureBinding &binding : context.Encoder.TextureBindings) {
        if (binding.Stage == 8 && binding.Uniform == u.s_textureVolume[0] && binding.Texture == 201)
            sawVolume = true;
        if (binding.Stage == 9 && binding.Uniform == u.s_textureCube[0] && binding.Texture == 202)
            sawCube = true;
    }
    TestCheck(sawVolume && sawCube,
              "Volume + cube fallback must bind each texture to the sampler type declared for its stage");

    ffp.Shutdown();
}

void VolumeAndCubeCacheMissUsesStaticSamplerLayoutFallback() {
    for (const ShaderProfileCase &profile : kSamplerLayoutProfiles) {
        printf("  profile %s\n", profile.Name);
        RunVolumeAndCubeCacheMissUsesStaticSamplerLayoutFallback(profile.Profile);
    }
}

void RunArbitrarySingleVolumeCubeLayoutUsesGenericFallback(CK_SHADER_PROFILE profile) {
    FFPDiagnosticDriver driver(profile);
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTexture(0, 211, CKRST_TEXTURE_VALID | CKRST_TEXTURE_VOLUMEMAP);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);

    ffp.SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(1, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_AARG1, CKRST_TA_CURRENT);

    ffp.SetTexture(2, 212, CKRST_TEXTURE_VALID | CKRST_TEXTURE_CUBEMAP);
    ffp.SetTextureStageState(2, CKRST_TSS_OP, CKRST_TOP_ADD);
    ffp.SetTextureStageState(2, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(2, CKRST_TSS_ARG2, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(2, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(2, CKRST_TSS_AARG1, CKRST_TA_CURRENT);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);

    TestCheck(context.Encoder.SubmitCount == 1,
              "arbitrary single volume+cube placement must draw through the generic fallback");
    TestCheck(context.Encoder.TextureBindCount == 2,
              "generic mixed fallback must bind both non-2D textures");
    TestCheck(context.CreatedProgramCount == 1,
              "generic mixed fallback must create one canonical program");

    const CKFFUniformHandles &u = ffp.GetShaderCache().GetUniforms();
    bool sawVolume = false;
    bool sawCube = false;
    for (const FFPTextureBinding &binding : context.Encoder.TextureBindings) {
        if (binding.Stage == 8 && binding.Uniform == u.s_textureVolume[0] && binding.Texture == 211)
            sawVolume = true;
        if (binding.Stage == 9 && binding.Uniform == u.s_textureCube[0] && binding.Texture == 212)
            sawCube = true;
    }
    TestCheck(sawVolume && sawCube,
              "generic mixed fallback must remap logical stages to canonical sampler slots");

    ffp.Shutdown();
}

void ArbitrarySingleVolumeCubeLayoutUsesGenericFallback() {
    for (const ShaderProfileCase &profile : kSamplerLayoutProfiles) {
        printf("  profile %s\n", profile.Name);
        RunArbitrarySingleVolumeCubeLayoutUsesGenericFallback(profile.Profile);
    }
}

void MultipleVolumeTexturesBindEachVolumeSampler() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTexture(0, 301, CKRST_TEXTURE_VALID | CKRST_TEXTURE_VOLUMEMAP);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);

    ffp.SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(1, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_AARG1, CKRST_TA_CURRENT);

    ffp.SetTexture(2, 302, CKRST_TEXTURE_VALID | CKRST_TEXTURE_VOLUMEMAP);
    ffp.SetTextureStageState(2, CKRST_TSS_OP, CKRST_TOP_ADD);
    ffp.SetTextureStageState(2, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(2, CKRST_TSS_ARG2, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(2, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(2, CKRST_TSS_AARG1, CKRST_TA_CURRENT);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);

    const CKFFUniformHandles &u = ffp.GetShaderCache().GetUniforms();
    bool sawStage0 = false;
    bool sawStage2 = false;
    for (const FFPTextureBinding &binding : context.Encoder.TextureBindings) {
        if (binding.Stage == 8 && binding.Uniform == u.s_textureVolume[0] && binding.Texture == 301)
            sawStage0 = true;
        if (binding.Stage == 10 && binding.Uniform == u.s_textureVolume[2] && binding.Texture == 302)
            sawStage2 = true;
    }
    TestCheck(context.Encoder.SubmitCount == 1,
              "Multi-volume runtime draw must submit");
    TestCheck(sawStage0 && sawStage2,
              "Multiple volume stages must bind their matching volume sampler uniforms");

    ffp.Shutdown();
}

void DepthTextureCompareFuncUploadsSamplerAndSpecialization() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTexture(0, 101, CKRST_TEXTURE_VALID | CKRST_TEXTURE_DEPTHSTENCIL);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKFFSpecializationInfo noCompareSpec = CurrentDrawSpecialization(ffp, context);
    TestCheck((noCompareSpec.Get(CKFF_SPEC_SAMPLER_TYPE_MASK) & 0x3u) == CKFF_SAMPLER_DEPTH,
              "Depth texture must mark stage 0 as depth sampler");
    TestCheck((noCompareSpec.Get(CKFF_SPEC_SAMPLER_COMPARE_FUNC_MASK) & 0xFu) == CKRST_COMPARE_NONE,
              "Depth texture without compare func must keep compare mask empty");
    TestCheck(context.Encoder.LastTextureSampler.CompareFunc == CKRST_COMPARE_NONE,
              "Depth texture without compare func must bind a non-compare sampler");

    ffp.SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
    ffp.SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    ffp.SetTextureStageState(0, CKRST_TSS_COMPAREFUNC, CKRST_COMPARE_LEQUAL);
    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKFFSpecializationInfo compareSpec = CurrentDrawSpecialization(ffp, context);
    TestCheck((compareSpec.Get(CKFF_SPEC_SAMPLER_COMPARE_FUNC_MASK) & 0xFu) == CKRST_COMPARE_LEQUAL,
              "Depth compare func must enter specialization mask");
    TestCheck(context.Encoder.LastTextureSampler.CompareFunc == CKRST_COMPARE_NONE,
              "Depth compare func must stay shader-evaluated and bind a non-compare sampler");

    ffp.Shutdown();
}

void FilteredDepthTextureCompareRejectsDraw() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTexture(0, 101, CKRST_TEXTURE_VALID | CKRST_TEXTURE_DEPTHSTENCIL);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_COMPAREFUNC, CKRST_COMPARE_LEQUAL);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(!drawn && context.Encoder.SubmitCount == 0,
              "Filtered shader depth compare must reject instead of changing PCF semantics");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_DEPTH_COMPARE_FILTER,
              "Filtered shader depth compare must expose its rejection reason");

    ffp.Shutdown();
}

void AffineTextureCoordinatesRejectDraw() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);
    ffp.SetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE, FALSE);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(!drawn && context.Encoder.SubmitCount == 0,
              "Affine texture coordinates must not silently render as perspective-correct");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_AFFINE_TEXCOORD,
              "Affine texture-coordinate rejection must expose its reason");
    ffp.Shutdown();
}

void InactiveUnsupportedStateDoesNotRejectDraw() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);
    ffp.SetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_STENCILENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_STENCILWRITEMASK, 0x0Fu);
    ffp.SetRenderState(VXRENDERSTATE_STENCILFAIL, VXSTENCILOP_KEEP);
    ffp.SetRenderState(VXRENDERSTATE_STENCILZFAIL, VXSTENCILOP_KEEP);
    ffp.SetRenderState(VXRENDERSTATE_STENCILPASS, VXSTENCILOP_KEEP);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(drawn && context.Encoder.SubmitCount == 1,
              "Unsupported state bits with no output effect must not reject a draw");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_NONE,
              "A successful no-effect state draw must clear the reject reason");
    ffp.Shutdown();
}

void DisabledTextureStageIgnoresLaterUnsupportedState() {
    FFPDiagnosticDriver driver(CKRST_SHADER_PROFILE_GLSL,
                               CKRST_SHADER_TARGET_NDC_MINUS_ONE_TO_ONE |
                               CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT);
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);
    ffp.SetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE, FALSE);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_DISABLE);
    ffp.SetTextureStageState(1, CKRST_TSS_OP, 0x7fffffffu);
    ffp.SetTextureStageState(1, CKRST_TSS_COMPAREFUNC, CKRST_COMPARE_LEQUAL);
    ffp.SetTexture(1, 77, CKRST_TEXTURE_VALID | CKRST_TEXTURE_RENDERTARGET |
                          CKRST_TEXTURE_CUBEMAP | CKRST_TEXTURE_DEPTHSTENCIL);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(drawn && context.Encoder.SubmitCount == 1,
              "COLOROP=DISABLE must make all later texture-stage state inactive");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_NONE,
              "Inactive later texture stages must not report a draw rejection");
    ffp.Shutdown();
}

void BoundUnusedTextureDoesNotRequireAffineInterpolation() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);
    ffp.SetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE, FALSE);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_DIFFUSE);
    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(drawn && context.Encoder.SubmitCount == 1,
              "A bound but unused texture must not require affine interpolation support");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_NONE,
              "Unused texture binding must not report an affine draw rejection");
    ffp.Shutdown();
}

void TextureStageSnapshotPreservesExplicitZeroArgument() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_DIFFUSE);

    CKFFTextureStageSnapshot snapshot;
    ffp.SaveTextureStage(0, snapshot);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);
    ffp.RestoreTextureStage(0, snapshot);
    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID);
    ffp.SetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE, FALSE);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(drawn && context.Encoder.SubmitCount == 1,
              "Texture-stage snapshot must preserve an explicitly selected zero-valued argument");
    ffp.Shutdown();
}

void UnknownTextureOpRejectsDraw() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, 0x7fffffffu);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(!drawn && context.Encoder.SubmitCount == 0,
              "An unknown texture op must not fall through to an approximate shader formula");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_TEXTURE_OP,
              "Unknown texture-op rejection must expose its reason");
    ffp.Shutdown();
}

void BottomLeftCubeRenderTargetRejectsDraw() {
    FFPDiagnosticDriver driver(CKRST_SHADER_PROFILE_GLSL,
                               CKRST_SHADER_TARGET_NDC_MINUS_ONE_TO_ONE |
                               CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT);
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID | CKRST_TEXTURE_RENDERTARGET |
                          CKRST_TEXTURE_CUBEMAP);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(!drawn && context.Encoder.SubmitCount == 0,
              "Bottom-left cube render targets must fail until face orientation is defined");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_RENDER_TARGET_TYPE,
              "Unsupported render-target type must expose its rejection reason");
    ffp.Shutdown();
}

void BorderColorUsesStableBgfxPaletteSlots() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_ADDRESSU, VXTEXTURE_ADDRESSBORDER);
    ffp.SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0x80402010u);
    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID);

    const CKBOOL first = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    const CKBOOL second = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(first && second,
              "A representable border color must submit normally");
    TestCheck(context.PaletteSetCount == 1,
              "Repeated border colors must reuse one stable palette slot");
    TestCheck(context.PaletteColors[0] == 0x40201080u,
              "Virtools ARGB border color must convert to bgfx RRGGBBAA");
    TestCheck(context.Encoder.LastTextureSampler.BorderColor == 0,
              "The backend sampler must receive the allocated palette index");

    ffp.Shutdown();
}

void BorderPaletteOverflowRejectsDraw() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_ADDRESSU, VXTEXTURE_ADDRESSBORDER);
    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID);

    CKBOOL firstSixteenSucceeded = TRUE;
    for (CKDWORD i = 0; i < 16; ++i) {
        ffp.SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0xFF000000u | i);
        firstSixteenSucceeded = firstSixteenSucceeded && ffp.DrawVertexBuffer(
            &context.Encoder, 1, VX_TRIANGLELIST,
            1, 0, 0, 3, 0, 0,
            CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    }
    ffp.SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0xFF000010u);
    const CKBOOL overflow = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(firstSixteenSucceeded && context.PaletteSetCount == 16,
              "All sixteen bgfx border palette slots must be usable");
    TestCheck(!overflow && context.Encoder.SubmitCount == 16,
              "A seventeenth border color must fail without backend submission");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_BORDER_PALETTE,
              "Border palette overflow must expose a stable rejection reason");

    ffp.Shutdown();
}

void BorderPaletteSlotsAreReusedAcrossFrames() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_ADDRESSU, VXTEXTURE_ADDRESSBORDER);
    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID);

    CKBOOL firstFrameSucceeded = TRUE;
    for (CKDWORD i = 0; i < 16; ++i) {
        ffp.SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0xFF000000u | i);
        firstFrameSucceeded = firstFrameSucceeded && ffp.DrawVertexBuffer(
            &context.Encoder, 1, VX_TRIANGLELIST,
            1, 0, 0, 3, 0, 0,
            CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    }
    ffp.GetRenderPipeline().EndFrame(CKRST_FRAME_SYNC_IMMEDIATE);
    ffp.SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0xFF000010u);
    const CKBOOL nextFrame = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(firstFrameSucceeded && nextFrame,
              "Border palette slots must be reusable after the rasterizer advances a frame");
    TestCheck(context.PaletteSetCount == 17 &&
                  context.Encoder.LastTextureSampler.BorderColor == 0,
              "The next frame must allocate its first border color from palette slot zero");
    ffp.Shutdown();
}

void UnusedBorderTexturesDoNotConsumePaletteSlots() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_ADDRESSU, VXTEXTURE_ADDRESSBORDER);
    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID);

    CKBOOL allDrawsSucceeded = TRUE;
    for (CKDWORD i = 0; i < 17; ++i) {
        ffp.SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0xFF000000u | i);
        allDrawsSucceeded = allDrawsSucceeded && ffp.DrawVertexBuffer(
            &context.Encoder, 1, VX_TRIANGLELIST,
            1, 0, 0, 3, 0, 0,
            CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    }

    TestCheck(allDrawsSucceeded && context.Encoder.SubmitCount == 17,
              "Unused border textures must not reject otherwise valid draws");
    TestCheck(context.PaletteSetCount == 0,
              "Unused border textures must not consume border palette slots");
    TestCheck(context.Encoder.TextureBindCount == 0,
              "Unused textures must not reach the backend binding path");

    ffp.Shutdown();
}

void UntexturedStageKeepsRuntimeStageParams() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_CONSTANT);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_CONSTANT);
    ffp.SetTextureStageState(0, CKRST_TSS_CONSTANT, 0x80402010u);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKDWORD uniform = ffp.GetShaderCache().GetUniforms().u_stageParams;
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator params =
        context.Encoder.FloatUniforms.find(uniform);
    TestCheck(drawn && params != context.Encoder.FloatUniforms.end(),
              "An untextured constant stage must upload runtime stage params");
    TestCheck(params != context.Encoder.FloatUniforms.end() &&
                  params->second.size() >= 4 &&
                  params->second[0] == (float)CKRST_TOP_SELECTARG1,
              "Texture binding span must not disable an active untextured stage");
    TestCheck(context.Encoder.TextureBindCount == 0,
              "An untextured constant stage must not create a texture binding");

    ffp.Shutdown();
}

void PremodulateImplicitTextureDependencyBindsNextStage() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_PREMODULATE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(1, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_AARG1, CKRST_TA_CURRENT);
    ffp.SetTexture(1, 88, CKRST_TEXTURE_VALID);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(drawn && context.Encoder.SubmitCount == 1,
              "PREMODULATE implicit texture dependency must remain drawable");
    TestCheck(context.Encoder.TextureBindCount == 1 &&
                  context.Encoder.LastTextureHandle == 88,
              "PREMODULATE must bind the next-stage texture used by CURRENT");

    ffp.Shutdown();
}

void UberShaderProgramModulesAreSharedAcrossStateBindings() {
    CKRenderSettingsClearOverridesForTests();
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::FFP,
                                        "UberShader", "1");

    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_GOURAUD);
    const CKBOOL gouraud = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    ffp.SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_FLAT);
    const CKBOOL flat = ffp.DrawVertexBuffer(
        &context.Encoder, 1, VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKFFSpecializationInfo current = CurrentDrawSpecialization(ffp, context);
    TestCheck(gouraud && flat && context.CreatedProgramCount == 1,
              "Uber state variants sharing shader blobs must create one backend program");
    TestCheck(ffp.GetShaderCache().CachedProgramCount() == 1 &&
                  ffp.GetShaderCache().CachedBindingCount() == 2,
              "Program-module cache and state-binding cache must have separate cardinality");
    TestCheck(current.Get(CKFF_SPEC_FLAT_SHADE) == 1,
              "Shared uber programs must still upload current-draw specialization state");

    ffp.Shutdown();
    CKRenderSettingsClearOverridesForTests();
}

void LegacyStageBlendZeroTerminatesStaleMultitextureState() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    VxVector positions[4] = {
        VxVector(-1.0f, -1.0f, 0.0f),
        VxVector( 1.0f, -1.0f, 0.0f),
        VxVector( 1.0f,  1.0f, 0.0f),
        VxVector(-1.0f,  1.0f, 0.0f)
    };
    Vx2DVector uvs[4] = {
        Vx2DVector(0.0f, 0.0f),
        Vx2DVector(1.0f, 0.0f),
        Vx2DVector(1.0f, 1.0f),
        Vx2DVector(0.0f, 1.0f)
    };
    CKDWORD colors[4] = {
        0x80FFFFFFu,
        0x80FFFFFFu,
        0x80FFFFFFu,
        0x80FFFFFFu
    };
    CKWORD indices[6] = {0, 1, 2, 0, 2, 3};
    VxDrawPrimitiveData data = {};
    data.VertexCount = 4;
    data.Flags = CKRST_DP_TR_CL_VCT;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    data.TexCoordPtr = uvs;
    data.TexCoordStride = sizeof(Vx2DVector);
    data.ColorPtr = colors;
    data.ColorStride = sizeof(CKDWORD);

    ffp.SetTexture(0, 101);
    ffp.SetTexture(1, 202);
    ffp.SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_ADD);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG2, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(1, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_AARG1, CKRST_TA_CURRENT);

    ffp.SetTexture(0, 303);
    ffp.SetTextureStageState(0, CKRST_TSS_TEXTUREMAPBLEND, VXTEXTUREBLEND_MODULATEALPHA);
    ffp.SetTextureStageState(1, CKRST_TSS_STAGEBLEND, 0);

    ffp.DrawPrimitive(&context.Encoder, 1, VX_TRIANGLELIST, indices, 6, &data);

    TestCheck(context.Encoder.SubmitCount == 1,
              "Particle-like draw must submit once");
    TestCheck(context.Encoder.TextureBindCount == 1,
              "STAGEBLEND zero on stage 1 must suppress stale stage 1 texture binding");
    TestCheck(context.Encoder.LastTextureStage == 0 &&
                  context.Encoder.LastTextureHandle == 303,
              "Particle-like draw must bind only its current stage 0 texture");

    ffp.Shutdown();
}

void LegacyTextureMapBlendClearsExplicitStageOps() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    VxVector positions[3] = {
        VxVector(-1.0f, -1.0f, 0.0f),
        VxVector( 1.0f, -1.0f, 0.0f),
        VxVector( 0.0f,  1.0f, 0.0f)
    };
    Vx2DVector uvs[3] = {
        Vx2DVector(0.0f, 0.0f),
        Vx2DVector(1.0f, 0.0f),
        Vx2DVector(0.5f, 1.0f)
    };
    CKDWORD colors[3] = {
        0x80FFFFFFu,
        0x80FFFFFFu,
        0x80FFFFFFu
    };
    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TR_CL_VCT;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    data.TexCoordPtr = uvs;
    data.TexCoordStride = sizeof(Vx2DVector);
    data.ColorPtr = colors;
    data.ColorStride = sizeof(CKDWORD);

    ffp.SetTexture(0, 101);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_ADD);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_CURRENT);

    ffp.SetTextureStageState(0, CKRST_TSS_TEXTUREMAPBLEND, VXTEXTUREBLEND_MODULATEALPHA);
    ffp.DrawPrimitive(&context.Encoder, 1, VX_TRIANGLELIST, nullptr, 0, &data);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    TestCheck(spec.Get(CKFF_SPEC_STAGE0_COLOR_OP) == CKRST_TOP_MODULATE,
              "TEXTUREMAPBLEND must restore legacy modulate color op over stale explicit op");
    TestCheck(spec.Get(CKFF_SPEC_STAGE0_ALPHA_OP) == CKRST_TOP_MODULATE,
              "TEXTUREMAPBLEND must restore legacy modulate alpha op over stale explicit op");

    ffp.Shutdown();
}

void PointSpriteDrawPrimitiveExpandsToTriangleList() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    VxVector position(0.0f, 0.0f, 0.0f);
    Vx2DVector uv(0.25f, 0.75f);
    VxDrawPrimitiveData data = {};
    data.VertexCount = 1;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_CL_V;
    data.PositionPtr = &position;
    data.PositionStride = sizeof(VxVector);
    data.TexCoordPtr = &uv;
    data.TexCoordStride = sizeof(Vx2DVector);

    const float pointSize = 2.0f;
    ffp.SetRenderState(VXRENDERSTATE_POINTSPRITEENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_POINTSIZE, FloatStageState(pointSize));

    ffp.DrawPrimitive(&context.Encoder, 1, VX_POINTLIST, nullptr, 1, &data);

    const CKDWORD stride = 36;
    TestCheck(context.Encoder.SubmitCount == 1,
              "Point sprite draw must submit once");
    TestCheck(DrawStateTopology(context.Encoder.LastState) == VX_TRIANGLELIST,
              "Point sprite draw state must submit triangle-list topology");
    TestCheck(context.Encoder.LastVertexBytes.size() == stride * 4,
              "One point sprite must expand to four transient vertices");
    TestCheck(context.Encoder.LastIndexBytes.size() == sizeof(CKWORD) * 6,
              "One point sprite must expand to six transient indices");

    float uv0[2], uv1[2], uv2[2], uv3[2];
    memcpy(uv0, &context.Encoder.LastVertexBytes[12], sizeof(uv0));
    memcpy(uv1, &context.Encoder.LastVertexBytes[stride + 12], sizeof(uv1));
    memcpy(uv2, &context.Encoder.LastVertexBytes[stride * 2 + 12], sizeof(uv2));
    memcpy(uv3, &context.Encoder.LastVertexBytes[stride * 3 + 12], sizeof(uv3));
    TestCheck(uv0[0] == 0.0f && uv0[1] == 0.0f,
              "Point sprite vertex 0 must use UV (0,0)");
    TestCheck(uv1[0] == 1.0f && uv1[1] == 0.0f,
              "Point sprite vertex 1 must use UV (1,0)");
    TestCheck(uv2[0] == 1.0f && uv2[1] == 1.0f,
              "Point sprite vertex 2 must use UV (1,1)");
    TestCheck(uv3[0] == 0.0f && uv3[1] == 1.0f,
              "Point sprite vertex 3 must use UV (0,1)");

    ffp.Shutdown();
}

void PointSpriteUsesPerVertexPointSize() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    struct PointVertex {
        float x, y, z;
        float size;
    };
    PointVertex vertex = {0.0f, 0.0f, 0.0f, 6.0f};
    Vx2DVector uv(0.25f, 0.75f);
    VxDrawPrimitiveData data = {};
    data.VertexCount = 1;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_CL_V | CKRST_DP_PSIZE;
    data.PositionPtr = &vertex;
    data.PositionStride = sizeof(PointVertex);
    data.TexCoordPtr = &uv;
    data.TexCoordStride = sizeof(Vx2DVector);

    ffp.SetRenderState(VXRENDERSTATE_POINTSPRITEENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_POINTSIZE, FloatStageState(2.0f));
    ffp.SetRenderState(VXRENDERSTATE_POINTSIZE_MIN, FloatStageState(1.0f));
    ffp.SetRenderState(VXRENDERSTATE_POINTSIZE_MAX, FloatStageState(4.0f));

    ffp.DrawPrimitive(&context.Encoder, 1, VX_POINTLIST, nullptr, 1, &data);

    const CKDWORD stride = 36;
    TestCheck(context.Encoder.LastVertexBytes.size() == stride * 4,
              "Per-vertex point sprite must expand to four transient vertices");
    if (context.Encoder.LastVertexBytes.size() == stride * 4) {
        float p0[3] = {};
        float p1[3] = {};
        memcpy(p0, &context.Encoder.LastVertexBytes[0], sizeof(p0));
        memcpy(p1, &context.Encoder.LastVertexBytes[stride], sizeof(p1));
        TestCheck(fabsf(p0[0] + 2.0f) < 0.001f && fabsf(p1[0] - 2.0f) < 0.001f,
                  "Per-vertex point size must drive sprite width after max clamp");
        TestCheck(fabsf(p0[1] - p1[1]) < 0.001f,
                  "Adjacent point sprite corners must stay on the same edge");
    }

    ffp.Shutdown();
}

void ProjectedSamplerStagesZeroToThreeEnterSpecializationMask() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(2, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(2, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(2, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_PROJECTED);
    ffp.SetTexture(2, 102, CKRST_TEXTURE_VALID);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    TestCheck((spec.Get(CKFF_SPEC_PROJECTED_SAMPLER_MASK) & (1u << 2)) != 0,
              "Stage 2 projected sampler must be encoded in the specialization mask");

    ffp.Shutdown();
}

void ProjectedSamplerStagesFourToSevenStayInRuntimeStageParams() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    for (int stage = 1; stage < 4; ++stage) {
        ffp.SetTextureStageState(stage, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
        ffp.SetTextureStageState(stage, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    }
    ffp.SetTextureStageState(4, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(4, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(4, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_PROJECTED);
    ffp.SetTexture(4, 104, CKRST_TEXTURE_VALID);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    const CKDWORD stageParamsUniform = ffp.GetShaderCache().GetUniforms().u_stageParams;
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator stageParams =
        context.Encoder.FloatUniforms.find(stageParamsUniform);

    TestCheck((spec.Get(CKFF_SPEC_PROJECTED_SAMPLER_MASK) & (1u << 4)) == 0,
              "Stage 4 projected sampler must not be encoded in the 4-bit optimized mask");
    TestCheck(stageParams != context.Encoder.FloatUniforms.end(),
              "Stage 4 projected sampler must force runtime stage params");
    TestCheck(stageParams->second.size() >= 4 * (4 * 4 + 3) &&
                  stageParams->second[(4 * 4 + 2) * 4 + 2] == (float)CKRST_TTF_PROJECTED,
              "Stage 4 runtime stage params must preserve projected transform flags");

    ffp.Shutdown();
}

void DrawUploadsPerStageBumpEnvUniforms() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTexture(0, 100);
    ffp.SetTexture(1, 101);
    ffp.SetTexture(2, 102);
    ffp.SetTexture(3, 103);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_BUMPENVMAP);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_BUMPENVMAT00, FloatStageState(1.0f));
    ffp.SetTextureStageState(0, CKRST_TSS_BUMPENVMAT01, FloatStageState(2.0f));
    ffp.SetTextureStageState(0, CKRST_TSS_BUMPENVMAT10, FloatStageState(3.0f));
    ffp.SetTextureStageState(0, CKRST_TSS_BUMPENVMAT11, FloatStageState(4.0f));
    ffp.SetTextureStageState(0, CKRST_TSS_BUMPENVLSCALE, FloatStageState(5.0f));
    ffp.SetTextureStageState(0, CKRST_TSS_BUMPENVLOFFSET, FloatStageState(6.0f));

    ffp.SetTextureStageState(2, CKRST_TSS_OP, CKRST_TOP_BUMPENVMAPLUMINANCE);
    ffp.SetTextureStageState(2, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(2, CKRST_TSS_BUMPENVMAT00, FloatStageState(7.0f));
    ffp.SetTextureStageState(2, CKRST_TSS_BUMPENVMAT01, FloatStageState(8.0f));
    ffp.SetTextureStageState(2, CKRST_TSS_BUMPENVMAT10, FloatStageState(9.0f));
    ffp.SetTextureStageState(2, CKRST_TSS_BUMPENVMAT11, FloatStageState(10.0f));
    ffp.SetTextureStageState(2, CKRST_TSS_BUMPENVLSCALE, FloatStageState(11.0f));
    ffp.SetTextureStageState(2, CKRST_TSS_BUMPENVLOFFSET, FloatStageState(12.0f));

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKDWORD bumpUniform = ffp.GetShaderCache().GetUniforms().u_bumpEnv;
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator bump =
        context.Encoder.FloatUniforms.find(bumpUniform);

    TestCheck(bump != context.Encoder.FloatUniforms.end(),
              "Bump env draws must upload bump env uniforms");
    TestCheck(context.Encoder.UniformCounts[bumpUniform] == CKFF_MAX_TEXTURE_STAGES * 2,
              "Bump env uniform upload must include every stage");
    TestCheck(bump->second.size() >= CKFF_MAX_TEXTURE_STAGES * 2 * 4,
              "Bump env uniform data must contain every stage slot");
    TestCheck(bump->second[0] == 1.0f && bump->second[1] == 2.0f &&
                  bump->second[2] == 3.0f && bump->second[3] == 4.0f,
              "Stage 0 bump env data must occupy slots 0 and 1");
    TestCheck(bump->second[16] == 7.0f && bump->second[17] == 8.0f &&
                  bump->second[18] == 9.0f && bump->second[19] == 10.0f,
              "Stage 2 bump env data must occupy slots 4 and 5");

    ffp.Shutdown();
}

bool LayoutHasAttrib(const std::vector<CKVertexElementDesc> &elements, CK_VERTEX_ATTRIB attrib) {
    for (size_t i = 0; i < elements.size(); ++i) {
        if (elements[i].Attrib == attrib)
            return true;
    }
    return false;
}

CKDWORD LayoutAttribCount(const std::vector<CKVertexElementDesc> &elements, CK_VERTEX_ATTRIB attrib) {
    for (size_t i = 0; i < elements.size(); ++i) {
        if (elements[i].Attrib == attrib)
            return elements[i].Count;
    }
    return 0;
}

void PositionTTextureTransformDoesNotUploadTextureMatrix() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    const CKDWORD texMatrixUniform = ffp.GetShaderCache().GetUniforms().u_texMatrix;
    context.Encoder.MatrixUniforms.insert(texMatrixUniform);

    VxMatrix texMatrix;
    texMatrix.Identity();
    texMatrix[0][0] = 2.0f;
    ffp.SetTransform(VXMATRIX_TEXTURE0, texMatrix);
    ffp.SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT2);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_VCT, CKFF_VF_POSITIONT | CKFF_VF_TEXCOORD0 | CKFF_VF_COLOR0, 1);

    TestCheck(context.Encoder.FloatUniforms.find(texMatrixUniform) == context.Encoder.FloatUniforms.end(),
              "POSITIONT texture transform flags must not upload or use texture matrices");

    ffp.Shutdown();
}

void TextureTransformCountOneUploadsTextureMatrix() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    const CKDWORD texMatrixUniform = ffp.GetShaderCache().GetUniforms().u_texMatrix;
    context.Encoder.MatrixUniforms.insert(texMatrixUniform);

    VxMatrix texMatrix;
    texMatrix.Identity();
    texMatrix[3][0] = 0.25f;
    ffp.SetTransform(VXMATRIX_TEXTURE0, texMatrix);
    ffp.SetTexcoordComponentCount(0, 1);
    ffp.SetTexture(0, 1);
    ffp.SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT1);

    VxVector positions[3] = {};
    float texcoords[3] = {0.0f, 0.5f, 1.0f};
    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_STAGES0;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    data.TexCoordPtr = texcoords;
    data.TexCoordStride = sizeof(float);

    TestCheck(ffp.DrawPrimitive(&context.Encoder, 1, VX_TRIANGLELIST,
                                nullptr, 0, &data) == TRUE,
              "COUNT1 texture transform draw must submit");
    TestCheck(context.Encoder.UniformCounts[texMatrixUniform] == 1,
              "COUNT1 texture transform must upload its matrix");

    ffp.Shutdown();
}

void LegacyTexcoordComponentCountReadsOnlyXY() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    VxVector positions[3] = {};
    float texcoords[3][4] = {
        {0.25f, 0.50f, 0.75f, 1.25f},
        {0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f}
    };
    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_CL_V | CKRST_DP_STAGES0;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    data.TexCoordPtr = texcoords;
    data.TexCoordStride = sizeof(texcoords[0]);

    ffp.DrawPrimitive(&context.Encoder, 1, VX_TRIANGLELIST, nullptr, 0, &data);

    TestCheck(LayoutAttribCount(context.LastVertexLayoutElements, CKRST_ATTRIB_TEXCOORD0) == 4,
              "Legacy texcoords must still use the canonical float4 transient layout");
    TestCheck(context.Encoder.LastVertexBytes.size() >= 28,
              "Transient vertex bytes must contain position and float4 texcoord");
    if (context.Encoder.LastVertexBytes.size() >= 28) {
        float packed[4] = {};
        memcpy(packed, &context.Encoder.LastVertexBytes[12], sizeof(packed));
        TestCheck(packed[0] == 0.25f && packed[1] == 0.50f &&
                      packed[2] == 0.0f && packed[3] == 0.0f,
                  "Default DrawPrimitive texcoords must read only legacy xy components");
    }

    ffp.Shutdown();
}

void PipelineTexcoordComponentCountPreservesSourceZW() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    VxVector positions[3] = {};
    float texcoords[3][4] = {
        {0.25f, 0.50f, 0.75f, 1.25f},
        {0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f}
    };
    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_CL_V | CKRST_DP_STAGES0;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    data.TexCoordPtr = texcoords;
    data.TexCoordStride = sizeof(texcoords[0]);

    ffp.SetTexcoordComponentCount(0, 4);
    ffp.DrawPrimitive(&context.Encoder, 1, VX_TRIANGLELIST, nullptr, 0, &data);

    TestCheck(LayoutAttribCount(context.LastVertexLayoutElements, CKRST_ATTRIB_TEXCOORD0) == 4,
              "Explicit 4-component texcoords must keep the float4 transient layout");
    TestCheck(context.Encoder.LastVertexBytes.size() >= 28,
              "Transient vertex bytes must contain position and float4 texcoord");
    if (context.Encoder.LastVertexBytes.size() >= 28) {
        float packed[4] = {};
        memcpy(packed, &context.Encoder.LastVertexBytes[12], sizeof(packed));
        TestCheck(packed[0] == 0.25f && packed[1] == 0.50f &&
                      packed[2] == 0.75f && packed[3] == 1.25f,
                  "Pipeline texcoord component state must preserve source z/w components");
    }

    ffp.Shutdown();
}

void InvalidTexcoordComponentCountFallsBackToLegacyXY() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    VxVector positions[3] = {};
    float texcoords[3][4] = {
        {0.25f, 0.50f, 0.75f, 1.25f},
        {0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f}
    };
    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_CL_V | CKRST_DP_STAGES0;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    data.TexCoordPtr = texcoords;
    data.TexCoordStride = sizeof(texcoords[0]);

    ffp.SetTexcoordComponentCount(0, 5);
    ffp.DrawPrimitive(&context.Encoder, 1, VX_TRIANGLELIST, nullptr, 0, &data);

    TestCheck(context.Encoder.LastVertexBytes.size() >= 28,
              "Invalid texcoord count draw must produce transient bytes");
    if (context.Encoder.LastVertexBytes.size() >= 28) {
        float packed[4] = {};
        memcpy(packed, &context.Encoder.LastVertexBytes[12], sizeof(packed));
        TestCheck(packed[0] == 0.25f && packed[1] == 0.50f &&
                      packed[2] == 0.0f && packed[3] == 0.0f,
                  "Invalid texcoord component count must fall back to legacy xy");
    }

    ffp.SetTexcoordComponentCount(0, 0);
    ffp.DrawPrimitive(&context.Encoder, 1, VX_TRIANGLELIST, nullptr, 0, &data);

    TestCheck(context.Encoder.LastVertexBytes.size() >= 28,
              "Zero texcoord count draw must produce transient bytes");
    if (context.Encoder.LastVertexBytes.size() >= 28) {
        float packed[4] = {};
        memcpy(packed, &context.Encoder.LastVertexBytes[12], sizeof(packed));
        TestCheck(packed[0] == 0.25f && packed[1] == 0.50f &&
                      packed[2] == 0.0f && packed[3] == 0.0f,
                  "Zero texcoord component count must fall back to legacy xy");
    }

    ffp.Shutdown();
}

void SimpleDrawPrimitiveDataUsesLegacyTexcoordPath() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    VxVector positions[3] = {};
    float texcoords[3][4] = {
        {0.25f, 0.50f, 0.75f, 1.25f},
        {0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f}
    };
    VxDrawPrimitiveDataSimple data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_CL_V | CKRST_DP_STAGES0;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    data.TexCoordPtr = texcoords;
    data.TexCoordStride = sizeof(texcoords[0]);

    ffp.DrawPrimitive(&context.Encoder, 1, VX_TRIANGLELIST, nullptr, 0,
                      (VxDrawPrimitiveData *)&data);

    TestCheck(context.Encoder.SubmitCount == 1,
              "VxDrawPrimitiveDataSimple cast path must submit normally");
    TestCheck(context.Encoder.LastVertexBytes.size() >= 28,
              "Simple draw data path must produce transient bytes");
    if (context.Encoder.LastVertexBytes.size() >= 28) {
        float packed[4] = {};
        memcpy(packed, &context.Encoder.LastVertexBytes[12], sizeof(packed));
        TestCheck(packed[0] == 0.25f && packed[1] == 0.50f &&
                      packed[2] == 0.0f && packed[3] == 0.0f,
                  "Simple draw data path must not read extended texcoord metadata");
    }

    ffp.Shutdown();
}

void VertexBlendZeroWeightsUploadsMatrixPalette() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_0WEIGHTS);
    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKFF_VF_POSITION | CKFF_VF_BLENDWEIGHT, 1);

    const CKDWORD matrixUniform = ffp.GetShaderCache().GetUniforms().u_ffMatrices;
    const CKDWORD paletteUniform = ffp.GetShaderCache().GetUniforms().u_vertexBlendMatrices;
    TestCheck(context.Encoder.UniformCounts[matrixUniform] == 4,
              "Normal vertex blend must keep base matrices separate from matrix palette");
    TestCheck(context.Encoder.UniformCounts[paletteUniform] == CKFF_VERTEX_BLEND_MATRIX_COUNT,
              "Normal vertex blend must upload the dedicated matrix palette uniform");

    ffp.Shutdown();
}

void VertexBlendUploadsWorldMatrixPaletteForClipPlanes() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    VxMatrix world;
    world.Identity();
    world[0][0] = 2.0f;
    VxMatrix view;
    view.Identity();
    view[0][0] = 3.0f;
    ffp.SetTransform(VXMATRIX_WORLD, world);
    ffp.SetTransform(VXMATRIX_VIEW, view);

    const CKDWORD paletteUniform = ffp.GetShaderCache().GetUniforms().u_vertexBlendMatrices;
    context.Encoder.MatrixUniforms.insert(paletteUniform);

    VxPlane plane;
    plane.m_Normal = VxVector(1.0f, 0.0f, 0.0f);
    plane.m_D = 0.0f;
    ffp.SetUserClipPlane(0, plane);
    ffp.SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, 1u);
    ffp.SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_0WEIGHTS);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKFF_VF_POSITION | CKFF_VF_BLENDWEIGHT, 1);

    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator matrices =
        context.Encoder.FloatUniforms.find(paletteUniform);
    TestCheck(matrices != context.Encoder.FloatUniforms.end() && matrices->second.size() >= 64,
              "Vertex blend with clip planes must upload matrix palette");
    if (matrices != context.Encoder.FloatUniforms.end() && matrices->second.size() >= 64) {
        TestCheck(matrices->second[0] == 2.0f,
                  "Default blend palette slot 0 must use the current world matrix");
    }

    ffp.Shutdown();
}

void VertexBlendUploadsExplicitMatrixPaletteSlot() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    VxMatrix palette;
    palette.Identity();
    palette[1][1] = 4.0f;
    ffp.SetVertexBlendMatrix(1, palette);

    const CKDWORD paletteUniform = ffp.GetShaderCache().GetUniforms().u_vertexBlendMatrices;
    context.Encoder.MatrixUniforms.insert(paletteUniform);

    ffp.SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_1WEIGHTS);
    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKFF_VF_POSITION | CKFF_VF_BLENDWEIGHT, 1);

    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator matrices =
        context.Encoder.FloatUniforms.find(paletteUniform);
    TestCheck(matrices != context.Encoder.FloatUniforms.end() && matrices->second.size() >= 32,
              "Explicit vertex blend palette must upload dedicated palette uniform");
    if (matrices != context.Encoder.FloatUniforms.end() && matrices->second.size() >= 32) {
        TestCheck(matrices->second[16 + 5] == 4.0f,
                  "Explicit vertex blend palette slot 1 must be preserved");
    }

    ffp.Shutdown();
}

void VertexBlendWeightFlagsCreateWeightLayout() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    struct Vertex {
        float Position[3];
        float Weights[2];
    } vertices[3] = {};

    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_WEIGHTS2;
    data.PositionPtr = vertices;
    data.PositionStride = sizeof(Vertex);

    ffp.SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_2WEIGHTS);
    ffp.DrawPrimitive(&context.Encoder, 1, VX_TRIANGLELIST, nullptr, 0, &data);

    TestCheck(LayoutHasAttrib(context.LastVertexLayoutElements, CKRST_ATTRIB_WEIGHT),
              "DP weight flags must create a blend weight vertex attribute");
    TestCheck(!LayoutHasAttrib(context.LastVertexLayoutElements, CKRST_ATTRIB_INDICES),
              "Non-indexed vertex blend must not create blend indices attribute");

    ffp.Shutdown();
}

void IndexedVertexBlendRequiresIndexLayout() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    struct Vertex {
        float Position[3];
        float Weights[2];
        CKDWORD Indices;
    } vertices[3] = {};

    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_WEIGHTS2 | CKRST_DP_MATRIXPAL;
    data.PositionPtr = vertices;
    data.PositionStride = sizeof(Vertex);

    ffp.SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_2WEIGHTS);
    ffp.SetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE, TRUE);
    ffp.DrawPrimitive(&context.Encoder, 1, VX_TRIANGLELIST, nullptr, 0, &data);

    TestCheck(LayoutHasAttrib(context.LastVertexLayoutElements, CKRST_ATTRIB_WEIGHT),
              "Indexed vertex blend must keep blend weight attribute");
    TestCheck(LayoutHasAttrib(context.LastVertexLayoutElements, CKRST_ATTRIB_INDICES),
              "Indexed vertex blend must create blend indices attribute");

    ffp.Shutdown();
}

void IndexedVertexBlendRejectsPaletteOverflow() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    struct Vertex {
        float Position[3];
        float Weights[2];
        CKDWORD Indices;
    } vertices[3] = {};
    vertices[0].Indices = 4;

    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_WEIGHTS2 | CKRST_DP_MATRIXPAL;
    data.PositionPtr = vertices;
    data.PositionStride = sizeof(Vertex);

    ffp.SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_2WEIGHTS);
    ffp.SetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE, TRUE);
    TestCheck(ffp.DrawPrimitive(&context.Encoder, 1, VX_TRIANGLELIST,
                                nullptr, 0, &data) == FALSE,
              "indexed blend must reject an out-of-range matrix index");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_VERTEX_BLEND_PALETTE,
              "indexed blend palette overflow must expose its reject reason");
    TestCheck(context.Encoder.SubmitCount == 0,
              "indexed blend palette overflow must stop before backend submission");

    VxMatrix matrix;
    matrix.Identity();
    TestCheck(ffp.SetVertexBlendMatrix(CKFF_VERTEX_BLEND_MATRIX_COUNT, matrix) == FALSE,
              "setting a matrix outside the supported palette must fail");

    ffp.Shutdown();
}

void PositionTVertexBlendDoesNotUploadMatrixPalette() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_2WEIGHTS);
    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKFF_VF_POSITIONT | CKFF_VF_BLENDWEIGHT, 1);

    const CKDWORD matrixUniform = ffp.GetShaderCache().GetUniforms().u_ffMatrices;
    TestCheck(context.Encoder.FloatUniforms.find(matrixUniform) == context.Encoder.FloatUniforms.end(),
              "POSITIONT vertex blend must not upload 3D matrix palette");

    ffp.Shutdown();
}

void LocalViewerDoesNotSplitShaderWhenLightingDisabled() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_LOCALVIEWER, FALSE);
    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKFF_VF_POSITION | CKFF_VF_NORMAL, 1);
    const void *nonLocalViewerShader = context.LastVertexShaderCode;
    const CKDWORD createdPrograms = context.CreatedProgramCount;

    ffp.SetRenderState(VXRENDERSTATE_LOCALVIEWER, TRUE);
    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKFF_VF_POSITION | CKFF_VF_NORMAL, 1);

    TestCheck(nonLocalViewerShader != nullptr,
              "Lighting-disabled draw must create a vertex shader");
    TestCheck(context.LastVertexShaderCode == nonLocalViewerShader,
              "LOCALVIEWER must not split the vertex shader key when lighting is disabled");
    TestCheck(context.CreatedProgramCount == createdPrograms,
              "LOCALVIEWER must not create a new FFP program when lighting is disabled");

    ffp.Shutdown();
}

void MaterialSourceUsesDeclaredDPColorStreams() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetRenderState(VXRENDERSTATE_LIGHTING, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_COLORVERTEX, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_DIFFUSEFROMVERTEX, TRUE);
    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKFF_VF_POSITION | CKFF_VF_NORMAL | CKFF_VF_COLOR0, 1);

    const CKDWORD uniform = ffp.GetShaderCache().GetUniforms().u_ffDrawParams;
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator it =
        context.Encoder.FloatUniforms.find(uniform);

    TestCheck(it != context.Encoder.FloatUniforms.end(),
              "Material-source draw must upload draw params");
    TestCheck(it != context.Encoder.FloatUniforms.end() &&
                  it->second.size() >= 24 &&
                  it->second[20] == (float)CKFF_MS_MATERIAL,
              "Format COLOR0 alone must not force material diffuse source without DP diffuse data");

    ffp.Shutdown();
}

void RenderPipelineQueuesStencilClearBetweenOpaqueAndTransparent() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKRenderPipeline pipeline;
    pipeline.Init(&context);

    CKRECT viewport;
    viewport.left = 0;
    viewport.top = 0;
    viewport.right = 64;
    viewport.bottom = 64;

    VxMatrix identity;
    Vx3DMatrixIdentity(identity);

    pipeline.BeginFrame(viewport,
                        CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH,
                        0x11223344, 1.0f,
                        identity, identity);

    TestCheck(context.ViewClears.size() >= 2,
              "BeginFrame must configure frame-start and idle stencil-clear views");
    TestCheck(context.ViewClears[0].View == CKRP_VIEW_CLEAR,
              "Frame-start clear must use the clear view");
    TestCheck(context.ViewClears[0].Flags == (CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH),
              "Frame-start clear flags must not be overwritten by later stencil clears");

    bool foundIdleStencilClear = false;
    for (size_t i = 0; i < context.ViewClears.size(); ++i) {
        if (context.ViewClears[i].View == CKRP_VIEW_STENCIL_CLEAR &&
            context.ViewClears[i].Flags == 0) {
            foundIdleStencilClear = true;
            break;
        }
    }
    TestCheck(foundIdleStencilClear,
              "BeginFrame must leave the stencil-clear view idle until requested");

    const CKDWORD touchCountAfterBegin = context.Encoder.TouchCount;
    const CKERROR queued = pipeline.QueueStencilClearBeforeTransparent(viewport, 7);
    TestCheck(queued == CK_OK,
              "Mid-frame stencil clear must queue while the frame is active");
    TestCheck(!context.ViewClears.empty() &&
                  context.ViewClears.back().View == CKRP_VIEW_STENCIL_CLEAR,
              "Mid-frame stencil clear must target the dedicated stencil-clear view");
    TestCheck(!context.ViewClears.empty() &&
                  context.ViewClears.back().Flags == CKRST_CTXCLEAR_STENCIL,
              "Mid-frame stencil clear must clear only stencil");
    TestCheck(!context.ViewClears.empty() &&
                  context.ViewClears.back().Stencil == 7,
              "Mid-frame stencil clear must preserve the requested stencil value");
    TestCheck(context.Encoder.TouchCount == touchCountAfterBegin + 1 &&
                  context.Encoder.LastTouchedView == CKRP_VIEW_STENCIL_CLEAR,
              "Mid-frame stencil clear must touch the dedicated view");
    TestCheck(CKRP_VIEW_OPAQUE3D < CKRP_VIEW_STENCIL_CLEAR &&
                  CKRP_VIEW_STENCIL_CLEAR < CKRP_VIEW_TRANSPARENT,
              "Dedicated stencil-clear view must sort between opaque and transparent views");

    pipeline.EndFrame(CKRST_FRAME_SYNC_IMMEDIATE);
}

void RenderPipelinePropagatesBeginFrameFailures() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKRenderPipeline pipeline;
    pipeline.Init(&context);

    CKRECT viewport = {0, 0, 64, 64};
    VxMatrix identity;
    Vx3DMatrixIdentity(identity);

    context.DeviceStatus = CKERR_INVALIDRENDERCONTEXT;
    TestCheck(pipeline.BeginFrame(viewport, 0, 0, 1.0f,
                                  identity, identity) ==
                  CKERR_INVALIDRENDERCONTEXT &&
                  pipeline.GetEncoder() == nullptr,
              "BeginFrame must propagate a latched device error before configuring views");

    context.DeviceStatus = CK_OK;
    context.FailBeginEncoder = TRUE;
    TestCheck(pipeline.BeginFrame(viewport, 0, 0, 1.0f,
                                  identity, identity) ==
                  CKERR_INVALIDOPERATION &&
                  pipeline.GetEncoder() == nullptr,
              "BeginFrame must report encoder acquisition failure");

    pipeline.Shutdown();
}

} // namespace

int main() {
    TestFramework tests;
    tests.Run("Null rasterizer supports headless FFP",
              &NullRasterizerSupportsHeadlessFFP);
    tests.Run("Render pipeline propagates frame failure",
              &RenderPipelinePropagatesFrameFailure);
    tests.Run("DrawVertexBuffer rejects partial stencil write masks",
              &DrawVertexBufferRejectsPartialStencilWriteMask);
    tests.Run("DrawVertexBuffer submits representable stencil masks",
              &DrawVertexBufferSubmitsRepresentableStencilMasks);
    tests.Run("Unsupported render states reject explicitly",
              &UnsupportedRenderStatesRejectExplicitly);
    tests.Run("Invalid state values reject before backend encoding",
              &InvalidStateValuesRejectBeforeBackendEncoding);
    tests.Run("Unsupported texture-stage states reject explicitly",
              &UnsupportedTextureStageStatesRejectExplicitly);
    tests.Run("Single cube-volume layout uses generic mixed sampler module",
              &SingleCubeVolumeLayoutUsesGenericMixedSamplerModule);
    tests.Run("DrawVertexBuffer propagates encoder failure",
              &DrawVertexBufferPropagatesEncoderFailure);
    tests.Run("DrawVertexBuffer stops before submit after binding failure",
              &DrawVertexBufferStopsBeforeSubmitAfterBindingFailure);
    tests.Run("DrawVertexBuffer stops uniform uploads after failure",
              &DrawVertexBufferStopsUniformUploadsAfterFailure);
    tests.Run("Affine texture coordinates reject draw",
              &AffineTextureCoordinatesRejectDraw);
    tests.Run("Inactive unsupported state does not reject draw",
              &InactiveUnsupportedStateDoesNotRejectDraw);
    tests.Run("Disabled texture stage ignores later unsupported state",
              &DisabledTextureStageIgnoresLaterUnsupportedState);
    tests.Run("Bound unused texture does not require affine interpolation",
              &BoundUnusedTextureDoesNotRequireAffineInterpolation);
    tests.Run("Texture-stage snapshot preserves explicit zero argument",
              &TextureStageSnapshotPreservesExplicitZeroArgument);
    tests.Run("Unknown texture op rejects draw",
              &UnknownTextureOpRejectsDraw);
    tests.Run("Bottom-left cube render target rejects draw",
              &BottomLeftCubeRenderTargetRejectsDraw);
    tests.Run("DrawVertexBuffer uploads alpha precision",
              &DrawVertexBufferUploadsAlphaPrecision);
    tests.Run("DrawVertexBuffer sets flat shade specialization",
              &DrawVertexBufferSetsFlatShadeSpecialization);
    tests.Run("DrawVertexBuffer uploads fog params",
              &DrawVertexBufferUploadsFogParams);
    tests.Run("POSITIONT fog uses POSITIONT shader key",
              &PositionTFogUsesPositionTShaderKey);
    tests.Run("Range fog changes specialization",
              &RangeFogChangesSpecialization);
    tests.Run("Pixel fog overrides vertex fog mode",
              &PixelFogOverridesVertexFogMode);
    tests.Run("DrawVertexBuffer compacts clip plane uniforms",
              &DrawVertexBufferCompactsClipPlaneUniforms);
    tests.Run("DrawVertexBuffer skips clip uniforms when disabled",
              &DrawVertexBufferSkipsClipUniformsWhenDisabled);
    tests.Run("Clip planes use dedicated vertex shader variant",
              &ClipPlanesUseDedicatedVertexShaderVariant);
    tests.Run("RESULTARG TEMP clears only last active stage",
              &ResultArgTempClearsOnlyLastActiveStage);
    tests.Run("MODULATE4X stays in texture stage specialization",
              &Modulate4XStaysInTextureStageSpecialization);
    tests.Run("PREMODULATE stays in texture stage specialization",
              &PremodulateStaysInTextureStageSpecialization);
    tests.Run("Texture arg modifiers stay in specialization",
              &TextureArgModifiersStayInSpecialization);
    tests.Run("Null texture stage preserves specialization",
              &NullTextureStagePreservesSpecialization);
    tests.Run("Stage constant does not create texture dependency",
              &StageConstantDoesNotCreateTextureDependency);
    tests.Run("Cube texture uses cube sampler specialization and binding",
              &CubeTextureUsesCubeSamplerSpecializationAndBinding);
    tests.Run("Volume texture modulate cache miss uses runtime shader",
              &VolumeTextureModulateCacheMissUsesUberShader);
    tests.Run("Volume texture stage seven binds volume sampler",
              &VolumeTextureStageSevenBindsVolumeSampler);
    tests.Run("Volume and cube cache miss uses static sampler layout fallback",
              &VolumeAndCubeCacheMissUsesStaticSamplerLayoutFallback);
    tests.Run("Arbitrary single volume-cube layout uses generic fallback",
              &ArbitrarySingleVolumeCubeLayoutUsesGenericFallback);
    tests.Run("Multiple volume textures bind each volume sampler",
              &MultipleVolumeTexturesBindEachVolumeSampler);
    tests.Run("Depth texture compare func uploads sampler and specialization",
              &DepthTextureCompareFuncUploadsSamplerAndSpecialization);
    tests.Run("Filtered depth texture compare rejects draw",
              &FilteredDepthTextureCompareRejectsDraw);
    tests.Run("Border color uses stable bgfx palette slots",
              &BorderColorUsesStableBgfxPaletteSlots);
    tests.Run("Border palette overflow rejects draw",
              &BorderPaletteOverflowRejectsDraw);
    tests.Run("Border palette slots are reused across frames",
              &BorderPaletteSlotsAreReusedAcrossFrames);
    tests.Run("Unused border textures do not consume palette slots",
              &UnusedBorderTexturesDoNotConsumePaletteSlots);
    tests.Run("Untextured stage keeps runtime stage params",
              &UntexturedStageKeepsRuntimeStageParams);
    tests.Run("PREMODULATE implicit texture dependency binds next stage",
              &PremodulateImplicitTextureDependencyBindsNextStage);
    tests.Run("Uber shader program modules are shared across state bindings",
              &UberShaderProgramModulesAreSharedAcrossStateBindings);
    tests.Run("Legacy STAGEBLEND zero terminates stale multitexture state",
              &LegacyStageBlendZeroTerminatesStaleMultitextureState);
    tests.Run("Legacy TEXTUREMAPBLEND clears explicit stage ops",
              &LegacyTextureMapBlendClearsExplicitStageOps);
    tests.Run("Point sprite DrawPrimitive expands to triangle list",
              &PointSpriteDrawPrimitiveExpandsToTriangleList);
    tests.Run("Point sprite uses per-vertex point size",
              &PointSpriteUsesPerVertexPointSize);
    tests.Run("POSITIONT texture transform does not upload texture matrix",
              &PositionTTextureTransformDoesNotUploadTextureMatrix);
    tests.Run("COUNT1 texture transform uploads texture matrix",
              &TextureTransformCountOneUploadsTextureMatrix);
    tests.Run("Legacy texcoord component count reads only xy",
              &LegacyTexcoordComponentCountReadsOnlyXY);
    tests.Run("Pipeline texcoord component count preserves source zw",
              &PipelineTexcoordComponentCountPreservesSourceZW);
    tests.Run("Invalid texcoord component count falls back to legacy xy",
              &InvalidTexcoordComponentCountFallsBackToLegacyXY);
    tests.Run("Simple DrawPrimitive data uses legacy texcoord path",
              &SimpleDrawPrimitiveDataUsesLegacyTexcoordPath);
    tests.Run("Projected sampler stages zero to three enter specialization mask",
              &ProjectedSamplerStagesZeroToThreeEnterSpecializationMask);
    tests.Run("Projected sampler stages four to seven stay in runtime stage params",
              &ProjectedSamplerStagesFourToSevenStayInRuntimeStageParams);
    tests.Run("Draw uploads per-stage bump env uniforms",
              &DrawUploadsPerStageBumpEnvUniforms);
    tests.Run("Vertex blend zero weights uploads matrix palette",
              &VertexBlendZeroWeightsUploadsMatrixPalette);
    tests.Run("Vertex blend uploads world matrix palette for clip planes",
              &VertexBlendUploadsWorldMatrixPaletteForClipPlanes);
    tests.Run("Vertex blend uploads explicit matrix palette slot",
              &VertexBlendUploadsExplicitMatrixPaletteSlot);
    tests.Run("Vertex blend weight flags create weight layout",
              &VertexBlendWeightFlagsCreateWeightLayout);
    tests.Run("Indexed vertex blend requires index layout",
              &IndexedVertexBlendRequiresIndexLayout);
    tests.Run("Indexed vertex blend rejects palette overflow",
              &IndexedVertexBlendRejectsPaletteOverflow);
    tests.Run("POSITIONT vertex blend does not upload matrix palette",
              &PositionTVertexBlendDoesNotUploadMatrixPalette);
    tests.Run("LOCALVIEWER does not split shader when lighting disabled",
              &LocalViewerDoesNotSplitShaderWhenLightingDisabled);
    tests.Run("Material source uses declared DP color streams",
              &MaterialSourceUsesDeclaredDPColorStreams);
    tests.Run("Render pipeline queues stencil clear between opaque and transparent",
              &RenderPipelineQueuesStencilClearBetweenOpaqueAndTransparent);
    tests.Run("Render pipeline propagates begin-frame failures",
              &RenderPipelinePropagatesBeginFrameFailures);
    return tests.ExitCode();
}
