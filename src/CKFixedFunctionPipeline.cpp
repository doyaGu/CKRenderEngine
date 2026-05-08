#include "CKFixedFunctionPipeline.h"
#include "CKRasterizer.h"

#include <cmath>
#include <cstring>

static void MulMatrix(VxMatrix &dst, const VxMatrix &a, const VxMatrix &b) {
    VxMatrix tmp;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            tmp[r][c] = a[r][0] * b[0][c] +
                        a[r][1] * b[1][c] +
                        a[r][2] * b[2][c] +
                        a[r][3] * b[3][c];
        }
    }
    dst = tmp;
}

static void TransposeMatrix(VxMatrix &dst, const VxMatrix &src) {
    VxMatrix tmp;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            tmp[r][c] = src[c][r];
        }
    }
    dst = tmp;
}

static CKDWORD BaseTextureArg(CKDWORD arg) {
    return arg & ~(CKRST_TA_COMPLEMENT | CKRST_TA_ALPHAREPLICATE);
}

static float SaturateFloat(float value) {
    if (value < 0.0f)
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

static VxColor SaturateColor(const VxColor &value) {
    VxColor result = value;
    result.r = SaturateFloat(result.r);
    result.g = SaturateFloat(result.g);
    result.b = SaturateFloat(result.b);
    result.a = SaturateFloat(result.a);
    return result;
}

static VxColor ColorSplat(float value) {
    return VxColor(value, value, value, value);
}

static VxColor ColorMad(const VxColor &a, const VxColor &b, const VxColor &c) {
    return VxColor(
        a.r * b.r + c.r,
        a.g * b.g + c.g,
        a.b * b.b + c.b,
        a.a * b.a + c.a);
}

static VxColor ColorComplement(const VxColor &value) {
    return VxColor(1.0f - value.r, 1.0f - value.g, 1.0f - value.b, 1.0f - value.a);
}

static VxColor ColorMix(const VxColor &a, const VxColor &b, const VxColor &factor) {
    return VxColor(
        a.r * (1.0f - factor.r) + b.r * factor.r,
        a.g * (1.0f - factor.g) + b.g * factor.g,
        a.b * (1.0f - factor.b) + b.b * factor.b,
        a.a * (1.0f - factor.a) + b.a * factor.a);
}

static float ReadFloatRenderState(const CKDrawStateCache &cache, VXRENDERSTATETYPE state, float fallback) {
    CKDWORD bits = cache.GetRenderState(state);
    if (bits == 0)
        return fallback;

    float value = fallback;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static CK_ADDRESS_MODE TranslateAddressMode(CKDWORD mode) {
    switch (mode) {
    case VXTEXTURE_ADDRESSMIRROR:
        return CKRST_ADDRESS_MIRROR;
    case VXTEXTURE_ADDRESSCLAMP:
        return CKRST_ADDRESS_CLAMP;
    case VXTEXTURE_ADDRESSBORDER:
        return CKRST_ADDRESS_BORDER;
    case VXTEXTURE_ADDRESSMIRRORONCE:
        // MIRRORONCE is not directly expressible in the current bgfx sampler contract.
        return CKRST_ADDRESS_CLAMP;
    case VXTEXTURE_ADDRESSWRAP:
    default:
        return CKRST_ADDRESS_WRAP;
    }
}

CKFFTextureStageOps CKFFLegacyTextureBlendToStageOps(CKDWORD blend) {
    CKFFTextureStageOps ops;
    ops.ColorOp = CKRST_TOP_MODULATE;
    ops.ColorArg0 = CKRST_TA_CURRENT;
    ops.ColorArg1 = CKRST_TA_TEXTURE;
    ops.ColorArg2 = CKRST_TA_CURRENT;
    ops.AlphaOp = CKRST_TOP_MODULATE;
    ops.AlphaArg0 = CKRST_TA_CURRENT;
    ops.AlphaArg1 = CKRST_TA_TEXTURE;
    ops.AlphaArg2 = CKRST_TA_CURRENT;
    ops.ResultArg = CKRST_TA_CURRENT;

    switch (blend & VXTEXTUREBLEND_MASK) {
    case VXTEXTUREBLEND_DECAL:
    case VXTEXTUREBLEND_COPY:
        ops.ColorOp = CKRST_TOP_SELECTARG1;
        ops.AlphaOp = CKRST_TOP_SELECTARG1;
        break;
    case VXTEXTUREBLEND_DECALALPHA:
    case VXTEXTUREBLEND_DECALMASK:
        ops.ColorOp = CKRST_TOP_BLENDTEXTUREALPHA;
        ops.AlphaOp = CKRST_TOP_SELECTARG1;
        ops.AlphaArg1 = CKRST_TA_DIFFUSE;
        break;
    case VXTEXTUREBLEND_MODULATEALPHA:
    case VXTEXTUREBLEND_MODULATEMASK:
        ops.AlphaOp = CKRST_TOP_MODULATE;
        break;
    case VXTEXTUREBLEND_ADD:
        ops.ColorOp = CKRST_TOP_ADD;
        ops.AlphaOp = CKRST_TOP_SELECTARG1;
        ops.AlphaArg1 = CKRST_TA_CURRENT;
        break;
    case VXTEXTUREBLEND_DOTPRODUCT3:
        ops.ColorOp = CKRST_TOP_DOTPRODUCT3;
        ops.ColorArg2 = CKRST_TA_TFACTOR;
        ops.AlphaOp = CKRST_TOP_SELECTARG1;
        ops.AlphaArg1 = CKRST_TA_CURRENT;
        break;
    case VXTEXTUREBLEND_MODULATE:
    default:
        break;
    }

    return ops;
}

VxColor CKFFEvaluateTextureOp(CKDWORD op,
                              const VxColor &arg0,
                              const VxColor &arg1,
                              const VxColor &arg2,
                              const VxColor &current,
                              const VxColor &diffuse,
                              const VxColor &textureColor,
                              const VxColor &textureFactor) {
    switch (op) {
    case CKRST_TOP_DISABLE:
        return current;
    case CKRST_TOP_SELECTARG1:
        return arg1;
    case CKRST_TOP_SELECTARG2:
        return arg2;
    case CKRST_TOP_MODULATE:
        return arg1 * arg2;
    case CKRST_TOP_MODULATE2X:
        return SaturateColor(arg1 * arg2 * 2.0f);
    case CKRST_TOP_MODULATE4X:
        return SaturateColor(arg1 * arg2 * 4.0f);
    case CKRST_TOP_ADD:
        return SaturateColor(arg1 + arg2);
    case CKRST_TOP_ADDSIGNED:
        return SaturateColor(arg1 + (arg2 - VxColor(0.5f, 0.5f, 0.5f, 0.5f)));
    case CKRST_TOP_ADDSIGNED2X:
        return SaturateColor((arg1 + (arg2 - VxColor(0.5f, 0.5f, 0.5f, 0.5f))) * 2.0f);
    case CKRST_TOP_SUBTRACT:
        return SaturateColor(arg1 - arg2);
    case CKRST_TOP_ADDSMOOTH:
        return SaturateColor(ColorMad(ColorComplement(arg1), arg2, arg1));
    case CKRST_TOP_BLENDDIFFUSEALPHA:
        return ColorMix(arg2, arg1, ColorSplat(diffuse.a));
    case CKRST_TOP_BLENDTEXTUREALPHA:
        return ColorMix(arg2, arg1, ColorSplat(textureColor.a));
    case CKRST_TOP_BLENDFACTORALPHA:
        return ColorMix(arg2, arg1, ColorSplat(textureFactor.a));
    case CKRST_TOP_BLENDTEXTUREALPHAPM:
        return SaturateColor(ColorMad(arg2, ColorSplat(1.0f - textureColor.a), arg1));
    case CKRST_TOP_BLENDCURRENTALPHA:
        return ColorMix(arg2, arg1, ColorSplat(current.a));
    case CKRST_TOP_MODULATEALPHA_ADDCOLOR:
        return SaturateColor(ColorMad(ColorSplat(arg1.a), arg2, arg1));
    case CKRST_TOP_MODULATECOLOR_ADDALPHA:
        return SaturateColor(ColorMad(arg1, arg2, ColorSplat(arg1.a)));
    case CKRST_TOP_MODULATEINVALPHA_ADDCOLOR:
        return SaturateColor(ColorMad(ColorSplat(1.0f - arg1.a), arg2, arg1));
    case CKRST_TOP_MODULATEINVCOLOR_ADDALPHA:
        return SaturateColor(ColorMad(ColorComplement(arg1), arg2, ColorSplat(arg1.a)));
    case CKRST_TOP_BUMPENVMAP:
    case CKRST_TOP_BUMPENVMAPLUMINANCE:
        return current;
    case CKRST_TOP_DOTPRODUCT3: {
        const float dot =
            (arg1.r - 0.5f) * (arg2.r - 0.5f) +
            (arg1.g - 0.5f) * (arg2.g - 0.5f) +
            (arg1.b - 0.5f) * (arg2.b - 0.5f);
        return ColorSplat(SaturateFloat(dot * 4.0f));
    }
    case CKRST_TOP_MULTIPLYADD:
        return SaturateColor(ColorMad(arg1, arg2, arg0));
    case CKRST_TOP_LERP:
        return SaturateColor(ColorMix(arg2, arg1, arg0));
    default:
        return arg1 * arg2;
    }
}

CKFFCoverage CKFFClassifyTextureOpCoverage(CKDWORD op) {
    switch (op) {
    case CKRST_TOP_DISABLE:
    case CKRST_TOP_SELECTARG1:
    case CKRST_TOP_SELECTARG2:
    case CKRST_TOP_MODULATE:
    case CKRST_TOP_MODULATE2X:
    case CKRST_TOP_MODULATE4X:
    case CKRST_TOP_ADD:
    case CKRST_TOP_ADDSIGNED:
    case CKRST_TOP_ADDSIGNED2X:
    case CKRST_TOP_SUBTRACT:
    case CKRST_TOP_ADDSMOOTH:
    case CKRST_TOP_BLENDDIFFUSEALPHA:
    case CKRST_TOP_BLENDTEXTUREALPHA:
    case CKRST_TOP_BLENDFACTORALPHA:
    case CKRST_TOP_BLENDTEXTUREALPHAPM:
    case CKRST_TOP_BLENDCURRENTALPHA:
    case CKRST_TOP_MODULATEALPHA_ADDCOLOR:
    case CKRST_TOP_MODULATECOLOR_ADDALPHA:
    case CKRST_TOP_MODULATEINVALPHA_ADDCOLOR:
    case CKRST_TOP_MODULATEINVCOLOR_ADDALPHA:
    case CKRST_TOP_DOTPRODUCT3:
    case CKRST_TOP_MULTIPLYADD:
    case CKRST_TOP_LERP:
        return CKFF_COVERAGE_EXACT;
    case CKRST_TOP_BUMPENVMAP:
    case CKRST_TOP_BUMPENVMAPLUMINANCE:
        return CKFF_COVERAGE_APPROXIMATE;
    case CKRST_TOP_PREMODULATE:
        return CKFF_COVERAGE_FALLBACK;
    default:
        return CKFF_COVERAGE_UNTESTED;
    }
}

CKFFCoverage CKFFClassifyShaderSemanticCoverage(CKFFShaderSemantic semantic) {
    switch (semantic) {
    case CKFF_SHADER_SEMANTIC_ARG_DIFFUSE:
    case CKFF_SHADER_SEMANTIC_ARG_CURRENT:
    case CKFF_SHADER_SEMANTIC_ARG_TEXTURE:
    case CKFF_SHADER_SEMANTIC_ARG_TFACTOR:
    case CKFF_SHADER_SEMANTIC_ARG_SPECULAR:
    case CKFF_SHADER_SEMANTIC_ARG_TEMP:
    case CKFF_SHADER_SEMANTIC_ARG_COMPLEMENT:
    case CKFF_SHADER_SEMANTIC_ARG_ALPHAREPLICATE:
    case CKFF_SHADER_SEMANTIC_RESULT_CURRENT:
    case CKFF_SHADER_SEMANTIC_RESULT_TEMP:
    case CKFF_SHADER_SEMANTIC_PROJECTED_SAMPLING:
    case CKFF_SHADER_SEMANTIC_TEXGEN_CAMERASPACENORMAL:
    case CKFF_SHADER_SEMANTIC_TEXGEN_CAMERASPACEPOSITION:
    case CKFF_SHADER_SEMANTIC_TEXGEN_CAMERASPACEREFLECTION:
    case CKFF_SHADER_SEMANTIC_TEXGEN_SPHEREMAP:
        return CKFF_COVERAGE_EXACT;
    case CKFF_SHADER_SEMANTIC_BUMPENVMAP:
    case CKFF_SHADER_SEMANTIC_BUMPENVMAPLUMINANCE:
        return CKFF_COVERAGE_APPROXIMATE;
    default:
        return CKFF_COVERAGE_UNTESTED;
    }
}

CKDWORD CKFFLegacyTextureBlendToColorOp(CKDWORD blend) {
    return CKFFLegacyTextureBlendToStageOps(blend).ColorOp;
}

CKDWORD CKFFLegacyTextureBlendToAlphaOp(CKDWORD blend) {
    return CKFFLegacyTextureBlendToStageOps(blend).AlphaOp;
}

CKBOOL CKFFStageBlendToTextureOps(CKDWORD stageBlend,
                                  CKDWORD &colorOp, CKDWORD &colorArg1, CKDWORD &colorArg2,
                                  CKDWORD &alphaOp, CKDWORD &alphaArg1, CKDWORD &alphaArg2) {
    const CKDWORD src = (stageBlend >> 4) & 0xF;
    const CKDWORD dst = stageBlend & 0xF;

    if (!((src == VXBLEND_ZERO && dst == VXBLEND_SRCCOLOR) ||
          (src == VXBLEND_DESTCOLOR && dst == VXBLEND_ZERO))) {
        return FALSE;
    }

    colorOp = CKRST_TOP_MODULATE;
    colorArg1 = CKRST_TA_TEXTURE;
    colorArg2 = CKRST_TA_CURRENT;
    alphaOp = CKRST_TOP_SELECTARG2;
    alphaArg1 = CKRST_TA_TEXTURE;
    alphaArg2 = CKRST_TA_CURRENT;
    return TRUE;
}

static float StageStateAsFloat(CKDWORD value) {
    float result = 0.0f;
    memcpy(&result, &value, sizeof(float));
    return result;
}

void CKFFPackBumpEnvUniform(const CKDWORD *stageState, float outBumpEnv[2][4]) {
    if (!outBumpEnv)
        return;

    memset(outBumpEnv, 0, sizeof(float) * 8);
    if (!stageState)
        return;

    outBumpEnv[0][0] = StageStateAsFloat(stageState[CKRST_TSS_BUMPENVMAT00]);
    outBumpEnv[0][1] = StageStateAsFloat(stageState[CKRST_TSS_BUMPENVMAT01]);
    outBumpEnv[0][2] = StageStateAsFloat(stageState[CKRST_TSS_BUMPENVMAT10]);
    outBumpEnv[0][3] = StageStateAsFloat(stageState[CKRST_TSS_BUMPENVMAT11]);
    outBumpEnv[1][0] = StageStateAsFloat(stageState[CKRST_TSS_BUMPENVLSCALE]);
    outBumpEnv[1][1] = StageStateAsFloat(stageState[CKRST_TSS_BUMPENVLOFFSET]);
}

static CKDWORD GetStageColorOp(const CKDWORD *stage, bool stageActive, bool hasTexture) {
    if (!stageActive)
        return CKRST_TOP_DISABLE;

    CKDWORD op = stage[CKRST_TSS_OP];
    if (op != 0)
        return op;
    if (!hasTexture)
        return CKRST_TOP_DISABLE;
    return CKFFLegacyTextureBlendToStageOps(stage[CKRST_TSS_TEXTUREMAPBLEND]).ColorOp;
}

static CKDWORD GetStageAlphaOp(const CKDWORD *stage, bool stageActive, bool hasTexture) {
    if (!stageActive)
        return CKRST_TOP_DISABLE;

    CKDWORD op = stage[CKRST_TSS_AOP];
    if (op != 0)
        return op;
    return hasTexture ? CKFFLegacyTextureBlendToStageOps(stage[CKRST_TSS_TEXTUREMAPBLEND]).AlphaOp : CKRST_TOP_DISABLE;
}

static CKDWORD GetStageColorArg1(const CKDWORD *stage, bool hasTexture) {
    CKDWORD arg = stage[CKRST_TSS_ARG1];
    if (arg != 0)
        return arg;
    return hasTexture ? CKFFLegacyTextureBlendToStageOps(stage[CKRST_TSS_TEXTUREMAPBLEND]).ColorArg1 : CKRST_TA_DIFFUSE;
}

static CKDWORD GetStageColorArg2(const CKDWORD *stage) {
    CKDWORD arg = stage[CKRST_TSS_ARG2];
    return arg != 0 ? arg : CKFFLegacyTextureBlendToStageOps(stage[CKRST_TSS_TEXTUREMAPBLEND]).ColorArg2;
}

static CKDWORD GetStageColorArg0(const CKDWORD *stage) {
    CKDWORD arg = stage[CKRST_TSS_COLORARG0];
    return arg != 0 ? arg : CKFFLegacyTextureBlendToStageOps(stage[CKRST_TSS_TEXTUREMAPBLEND]).ColorArg0;
}

static CKDWORD GetStageAlphaArg1(const CKDWORD *stage, bool hasTexture) {
    CKDWORD arg = stage[CKRST_TSS_AARG1];
    if (arg != 0)
        return arg;
    return hasTexture ? CKFFLegacyTextureBlendToStageOps(stage[CKRST_TSS_TEXTUREMAPBLEND]).AlphaArg1 : CKRST_TA_DIFFUSE;
}

static CKDWORD GetStageAlphaArg2(const CKDWORD *stage) {
    CKDWORD arg = stage[CKRST_TSS_AARG2];
    return arg != 0 ? arg : CKFFLegacyTextureBlendToStageOps(stage[CKRST_TSS_TEXTUREMAPBLEND]).AlphaArg2;
}

static CKDWORD GetStageAlphaArg0(const CKDWORD *stage) {
    CKDWORD arg = stage[CKRST_TSS_ALPHAARG0];
    return arg != 0 ? arg : CKFFLegacyTextureBlendToStageOps(stage[CKRST_TSS_TEXTUREMAPBLEND]).AlphaArg0;
}

static CKDWORD GetStageResultArg(const CKDWORD *stage) {
    CKDWORD arg = stage[CKRST_TSS_RESULTARG0];
    return arg != 0 ? arg : CKFFLegacyTextureBlendToStageOps(stage[CKRST_TSS_TEXTUREMAPBLEND]).ResultArg;
}

int CKFFActiveTextureCountFromDPFlags(CKDWORD dpFlags) {
    CKDWORD mask = dpFlags & CKRST_DP_STAGESMASK;
    int count = 0;
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        if (mask & CKRST_DP_STAGE(stage))
            count = stage + 1;
    }
    return count;
}

CKDWORD CKFFPackTexcoordIndex(CKDWORD index, CKDWORD generation) {
    return (index & 0xFFFFu) | ((generation & 0xFFFFu) << 16);
}

CKDWORD CKFFTexcoordIndex(CKDWORD packed) {
    return packed & 0xFFFFu;
}

CKDWORD CKFFTexcoordGeneration(CKDWORD packed) {
    return (packed >> 16) & 0xFFFFu;
}

static CKDWORD ResolveMaterialSource(CKBOOL lighting, CKBOOL colorVertex, CKBOOL fromVertex,
                                     CKDWORD dpFlags, CKDWORD streamFlag,
                                     CKFFMaterialSource vertexSource) {
    (void)lighting;
    if (!colorVertex || !fromVertex || ((dpFlags & streamFlag) == 0))
        return CKFF_MS_MATERIAL;
    return vertexSource;
}

static float EncodeShaderLightType(VXLIGHT_TYPE type) {
    switch (type) {
    case VX_LIGHTDIREC: return 0.0f;
    case VX_LIGHTSPOT:  return 2.0f;
    case VX_LIGHTPOINT:
    default:            return 1.0f;
    }
}

CKFixedFunctionPipeline::CKFixedFunctionPipeline()
    : m_Context(nullptr), m_ActiveLightCount(0), m_CurrentActiveTextureCount(0),
      m_CurrentLightingEnabled(false), m_DirtyFlags(CKFF_DIRTY_ALL) {
    Vx3DMatrixIdentity(m_World);
    Vx3DMatrixIdentity(m_View);
    Vx3DMatrixIdentity(m_Projection);
    for (int i = 0; i < CKFF_MAX_TEXTURE_STAGES; i++)
        Vx3DMatrixIdentity(m_TexMatrix[i]);
    ResetMaterial();
    memset(m_Lights, 0, sizeof(m_Lights));
    memset(m_LightEnabled, 0, sizeof(m_LightEnabled));
    memset(m_TextureHandles, 0, sizeof(m_TextureHandles));
    memset(m_StageStates, 0, sizeof(m_StageStates));
    memset(m_UserClipPlanes, 0, sizeof(m_UserClipPlanes));
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        m_StageStates[stage][CKRST_TSS_TEXCOORDINDEX] = (CKDWORD)stage;
    m_Viewport[0] = 2.0f / 800.0f;
    m_Viewport[1] = -2.0f / 600.0f;
    m_Viewport[2] = -1.0f;
    m_Viewport[3] = 1.0f;
    m_MaterialSource[0] = (float)CKFF_MS_MATERIAL;
    m_MaterialSource[1] = (float)CKFF_MS_MATERIAL;
    m_MaterialSource[2] = (float)CKFF_MS_MATERIAL;
    m_MaterialSource[3] = (float)CKFF_MS_MATERIAL;
}

CKFixedFunctionPipeline::~CKFixedFunctionPipeline() {
    Shutdown();
}

void CKFixedFunctionPipeline::Init(CKRasterizerContext *ctx) {
    m_Context = ctx;
    m_ShaderCache.Init(ctx);
    m_DrawStateCache.Reset();
    m_VertexLayoutCache.Init(ctx);
    m_TransientGeometry.Init(ctx, &m_VertexLayoutCache);
    m_RenderPipeline.Init(ctx);
    m_DirtyFlags = CKFF_DIRTY_ALL;
}

void CKFixedFunctionPipeline::Shutdown() {
    m_TransientGeometry.Shutdown();
    m_VertexLayoutCache.Shutdown();
    m_ShaderCache.Shutdown();
    m_RenderPipeline.Shutdown();
    m_Context = nullptr;
}

CKFFStateGuard::CKFFStateGuard(CKFixedFunctionPipeline &pipeline)
    : m_Pipeline(&pipeline),
      m_ColorWriteMask(pipeline.GetColorWriteMask()),
      m_World(pipeline.GetWorldMatrix()),
      m_View(pipeline.GetViewMatrix()),
      m_Projection(pipeline.GetProjectionMatrix()) {
    for (int i = 0; i < CKFF_RS_COUNT; ++i)
        m_RenderStates[i] = pipeline.GetRenderState((VXRENDERSTATETYPE)i);
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        pipeline.SaveTextureStage(stage, m_TextureStages[stage]);
}

CKFFStateGuard::~CKFFStateGuard() {
    Restore();
}

void CKFFStateGuard::Restore() {
    if (!m_Pipeline)
        return;
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        m_Pipeline->RestoreTextureStage(stage, m_TextureStages[stage]);
    m_Pipeline->SetTransform(VXMATRIX_WORLD, m_World);
    m_Pipeline->SetTransform(VXMATRIX_VIEW, m_View);
    m_Pipeline->SetTransform(VXMATRIX_PROJECTION, m_Projection);
    m_Pipeline->SetColorWriteMask(m_ColorWriteMask);
    for (int i = 0; i < CKFF_RS_COUNT; ++i)
        m_Pipeline->SetRenderState((VXRENDERSTATETYPE)i, m_RenderStates[i]);
    m_Pipeline = nullptr;
}

void CKFFStateGuard::Dismiss() {
    m_Pipeline = nullptr;
}

CKFFRenderStateGuard::CKFFRenderStateGuard(CKFixedFunctionPipeline &pipeline, VXRENDERSTATETYPE state, CKBOOL active)
    : m_Pipeline(active ? &pipeline : nullptr),
      m_State(state),
      m_Value(active ? pipeline.GetRenderState(state) : 0) {
}

CKFFRenderStateGuard::~CKFFRenderStateGuard() {
    Restore();
}

void CKFFRenderStateGuard::Restore() {
    if (!m_Pipeline)
        return;
    m_Pipeline->SetRenderState(m_State, m_Value);
    m_Pipeline = nullptr;
}

void CKFFRenderStateGuard::Dismiss() {
    m_Pipeline = nullptr;
}

// ============================================================================
// State tracking
// ============================================================================

void CKFixedFunctionPipeline::SetRenderState(VXRENDERSTATETYPE state, CKDWORD value) {
    m_DrawStateCache.SetRenderState(state, value);

    switch (state) {
    case VXRENDERSTATE_FOGENABLE:
    case VXRENDERSTATE_FOGVERTEXMODE:
    case VXRENDERSTATE_FOGPIXELMODE:
    case VXRENDERSTATE_FOGSTART:
    case VXRENDERSTATE_FOGEND:
    case VXRENDERSTATE_FOGDENSITY:
    case VXRENDERSTATE_FOGCOLOR:
        m_DirtyFlags |= CKFF_DIRTY_FOG;
        break;
    case VXRENDERSTATE_AMBIENT:
        m_DirtyFlags |= CKFF_DIRTY_LIGHTS;
        break;
    case VXRENDERSTATE_TEXTUREFACTOR:
        m_DirtyFlags |= CKFF_DIRTY_TEXFACTOR;
        break;
    case VXRENDERSTATE_ALPHATESTENABLE:
    case VXRENDERSTATE_ALPHAFUNC:
    case VXRENDERSTATE_ALPHAREF:
        m_DirtyFlags |= CKFF_DIRTY_ALPHATEST;
        break;
    default:
        break;
    }
}

CKDWORD CKFixedFunctionPipeline::GetRenderState(VXRENDERSTATETYPE state) const {
    return m_DrawStateCache.GetRenderState(state);
}

void CKFixedFunctionPipeline::SetColorWriteMask(CKBOOL r, CKBOOL g, CKBOOL b, CKBOOL a) {
    m_DrawStateCache.SetColorWriteMask(r, g, b, a);
}

CKDWORD CKFixedFunctionPipeline::GetColorWriteMask() const {
    return m_DrawStateCache.GetColorWriteMask();
}

void CKFixedFunctionPipeline::SetColorWriteMask(CKDWORD mask) {
    m_DrawStateCache.SetColorWriteMask(mask);
}

void CKFixedFunctionPipeline::ResetTextureStage(int stage) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return;

    m_TextureHandles[stage] = 0;
    memset(m_StageStates[stage], 0, sizeof(m_StageStates[stage]));
    m_StageStates[stage][CKRST_TSS_TEXCOORDINDEX] = (CKDWORD)stage;
    m_StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS] = CKRST_TTF_NONE;
    Vx3DMatrixIdentity(m_TexMatrix[stage]);
}

