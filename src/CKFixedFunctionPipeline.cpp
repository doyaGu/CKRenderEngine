#include "CKFixedFunctionPipeline.h"
#include "CKRasterizer.h"
#include "CKFFUniformState.h"
#include "CKDebugLogger.h"
#include "CKRenderDebugConfig.h"
#include "CKRenderPerfStats.h"

#include <algorithm>
#include <cmath>
#include <cstring>

static const char *CKFFUniformDebugName(const CKFFUniformHandles &u, CKDWORD uniform) {
    switch (uniform) {
    case 1: return "u_ffMatrices";
    case 2: return "u_ffDrawParams";
    case 3: return "u_ffVertexParams";
    case 4: return "u_ffFragmentParams";
    case 5: return "u_lights";
    case 6: return "u_ckModelViewProj";
    case 7: return "u_ckModel";
    case 8: return "u_ckModelView";
    case 9: return "u_ckNormalMatrix";
    case 10: return "u_texMatrix";
    case 11: return "u_lightParams";
    case 12: return "u_material";
    case 13: return "u_ffParams";
    case 14: return "u_lightModelParams";
    case 15: return "u_fogParams";
    case 16: return "u_fogColor";
    case 17: return "u_texFactor";
    case 18: return "u_alphaParams";
    case 19: return "u_bumpEnv";
    case 20: return "u_viewport";
    case 21: return "u_stageParams";
    case 22: return "u_ffSpec";
    case 23: return "u_clipPlanes";
    case 24: return "u_clipParams";
    default: break;
    }
    if (uniform == u.u_ffMatrices) return "u_ffMatrices";
    if (uniform == u.u_ffDrawParams) return "u_ffDrawParams";
    if (uniform == u.u_ffVertexParams) return "u_ffVertexParams";
    if (uniform == u.u_ffFragmentParams) return "u_ffFragmentParams";
    if (uniform == u.u_lights) return "u_lights";
    if (uniform == u.u_ckModelViewProj) return "u_ckModelViewProj";
    if (uniform == u.u_ckModel) return "u_ckModel";
    if (uniform == u.u_ckModelView) return "u_ckModelView";
    if (uniform == u.u_ckNormalMatrix) return "u_ckNormalMatrix";
    if (uniform == u.u_texMatrix) return "u_texMatrix";
    if (uniform == u.u_lightParams) return "u_lightParams";
    if (uniform == u.u_material) return "u_material";
    if (uniform == u.u_ffParams) return "u_ffParams";
    if (uniform == u.u_lightModelParams) return "u_lightModelParams";
    if (uniform == u.u_fogParams) return "u_fogParams";
    if (uniform == u.u_fogColor) return "u_fogColor";
    if (uniform == u.u_texFactor) return "u_texFactor";
    if (uniform == u.u_alphaParams) return "u_alphaParams";
    if (uniform == u.u_bumpEnv) return "u_bumpEnv";
    if (uniform == u.u_viewport) return "u_viewport";
    if (uniform == u.u_stageParams) return "u_stageParams";
    if (uniform == u.u_ffSpec) return "u_ffSpec";
    if (uniform == u.u_clipPlanes) return "u_clipPlanes";
    if (uniform == u.u_clipParams) return "u_clipParams";
    return "unknown";
}

static CKDWORD CKFFUniformDebugSlot(const CKFFUniformHandles &u, CKDWORD uniform) {
    if (uniform == u.u_ffMatrices) return 1;
    if (uniform == u.u_ffDrawParams) return 2;
    if (uniform == u.u_ffVertexParams) return 3;
    if (uniform == u.u_ffFragmentParams) return 4;
    if (uniform == u.u_lights) return 5;
    if (uniform == u.u_ckModelViewProj) return 6;
    if (uniform == u.u_ckModel) return 7;
    if (uniform == u.u_ckModelView) return 8;
    if (uniform == u.u_ckNormalMatrix) return 9;
    if (uniform == u.u_texMatrix) return 10;
    if (uniform == u.u_lightParams) return 11;
    if (uniform == u.u_material) return 12;
    if (uniform == u.u_ffParams) return 13;
    if (uniform == u.u_lightModelParams) return 14;
    if (uniform == u.u_fogParams) return 15;
    if (uniform == u.u_fogColor) return 16;
    if (uniform == u.u_texFactor) return 17;
    if (uniform == u.u_alphaParams) return 18;
    if (uniform == u.u_bumpEnv) return 19;
    if (uniform == u.u_viewport) return 20;
    if (uniform == u.u_stageParams) return 21;
    if (uniform == u.u_ffSpec) return 22;
    if (uniform == u.u_clipPlanes) return 23;
    if (uniform == u.u_clipParams) return 24;
    return 0;
}

static bool CKFFDrawStateEquals(const CKDrawState &a, const CKDrawState &b) {
    return a.Lo == b.Lo && a.Mid == b.Mid && a.Hi == b.Hi;
}

static bool CKFFTextureSetEquals(
    CKDWORD aCount, const CKDWORD *a,
    CKDWORD bCount, const CKDWORD *b)
{
    if (aCount != bCount)
        return false;
    for (CKDWORD i = 0; i < aCount && i < CKFF_MAX_TEXTURE_STAGES; ++i) {
        if (a[i] != b[i])
            return false;
    }
    return true;
}

