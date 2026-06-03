#include "CKFixedFunctionPipeline.h"
#include "CKRasterizer.h"
#include "CKFFUniformState.h"
#include "CKFFShaderABI.h"
#include "CKFFStateResolver.h"
#include "CKDebugLogger.h"
#include "CKRenderSettings.h"
#include "CKRenderPerfStats.h"
#include "CKRenderFrameCostStats.h"

#include <cmath>
#include <cstring>

static CKBYTE CKFFTexcoordComponentCount(CKDWORD count) {
    if (count < 1 || count > 4)
        return 2;
    return (CKBYTE)count;
}

static CKBOOL CKFFRenderStateAffectsProgram(VXRENDERSTATETYPE state)
{
    switch (state) {
    case VXRENDERSTATE_LIGHTING:
    case VXRENDERSTATE_SPECULARENABLE:
    case VXRENDERSTATE_NORMALIZENORMALS:
    case VXRENDERSTATE_LOCALVIEWER:
    case VXRENDERSTATE_VERTEXBLEND:
    case VXRENDERSTATE_INDEXVBLENDENABLE:
    case VXRENDERSTATE_COLORVERTEX:
    case VXRENDERSTATE_DIFFUSEFROMVERTEX:
    case VXRENDERSTATE_AMBIENTFROMVERTEX:
    case VXRENDERSTATE_SPECULARFROMVERTEX:
    case VXRENDERSTATE_EMISSIVEFROMVERTEX:
    case VXRENDERSTATE_FOGENABLE:
    case VXRENDERSTATE_FOGVERTEXMODE:
    case VXRENDERSTATE_FOGPIXELMODE:
    case VXRENDERSTATE_RANGEFOGENABLE:
    case VXRENDERSTATE_SHADEMODE:
    case VXRENDERSTATE_ALPHATESTENABLE:
    case VXRENDERSTATE_ALPHAFUNC:
    case VXRENDERSTATE_CLIPPLANEENABLE:
        return TRUE;
    default:
        return FALSE;
    }
}

static CKFFShaderKey CKFFBuildCurrentShaderKey(const CKFFPreparedState *prepared)
{
    if (!prepared)
        return CKFFShaderKey();
    return CKFFBuildShaderKey(prepared->StateDesc, prepared->TextureBoundMask);
}

CKFixedFunctionPipeline::CKFixedFunctionPipeline()
    : m_Context(nullptr),
      m_DisableTextureFiltering(FALSE), m_DisableMipmaps(FALSE),
      m_ForceAnisotropicFiltering(FALSE),
#if CKRE_ENABLE_FFP_DIAGNOSTICS
      m_TextureBinder(m_State, m_ShaderCache, m_Probes),
      m_UniformEmitter(m_State, m_DrawStateCache, m_ShaderCache, m_Probes),
#else
      m_TextureBinder(m_State, m_ShaderCache),
      m_UniformEmitter(m_State, m_DrawStateCache, m_ShaderCache),
#endif
      m_OpaquePackets() {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const CKRenderFFPStatsConfig &settings = CKRenderDiagnosticsSettings().FFPStats;
    m_Probes.Config.StatsEnabled = settings.Enabled;
    m_Probes.Config.UniformHistEnabled = settings.UniformHistogram;
    m_Probes.Config.StatsInterval = settings.Interval;
#endif
    m_OpaquePackets.SetSortingEnabled(CKRenderFFPSettings().GetBool("SortOpaqueObjects", false) ? TRUE : FALSE);
    m_OpaquePackets.SetInstancingEnabled(CKRenderFFPSettings().GetBool("InstanceOpaqueObjects", true) ? TRUE : FALSE);

    m_State.Reset();
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    memset(&m_Probes.Stats, 0, sizeof(m_Probes.Stats));
#endif
}

#if !CKRE_ENABLE_FFP_DIAGNOSTICS
const CKFFFrameStats &CKFixedFunctionPipeline::GetFrameStats() const {
    static const CKFFFrameStats s_EmptyStats = {};
    return s_EmptyStats;
}
#endif

CKFixedFunctionPipeline::~CKFixedFunctionPipeline() {
    Shutdown();
}