void CKFixedFunctionPipeline::DisableTextureStagesFrom(int firstStage) {
    if (firstStage < 0)
        firstStage = 0;
    for (int stage = firstStage; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        ResetTextureStage(stage);
}

void CKFixedFunctionPipeline::SaveTextureStage(int stage, CKFFTextureStageSnapshot &snapshot) const {
    memset(&snapshot, 0, sizeof(snapshot));
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return;

    snapshot.Texture = m_TextureHandles[stage];
    memcpy(snapshot.States, m_StageStates[stage], sizeof(snapshot.States));
    snapshot.TextureMatrix = m_TexMatrix[stage];
}

void CKFixedFunctionPipeline::RestoreTextureStage(int stage, const CKFFTextureStageSnapshot &snapshot) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return;

    m_TextureHandles[stage] = snapshot.Texture;
    memcpy(m_StageStates[stage], snapshot.States, sizeof(m_StageStates[stage]));
    m_TexMatrix[stage] = snapshot.TextureMatrix;
}

void CKFixedFunctionPipeline::SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type, CKDWORD value) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return;
    if ((int)type >= CKFF_MAX_TEXTURE_STAGE_STATES) return;
    m_StageStates[stage][(int)type] = value;
}

CKDWORD CKFixedFunctionPipeline::GetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type) const {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return 0;
    if ((int)type >= CKFF_MAX_TEXTURE_STAGE_STATES) return 0;
    return m_StageStates[stage][(int)type];
}