CKFixedFunctionPipeline::CKFixedFunctionPipeline()
    : m_Context(nullptr), m_ActiveLightCount(0), m_CurrentActiveTextureCount(0),
      m_CurrentLightingEnabled(false), m_DirtyFlags(CKFF_DIRTY_ALL) {
    m_DiagnosticConfig.StatsEnabled = CKRenderDebugConfigBool("CK2_3D_DEBUG_FFP_STATS", false);
    m_DiagnosticConfig.UniformHistEnabled = CKRenderDebugConfigBool("CK2_3D_DEBUG_FFP_UNIFORM_HIST", false);
    m_DiagnosticConfig.StatsInterval = CKRenderDebugConfigInt("CK2_3D_DEBUG_FFP_STATS_INTERVAL", 60);
    if (m_DiagnosticConfig.StatsInterval <= 0)
        m_DiagnosticConfig.StatsInterval = 60;

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
    memset(&m_FrameStats, 0, sizeof(m_FrameStats));
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
    dst.Position[3] = CKFFEncodeShaderLightType(light->Type);

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
    LogAndResetFrameStats();
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
    const bool collectStats = m_DiagnosticConfig.StatsEnabled || m_DiagnosticConfig.UniformHistEnabled;
    if (collectStats)
        ++m_FrameStats.SoftwareDraws;

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

    // Prepare transient geometry
    const CKDWORD wrapMode = m_DrawStateCache.GetRenderState(VXRENDERSTATE_WRAP0);
    CKFFPointSpriteParams pointParams;
    pointParams.Size = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSIZE, 1.0f);
    pointParams.MinSize = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSIZE_MIN, 1.0f);
    pointParams.MaxSize = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSIZE_MAX, 64.0f);
    pointParams.ScaleEnable = m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSCALEENABLE);
    pointParams.ScaleA = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSCALE_A, 1.0f);
    pointParams.ScaleB = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSCALE_B, 0.0f);
    pointParams.ScaleC = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSCALE_C, 0.0f);
    pointParams.World = m_World;
    pointParams.View = m_View;
    const bool statsTiming = m_DiagnosticConfig.StatsEnabled;
    double statsStart = 0.0;
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    if (!m_TransientGeometry.Prepare(
            encoder, type, indices, indexCount, data, wrapMode,
            m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE), &pointParams)) {
        m_DebugState.LogDrawPrimitivePrepareFailed();
        if (collectStats)
            ++m_FrameStats.PrepareFailures;
        return;
    }
    if (statsTiming)
        m_FrameStats.PrepareUs += CKRenderPerfElapsedUs(statsStart);
    if (collectStats) {
        m_FrameStats.TransientVertexBytes += m_TransientGeometry.GetLastVertexBytes();
        m_FrameStats.TransientIndexBytes += m_TransientGeometry.GetLastIndexBytes();
    }

    // Build the fixed-function state description and select the matching program.
    m_CurrentActiveTextureCount = CKFFResolveActiveTextureCount(data->Flags, m_TextureHandles, m_StageStates);
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    CKFFStateDesc stateDesc = BuildCurrentStateDesc(data->Flags, formatFlags);
    CKFFShaderKey shaderKey = BuildCurrentShaderKey(stateDesc);
    if (statsTiming)
        m_FrameStats.StateUs += CKRenderPerfElapsedUs(statsStart);
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    CKFFProgramBinding programBinding = m_ShaderCache.GetProgram(shaderKey);
    SetCurrentProgramBinding(shaderKey, programBinding);
    CKDWORD program = programBinding.Program;
    if (statsTiming)
        m_FrameStats.ProgramUs += CKRenderPerfElapsedUs(statsStart);
    if (program == 0) {
        m_DebugState.LogDrawPrimitiveProgramMissing();
        if (collectStats)
            ++m_FrameStats.ProgramMisses;
        return;
    }
    if (collectStats) {
        if (m_FrameStats.HasLastProgram && m_FrameStats.LastProgram == program)
            ++m_FrameStats.ConsecutiveProgramRepeats;
        m_FrameStats.LastProgram = program;
        m_FrameStats.HasLastProgram = TRUE;
    }

    debugInfo.Program = program;
    debugInfo.ActiveTextureCount = m_CurrentActiveTextureCount;
    debugInfo.ActiveLightCount = m_ActiveLightCount;
    debugInfo.StateDesc = &stateDesc;
    debugInfo.DrawState = &m_DrawStateCache;
    debugInfo.Stage0.ColorOp = stateDesc.FS.GetStageColorOp(0);
    debugInfo.Stage0.ColorArg1 = CKFFResolveStageColorArg1(m_StageStates[0], m_CurrentActiveTextureCount > 0 && m_TextureHandles[0] != 0);
    debugInfo.Stage0.ColorArg2 = CKFFResolveStageColorArg2(m_StageStates[0]);
    debugInfo.Stage0.AlphaOp = CKFFResolveStageAlphaOp(m_StageStates[0], m_CurrentActiveTextureCount > 0, m_TextureHandles[0] != 0);
    debugInfo.Stage0.AlphaArg1 = CKFFResolveStageAlphaArg1(m_StageStates[0], m_CurrentActiveTextureCount > 0 && m_TextureHandles[0] != 0);
    debugInfo.Stage0.AlphaArg2 = CKFFResolveStageAlphaArg2(m_StageStates[0]);
    debugInfo.Stage0.Texture = m_TextureHandles[0];
    m_DebugState.LogDrawPrimitiveDetails(debugInfo);

    // Upload uniforms
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    UploadUniforms(encoder);
    if (statsTiming)
        m_FrameStats.UniformUs += CKRenderPerfElapsedUs(statsStart);

    // Set world transform
    if (collectStats) {
        if (m_FrameStats.HasLastWorldMatrix && memcmp(&m_FrameStats.LastWorldMatrix, &m_World, sizeof(VxMatrix)) == 0)
            ++m_FrameStats.ConsecutiveWorldMatrixRepeats;
        memcpy(&m_FrameStats.LastWorldMatrix, &m_World, sizeof(VxMatrix));
        m_FrameStats.HasLastWorldMatrix = TRUE;
    }
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    CKDWORD transformIdx = m_Context->AllocTransform(&m_World, 1);
    encoder->SetTransform(transformIdx, 1);
    if (collectStats)
        ++m_FrameStats.TransformSets;
    if (statsTiming)
        m_FrameStats.TransformUs += CKRenderPerfElapsedUs(statsStart);

    // Set draw state
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    CKDrawState drawState = m_DrawStateCache.BuildDrawState(
        (type == VX_TRIANGLEFAN || type == VX_TRIANGLESTRIP ||
         (type == VX_POINTLIST && m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE)))
            ? VX_TRIANGLELIST
            : type);
    if (statsTiming)
        m_FrameStats.DrawStateBuildUs += CKRenderPerfElapsedUs(statsStart);
    if (collectStats) {
        if (m_FrameStats.HasLastDrawState && CKFFDrawStateEquals(m_FrameStats.LastDrawState, drawState))
            ++m_FrameStats.ConsecutiveDrawStateRepeats;
        m_FrameStats.LastDrawState = drawState;
        m_FrameStats.HasLastDrawState = TRUE;
    }
    const CKDWORD stencilRef = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILREF);
    const CKDWORD stencilReadMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILMASK);
    const CKDWORD stencilWriteMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK);
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    encoder->SetState(drawState);
    if (statsTiming)
        m_FrameStats.EncoderStateUs += CKRenderPerfElapsedUs(statsStart);
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    encoder->SetStencilRef(stencilRef);
    encoder->SetStencilMask(stencilReadMask, stencilWriteMask);
    if (statsTiming)
        m_FrameStats.StencilUs += CKRenderPerfElapsedUs(statsStart);

    // Bind textures
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    BindTextures(encoder);
    if (statsTiming)
        m_FrameStats.TextureUs += CKRenderPerfElapsedUs(statsStart);

    // Submit
    float depth = ComputeDepthKey();
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    encoder->Submit(view, program, *(CKDWORD *)&depth, SubmitDiscardFlags());
    if (statsTiming)
        m_FrameStats.SubmitUs += CKRenderPerfElapsedUs(statsStart);
    if (collectStats)
        ++m_FrameStats.SubmittedDraws;
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
    const bool collectStats = m_DiagnosticConfig.StatsEnabled || m_DiagnosticConfig.UniformHistEnabled;
    if (collectStats)
        ++m_FrameStats.HardwareDraws;

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

    m_CurrentActiveTextureCount = CKFFResolveActiveTextureCount(dpFlags, m_TextureHandles, m_StageStates);
    const bool statsTiming = m_DiagnosticConfig.StatsEnabled;
    double statsStart = 0.0;
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    CKFFStateDesc stateDesc = BuildCurrentStateDesc(dpFlags, formatFlags);
    CKFFShaderKey shaderKey = BuildCurrentShaderKey(stateDesc);
    if (statsTiming)
        m_FrameStats.StateUs += CKRenderPerfElapsedUs(statsStart);
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    CKFFProgramBinding programBinding = m_ShaderCache.GetProgram(shaderKey);
    SetCurrentProgramBinding(shaderKey, programBinding);
    CKDWORD program = programBinding.Program;
    if (statsTiming)
        m_FrameStats.ProgramUs += CKRenderPerfElapsedUs(statsStart);
    if (program == 0) {
        if (collectStats)
            ++m_FrameStats.ProgramMisses;
        return;
    }
    if (collectStats) {
        if (m_FrameStats.HasLastProgram && m_FrameStats.LastProgram == program)
            ++m_FrameStats.ConsecutiveProgramRepeats;
        m_FrameStats.LastProgram = program;
        m_FrameStats.HasLastProgram = TRUE;
    }

    debugInfo.Program = program;
    debugInfo.ActiveTextureCount = m_CurrentActiveTextureCount;
    debugInfo.ActiveLightCount = m_ActiveLightCount;
    debugInfo.StateDesc = &stateDesc;
    debugInfo.DrawState = &m_DrawStateCache;
    debugInfo.Stage0.ColorOp = stateDesc.FS.GetStageColorOp(0);
    debugInfo.Stage0.ColorArg1 = CKFFResolveStageColorArg1(m_StageStates[0], m_CurrentActiveTextureCount > 0 && m_TextureHandles[0] != 0);
    debugInfo.Stage0.ColorArg2 = CKFFResolveStageColorArg2(m_StageStates[0]);
    debugInfo.Stage0.AlphaOp = CKFFResolveStageAlphaOp(m_StageStates[0], m_CurrentActiveTextureCount > 0, m_TextureHandles[0] != 0);
    debugInfo.Stage0.AlphaArg1 = CKFFResolveStageAlphaArg1(m_StageStates[0], m_CurrentActiveTextureCount > 0 && m_TextureHandles[0] != 0);
    debugInfo.Stage0.AlphaArg2 = CKFFResolveStageAlphaArg2(m_StageStates[0]);
    debugInfo.Stage0.Texture = m_TextureHandles[0];
    m_DebugState.LogDrawVertexBufferDetails(debugInfo);

    // Upload uniforms
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    UploadUniforms(encoder);
    if (statsTiming)
        m_FrameStats.UniformUs += CKRenderPerfElapsedUs(statsStart);

    // Set world transform
    if (collectStats) {
        if (m_FrameStats.HasLastWorldMatrix && memcmp(&m_FrameStats.LastWorldMatrix, &m_World, sizeof(VxMatrix)) == 0)
            ++m_FrameStats.ConsecutiveWorldMatrixRepeats;
        memcpy(&m_FrameStats.LastWorldMatrix, &m_World, sizeof(VxMatrix));
        m_FrameStats.HasLastWorldMatrix = TRUE;
    }
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    CKDWORD transformIdx = m_Context->AllocTransform(&m_World, 1);
    encoder->SetTransform(transformIdx, 1);
    if (collectStats)
        ++m_FrameStats.TransformSets;
    if (statsTiming)
        m_FrameStats.TransformUs += CKRenderPerfElapsedUs(statsStart);

    // Set draw state
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    CKDrawState drawState = m_DrawStateCache.BuildDrawState(type);
    if (statsTiming)
        m_FrameStats.DrawStateBuildUs += CKRenderPerfElapsedUs(statsStart);
    if (collectStats) {
        if (m_FrameStats.HasLastDrawState && CKFFDrawStateEquals(m_FrameStats.LastDrawState, drawState))
            ++m_FrameStats.ConsecutiveDrawStateRepeats;
        m_FrameStats.LastDrawState = drawState;
        m_FrameStats.HasLastDrawState = TRUE;
    }
    const CKDWORD stencilRef = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILREF);
    const CKDWORD stencilReadMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILMASK);
    const CKDWORD stencilWriteMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK);
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    encoder->SetState(drawState);
    if (statsTiming)
        m_FrameStats.EncoderStateUs += CKRenderPerfElapsedUs(statsStart);
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    encoder->SetStencilRef(stencilRef);
    encoder->SetStencilMask(stencilReadMask, stencilWriteMask);
    if (statsTiming)
        m_FrameStats.StencilUs += CKRenderPerfElapsedUs(statsStart);

    // Set vertex layout
    if (vertexLayout) {
        if (statsTiming)
            statsStart = CKRenderPerfNow();
        encoder->SetVertexLayout(vertexLayout);
        if (collectStats)
            ++m_FrameStats.VertexLayoutSets;
        if (statsTiming)
            m_FrameStats.LayoutUs += CKRenderPerfElapsedUs(statsStart);
    }

    // Bind buffers
    if (collectStats) {
        if (m_FrameStats.HasLastVertexBuffer &&
            m_FrameStats.LastVertexBuffer == vb &&
            m_FrameStats.LastVertexLayout == vertexLayout)
            ++m_FrameStats.ConsecutiveVertexBufferRepeats;
        m_FrameStats.LastVertexBuffer = vb;
        m_FrameStats.LastVertexLayout = vertexLayout;
        m_FrameStats.HasLastVertexBuffer = TRUE;
        if (ib && m_FrameStats.HasLastIndexBuffer && m_FrameStats.LastIndexBuffer == ib)
            ++m_FrameStats.ConsecutiveIndexBufferRepeats;
        m_FrameStats.LastIndexBuffer = ib;
        m_FrameStats.HasLastIndexBuffer = TRUE;
    }
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    encoder->SetVertexBuffer(0, vb, baseVertex, vertexCount);
    if (collectStats)
        ++m_FrameStats.VertexBufferSets;
    if (ib) {
        encoder->SetIndexBuffer(ib, startIndex, indexCount);
        if (collectStats)
            ++m_FrameStats.IndexBufferSets;
    }
    if (statsTiming)
        m_FrameStats.BufferBindUs += CKRenderPerfElapsedUs(statsStart);

    // Bind textures
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    BindTextures(encoder);
    if (statsTiming)
        m_FrameStats.TextureUs += CKRenderPerfElapsedUs(statsStart);

    // Submit
    float depth = ComputeDepthKey();
    if (statsTiming)
        statsStart = CKRenderPerfNow();
    encoder->Submit(view, program, *(CKDWORD *)&depth, SubmitDiscardFlags());
    if (statsTiming)
        m_FrameStats.SubmitUs += CKRenderPerfElapsedUs(statsStart);
    if (collectStats)
        ++m_FrameStats.SubmittedDraws;
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
        stateDesc.VS.SetTextureTransformFlags(stage, transformFlags);
        if (!positionT) {
            const CKDWORD texgen = (packedTexcoord >> 16) & 0xFFFFu;
            const bool hasTransform = transformFlags != 0;
            stateDesc.VS.SetTexGen(stage, texgen, hasTransform);
        }
    }

    CKBOOL lighting = m_DrawStateCache.GetRenderState(VXRENDERSTATE_LIGHTING);
    CKBOOL specular = m_DrawStateCache.GetRenderState(VXRENDERSTATE_SPECULARENABLE);
    CKBOOL normalize = m_DrawStateCache.GetRenderState(VXRENDERSTATE_NORMALIZENORMALS);

    stateDesc.VS.SetLightingEnabled(!positionT && lighting && stateDesc.VS.GetHasNormal());
    m_CurrentLightingEnabled = stateDesc.VS.GetLightingEnabled();
    stateDesc.VS.SetSpecularEnabled(specular != 0);
    stateDesc.VS.SetNormalizeNormals(normalize != 0);
    stateDesc.VS.SetLocalViewer(m_DrawStateCache.GetRenderState(VXRENDERSTATE_LOCALVIEWER) != 0);
    stateDesc.VS.SetLightCount(stateDesc.VS.GetLightingEnabled() ? m_ActiveLightCount : 0);

    const CKFFVertexBlendState vertexBlend = CKFFResolveVertexBlendState(
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_VERTEXBLEND),
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE) != 0,
        positionT ? 0 : formatFlags);
    stateDesc.VS.SetVertexBlendMode(vertexBlend.Mode);
    stateDesc.VS.SetVertexBlendIndexed(vertexBlend.Indexed != 0);
    stateDesc.VS.SetVertexBlendCount(vertexBlend.Count);

    const CKBOOL colorVertex = m_DrawStateCache.GetRenderState(VXRENDERSTATE_COLORVERTEX);
    const CKDWORD diffuseSource = CKFFResolveMaterialSource(
        stateDesc.VS.GetLightingEnabled(), colorVertex,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_DIFFUSEFROMVERTEX),
        dpFlags, CKRST_DP_DIFFUSE, CKFF_MS_COLOR0);
    const CKDWORD ambientSource = CKFFResolveMaterialSource(
        stateDesc.VS.GetLightingEnabled(), colorVertex,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_AMBIENTFROMVERTEX),
        dpFlags, CKRST_DP_DIFFUSE, CKFF_MS_COLOR0);
    const CKDWORD specularSource = CKFFResolveMaterialSource(
        stateDesc.VS.GetLightingEnabled(), colorVertex,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_SPECULARFROMVERTEX),
        dpFlags, CKRST_DP_SPECULAR, CKFF_MS_COLOR1);
    const CKDWORD emissiveSource = CKFFResolveMaterialSource(
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
        const CKDWORD colorOp = CKFFResolveStageColorOp(m_StageStates[stage], stageActive, hasTexture);
        const CKDWORD alphaOp = CKFFResolveStageAlphaOp(m_StageStates[stage], stageActive, hasTexture);
        stateDesc.FS.SetStageColorOp(stage, colorOp);
        stateDesc.FS.SetStageColorArg0(stage, CKFFResolveStageColorArg0(m_StageStates[stage]));
        stateDesc.FS.SetStageColorArg1(stage, CKFFBaseTextureArg(CKFFResolveStageColorArg1(m_StageStates[stage], hasTexture)));
        stateDesc.FS.SetStageColorArg2(stage, CKFFBaseTextureArg(CKFFResolveStageColorArg2(m_StageStates[stage])));
        stateDesc.FS.SetStageAlphaOp(stage, alphaOp);
        stateDesc.FS.SetStageAlphaArg0(stage, CKFFResolveStageAlphaArg0(m_StageStates[stage]));
        stateDesc.FS.SetStageAlphaArg1(stage, CKFFBaseTextureArg(CKFFResolveStageAlphaArg1(m_StageStates[stage], hasTexture)));
        stateDesc.FS.SetStageAlphaArg2(stage, CKFFBaseTextureArg(CKFFResolveStageAlphaArg2(m_StageStates[stage])));
        stateDesc.FS.SetStageResultIsTemp(stage, CKFFBaseTextureArg(CKFFResolveStageResultArg(m_StageStates[stage])) == CKRST_TA_TEMP);
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
    stateDesc.FS.SetClipEnabled(m_DrawStateCache.GetRenderState(VXRENDERSTATE_CLIPPLANEENABLE) != 0);

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

void CKFixedFunctionPipeline::SetCurrentProgramBinding(const CKFFShaderKey &shaderKey, const CKFFProgramBinding &binding) {
    m_CurrentShaderKey = shaderKey;
    m_CurrentProgramBinding = binding;
    m_CurrentSpecializationInfo = binding.Specialization;
}

CKDWORD CKFixedFunctionPipeline::CurrentTextureMatrixUploadCount() const {
    CKDWORD count = 0;
    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const CKDWORD flags = m_StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS];
        const CKDWORD componentCount = flags & 0xFFu;
        if (componentCount > 1 && componentCount <= 4)
            count = stage + 1;
    }
    return count;
}