void CKFixedFunctionPipeline::Init(CKRasterizerContext *ctx) {
    m_Context = ctx;
    m_ShaderCache.Init(ctx);
    m_DrawStateCache.Reset();
    m_VertexLayoutCache.Init(ctx);
    m_OpaquePackets.SetInstanceLayout(m_VertexLayoutCache.GetLayout(CKFF_VF_TEXCOORD0 |
                                                                    CKFF_VF_TEXCOORD1 |
                                                                    CKFF_VF_TEXCOORD2 |
                                                                    CKFF_VF_TEXCOORD3));
    m_TransientGeometry.Init(ctx, &m_VertexLayoutCache);
    m_RenderPipeline.Init(ctx);
    m_State.DirtyFlags = CKFF_DIRTY_ALL;
    m_State.MarkViewProjectionDirty();
    MarkPacketProgramDirty();
    m_OpaquePackets.ClearRenderPackets();
    m_OpaquePackets.ResetRenderPacketFrameState(*this);
}

void CKFixedFunctionPipeline::Shutdown() {
    m_OpaquePackets.ClearRenderPackets();
    m_TransientGeometry.Shutdown();
    m_VertexLayoutCache.Shutdown();
    m_OpaquePackets.SetInstanceLayout(0);
    m_ShaderCache.Shutdown();
    m_RenderPipeline.Shutdown();
    m_Context = nullptr;
}

void CKFixedFunctionPipeline::SetRenderOptions(CKBOOL DisableTextureFiltering, CKBOOL DisableMipmaps,
                                               CKBOOL ForceAnisotropicFiltering) {
    if (m_DisableTextureFiltering == DisableTextureFiltering &&
        m_DisableMipmaps == DisableMipmaps &&
        m_ForceAnisotropicFiltering == ForceAnisotropicFiltering)
        return;
    m_DisableTextureFiltering = DisableTextureFiltering;
    m_DisableMipmaps = DisableMipmaps;
    m_ForceAnisotropicFiltering = ForceAnisotropicFiltering;
    m_TextureBinder.SetRenderOptions(DisableTextureFiltering, DisableMipmaps, ForceAnisotropicFiltering);
}