void CKFixedFunctionPipeline::SetViewport(const CKViewportData &viewport) {
    const float w = viewport.ViewWidth > 0 ? (float)viewport.ViewWidth : 1.0f;
    const float h = viewport.ViewHeight > 0 ? (float)viewport.ViewHeight : 1.0f;
    const float x = (float)viewport.ViewX;
    const float y = (float)viewport.ViewY;

    m_Viewport[0] = 2.0f / w;
    m_Viewport[1] = -2.0f / h;
    m_Viewport[2] = -1.0f - (2.0f * x / w);
    m_Viewport[3] = 1.0f + (2.0f * y / h);
}

void CKFixedFunctionPipeline::SetUserClipPlane(int index, const VxPlane &plane) {
    if (index < 0 || index >= 6)
        return;
    m_UserClipPlanes[index] = plane;
}

void CKFixedFunctionPipeline::SetTransform(VXMATRIX_TYPE type, const VxMatrix &matrix) {
    switch (type) {
    case VXMATRIX_WORLD:
        m_World = matrix;
        m_DirtyFlags |= CKFF_DIRTY_MATRICES;
        break;
    case VXMATRIX_VIEW:
        m_View = matrix;
        m_DirtyFlags |= CKFF_DIRTY_MATRICES | CKFF_DIRTY_LIGHTS;
        break;
    case VXMATRIX_PROJECTION:
        m_Projection = matrix;
        m_DirtyFlags |= CKFF_DIRTY_MATRICES;
        break;
    default:
        if (type >= VXMATRIX_TEXTURE0 && type <= VXMATRIX_TEXTURE7) {
            int idx = type - VXMATRIX_TEXTURE0;
            if (idx < CKFF_MAX_TEXTURE_STAGES)
                m_TexMatrix[idx] = matrix;
        }
        break;
    }
}