bool CKFixedFunctionPipeline::CurrentShaderUsesBumpEnv() const {
    const CKDWORD lastStage = m_CurrentShaderKey.FS.LastActiveTextureStage;
    for (CKDWORD stage = 0; stage <= lastStage && stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const CKDWORD op = m_CurrentShaderKey.FS.Stages[stage].ColorOp;
        if (op == CKRST_TOP_BUMPENVMAP || op == CKRST_TOP_BUMPENVMAPLUMINANCE)
            return true;
    }
    return false;
}

bool CKFixedFunctionPipeline::CurrentShaderUsesTexFactor() const {
    const CKDWORD lastStage = m_CurrentShaderKey.FS.LastActiveTextureStage;
    for (CKDWORD stage = 0; stage <= lastStage && stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const CKFFShaderKeyFSStage &s = m_CurrentShaderKey.FS.Stages[stage];
        if (s.ColorOp == CKRST_TOP_BLENDFACTORALPHA || s.AlphaOp == CKRST_TOP_BLENDFACTORALPHA)
            return true;
        const CKDWORD args[] = { s.ColorArg0, s.ColorArg1, s.ColorArg2, s.AlphaArg0, s.AlphaArg1, s.AlphaArg2 };
        for (CKDWORD arg : args) {
            if ((arg & ~(0x10u | 0x20u)) == CKRST_TA_TFACTOR)
                return true;
        }
    }
    return false;
}