void CKFixedFunctionPipeline::SetAlphaTestPrecision(CKDWORD precision) {
    precision &= 0xFu;
    if (m_State.AlphaTestPrecision == precision)
        return;
    m_State.AlphaTestPrecision = precision;
    m_State.DirtyFlags |= CKFF_DIRTY_ALPHATEST;
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

CKDWORD CKFixedFunctionPipeline::GetAlphaTestPrecision() const {
    return m_State.AlphaTestPrecision;
}

void CKFixedFunctionPipeline::SetVertexBlendMatrix(CKDWORD index, const VxMatrix &matrix) {
    if (index >= CKFF_VERTEX_BLEND_MATRIX_COUNT)
        return;
    m_State.VertexBlendMatrices[index] = matrix;
    m_State.VertexBlendMatrixSet[index] = TRUE;
    m_State.DirtyFlags |= CKFF_DIRTY_MATRICES;
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::ResetVertexBlendMatrices() {
    for (int i = 0; i < CKFF_VERTEX_BLEND_MATRIX_COUNT; ++i) {
        Vx3DMatrixIdentity(m_State.VertexBlendMatrices[i]);
        m_State.VertexBlendMatrixSet[i] = FALSE;
    }
    m_State.DirtyFlags |= CKFF_DIRTY_MATRICES;
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetTexcoordComponentCount(CKDWORD stage, CKDWORD count) {
    if (stage >= CKFF_MAX_TEXTURE_STAGES)
        return;
    CKBYTE componentCount = CKFFTexcoordComponentCount(count);
    if (m_State.TexcoordComponentCounts[stage] == componentCount)
        return;
    m_State.TexcoordComponentCounts[stage] = componentCount;
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM);
}

void CKFixedFunctionPipeline::ResetTexcoordComponentCounts() {
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        m_State.TexcoordComponentCounts[stage] = 2;
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM);
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

CKFFOpaquePacketGuard::CKFFOpaquePacketGuard(CKFixedFunctionPipeline &pipeline, CKBOOL active)
    : m_Pipeline(active ? &pipeline : nullptr),
      m_SavedAllowed(active ? pipeline.GetOpaqueRenderPacketsAllowed() : TRUE) {
    if (m_Pipeline) {
        m_Pipeline->FlushOpaqueRenderPackets(nullptr, FALSE, FALSE);
        m_Pipeline->SetOpaqueRenderPacketsAllowed(FALSE);
    }
}

CKFFOpaquePacketGuard::~CKFFOpaquePacketGuard() {
    Restore();
}

void CKFFOpaquePacketGuard::Restore() {
    if (!m_Pipeline)
        return;
    m_Pipeline->SetOpaqueRenderPacketsAllowed(m_SavedAllowed);
    m_Pipeline = nullptr;
}

void CKFFOpaquePacketGuard::Dismiss() {
    m_Pipeline = nullptr;
}

void CKFixedFunctionPipeline::SetOpaqueSortingEnabled(CKBOOL enabled)
{
    m_OpaquePackets.SetSortingEnabled(enabled);
    m_OpaquePackets.ResetRenderPacketFrameState(*this);
}

// ============================================================================
// State tracking
// ============================================================================

void CKFixedFunctionPipeline::MarkStaticUniformsDirty()
{
    m_OpaquePackets.MarkStaticUniformsDirty();
}

void CKFixedFunctionPipeline::MarkPacketProgramDirty()
{
    m_OpaquePackets.MarkPacketProgramDirty();
}

void CKFixedFunctionPipeline::OnFixedFunctionStateChanged(CKDWORD changeMask)
{
    if (changeMask & CKFF_CHANGE_PROGRAM)
        MarkPacketProgramDirty();
    if (changeMask & CKFF_CHANGE_STATIC_UNIFORM)
        MarkStaticUniformsDirty();
}

void CKFixedFunctionPipeline::BuildCurrentTextureBindingSet(CKFFTextureBindingSet *bindingSet,
                                                            CKDWORD activeTextureCount)
{
    m_TextureBinder.BuildBindingSet(bindingSet, activeTextureCount);
}

void CKFixedFunctionPipeline::SetRenderState(VXRENDERSTATETYPE state, CKDWORD value) {
    if (m_DrawStateCache.GetRenderState(state) == value)
        return;
    m_DrawStateCache.SetRenderState(state, value);

    switch (state) {
    case VXRENDERSTATE_FOGENABLE:
    case VXRENDERSTATE_FOGVERTEXMODE:
    case VXRENDERSTATE_FOGPIXELMODE:
    case VXRENDERSTATE_FOGSTART:
    case VXRENDERSTATE_FOGEND:
    case VXRENDERSTATE_FOGDENSITY:
    case VXRENDERSTATE_FOGCOLOR:
        m_State.DirtyFlags |= CKFF_DIRTY_FOG;
        break;
    case VXRENDERSTATE_AMBIENT:
        m_State.DirtyFlags |= CKFF_DIRTY_LIGHTS;
        break;
    case VXRENDERSTATE_TEXTUREFACTOR:
        m_State.DirtyFlags |= CKFF_DIRTY_TEXFACTOR;
        break;
    case VXRENDERSTATE_ALPHATESTENABLE:
    case VXRENDERSTATE_ALPHAFUNC:
    case VXRENDERSTATE_ALPHAREF:
        m_State.DirtyFlags |= CKFF_DIRTY_ALPHATEST;
        break;
    default:
        break;
    }
    CKDWORD changeMask = CKFF_CHANGE_STATIC_UNIFORM;
    if (CKFFRenderStateAffectsProgram(state))
        changeMask |= CKFF_CHANGE_PROGRAM;
    OnFixedFunctionStateChanged(changeMask);
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

static void ClearExplicitTextureCombineState(CKDWORD *stageState) {
    if (!stageState)
        return;

    stageState[CKRST_TSS_OP] = 0;
    stageState[CKRST_TSS_ARG1] = 0;
    stageState[CKRST_TSS_ARG2] = 0;
    stageState[CKRST_TSS_AOP] = 0;
    stageState[CKRST_TSS_AARG1] = 0;
    stageState[CKRST_TSS_AARG2] = 0;
    stageState[CKRST_TSS_COLORARG0] = 0;
    stageState[CKRST_TSS_ALPHAARG0] = 0;
    stageState[CKRST_TSS_RESULTARG0] = 0;
}

void CKFixedFunctionPipeline::ResetTextureStage(int stage) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return;

    m_State.TextureHandles[stage] = 0;
    m_State.TextureFlags[stage] = 0;
    memset(m_State.StageStates[stage], 0, sizeof(m_State.StageStates[stage]));
    m_State.StageStates[stage][CKRST_TSS_TEXCOORDINDEX] = (CKDWORD)stage;
    m_State.StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS] = CKRST_TTF_NONE;
    Vx3DMatrixIdentity(m_State.TexMatrix[stage]);
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM | CKFF_CHANGE_STATIC_UNIFORM);
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

    snapshot.Texture = m_State.TextureHandles[stage];
    snapshot.TextureFlags = m_State.TextureFlags[stage];
    memcpy(snapshot.States, m_State.StageStates[stage], sizeof(snapshot.States));
    snapshot.TextureMatrix = m_State.TexMatrix[stage];
}