void CKFixedFunctionPipeline::ResetMaterial() {
    memset(&m_Material, 0, sizeof(m_Material));
    m_Material.Diffuse[0] = 1.0f;
    m_Material.Diffuse[1] = 1.0f;
    m_Material.Diffuse[2] = 1.0f;
    m_Material.Diffuse[3] = 1.0f;
    m_Material.Ambient[0] = 1.0f;
    m_Material.Ambient[1] = 1.0f;
    m_Material.Ambient[2] = 1.0f;
    m_Material.Ambient[3] = 1.0f;
    m_DirtyFlags |= CKFF_DIRTY_MATERIAL;
}

void CKFixedFunctionPipeline::SetMaterial(const CKMaterialData *mat) {
    if (!mat) return;
    m_Material.Diffuse[0] = mat->Diffuse.r;
    m_Material.Diffuse[1] = mat->Diffuse.g;
    m_Material.Diffuse[2] = mat->Diffuse.b;
    m_Material.Diffuse[3] = mat->Diffuse.a;
    m_Material.Ambient[0] = mat->Ambient.r;
    m_Material.Ambient[1] = mat->Ambient.g;
    m_Material.Ambient[2] = mat->Ambient.b;
    m_Material.Ambient[3] = mat->Ambient.a;
    m_Material.Specular[0] = mat->Specular.r;
    m_Material.Specular[1] = mat->Specular.g;
    m_Material.Specular[2] = mat->Specular.b;
    m_Material.Specular[3] = mat->Specular.a;
    m_Material.Emissive[0] = mat->Emissive.r;
    m_Material.Emissive[1] = mat->Emissive.g;
    m_Material.Emissive[2] = mat->Emissive.b;
    m_Material.Emissive[3] = mat->Emissive.a;
    m_Material.Power = mat->SpecularPower;
    m_DirtyFlags |= CKFF_DIRTY_MATERIAL;
}

void CKFixedFunctionPipeline::SetLight(int index, const CKLightData *light) {
    if (index < 0 || index >= CKFF_MAX_LIGHTS || !light) return;

    CKFFLightData &dst = m_Lights[index];

    // Store in world space; will be transformed to view space at upload time
    dst.Position[0] = light->Position.x;
    dst.Position[1] = light->Position.y;
    dst.Position[2] = light->Position.z;
    dst.Position[3] = EncodeShaderLightType(light->Type);

    dst.Direction[0] = light->Direction.x;
    dst.Direction[1] = light->Direction.y;
    dst.Direction[2] = light->Direction.z;
    dst.Direction[3] = light->Range;

    dst.Diffuse[0] = light->Diffuse.r;
    dst.Diffuse[1] = light->Diffuse.g;
    dst.Diffuse[2] = light->Diffuse.b;
    dst.Diffuse[3] = light->Diffuse.a;

    dst.Specular[0] = light->Specular.r;
    dst.Specular[1] = light->Specular.g;
    dst.Specular[2] = light->Specular.b;
    dst.Specular[3] = light->Specular.a;

    dst.Ambient[0] = light->Ambient.r;
    dst.Ambient[1] = light->Ambient.g;
    dst.Ambient[2] = light->Ambient.b;
    dst.Ambient[3] = light->Ambient.a;

    dst.Attenuation[0] = light->Attenuation0;
    dst.Attenuation[1] = light->Attenuation1;
    dst.Attenuation[2] = light->Attenuation2;
    dst.Attenuation[3] = light->Falloff;

    dst.SpotParams[0] = cosf(light->InnerSpotCone * 0.5f);
    dst.SpotParams[1] = cosf(light->OuterSpotCone * 0.5f);
    dst.SpotParams[2] = 0.0f;
    dst.SpotParams[3] = 0.0f;

    m_DirtyFlags |= CKFF_DIRTY_LIGHTS;
}