bool CKFixedFunctionPipeline::CurrentShaderUsesMaterialUniform() const {
    if (m_CurrentShaderKey.VS.GetHasPositionT())
        return false;

    if (!m_CurrentProgramBinding.FullSpecialized)
        return true;

    const uint64_t bits = m_CurrentShaderKey.VS.Bits;
    const CKDWORD diffuseSource = (CKDWORD)((bits >> 25) & 3u);
    if (diffuseSource == CKFF_MS_MATERIAL)
        return true;

    const bool lightingEnabled = (bits & (1ull << 13)) != 0;
    if (!lightingEnabled)
        return false;

    const CKDWORD ambientSource = (CKDWORD)((bits >> 27) & 3u);
    const CKDWORD specularSource = (CKDWORD)((bits >> 29) & 3u);
    const CKDWORD emissiveSource = (CKDWORD)((bits >> 31) & 3u);
    if (ambientSource == CKFF_MS_MATERIAL ||
        specularSource == CKFF_MS_MATERIAL ||
        emissiveSource == CKFF_MS_MATERIAL) {
        return true;
    }

    return true; // Lighting still reads u_ffDrawParams[4].x for specular power.
}

bool CKFixedFunctionPipeline::CurrentShaderUsesViewSpaceUniforms() const {
    if (m_CurrentShaderKey.VS.GetHasPositionT())
        return false;

    if (!m_CurrentProgramBinding.FullSpecialized)
        return true;

    const uint64_t bits = m_CurrentShaderKey.VS.Bits;
    if ((bits & (1ull << 13)) != 0)
        return true;

    if (m_CurrentShaderKey.FS.VertexFogMode != 0)
        return true;

    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        if ((m_CurrentShaderKey.VS.TexGen[stage] & 7u) != 0)
            return true;
    }

    return false;
}