void CKFixedFunctionPipeline::RestoreTextureStage(int stage, const CKFFTextureStageSnapshot &snapshot) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return;

    m_State.TextureHandles[stage] = snapshot.Texture;
    m_State.TextureFlags[stage] = snapshot.TextureFlags;
    memcpy(m_State.StageStates[stage], snapshot.States, sizeof(m_State.StageStates[stage]));
    m_State.TexMatrix[stage] = snapshot.TextureMatrix;
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM | CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type, CKDWORD value) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return;
    if ((int)type >= CKFF_MAX_TEXTURE_STAGE_STATES) return;

    if (type == CKRST_TSS_STAGEBLEND && value == 0 && stage > 0) {
        DisableTextureStagesFrom(stage);
        return;
    }

    if (m_State.StageStates[stage][(int)type] == value)
        return;

    m_State.StageStates[stage][(int)type] = value;

    if (type == CKRST_TSS_TEXTUREMAPBLEND) {
        ClearExplicitTextureCombineState(m_State.StageStates[stage]);
    } else if (type == CKRST_TSS_STAGEBLEND) {
        CKDWORD colorOp = 0;
        CKDWORD colorArg1 = 0;
        CKDWORD colorArg2 = 0;
        CKDWORD alphaOp = 0;
        CKDWORD alphaArg1 = 0;
        CKDWORD alphaArg2 = 0;
        if (CKFFStageBlendToTextureOps(value,
                                       colorOp, colorArg1, colorArg2,
                                       alphaOp, alphaArg1, alphaArg2)) {
            m_State.StageStates[stage][CKRST_TSS_OP] = colorOp;
            m_State.StageStates[stage][CKRST_TSS_ARG1] = colorArg1;
            m_State.StageStates[stage][CKRST_TSS_ARG2] = colorArg2;
            m_State.StageStates[stage][CKRST_TSS_AOP] = alphaOp;
            m_State.StageStates[stage][CKRST_TSS_AARG1] = alphaArg1;
            m_State.StageStates[stage][CKRST_TSS_AARG2] = alphaArg2;
        }
    }
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM | CKFF_CHANGE_STATIC_UNIFORM);
}

CKDWORD CKFixedFunctionPipeline::GetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type) const {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return 0;
    if ((int)type >= CKFF_MAX_TEXTURE_STAGE_STATES) return 0;
    return m_State.StageStates[stage][(int)type];
}