void CKFixedFunctionPipeline::EnableLight(int index, CKBOOL enable) {
    if (index < 0 || index >= CKFF_MAX_LIGHTS) return;
    m_LightEnabled[index] = enable;

    m_ActiveLightCount = 0;
    for (int i = 0; i < CKFF_MAX_LIGHTS; i++) {
        if (m_LightEnabled[i]) m_ActiveLightCount++;
    }
    m_DirtyFlags |= CKFF_DIRTY_LIGHTS;
}

void CKFixedFunctionPipeline::SetTexture(int stage, CKDWORD textureHandle) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return;
    m_TextureHandles[stage] = textureHandle;
}

CKDWORD CKFixedFunctionPipeline::GetTexture(int stage) const {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return 0;
    return m_TextureHandles[stage];
}

void CKFixedFunctionPipeline::BeginDebugFrame() {
    m_DebugState.BeginFrame();
}

// ============================================================================
// Drawing
// ============================================================================

void CKFixedFunctionPipeline::DrawPrimitive(
    CKRasterizerEncoder *encoder, CKRenderView view,
    VXPRIMITIVETYPE type, CKWORD *indices, int indexCount,
    VxDrawPrimitiveData *data)
{
    if (!encoder || !data || data->VertexCount == 0) return;

    bool hasNormal = (data->NormalPtr != nullptr);
    bool hasUV = (data->TexCoordPtr != nullptr);
    CKDWORD formatFlags = CKVertexLayoutCache::DPFlagsToFormatFlags(data->Flags, hasNormal, hasUV);
    const bool positionT = (formatFlags & CKFF_VF_POSITIONT) != 0;
    const int debugDrawSerial = m_DebugState.NextDrawSerial(view);
    CKFFDrawDebugInfo debugInfo = {};
    debugInfo.View = view;
    debugInfo.Type = type;
    debugInfo.Indices = indices;
    debugInfo.IndexCount = indexCount;
    debugInfo.Data = data;
    debugInfo.FormatFlags = formatFlags;
    debugInfo.DrawSerial = debugDrawSerial;
    debugInfo.World = &m_World;
    debugInfo.ViewMatrix = &m_View;
    debugInfo.Projection = &m_Projection;
    debugInfo.Viewport = m_Viewport;
    m_DebugState.LogDrawPrimitiveHeader(debugInfo);

    if (m_DebugState.ShouldSkip(view, debugDrawSerial, positionT)) {
        m_DebugState.LogDrawPrimitiveSkipped(debugInfo, positionT);
        return;
    }

    // Prepare transient geometry
    const CKDWORD wrapMode = m_DrawStateCache.GetRenderState(VXRENDERSTATE_WRAP0);
    CKFFPointSpriteParams pointParams;
    pointParams.Size = ReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSIZE, 1.0f);
    pointParams.MinSize = ReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSIZE_MIN, 1.0f);
    pointParams.MaxSize = ReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSIZE_MAX, 64.0f);
    pointParams.ScaleEnable = m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSCALEENABLE);
    pointParams.ScaleA = ReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSCALE_A, 1.0f);
    pointParams.ScaleB = ReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSCALE_B, 0.0f);
    pointParams.ScaleC = ReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSCALE_C, 0.0f);
    pointParams.World = m_World;
    pointParams.View = m_View;
    if (!m_TransientGeometry.Prepare(
            encoder, type, indices, indexCount, data, wrapMode,
            m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE), &pointParams)) {
        m_DebugState.LogDrawPrimitivePrepareFailed();
        return;
    }

    // Build the fixed-function state description and select the matching program.
    m_CurrentActiveTextureCount = CKFFActiveTextureCountFromDPFlags(data->Flags);
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const CKDWORD op = m_StageStates[stage][CKRST_TSS_OP];
        if (m_TextureHandles[stage] != 0 ||
            (op != 0 && op != CKRST_TOP_DISABLE)) {
            if (m_CurrentActiveTextureCount < stage + 1)
                m_CurrentActiveTextureCount = stage + 1;
        }
    }
    CKFFStateDesc stateDesc = BuildCurrentStateDesc(data->Flags, formatFlags);
    CKFFShaderKey shaderKey = BuildCurrentShaderKey(stateDesc);
    SetCurrentShaderKey(shaderKey);
    CKDWORD program = m_ShaderCache.GetProgram(shaderKey);
    if (program == 0) {
        m_DebugState.LogDrawPrimitiveProgramMissing();
        return;
    }

    debugInfo.Program = program;
    debugInfo.ActiveTextureCount = m_CurrentActiveTextureCount;
    debugInfo.ActiveLightCount = m_ActiveLightCount;
    debugInfo.StateDesc = &stateDesc;
    debugInfo.DrawState = &m_DrawStateCache;
    debugInfo.Stage0.ColorOp = stateDesc.FS.GetStageColorOp(0);
    debugInfo.Stage0.ColorArg1 = GetStageColorArg1(m_StageStates[0], m_CurrentActiveTextureCount > 0 && m_TextureHandles[0] != 0);
    debugInfo.Stage0.ColorArg2 = GetStageColorArg2(m_StageStates[0]);
    debugInfo.Stage0.AlphaOp = GetStageAlphaOp(m_StageStates[0], m_CurrentActiveTextureCount > 0, m_TextureHandles[0] != 0);
    debugInfo.Stage0.AlphaArg1 = GetStageAlphaArg1(m_StageStates[0], m_CurrentActiveTextureCount > 0 && m_TextureHandles[0] != 0);
    debugInfo.Stage0.AlphaArg2 = GetStageAlphaArg2(m_StageStates[0]);
    debugInfo.Stage0.Texture = m_TextureHandles[0];
    m_DebugState.LogDrawPrimitiveDetails(debugInfo);

    // Upload uniforms
    UploadUniforms(encoder);

    // Set world transform
    CKDWORD transformIdx = m_Context->AllocTransform(&m_World, 1);
    encoder->SetTransform(transformIdx, 1);

    // Set draw state
    CKDrawState drawState = m_DrawStateCache.BuildDrawState(
        (type == VX_TRIANGLEFAN || type == VX_TRIANGLESTRIP ||
         (type == VX_POINTLIST && m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE)))
            ? VX_TRIANGLELIST
            : type);
    encoder->SetState(drawState);
    encoder->SetStencilRef(m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILREF));
    encoder->SetStencilMask(m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILMASK),
                            m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK));

    // Bind textures
    BindTextures(encoder);

    // Submit
    float depth = ComputeDepthKey();
    encoder->Submit(view, program, *(CKDWORD *)&depth, CKRST_DISCARD_ALL);
}