void CKFixedFunctionPipeline::UploadUniforms(CKRasterizerEncoder *encoder) {
    if (!encoder) return;

    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();
    const bool fullSpecialized = m_CurrentProgramBinding.FullSpecialized;
    const bool positionT = m_CurrentShaderKey.VS.GetHasPositionT();

    VxMatrix modelView;
    VxMatrix normalMatrix;
    VxMatrix viewProj;
    VxMatrix modelViewProj;
    if (!positionT) {
        const bool viewSpaceUniforms = CurrentShaderUsesViewSpaceUniforms();
        if (viewSpaceUniforms) {
            Vx3DMultiplyMatrix4(modelView, m_View, m_World);
            Vx3DInverseMatrix(normalMatrix, modelView);
            Vx3DTransposeMatrix(normalMatrix, normalMatrix);
        }
        Vx3DMultiplyMatrix4(viewProj, m_Projection, m_View);
        Vx3DMultiplyMatrix4(modelViewProj, viewProj, m_World);
        VxMatrix matrices[4];
        matrices[0] = modelViewProj;
        matrices[1] = m_World;
        matrices[2] = modelView;
        matrices[3] = normalMatrix;
        UploadUniform(encoder, u.u_ffMatrices, matrices, viewSpaceUniforms ? 4 : 2);
    }
    const CKDWORD texMatrixCount = CurrentTextureMatrixUploadCount();
    if (texMatrixCount > 0)
        UploadUniform(encoder, u.u_texMatrix, m_TexMatrix, texMatrixCount);

    // bgfx uniform bindings are draw state. Since draws submit with
    // CKRST_DISCARD_ALL, upload the current fixed-function constants for every
    // draw instead of relying on dirty flags across submissions.
    int packed = 0;
    CKFFLightData viewLights[CKFF_MAX_LIGHTS];
    const bool shaderUsesLighting = !positionT && (!fullSpecialized || ((m_CurrentShaderKey.VS.Bits & (1ull << 13)) != 0));
    if (shaderUsesLighting) {
        packed = CKFFPackViewLights(m_Lights, m_LightEnabled, m_ActiveLightCount,
                                    m_CurrentLightingEnabled ? TRUE : FALSE, m_View, viewLights);

        if (packed > 1) {
            UploadUniform(encoder, u.u_lights, viewLights, packed * 7);
        }
    }

    float drawParams[19][4];
    memset(drawParams, 0, sizeof(drawParams));
    memcpy(drawParams[0], m_Material.Diffuse, sizeof(drawParams[0]));
    memcpy(drawParams[1], m_Material.Ambient, sizeof(drawParams[1]));
    memcpy(drawParams[2], m_Material.Specular, sizeof(drawParams[2]));
    memcpy(drawParams[3], m_Material.Emissive, sizeof(drawParams[3]));
    drawParams[4][0] = m_Material.Power;
    memcpy(drawParams[5], m_MaterialSource, sizeof(drawParams[5]));
    if (shaderUsesLighting) {
        CKDWORD ambientColor = m_DrawStateCache.GetRenderState(VXRENDERSTATE_AMBIENT);
        float ambientColorF[4];
        CKFFPackColorARGB(ambientColor, ambientColorF);
        drawParams[6][0] = m_CurrentLightingEnabled ? (float)packed : -1.0f;
        drawParams[6][1] = ambientColorF[0];
        drawParams[6][2] = ambientColorF[1];
        drawParams[6][3] = ambientColorF[2];
        drawParams[7][0] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_LOCALVIEWER) ? 1.0f : 0.0f;
        drawParams[7][1] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_NORMALIZENORMALS) ? 1.0f : 0.0f;
        drawParams[7][2] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_RANGEFOGENABLE) ? 1.0f : 0.0f;
        drawParams[7][3] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGPIXELMODE) ? (float)m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGPIXELMODE) : 0.0f;
        if (packed == 1) {
            memcpy(drawParams[12], viewLights[0].Position, sizeof(drawParams[12]));
            memcpy(drawParams[13], viewLights[0].Direction, sizeof(drawParams[13]));
            memcpy(drawParams[14], viewLights[0].Diffuse, sizeof(drawParams[14]));
            memcpy(drawParams[15], viewLights[0].Specular, sizeof(drawParams[15]));
            memcpy(drawParams[16], viewLights[0].Ambient, sizeof(drawParams[16]));
            memcpy(drawParams[17], viewLights[0].Attenuation, sizeof(drawParams[17]));
            memcpy(drawParams[18], viewLights[0].SpotParams, sizeof(drawParams[18]));
            drawParams[7][3] = 1.0f;
        }
    }
    const bool shaderUsesVertexParams = !positionT && (!fullSpecialized || shaderUsesLighting || CurrentShaderUsesMaterialUniform());
    CKDWORD drawParamCount = shaderUsesVertexParams ? (shaderUsesLighting ? (packed == 1 ? 19 : 8) : 6) : 0;

    const bool fogEnabled = m_CurrentShaderKey.FS.FogEnable;
    drawParams[8][0] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAREF) / 255.0f;
    drawParams[8][1] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHATESTENABLE)
        ? (float)m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAFUNC)
        : 0.0f;
    drawParams[8][2] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_SPECULARENABLE) ? 1.0f : 0.0f;
    drawParams[8][3] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGPIXELMODE) ? (float)m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGPIXELMODE) : 0.0f;

    CKDWORD tf = m_DrawStateCache.GetRenderState(VXRENDERSTATE_TEXTUREFACTOR);
    CKFFPackColorARGB(tf, drawParams[9]);

    drawParams[10][0] = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_FOGSTART, 0.0f);
    drawParams[10][1] = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_FOGEND, 1.0f);
    drawParams[10][2] = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_FOGDENSITY, 1.0f);
    drawParams[10][3] = fogEnabled ? (float)m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGVERTEXMODE) : 0.0f;

    CKDWORD fogColor = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGCOLOR);
    CKFFPackColorARGB(fogColor, drawParams[11]);

    CKDWORD fragmentParamCount = 0;
    if (!fullSpecialized) {
        fragmentParamCount = 4;
    } else if (fogEnabled) {
        fragmentParamCount = 4;
    } else if (CurrentShaderUsesTexFactor()) {
        fragmentParamCount = 2;
    } else if (m_CurrentShaderKey.FS.AlphaTestEnable) {
        fragmentParamCount = 1;
    }
    if (fragmentParamCount > 0)
        drawParamCount = std::max<CKDWORD>(drawParamCount, 8 + fragmentParamCount);
    if (drawParamCount > 0)
        UploadUniform(encoder, u.u_ffDrawParams, drawParams, drawParamCount);

    if (CurrentShaderUsesBumpEnv()) {
        float bumpEnv[2][4] = {};
        for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
            const CKDWORD op = m_StageStates[stage][CKRST_TSS_OP];
            if (op == CKRST_TOP_BUMPENVMAP || op == CKRST_TOP_BUMPENVMAPLUMINANCE) {
                CKFFPackBumpEnvUniform(m_StageStates[stage], bumpEnv);
                break;
            }
        }
        UploadUniform(encoder, u.u_bumpEnv, bumpEnv, 2);
    }

    if (positionT)
        UploadUniform(encoder, u.u_viewport, m_Viewport, 1);

    if (!fullSpecialized) {
        CKFFStageParamsUniform stageParams;
        CKFFPackStageParams(m_StageStates, m_TextureHandles, m_CurrentActiveTextureCount, stageParams);
        UploadUniform(encoder, u.u_stageParams, stageParams.Values, CKFF_MAX_TEXTURE_STAGES * 4);

        CKFFSpecUniform ffSpec;
        CKFFPackSpecializationDwords(m_CurrentProgramBinding.Specialization, ffSpec);
        UploadUniform(encoder, u.u_ffSpec, ffSpec.Values, CKFFSpecializationInfo::MaxSpecDwords);
    }

    CKFFClipPlaneUniform clip;
    const CKDWORD clipMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_CLIPPLANEENABLE);
    if (!fullSpecialized || m_CurrentShaderKey.FS.ClipEnable) {
        if (clipMask != 0) {
            CKFFPackClipPlaneUniforms(m_UserClipPlanes, clipMask, clip);
            UploadUniform(encoder, u.u_clipPlanes, clip.Planes, 6);
        } else {
            memset(&clip, 0, sizeof(clip));
        }
        UploadUniform(encoder, u.u_clipParams, clip.Params, 1);
    }

    m_DirtyFlags = 0;
}