void CKFixedFunctionPipeline::SetViewport(const CKViewportData &viewport) {
    CKRenderFrameCostStatsAddViewportSet();

    const float w = viewport.ViewWidth > 0 ? (float)viewport.ViewWidth : 1.0f;
    const float h = viewport.ViewHeight > 0 ? (float)viewport.ViewHeight : 1.0f;
    const float x = (float)viewport.ViewX;
    const float y = (float)viewport.ViewY;

    m_State.Viewport[0] = 2.0f / w;
    m_State.Viewport[1] = -2.0f / h;
    m_State.Viewport[2] = -1.0f - (2.0f * x / w);
    m_State.Viewport[3] = 1.0f + (2.0f * y / h);
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetUserClipPlane(int index, const VxPlane &plane) {
    if (index < 0 || index >= 6)
        return;
    m_State.UserClipPlanes[index] = plane;
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetTransform(VXMATRIX_TYPE type, const VxMatrix &matrix) {
    switch (type) {
    case VXMATRIX_WORLD:
        m_State.World = matrix;
        m_State.DirtyFlags |= CKFF_DIRTY_MATRICES;
        OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
        break;
    case VXMATRIX_VIEW:
        m_State.View = matrix;
        m_State.DirtyFlags |= CKFF_DIRTY_MATRICES | CKFF_DIRTY_LIGHTS;
        m_State.MarkViewProjectionDirty();
        OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
        break;
    case VXMATRIX_PROJECTION:
        m_State.Projection = matrix;
        m_State.DirtyFlags |= CKFF_DIRTY_MATRICES;
        m_State.MarkViewProjectionDirty();
        OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
        break;
    default:
        if (type >= VXMATRIX_TEXTURE0 && type <= VXMATRIX_TEXTURE7) {
            int idx = type - VXMATRIX_TEXTURE0;
            if (idx < CKFF_MAX_TEXTURE_STAGES) {
                m_State.TexMatrix[idx] = matrix;
                OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
            }
        }
        break;
    }
}

void CKFixedFunctionPipeline::ResetMaterial() {
    memset(&m_State.Material, 0, sizeof(m_State.Material));
    m_State.Material.Diffuse[0] = 1.0f;
    m_State.Material.Diffuse[1] = 1.0f;
    m_State.Material.Diffuse[2] = 1.0f;
    m_State.Material.Diffuse[3] = 1.0f;
    m_State.Material.Ambient[0] = 1.0f;
    m_State.Material.Ambient[1] = 1.0f;
    m_State.Material.Ambient[2] = 1.0f;
    m_State.Material.Ambient[3] = 1.0f;
    m_State.DirtyFlags |= CKFF_DIRTY_MATERIAL;
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetMaterial(const CKMaterialData *mat) {
    if (!mat) return;
    m_State.Material.Diffuse[0] = mat->Diffuse.r;
    m_State.Material.Diffuse[1] = mat->Diffuse.g;
    m_State.Material.Diffuse[2] = mat->Diffuse.b;
    m_State.Material.Diffuse[3] = mat->Diffuse.a;
    m_State.Material.Ambient[0] = mat->Ambient.r;
    m_State.Material.Ambient[1] = mat->Ambient.g;
    m_State.Material.Ambient[2] = mat->Ambient.b;
    m_State.Material.Ambient[3] = mat->Ambient.a;
    m_State.Material.Specular[0] = mat->Specular.r;
    m_State.Material.Specular[1] = mat->Specular.g;
    m_State.Material.Specular[2] = mat->Specular.b;
    m_State.Material.Specular[3] = mat->Specular.a;
    m_State.Material.Emissive[0] = mat->Emissive.r;
    m_State.Material.Emissive[1] = mat->Emissive.g;
    m_State.Material.Emissive[2] = mat->Emissive.b;
    m_State.Material.Emissive[3] = mat->Emissive.a;
    m_State.Material.Power = mat->SpecularPower;
    m_State.DirtyFlags |= CKFF_DIRTY_MATERIAL;
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetLight(int index, const CKLightData *light) {
    if (index < 0 || index >= CKFF_MAX_LIGHTS || !light) return;

    CKFFLightData &dst = m_State.Lights[index];

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

    m_State.DirtyFlags |= CKFF_DIRTY_LIGHTS;
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::EnableLight(int index, CKBOOL enable) {
    if (index < 0 || index >= CKFF_MAX_LIGHTS) return;
    if (m_State.LightEnabled[index] == enable)
        return;
    m_State.LightEnabled[index] = enable;

    m_State.ActiveLightCount = 0;
    for (int i = 0; i < CKFF_MAX_LIGHTS; i++) {
        if (m_State.LightEnabled[i]) m_State.ActiveLightCount++;
    }
    m_State.DirtyFlags |= CKFF_DIRTY_LIGHTS;
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM | CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetTexture(int stage, CKDWORD textureHandle) {
    SetTexture(stage, textureHandle, textureHandle != 0 ? CKRST_TEXTURE_VALID : 0);
}

void CKFixedFunctionPipeline::SetTexture(int stage, CKDWORD textureHandle, CKDWORD textureFlags) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return;
    const CKDWORD normalizedFlags = textureHandle != 0 ? textureFlags : 0;
    if (m_State.TextureHandles[stage] == textureHandle && m_State.TextureFlags[stage] == normalizedFlags)
        return;
    const CKBOOL oldHasTexture = m_State.TextureHandles[stage] != 0 ? TRUE : FALSE;
    const CKBOOL newHasTexture = textureHandle != 0 ? TRUE : FALSE;
    const CKDWORD oldStaticFlags = CKFFStaticTextureFlags(m_State.TextureFlags[stage]);
    const CKDWORD newStaticFlags = CKFFStaticTextureFlags(normalizedFlags);
    m_State.TextureHandles[stage] = textureHandle;
    m_State.TextureFlags[stage] = normalizedFlags;
    if (oldHasTexture != newHasTexture || oldStaticFlags != newStaticFlags) {
        OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM | CKFF_CHANGE_STATIC_UNIFORM);
    }
}

CKDWORD CKFixedFunctionPipeline::GetTexture(int stage) const {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return 0;
    return m_State.TextureHandles[stage];
}

void CKFixedFunctionPipeline::BeginDebugFrame() {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    m_DebugState.BeginFrame();
    LogAndResetFrameStats();
#endif
    m_OpaquePackets.ResetRenderPacketFrameState(*this);
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
    if (HasOpaqueRenderPackets())
        FlushOpaqueRenderPackets(encoder, FALSE, FALSE);
    CKFF_PROBE(m_Probes, OnSoftwareDraw());

    bool hasNormal = (data->NormalPtr != nullptr);
    bool hasUV = (data->TexCoordPtr != nullptr);
    CKDWORD formatFlags = CKVertexLayoutCache::DPFlagsToFormatFlags(data->Flags, hasNormal, hasUV, data->PositionStride);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const bool debugLogging = m_DebugState.AnyLoggingEnabled();
    const int debugDrawSerial = debugLogging ? m_DebugState.NextDrawSerial(view) : -1;
    if (debugLogging) {
        CKFFDrawDebugInfo debugInfo = {};
        debugInfo.View = view;
        debugInfo.Type = type;
        debugInfo.Indices = indices;
        debugInfo.IndexCount = indexCount;
        debugInfo.Data = data;
        debugInfo.FormatFlags = formatFlags;
        debugInfo.DrawSerial = debugDrawSerial;
        debugInfo.World = &m_State.World;
        debugInfo.ViewMatrix = &m_State.View;
        debugInfo.Projection = &m_State.Projection;
        debugInfo.Viewport = m_State.Viewport;
        m_DebugState.LogDrawPrimitiveHeader(debugInfo);
    }
#endif

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
    pointParams.World = m_State.World;
    pointParams.View = m_State.View;
    CKBOOL prepared = FALSE;
    {
        CKFF_SCOPE_TIME(m_Probes, PrepareUs);
        prepared = m_TransientGeometry.Prepare(
            encoder, type, indices, indexCount, data, wrapMode,
            m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE), &pointParams,
            m_State.TexcoordComponentCounts);
    }
    if (!prepared) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (debugLogging)
            m_DebugState.LogDrawPrimitivePrepareFailed();
#endif
        CKFF_PROBE(m_Probes, OnPrepareFailure());
        return;
    }
    CKFF_PROBE(m_Probes, OnTransientGeometry(m_TransientGeometry.GetLastVertexBytes(),
                                             m_TransientGeometry.GetLastIndexBytes()));

    // Build the fixed-function state description and select the matching program.
    const CKDWORD activeTextureCount = (CKDWORD)CKFFResolveActiveTextureCount(
        data->Flags, m_State.TextureHandles, m_State.StageStates);
    CKFFPreparedState preparedState;
    CKFFShaderKey shaderKey;
    {
        CKFF_SCOPE_TIME(m_Probes, StateUs);
        BuildCurrentPreparedState(&preparedState, data->Flags, activeTextureCount,
                                  formatFlags, m_State.TexcoordComponentCounts);
        shaderKey = CKFFBuildCurrentShaderKey(&preparedState);
    }
    CKFFProgramBinding programBinding;
    {
        CKFF_SCOPE_TIME(m_Probes, ProgramUs);
        programBinding = m_ShaderCache.GetProgram(shaderKey);
    }
    CKFFProgramContext programContext;
    CKFFInitProgramContext(&programContext, shaderKey, programBinding);
    CKDWORD program = programContext.Program;
    if (program == 0) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (debugLogging)
            m_DebugState.LogDrawPrimitiveProgramMissing();
#endif
        CKFF_PROBE(m_Probes, OnProgramMiss());
        return;
    }
    CKFF_PROBE(m_Probes, OnProgram(program));
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (debugLogging) {
        CKFFDrawDebugInfo debugInfo = {};
        debugInfo.View = view;
        debugInfo.Type = type;
        debugInfo.Indices = indices;
        debugInfo.IndexCount = indexCount;
        debugInfo.Data = data;
        debugInfo.FormatFlags = formatFlags;
        debugInfo.DrawSerial = debugDrawSerial;
        debugInfo.World = &m_State.World;
        debugInfo.ViewMatrix = &m_State.View;
        debugInfo.Projection = &m_State.Projection;
        debugInfo.Viewport = m_State.Viewport;
        debugInfo.Program = program;
        debugInfo.ActiveTextureCount = (int)preparedState.ActiveTextureCount;
        debugInfo.ActiveLightCount = m_State.ActiveLightCount;
        debugInfo.StateDesc = &preparedState.StateDesc;
        debugInfo.DrawState = &m_DrawStateCache;
        debugInfo.Stage0.ColorOp = preparedState.StateDesc.FS.GetStageColorOp(0);
        debugInfo.Stage0.ColorArg1 = CKFFResolveStageColorArg1(m_State.StageStates[0], preparedState.ActiveTextureCount > 0 && m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.ColorArg2 = CKFFResolveStageColorArg2(m_State.StageStates[0]);
        debugInfo.Stage0.AlphaOp = CKFFResolveStageAlphaOp(m_State.StageStates[0], preparedState.ActiveTextureCount > 0, m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.AlphaArg1 = CKFFResolveStageAlphaArg1(m_State.StageStates[0], preparedState.ActiveTextureCount > 0 && m_State.TextureHandles[0] != 0);
        debugInfo.Stage0.AlphaArg2 = CKFFResolveStageAlphaArg2(m_State.StageStates[0]);
        debugInfo.Stage0.Texture = m_State.TextureHandles[0];
        m_DebugState.LogDrawPrimitiveDetails(debugInfo);
    }
#endif

    CKFFTextureBindingSet textureBindingSet;
    BuildCurrentTextureBindingSet(&textureBindingSet, preparedState.ActiveTextureCount);

    // Upload uniforms
    {
        CKFF_SCOPE_TIME(m_Probes, UniformUs);
        m_UniformEmitter.UploadUniforms(encoder, &programContext, preparedState.ActiveTextureCount);
    }

    // Set world transform
    CKFF_PROBE(m_Probes, OnWorldMatrix(m_State.World));
    CKDWORD transformIdx = m_Context->AllocTransform(&m_State.World, 1);
    {
        CKFF_SCOPE_TIME(m_Probes, TransformUs);
        encoder->SetTransform(transformIdx, 1);
    }
    CKFF_PROBE(m_Probes, OnTransformSet());

    // Set draw state
    CKDrawState drawState;
    {
        CKFF_SCOPE_TIME(m_Probes, DrawStateBuildUs);
        drawState = m_DrawStateCache.BuildDrawState(
            (type == VX_TRIANGLEFAN || type == VX_TRIANGLESTRIP ||
             (type == VX_POINTLIST && m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE)))
                ? VX_TRIANGLELIST
                : type);
    }
    CKFF_PROBE(m_Probes, OnDrawState(drawState));
    const CKDWORD stencilRef = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILREF);
    const CKDWORD stencilReadMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILMASK);
    const CKDWORD stencilWriteMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK);
    {
        CKFF_SCOPE_TIME(m_Probes, EncoderStateUs);
        encoder->SetState(drawState);
    }
    {
        CKFF_SCOPE_TIME(m_Probes, StencilUs);
        encoder->SetStencilRef(stencilRef);
        encoder->SetStencilMask(stencilReadMask, stencilWriteMask);
    }

    // Bind textures
    {
        CKFF_SCOPE_TIME(m_Probes, TextureUs);
        BindTextures(encoder, &textureBindingSet);
    }

    // Submit
    float depth = ComputeDepthKey();
    {
        CKFF_SCOPE_TIME(m_Probes, SubmitUs);
        encoder->Submit(view, program, *(CKDWORD *)&depth, SubmitDiscardFlags());
        CK_FRAME_COST_ADD_PRIMITIVE_SUBMIT();
        CK_FRAME_COST_ADD_SUBMITTED_DRAW();
    }
    CKFF_PROBE(m_Probes, OnSubmittedDraw());
}