void CKFixedFunctionPipeline::DrawVertexBuffer(
    CKRasterizerEncoder *encoder, CKRenderView view,
    VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
    CKDWORD baseVertex, CKDWORD vertexCount,
    CKDWORD startIndex, CKDWORD indexCount,
    CKDWORD dpFlags, CKDWORD formatFlags,
    CKDWORD vertexLayout)
{
    if (!encoder || !vb) return;

    // Build the fixed-function state description from the actual mesh vertex format.
    const bool positionT = (formatFlags & CKFF_VF_POSITIONT) != 0;
    const int debugDrawSerial = m_DebugState.NextDrawSerial(view);
    CKFFDrawDebugInfo debugInfo = {};
    debugInfo.View = view;
    debugInfo.Type = type;
    debugInfo.World = &m_World;
    debugInfo.ViewMatrix = &m_View;
    debugInfo.Projection = &m_Projection;
    debugInfo.VertexBuffer = vb;
    debugInfo.IndexBuffer = ib;
    debugInfo.BaseVertex = baseVertex;
    debugInfo.VertexCount = vertexCount;
    debugInfo.StartIndex = startIndex;
    debugInfo.PersistentIndexCount = indexCount;
    debugInfo.DPFlags = dpFlags;
    debugInfo.FormatFlags = formatFlags;
    debugInfo.VertexLayout = vertexLayout;
    debugInfo.DrawSerial = debugDrawSerial;
    m_DebugState.LogDrawVertexBufferHeader(debugInfo);

    if (m_DebugState.ShouldSkip(view, debugDrawSerial, positionT)) {
        m_DebugState.LogDrawVertexBufferSkipped(debugInfo, positionT);
        return;
    }

    m_CurrentActiveTextureCount = CKFFActiveTextureCountFromDPFlags(dpFlags);
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const CKDWORD op = m_StageStates[stage][CKRST_TSS_OP];
        if (m_TextureHandles[stage] != 0 ||
            (op != 0 && op != CKRST_TOP_DISABLE)) {
            if (m_CurrentActiveTextureCount < stage + 1)
                m_CurrentActiveTextureCount = stage + 1;
        }
    }
    CKFFStateDesc stateDesc = BuildCurrentStateDesc(dpFlags, formatFlags);
    CKFFShaderKey shaderKey = BuildCurrentShaderKey(stateDesc);
    SetCurrentShaderKey(shaderKey);
    CKDWORD program = m_ShaderCache.GetProgram(shaderKey);
    if (program == 0) return;

    debugInfo.Program = program;
    debugInfo.ActiveTextureCount = m_CurrentActiveTextureCount;
    debugInfo.ActiveLightCount = m_ActiveLightCount;
    debugInfo.StateDesc = &stateDesc;
    debugInfo.DrawState = &m_DrawStateCache;
    debugInfo.Stage0.ColorOp = stateDesc.FS.GetStageColorOp(0);
    debugInfo.Stage0.ColorArg1 = GetStageColorArg1(m_StageStates[0], m_CurrentActiveTextureCount > 0 && m_TextureHandles[0] != 0);
    debugInfo.Stage0.ColorArg2 = GetStageColorArg2(m_StageStates[0]);
    debugInfo.Stage0.AlphaOp = GetStageAlphaOp(m_StageStates[0], m_CurrentActiveTextureCount > 0, m_TextureHandles[0] != 0);
    debugInfo.Stage0.AlphaArg1 = GetStageAlphaArg1(m_StageStates[0], m_CurrentActiveTextureCount > 0 && m_TextureHandles[0] != 0);
    debugInfo.Stage0.AlphaArg2 = GetStageAlphaArg2(m_StageStates[0]);
    debugInfo.Stage0.Texture = m_TextureHandles[0];
    m_DebugState.LogDrawVertexBufferDetails(debugInfo);

    // Upload uniforms
    UploadUniforms(encoder);

    // Set world transform
    CKDWORD transformIdx = m_Context->AllocTransform(&m_World, 1);
    encoder->SetTransform(transformIdx, 1);

    // Set draw state
    CKDrawState drawState = m_DrawStateCache.BuildDrawState(type);
    encoder->SetState(drawState);
    encoder->SetStencilRef(m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILREF));
    encoder->SetStencilMask(m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILMASK),
                            m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK));

    // Set vertex layout
    if (vertexLayout)
        encoder->SetVertexLayout(vertexLayout);

    // Bind buffers
    encoder->SetVertexBuffer(0, vb, baseVertex, vertexCount);
    if (ib)
        encoder->SetIndexBuffer(ib, startIndex, indexCount);

    // Bind textures
    BindTextures(encoder);

    // Submit
    float depth = ComputeDepthKey();
    encoder->Submit(view, program, *(CKDWORD *)&depth, CKRST_DISCARD_ALL);
}

// ============================================================================
// Internal methods
// ============================================================================

CKFFStateDesc CKFixedFunctionPipeline::BuildCurrentStateDesc(CKDWORD dpFlags, CKDWORD formatFlags) {
    CKFFStateDesc stateDesc;

    const bool hasFormat = formatFlags != 0;
    const bool positionT = hasFormat ? ((formatFlags & CKFF_VF_POSITIONT) != 0) : ((dpFlags & CKRST_DP_TRANSFORM) == 0);

    // Vertex state description
    stateDesc.VS.SetHasPosition(!positionT);
    stateDesc.VS.SetHasPositionT(positionT);
    stateDesc.VS.SetHasNormal(hasFormat ? ((formatFlags & CKFF_VF_NORMAL) != 0) : ((dpFlags & CKRST_DP_LIGHT) != 0));
    stateDesc.VS.SetHasColor0(hasFormat ? ((formatFlags & CKFF_VF_COLOR0) != 0) : ((dpFlags & CKRST_DP_DIFFUSE) != 0));
    stateDesc.VS.SetHasColor1(hasFormat ? ((formatFlags & CKFF_VF_COLOR1) != 0) : ((dpFlags & CKRST_DP_SPECULAR) != 0));
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        stateDesc.VS.SetHasTexCoord(
            stage,
            hasFormat ? ((formatFlags & CKFF_VF_TEXCOORD(stage)) != 0) : (m_CurrentActiveTextureCount > stage));
        const CKDWORD packedTexcoord = m_StageStates[stage][CKRST_TSS_TEXCOORDINDEX];
        const CKDWORD transformFlags = m_StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS];
        stateDesc.VS.SetTexCoordIndex(stage, CKFFTexcoordIndex(packedTexcoord));
        if (!positionT) {
            stateDesc.VS.SetTextureTransformFlags(stage, transformFlags);
            const CKDWORD texgen = (packedTexcoord >> 16) & 0xFFFFu;
            const bool hasTransform = transformFlags != 0;
            stateDesc.VS.SetTexGen(stage, texgen, hasTransform);
        }
    }

    CKBOOL lighting = m_DrawStateCache.GetRenderState(VXRENDERSTATE_LIGHTING);
    CKBOOL specular = m_DrawStateCache.GetRenderState(VXRENDERSTATE_SPECULARENABLE);
    CKBOOL normalize = m_DrawStateCache.GetRenderState(VXRENDERSTATE_NORMALIZENORMALS);

    if (m_DebugState.ForceUnlit())
        lighting = FALSE;

    stateDesc.VS.SetLightingEnabled(!positionT && lighting && stateDesc.VS.GetHasNormal());
    m_CurrentLightingEnabled = stateDesc.VS.GetLightingEnabled();
    stateDesc.VS.SetSpecularEnabled(specular != 0);
    stateDesc.VS.SetNormalizeNormals(normalize != 0);
    stateDesc.VS.SetLightCount(stateDesc.VS.GetLightingEnabled() ? m_ActiveLightCount : 0);

    const CKBOOL colorVertex = m_DrawStateCache.GetRenderState(VXRENDERSTATE_COLORVERTEX);
    const CKDWORD diffuseSource = ResolveMaterialSource(
        stateDesc.VS.GetLightingEnabled(), colorVertex,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_DIFFUSEFROMVERTEX),
        dpFlags, CKRST_DP_DIFFUSE, CKFF_MS_COLOR0);
    const CKDWORD ambientSource = ResolveMaterialSource(
        stateDesc.VS.GetLightingEnabled(), colorVertex,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_AMBIENTFROMVERTEX),
        dpFlags, CKRST_DP_DIFFUSE, CKFF_MS_COLOR0);
    const CKDWORD specularSource = ResolveMaterialSource(
        stateDesc.VS.GetLightingEnabled(), colorVertex,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_SPECULARFROMVERTEX),
        dpFlags, CKRST_DP_SPECULAR, CKFF_MS_COLOR1);
    const CKDWORD emissiveSource = ResolveMaterialSource(
        stateDesc.VS.GetLightingEnabled(), colorVertex,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_EMISSIVEFROMVERTEX),
        dpFlags, CKRST_DP_DIFFUSE, CKFF_MS_COLOR0);

    stateDesc.VS.SetDiffuseSource(diffuseSource);
    stateDesc.VS.SetAmbientSource(ambientSource);
    stateDesc.VS.SetSpecularSource(specularSource);
    stateDesc.VS.SetEmissiveSource(emissiveSource);
    m_MaterialSource[0] = (float)diffuseSource;
    m_MaterialSource[1] = (float)ambientSource;
    m_MaterialSource[2] = (float)specularSource;
    m_MaterialSource[3] = (float)emissiveSource;

    // Fog
    CKBOOL fogEnable = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGENABLE);
    if (m_DebugState.DisableFog())
        fogEnable = FALSE;
    if (fogEnable) {
        CKDWORD vertexFogMode = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGVERTEXMODE);
        CKDWORD pixelFogMode = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGPIXELMODE);
        stateDesc.VS.SetFogMode(vertexFogMode);
        stateDesc.VS.SetRangeFog(m_DrawStateCache.GetRenderState(VXRENDERSTATE_RANGEFOGENABLE) != 0);
        stateDesc.FS.SetVertexFogMode(vertexFogMode);
        stateDesc.FS.SetPixelFogMode(pixelFogMode);
        stateDesc.FS.SetRangeFog(m_DrawStateCache.GetRenderState(VXRENDERSTATE_RANGEFOGENABLE) != 0);
    }

    // Fragment state description mirrors the active fixed-function texture-stage contract.
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const bool stageActive = stage < m_CurrentActiveTextureCount;
        const bool hasTexture = stageActive && m_TextureHandles[stage] != 0;
        const CKDWORD colorOp = GetStageColorOp(m_StageStates[stage], stageActive, hasTexture);
        const CKDWORD alphaOp = GetStageAlphaOp(m_StageStates[stage], stageActive, hasTexture);
        stateDesc.FS.SetStageColorOp(stage, colorOp);
        stateDesc.FS.SetStageColorArg0(stage, GetStageColorArg0(m_StageStates[stage]));
        stateDesc.FS.SetStageColorArg1(stage, BaseTextureArg(GetStageColorArg1(m_StageStates[stage], hasTexture)));
        stateDesc.FS.SetStageColorArg2(stage, BaseTextureArg(GetStageColorArg2(m_StageStates[stage])));
        stateDesc.FS.SetStageAlphaOp(stage, alphaOp);
        stateDesc.FS.SetStageAlphaArg0(stage, GetStageAlphaArg0(m_StageStates[stage]));
        stateDesc.FS.SetStageAlphaArg1(stage, BaseTextureArg(GetStageAlphaArg1(m_StageStates[stage], hasTexture)));
        stateDesc.FS.SetStageAlphaArg2(stage, BaseTextureArg(GetStageAlphaArg2(m_StageStates[stage])));
        stateDesc.FS.SetStageResultIsTemp(stage, BaseTextureArg(GetStageResultArg(m_StageStates[stage])) == CKRST_TA_TEMP);
        stateDesc.FS.SetStageProjectedSampler(stage, (m_StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS] & CKRST_TTF_PROJECTED) != 0);

        if (colorOp == CKRST_TOP_DISABLE)
            break;
    }

    stateDesc.FS.SetSpecularAdd(specular != 0);
    stateDesc.FS.SetFogEnabled(fogEnable != 0);

    CKBOOL alphaTest = m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHATESTENABLE);
    if (alphaTest) {
        stateDesc.FS.SetAlphaTestEnabled(true);
        stateDesc.FS.SetAlphaFunc(m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAFUNC));
    }

    return stateDesc;
}