void CKFixedFunctionPipeline::UploadUniform(CKRasterizerEncoder *encoder, CKDWORD uniform, const void *data, CKDWORD count) {
    if (!encoder)
        return;
    encoder->SetUniform(uniform, data, count);
    if (m_DiagnosticConfig.StatsEnabled || m_DiagnosticConfig.UniformHistEnabled) {
        ++m_FrameStats.UniformSets;
        m_FrameStats.UniformVec4s += count;
    }
    CKDWORD slot = m_DiagnosticConfig.UniformHistEnabled
        ? CKFFUniformDebugSlot(m_ShaderCache.GetUniforms(), uniform)
        : 64;
    if (slot < 64) {
        ++m_FrameStats.UniformHandleSets[slot];
        m_FrameStats.UniformHandleVec4s[slot] += count;
    }
}

void CKFixedFunctionPipeline::BindTextures(CKRasterizerEncoder *encoder) {
    if (!encoder) return;
    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();

    CKDWORD activeCount = (CKDWORD)m_CurrentActiveTextureCount;
    if (activeCount > CKFF_MAX_TEXTURE_STAGES)
        activeCount = CKFF_MAX_TEXTURE_STAGES;
    CKDWORD desiredTextures[CKFF_MAX_TEXTURE_STAGES] = {};
    CKSamplerDesc desiredSamplers[CKFF_MAX_TEXTURE_STAGES] = {};
    for (CKDWORD i = 0; i < CKFF_MAX_TEXTURE_STAGES; ++i) {
        desiredTextures[i] = (i < activeCount) ? m_TextureHandles[i] : 0;
        desiredSamplers[i] = BuildSamplerDesc((int)i);
    }
    const bool collectStats = m_DiagnosticConfig.StatsEnabled || m_DiagnosticConfig.UniformHistEnabled;
    if (collectStats) {
        if (m_FrameStats.HasLastTextureSet &&
            CKFFTextureSetEquals(m_FrameStats.LastActiveTextureCount, m_FrameStats.LastTextureHandles,
                                 activeCount, desiredTextures))
            ++m_FrameStats.ConsecutiveTextureSetRepeats;
        m_FrameStats.LastActiveTextureCount = activeCount;
        memcpy(m_FrameStats.LastTextureHandles, desiredTextures, sizeof(desiredTextures));
        m_FrameStats.HasLastTextureSet = TRUE;
    }

    for (CKDWORD i = 0; i < activeCount; ++i) {
        const CKDWORD texture = desiredTextures[i];
        if (texture == 0)
            continue;
        CKSamplerDesc sampler = desiredSamplers[i];
        encoder->SetTexture(i, u.s_texture[i], texture, &sampler);
        if (collectStats)
            ++m_FrameStats.TextureBinds;
    }
}