void CKFixedFunctionPipeline::DrawVertexBuffer(
    CKRasterizerEncoder *encoder, CKRenderView view,
    VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
    CKDWORD baseVertex, CKDWORD vertexCount,
    CKDWORD startIndex, CKDWORD indexCount,
    CKDWORD dpFlags, CKDWORD formatFlags,
    CKDWORD vertexLayout)
{
    m_OpaquePackets.DrawVertexBuffer(
        *this, encoder, view, type, vb, ib,
        baseVertex, vertexCount, startIndex, indexCount,
        dpFlags, formatFlags, vertexLayout);
}

// ============================================================================
// Internal methods
// ============================================================================

void CKFixedFunctionPipeline::BuildCurrentPreparedState(
    CKFFPreparedState *prepared, CKDWORD dpFlags, CKDWORD activeTextureCount,
    CKDWORD formatFlags, const CKBYTE *texcoordComponentCounts) {
    CKFFStateResolver::BuildPreparedState(m_State, m_DrawStateCache, prepared, dpFlags,
                                          activeTextureCount, formatFlags, texcoordComponentCounts);
}

CKBOOL CKFixedFunctionPipeline::BuildStaticUniformPayload(CKFFRenderPacketUniformPayload *payload,
                                                          const CKFFProgramContext *programContext,
                                                          CKDWORD activeTextureCount)
{
    return m_UniformEmitter.BuildStaticUniformPayload(payload, programContext, activeTextureCount);
}

