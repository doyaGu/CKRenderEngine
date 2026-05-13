#include "CKFixedFunctionPipeline.h"
#include "CKFFSpecializationInfo.h"
#include "CKFFUniformState.h"
#include "FFPDiagnosticHarness.h"
#include "TestTriangleMultiset.h"

#include <cmath>
#include <cstring>

namespace {

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

void DrawVertexBufferSubmitsStencilRefAndMasks() {
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

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(context.Encoder.SubmitCount == 1,
              "FFP test draw must submit once");
    TestCheck(context.Encoder.StencilRefSetCount == 1,
              "FFP draw must submit stencil ref");
    TestCheck(context.Encoder.StencilMaskSetCount == 1,
              "FFP draw must submit stencil masks");
    TestCheck(context.Encoder.LastStencilRef == 0x12,
              "FFP draw must forward stencil ref");
    TestCheck(context.Encoder.LastStencilReadMask == 0xF0,
              "FFP draw must forward stencil read mask");
    TestCheck(context.Encoder.LastStencilWriteMask == 0x0F,
              "FFP draw must forward stencil write mask");

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

    CKFFSpecializationInfo gouraudSpec;
    TestCheck(!context.LastProgramSpecializationDwords.empty(),
              "Gouraud draw must submit specialization data");
    if (!context.LastProgramSpecializationDwords.empty())
        gouraudSpec.SetDwords(&context.LastProgramSpecializationDwords[0],
                              (CKDWORD)context.LastProgramSpecializationDwords.size());
    TestCheck(gouraudSpec.Get(CKFF_SPEC_FLAT_SHADE) == 0,
              "Gouraud shade mode must not set flat shade specialization");

    ffp.SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_FLAT);
    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo flatSpec;
    TestCheck(!context.LastProgramSpecializationDwords.empty(),
              "Flat draw must submit specialization data");
    if (!context.LastProgramSpecializationDwords.empty())
        flatSpec.SetDwords(&context.LastProgramSpecializationDwords[0],
                           (CKDWORD)context.LastProgramSpecializationDwords.size());
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

    CKFFSpecializationInfo specNoRange;
    TestCheck(!context.LastProgramSpecializationDwords.empty(),
              "Non-range fog draw must submit specialization data");
    if (!context.LastProgramSpecializationDwords.empty())
        specNoRange.SetDwords(&context.LastProgramSpecializationDwords[0],
                              (CKDWORD)context.LastProgramSpecializationDwords.size());
    TestCheck(specNoRange.Get(CKFF_SPEC_RANGE_FOG) == 0,
              "Range fog disabled must clear range fog specialization");

    ffp.SetRenderState(VXRENDERSTATE_RANGEFOGENABLE, TRUE);
    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo specRange;
    TestCheck(!context.LastProgramSpecializationDwords.empty(),
              "Range fog draw must submit specialization data");
    if (!context.LastProgramSpecializationDwords.empty())
        specRange.SetDwords(&context.LastProgramSpecializationDwords[0],
                            (CKDWORD)context.LastProgramSpecializationDwords.size());
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

    CKFFSpecializationInfo spec;
    TestCheck(!context.LastProgramSpecializationDwords.empty(),
              "Pixel fog draw must submit specialization data");
    if (!context.LastProgramSpecializationDwords.empty())
        spec.SetDwords(&context.LastProgramSpecializationDwords[0],
                       (CKDWORD)context.LastProgramSpecializationDwords.size());

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

    CKFFSpecializationInfo spec;
    TestCheck(!context.LastProgramSpecializationDwords.empty(),
              "RESULTARG draw must submit specialization data");
    if (!context.LastProgramSpecializationDwords.empty())
        spec.SetDwords(&context.LastProgramSpecializationDwords[0],
                       (CKDWORD)context.LastProgramSpecializationDwords.size());

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

    CKFFSpecializationInfo spec;
    TestCheck(!context.LastProgramSpecializationDwords.empty(),
              "MODULATE4X draw must submit specialization data");
    if (!context.LastProgramSpecializationDwords.empty())
        spec.SetDwords(&context.LastProgramSpecializationDwords[0],
                       (CKDWORD)context.LastProgramSpecializationDwords.size());

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

    CKFFSpecializationInfo spec;
    TestCheck(!context.LastProgramSpecializationDwords.empty(),
              "PREMODULATE draw must submit specialization data");
    if (!context.LastProgramSpecializationDwords.empty())
        spec.SetDwords(&context.LastProgramSpecializationDwords[0],
                       (CKDWORD)context.LastProgramSpecializationDwords.size());

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

    CKFFSpecializationInfo spec;
    TestCheck(!context.LastProgramSpecializationDwords.empty(),
              "Texture arg modifier draw must upload specialization dwords");
    if (!context.LastProgramSpecializationDwords.empty())
        spec.SetDwords(&context.LastProgramSpecializationDwords[0],
                       (CKDWORD)context.LastProgramSpecializationDwords.size());

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

    CKFFSpecializationInfo spec;
    TestCheck(!context.LastProgramSpecializationDwords.empty(),
              "Null texture draw must submit specialization data");
    if (!context.LastProgramSpecializationDwords.empty())
        spec.SetDwords(&context.LastProgramSpecializationDwords[0],
                       (CKDWORD)context.LastProgramSpecializationDwords.size());

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

    CKFFSpecializationInfo spec;
    TestCheck(!context.LastProgramSpecializationDwords.empty(),
              "Stage constant draw must submit specialization data");
    if (!context.LastProgramSpecializationDwords.empty())
        spec.SetDwords(&context.LastProgramSpecializationDwords[0],
                       (CKDWORD)context.LastProgramSpecializationDwords.size());

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