CKFFShaderKey CKFixedFunctionPipeline::BuildCurrentShaderKey(const CKFFStateDesc &stateDesc) const {
    CKDWORD textureBoundMask = 0;
    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        if (m_TextureHandles[stage] != 0)
            textureBoundMask |= (1u << stage);
    }
    return CKFFBuildShaderKey(stateDesc, textureBoundMask);
}

void CKFixedFunctionPipeline::SetCurrentShaderKey(const CKFFShaderKey &shaderKey) {
    m_CurrentSpecializationInfo = CKFFBuildSpecializationInfo(shaderKey.FS);
}

void CKFixedFunctionPipeline::UploadUniforms(CKRasterizerEncoder *encoder) {
    if (!encoder) return;

    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();

    VxMatrix modelView;
    VxMatrix normalMatrix;
    VxMatrix viewProj;
    VxMatrix modelViewProj;
    MulMatrix(modelView, m_World, m_View);
    Vx3DInverseMatrix(normalMatrix, modelView);
    TransposeMatrix(normalMatrix, normalMatrix);
    MulMatrix(viewProj, m_View, m_Projection);
    MulMatrix(modelViewProj, m_World, viewProj);
    encoder->SetUniform(u.u_ckModelViewProj, &modelViewProj, 1);
    encoder->SetUniform(u.u_ckModel, &m_World, 1);
    encoder->SetUniform(u.u_ckModelView, &modelView, 1);
    encoder->SetUniform(u.u_ckNormalMatrix, &normalMatrix, 1);
    encoder->SetUniform(u.u_texMatrix, m_TexMatrix, CKFF_MAX_TEXTURE_STAGES);

    // bgfx uniform bindings are draw state. Since draws submit with
    // CKRST_DISCARD_ALL, upload the current fixed-function constants for every
    // draw instead of relying on dirty flags across submissions.
    CKFFLightData viewLights[CKFF_MAX_LIGHTS];
    int packed = 0;
    for (int i = 0; m_CurrentLightingEnabled && i < CKFF_MAX_LIGHTS && packed < m_ActiveLightCount; i++) {
        if (!m_LightEnabled[i]) continue;

        viewLights[packed] = m_Lights[i];

        // Transform position to view space
        VxVector pos(m_Lights[i].Position[0], m_Lights[i].Position[1], m_Lights[i].Position[2]);
        VxVector viewPos;
        Vx3DMultiplyMatrixVector(&viewPos, m_View, &pos);
        viewLights[packed].Position[0] = viewPos.x;
        viewLights[packed].Position[1] = viewPos.y;
        viewLights[packed].Position[2] = viewPos.z;

        // Transform direction to view space (rotation only)
        VxVector dir(m_Lights[i].Direction[0], m_Lights[i].Direction[1], m_Lights[i].Direction[2]);
        VxVector viewDir;
        Vx3DRotateVector(&viewDir, m_View, &dir);
        viewLights[packed].Direction[0] = viewDir.x;
        viewLights[packed].Direction[1] = viewDir.y;
        viewLights[packed].Direction[2] = viewDir.z;

        packed++;
    }

    if (packed > 0) {
        encoder->SetUniform(u.u_lights, viewLights, packed * 7);
    }

    // Light params: x=count, y/z/w = global ambient RGB
    CKDWORD ambientColor = m_DrawStateCache.GetRenderState(VXRENDERSTATE_AMBIENT);
    float lightParams[4];
    lightParams[0] = m_CurrentLightingEnabled ? (float)packed : -1.0f;
    lightParams[1] = ((ambientColor >> 16) & 0xFF) / 255.0f;
    lightParams[2] = ((ambientColor >> 8) & 0xFF) / 255.0f;
    lightParams[3] = (ambientColor & 0xFF) / 255.0f;
    encoder->SetUniform(u.u_lightParams, lightParams, 1);

    encoder->SetUniform(u.u_material, &m_Material, 5);
    encoder->SetUniform(u.u_ffParams, m_MaterialSource, 1);
    float lightModelParams[4] = {
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_LOCALVIEWER) ? 1.0f : 0.0f,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_NORMALIZENORMALS) ? 1.0f : 0.0f,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_RANGEFOGENABLE) ? 1.0f : 0.0f,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGPIXELMODE) ? (float)m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGPIXELMODE) : 0.0f
    };
    encoder->SetUniform(u.u_lightModelParams, lightModelParams, 1);

    float fogParams[4];
    CKDWORD fs = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGSTART);
    CKDWORD fe = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGEND);
    CKDWORD fd = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGDENSITY);
    memcpy(&fogParams[0], &fs, sizeof(float));
    memcpy(&fogParams[1], &fe, sizeof(float));
    memcpy(&fogParams[2], &fd, sizeof(float));
    fogParams[3] = (m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGENABLE) &&
                    !m_DebugState.DisableFog())
        ? (float)m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGVERTEXMODE)
        : 0.0f;
    encoder->SetUniform(u.u_fogParams, fogParams, 1);

    CKDWORD fogColor = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGCOLOR);
    float fogColorF[4];
    fogColorF[0] = ((fogColor >> 16) & 0xFF) / 255.0f;
    fogColorF[1] = ((fogColor >> 8) & 0xFF) / 255.0f;
    fogColorF[2] = (fogColor & 0xFF) / 255.0f;
    fogColorF[3] = ((fogColor >> 24) & 0xFF) / 255.0f;
    encoder->SetUniform(u.u_fogColor, fogColorF, 1);

    CKDWORD tf = m_DrawStateCache.GetRenderState(VXRENDERSTATE_TEXTUREFACTOR);
    float texFactor[4];
    texFactor[0] = ((tf >> 16) & 0xFF) / 255.0f;
    texFactor[1] = ((tf >> 8) & 0xFF) / 255.0f;
    texFactor[2] = (tf & 0xFF) / 255.0f;
    texFactor[3] = ((tf >> 24) & 0xFF) / 255.0f;
    encoder->SetUniform(u.u_texFactor, texFactor, 1);

    float alphaParams[4];
    alphaParams[0] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAREF) / 255.0f;
    alphaParams[1] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHATESTENABLE)
        ? (float)m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAFUNC)
        : 0.0f;
    alphaParams[2] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_SPECULARENABLE) ? 1.0f : 0.0f;
    alphaParams[3] = 0.0f;
    encoder->SetUniform(u.u_alphaParams, alphaParams, 1);

    float bumpEnv[2][4] = {};
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const CKDWORD op = m_StageStates[stage][CKRST_TSS_OP];
        if (op == CKRST_TOP_BUMPENVMAP || op == CKRST_TOP_BUMPENVMAPLUMINANCE) {
            CKFFPackBumpEnvUniform(m_StageStates[stage], bumpEnv);
            break;
        }
    }
    encoder->SetUniform(u.u_bumpEnv, bumpEnv, 2);

    encoder->SetUniform(u.u_viewport, m_Viewport, 1);

    float stageParams[CKFF_MAX_TEXTURE_STAGES * 4][4];
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const bool stageActive = stage < m_CurrentActiveTextureCount;
        const bool hasTexture = stageActive && m_TextureHandles[stage] != 0;
        stageParams[stage * 4 + 0][0] = (float)GetStageColorOp(m_StageStates[stage], stageActive, hasTexture);
        stageParams[stage * 4 + 0][1] = (float)GetStageColorArg1(m_StageStates[stage], hasTexture);
        stageParams[stage * 4 + 0][2] = (float)GetStageColorArg2(m_StageStates[stage]);
        stageParams[stage * 4 + 0][3] = hasTexture ? 1.0f : 0.0f;
        stageParams[stage * 4 + 1][0] = (float)GetStageAlphaOp(m_StageStates[stage], stageActive, hasTexture);
        stageParams[stage * 4 + 1][1] = (float)GetStageAlphaArg1(m_StageStates[stage], hasTexture);
        stageParams[stage * 4 + 1][2] = (float)GetStageAlphaArg2(m_StageStates[stage]);
        stageParams[stage * 4 + 1][3] = (float)GetStageResultArg(m_StageStates[stage]);
        stageParams[stage * 4 + 2][0] = (float)GetStageColorArg0(m_StageStates[stage]);
        stageParams[stage * 4 + 2][1] = (float)m_StageStates[stage][CKRST_TSS_TEXCOORDINDEX];
        stageParams[stage * 4 + 2][2] = (float)m_StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS];
        stageParams[stage * 4 + 2][3] = 0.0f;
        stageParams[stage * 4 + 3][0] = (float)GetStageAlphaArg0(m_StageStates[stage]);
        stageParams[stage * 4 + 3][1] = 0.0f;
        stageParams[stage * 4 + 3][2] = 0.0f;
        stageParams[stage * 4 + 3][3] = 0.0f;
    }
    encoder->SetUniform(u.u_stageParams, stageParams, CKFF_MAX_TEXTURE_STAGES * 4);

    float ffSpec[CKFFSpecializationInfo::MaxSpecDwords][4] = {};
    const CKDWORD *specDwords = m_CurrentSpecializationInfo.Data();
    for (CKDWORD i = 0; i < CKFFSpecializationInfo::MaxSpecDwords; ++i) {
        ffSpec[i][0] = (float)(specDwords[i] & 0xFFu);
        ffSpec[i][1] = (float)((specDwords[i] >> 8) & 0xFFu);
        ffSpec[i][2] = (float)((specDwords[i] >> 16) & 0xFFu);
        ffSpec[i][3] = (float)((specDwords[i] >> 24) & 0xFFu);
    }
    encoder->SetUniform(u.u_ffSpec, ffSpec, CKFFSpecializationInfo::MaxSpecDwords);

    float clipPlanes[6][4] = {};
    int clipCount = 0;
    CKDWORD clipMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_CLIPPLANEENABLE);
    for (int i = 0; i < 6; ++i) {
        if ((clipMask & (1u << i)) == 0)
            continue;
        clipPlanes[clipCount][0] = m_UserClipPlanes[i].m_Normal.x;
        clipPlanes[clipCount][1] = m_UserClipPlanes[i].m_Normal.y;
        clipPlanes[clipCount][2] = m_UserClipPlanes[i].m_Normal.z;
        clipPlanes[clipCount][3] = m_UserClipPlanes[i].m_D;
        ++clipCount;
    }
    float clipParams[4] = {(float)clipCount, 0.0f, 0.0f, 0.0f};
    encoder->SetUniform(u.u_clipPlanes, clipPlanes, 6);
    encoder->SetUniform(u.u_clipParams, clipParams, 1);

    m_DirtyFlags = 0;
}