void CKFixedFunctionPipeline::UpdateViewProjectionCache()
{
    if (!m_State.EnsureViewProjection())
        return;
    CKFF_PROBE(m_Probes, OnViewProjectionRebuild());
}

CKBOOL CKFixedFunctionPipeline::BuildPacketObjectUniforms(CKRenderPacketObjectUniforms *uniforms,
                                                          const CKFFProgramContext *programContext)
{
    UpdateViewProjectionCache();
    return m_UniformEmitter.BuildObjectUniforms(uniforms, programContext);
}

void CKFixedFunctionPipeline::BindTextures(CKRasterizerEncoder *encoder,
                                           const CKFFTextureBindingSet *bindingSet) {
    m_TextureBinder.Bind(encoder, bindingSet);
}

CKDWORD CKFixedFunctionPipeline::SubmitDiscardFlags() const {
    return CKFFSubmitDiscardFlags(m_State, m_DrawStateCache);
}

void CKFixedFunctionPipeline::LogAndResetFrameStats() {
    CKFF_PROBE(m_Probes, LogAndReset(m_DrawStateCache, m_ShaderCache.GetUniforms()));
}

CKSamplerDesc CKFixedFunctionPipeline::BuildSamplerDesc(int stage) const {
    return m_TextureBinder.BuildSamplerDesc(stage);
}

float CKFixedFunctionPipeline::ComputeDepthKey() const {
    return CKFFComputeDepthKey(m_State, m_DrawStateCache);
}

void CKFixedFunctionPipeline::FlushOpaqueRenderPackets(CKRasterizerEncoder *encoder,
                                                       CKBOOL forceDirectReplay,
                                                       CKBOOL allowAdaptiveLearning)
{
    m_OpaquePackets.FlushRenderPackets(*this, encoder, forceDirectReplay, allowAdaptiveLearning);
}