    CKFFSpecializationInfo spec;
    TestCheck(!context.LastProgramSpecializationDwords.empty(),
              "Cubemap draw must submit specialization data");
    if (!context.LastProgramSpecializationDwords.empty())
        spec.SetDwords(&context.LastProgramSpecializationDwords[0],
                       (CKDWORD)context.LastProgramSpecializationDwords.size());
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

void VolumeTextureModulateCacheMissDoesNotUseGenericNullSampler() {
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

    TestCheck(context.Encoder.SubmitCount == 0,
              "Volume texture cache miss must not fall back to generic null texture sampling");
    TestCheck(context.Encoder.TextureBindCount == 0,
              "Volume texture cache miss must not bind a texture for a skipped draw");

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

    CKFFSpecializationInfo noCompareSpec;
    TestCheck(!context.LastProgramSpecializationDwords.empty(),
              "Depth draw must submit specialization data");
    if (!context.LastProgramSpecializationDwords.empty())
        noCompareSpec.SetDwords(&context.LastProgramSpecializationDwords[0],
                                (CKDWORD)context.LastProgramSpecializationDwords.size());
    TestCheck((noCompareSpec.Get(CKFF_SPEC_SAMPLER_TYPE_MASK) & 0x3u) == CKFF_SAMPLER_DEPTH,
              "Depth texture must mark stage 0 as depth sampler");
    TestCheck((noCompareSpec.Get(CKFF_SPEC_SAMPLER_COMPARE_FUNC_MASK) & 0xFu) == CKRST_COMPARE_NONE,
              "Depth texture without compare func must keep compare mask empty");
    TestCheck(context.Encoder.LastTextureSampler.CompareFunc == CKRST_COMPARE_NONE,
              "Depth texture without compare func must bind a non-compare sampler");

    ffp.SetTextureStageState(0, CKRST_TSS_COMPAREFUNC, CKRST_COMPARE_LEQUAL);
    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo compareSpec;
    TestCheck(!context.LastProgramSpecializationDwords.empty(),
              "Depth compare draw must submit specialization data");
    if (!context.LastProgramSpecializationDwords.empty())
        compareSpec.SetDwords(&context.LastProgramSpecializationDwords[0],
                              (CKDWORD)context.LastProgramSpecializationDwords.size());
    TestCheck((compareSpec.Get(CKFF_SPEC_SAMPLER_COMPARE_FUNC_MASK) & 0xFu) == CKRST_COMPARE_LEQUAL,
              "Depth compare func must enter specialization mask");
    TestCheck(context.Encoder.LastTextureSampler.CompareFunc == CKRST_COMPARE_NONE,
              "Depth compare func must stay shader-evaluated and bind a non-compare sampler");

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
    ffp.SetTextureStageState(2, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(2, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_PROJECTED);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo spec;
    TestCheck(!context.LastProgramSpecializationDwords.empty(),
              "Projected stage 2 draw must submit specialization data");
    if (!context.LastProgramSpecializationDwords.empty())
        spec.SetDwords(&context.LastProgramSpecializationDwords[0],
                       (CKDWORD)context.LastProgramSpecializationDwords.size());

    TestCheck((spec.Get(CKFF_SPEC_PROJECTED_SAMPLER_MASK) & (1u << 2)) != 0,
              "Stage 2 projected sampler must be encoded in the specialization mask");

    ffp.Shutdown();
}

void ProjectedSamplerStagesFourToSevenStayInRuntimeStageParams() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&context);

    ffp.SetTextureStageState(4, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(4, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(4, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_PROJECTED);

    ffp.DrawVertexBuffer(&context.Encoder, 1, VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo spec;
    TestCheck(!context.LastProgramSpecializationDwords.empty(),
              "Projected stage 4 draw must submit specialization data");
    if (!context.LastProgramSpecializationDwords.empty())
        spec.SetDwords(&context.LastProgramSpecializationDwords[0],
                       (CKDWORD)context.LastProgramSpecializationDwords.size());

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

} // namespace

int main() {
    TestFramework tests;
    tests.Run("DrawVertexBuffer submits stencil ref and masks",
              &DrawVertexBufferSubmitsStencilRefAndMasks);
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
    tests.Run("Volume texture modulate cache miss does not use generic null sampler",
              &VolumeTextureModulateCacheMissDoesNotUseGenericNullSampler);
    tests.Run("Depth texture compare func uploads sampler and specialization",
              &DepthTextureCompareFuncUploadsSamplerAndSpecialization);
    tests.Run("Point sprite DrawPrimitive expands to triangle list",
              &PointSpriteDrawPrimitiveExpandsToTriangleList);
    tests.Run("Point sprite uses per-vertex point size",
              &PointSpriteUsesPerVertexPointSize);
    tests.Run("POSITIONT texture transform does not upload texture matrix",
              &PositionTTextureTransformDoesNotUploadTextureMatrix);
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
    tests.Run("POSITIONT vertex blend does not upload matrix palette",
              &PositionTVertexBlendDoesNotUploadMatrixPalette);
    tests.Run("LOCALVIEWER does not split shader when lighting disabled",
              &LocalViewerDoesNotSplitShaderWhenLightingDisabled);
    tests.Run("Material source uses declared DP color streams",
              &MaterialSourceUsesDeclaredDPColorStreams);
    return tests.ExitCode();
}