void CKFixedFunctionPipeline::BindTextures(CKRasterizerEncoder *encoder) {
    if (!encoder) return;
    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();

    for (int i = 0; i < CKFF_MAX_TEXTURE_STAGES; i++) {
        CKSamplerDesc sampler = BuildSamplerDesc(i);
        const CKDWORD texture = (i < m_CurrentActiveTextureCount) ? m_TextureHandles[i] : 0;
        encoder->SetTexture(i, u.s_texture[i], texture, &sampler);
    }
}

CKSamplerDesc CKFixedFunctionPipeline::BuildSamplerDesc(int stage) const {
    CKSamplerDesc desc;
    memset(&desc, 0, sizeof(desc));
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return desc;

    CKDWORD mag = m_StageStates[stage][CKRST_TSS_MAGFILTER];
    CKDWORD min = m_StageStates[stage][CKRST_TSS_MINFILTER];
    CKDWORD addr = m_StageStates[stage][CKRST_TSS_ADDRESS];
    CKDWORD addrU = m_StageStates[stage][CKRST_TSS_ADDRESSU];
    CKDWORD addrV = m_StageStates[stage][CKRST_TSS_ADDRESSV];
    CKDWORD addrW = m_StageStates[stage][CKRST_TSS_ADDRESW];

    // Filter mode translation (VXTEXTUREFILTER -> CK_FILTER_MODE)
    // VXTEXTUREFILTER: 1=NEAREST, 2=LINEAR, 3=MIPNEAREST, 4=MIPLINEAR, 5=LINEARMIPNEAREST, 6=LINEARMIPLINEAR
    switch (mag) {
    case VXTEXTUREFILTER_NEAREST:
        desc.MagFilter = CKRST_FILTER_NEAREST;
        break;
    case VXTEXTUREFILTER_ANISOTROPIC:
        desc.MagFilter = CKRST_FILTER_ANISOTROPIC;
        break;
    default:
        desc.MagFilter = CKRST_FILTER_LINEAR;
        break;
    }

    switch (min) {
    case VXTEXTUREFILTER_NEAREST:
    case VXTEXTUREFILTER_MIPNEAREST:
        desc.MinFilter = CKRST_FILTER_NEAREST;
        desc.MipFilter = CKRST_FILTER_NEAREST;
        break;
    case VXTEXTUREFILTER_MIPLINEAR:
        desc.MinFilter = CKRST_FILTER_NEAREST;
        desc.MipFilter = CKRST_FILTER_LINEAR;
        break;
    case VXTEXTUREFILTER_LINEARMIPNEAREST:
        desc.MinFilter = CKRST_FILTER_LINEAR;
        desc.MipFilter = CKRST_FILTER_NEAREST;
        break;
    case VXTEXTUREFILTER_LINEARMIPLINEAR:
        desc.MinFilter = CKRST_FILTER_LINEAR;
        desc.MipFilter = CKRST_FILTER_LINEAR;
        break;
    case VXTEXTUREFILTER_ANISOTROPIC:
        desc.MinFilter = CKRST_FILTER_ANISOTROPIC;
        desc.MipFilter = CKRST_FILTER_ANISOTROPIC;
        break;
    case VXTEXTUREFILTER_LINEAR:
    default:
        desc.MinFilter = CKRST_FILTER_LINEAR;
        desc.MipFilter = CKRST_FILTER_LINEAR;
        break;
    }

    CK_ADDRESS_MODE translateAddr = TranslateAddressMode(addr);
    desc.AddressU = (addrU != 0) ? TranslateAddressMode(addrU) : translateAddr;
    desc.AddressV = (addrV != 0) ? TranslateAddressMode(addrV) : translateAddr;
    desc.AddressW = (addrW != 0) ? TranslateAddressMode(addrW) : translateAddr;

    desc.BorderColor = m_StageStates[stage][CKRST_TSS_BORDERCOLOR];
    desc.CompareFunc = CKRST_COMPARE_NONE;

    return desc;
}

float CKFixedFunctionPipeline::ComputeDepthKey() const {
    // Depth key = distance from camera (view-space Z of the world origin)
    float z = m_World[3][0] * m_View[0][2] +
              m_World[3][1] * m_View[1][2] +
              m_World[3][2] * m_View[2][2] +
              m_View[3][2];
    return z;
}