CKDWORD CKFixedFunctionPipeline::SubmitDiscardFlags() const {
    return CKRST_DISCARD_ALL;
}

void CKFixedFunctionPipeline::LogAndResetFrameStats() {
    const bool collectStats = m_DiagnosticConfig.StatsEnabled || m_DiagnosticConfig.UniformHistEnabled;
    if (!collectStats)
        return;

    m_FrameStats.DrawStateCacheHits = m_DrawStateCache.GetBuildCacheHits();
    m_FrameStats.DrawStateRebuilds = m_DrawStateCache.GetBuildRebuilds();
    if (m_DiagnosticConfig.StatsEnabled && m_FrameStats.FrameIndex > 0 &&
        (m_DiagnosticConfig.StatsInterval == 1 || (m_FrameStats.FrameIndex % (CKDWORD)m_DiagnosticConfig.StatsInterval) == 0)) {
        const double vec4PerDraw = m_FrameStats.SubmittedDraws > 0
            ? (double)m_FrameStats.UniformVec4s / (double)m_FrameStats.SubmittedDraws
            : 0.0;
        const double uniformsPerDraw = m_FrameStats.SubmittedDraws > 0
            ? (double)m_FrameStats.UniformSets / (double)m_FrameStats.SubmittedDraws
            : 0.0;
        const double texBindsPerDraw = m_FrameStats.SubmittedDraws > 0
            ? (double)m_FrameStats.TextureBinds / (double)m_FrameStats.SubmittedDraws
            : 0.0;
        CK_LOG_FMT("FFPStats",
                   "frame=%u sw=%u hw=%u submitted=%u prepareFail=%u programMiss=%u uniforms=%u uniformsPerDraw=%.2f vec4=%u vec4PerDraw=%.2f texBinds=%u texBindsPerDraw=%.2f layouts=%u vbSets=%u ibSets=%u transforms=%u repeatProgram=%u repeatState=%u repeatTexSet=%u repeatVB=%u repeatIB=%u repeatWorld=%u drawStateCacheHits=%u drawStateRebuilds=%u transientVB=%u transientIB=%u prepareUs=%.1f stateUs=%.1f programUs=%.1f uniformUs=%.1f textureUs=%.1f transformUs=%.1f drawStateBuildUs=%.1f encoderStateUs=%.1f stencilUs=%.1f layoutUs=%.1f bufferBindUs=%.1f submitUs=%.1f",
                   m_FrameStats.FrameIndex,
                   m_FrameStats.SoftwareDraws,
                   m_FrameStats.HardwareDraws,
                   m_FrameStats.SubmittedDraws,
                   m_FrameStats.PrepareFailures,
                   m_FrameStats.ProgramMisses,
                   m_FrameStats.UniformSets,
                   uniformsPerDraw,
                   m_FrameStats.UniformVec4s,
                   vec4PerDraw,
                   m_FrameStats.TextureBinds,
                   texBindsPerDraw,
                   m_FrameStats.VertexLayoutSets,
                   m_FrameStats.VertexBufferSets,
                   m_FrameStats.IndexBufferSets,
                   m_FrameStats.TransformSets,
                   m_FrameStats.ConsecutiveProgramRepeats,
                   m_FrameStats.ConsecutiveDrawStateRepeats,
                   m_FrameStats.ConsecutiveTextureSetRepeats,
                   m_FrameStats.ConsecutiveVertexBufferRepeats,
                   m_FrameStats.ConsecutiveIndexBufferRepeats,
                   m_FrameStats.ConsecutiveWorldMatrixRepeats,
                   m_FrameStats.DrawStateCacheHits,
                   m_FrameStats.DrawStateRebuilds,
                   m_FrameStats.TransientVertexBytes,
                   m_FrameStats.TransientIndexBytes,
                   m_FrameStats.PrepareUs,
                   m_FrameStats.StateUs,
                   m_FrameStats.ProgramUs,
                   m_FrameStats.UniformUs,
                   m_FrameStats.TextureUs,
                   m_FrameStats.TransformUs,
                   m_FrameStats.DrawStateBuildUs,
                   m_FrameStats.EncoderStateUs,
                   m_FrameStats.StencilUs,
                   m_FrameStats.LayoutUs,
                   m_FrameStats.BufferBindUs,
                   m_FrameStats.SubmitUs);
        if (m_DiagnosticConfig.UniformHistEnabled) {
            const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();
            for (CKDWORD slot = 0; slot < 64; ++slot) {
                if (m_FrameStats.UniformHandleSets[slot] == 0)
                    continue;
                CK_LOG_FMT("FFPUniformHist",
                           "frame=%u uniform=%u name=%s sets=%u vec4=%u",
                           m_FrameStats.FrameIndex,
                           slot,
                           CKFFUniformDebugName(u, slot),
                           m_FrameStats.UniformHandleSets[slot],
                           m_FrameStats.UniformHandleVec4s[slot]);
            }
        }
    }

    CKDWORD nextFrame = m_FrameStats.FrameIndex + 1;
    m_DrawStateCache.ResetBuildStats();
    memset(&m_FrameStats, 0, sizeof(m_FrameStats));
    m_FrameStats.FrameIndex = nextFrame;
}

CKSamplerDesc CKFixedFunctionPipeline::BuildSamplerDesc(int stage) const {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return CKFFBuildSamplerDesc(nullptr);
    return CKFFBuildSamplerDesc(m_StageStates[stage]);
}

float CKFixedFunctionPipeline::ComputeDepthKey() const {
    // Depth key = distance from camera (view-space Z of the world origin)
    float z = m_World[3][0] * m_View[0][2] +
              m_World[3][1] * m_View[1][2] +
              m_World[3][2] * m_View[2][2] +
              m_View[3][2];
    return z;
}
