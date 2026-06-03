#include "CKFixedFunctionPipeline.h"
#include "CKRasterizer.h"
#include "CKFFUniformState.h"
#include "CKFFShaderABI.h"
#include "CKDebugLogger.h"
#include "CKRenderSettings.h"
#include "CKRenderPerfStats.h"
#include "CKRenderFrameCostStats.h"

#include <cmath>
#include <cstring>

static CKDWORD CKFFShaderKeyVertexBlendMode(const CKFFShaderKeyVS &vs) {
    return (CKDWORD)((vs.Bits >> 35) & 3u);
}

static CKBOOL CKFFShaderKeyLightingEnabled(const CKFFShaderKeyVS &vs)
{
    return (vs.Bits & (1ull << 13)) != 0 ? TRUE : FALSE;
}

static void CKFFShaderKeyMaterialSources(const CKFFShaderKeyVS &vs, float materialSource[4])
{
    materialSource[0] = (float)((vs.Bits >> 25) & 3u);
    materialSource[1] = (float)((vs.Bits >> 27) & 3u);
    materialSource[2] = (float)((vs.Bits >> 29) & 3u);
    materialSource[3] = (float)((vs.Bits >> 31) & 3u);
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

struct CKFFUniformEmitter {
    static void EmitObjectMatrixUniforms(CKFixedFunctionPipeline *pipeline,
                                         const CKFFUniformEmissionContext *context);
    static void EmitTextureMatrixUniforms(CKFixedFunctionPipeline *pipeline,
                                          const CKFFUniformEmissionContext *context);
    static void EmitStageAndSpecUniforms(CKFixedFunctionPipeline *pipeline,
                                         const CKFFUniformEmissionContext *context);
    static void EmitClipPlaneUniforms(CKFixedFunctionPipeline *pipeline,
                                      const CKFFUniformEmissionContext *context);
};

static void CKFFInitPreparedState(CKFFPreparedState *prepared)
{
    if (!prepared)
        return;
    prepared->StateDesc = CKFFStateDesc();
    prepared->ActiveTextureCount = 0;
    prepared->PositionT = FALSE;
    prepared->LightingEnabled = FALSE;
    prepared->MaterialSource[0] = (float)CKFF_MS_MATERIAL;
    prepared->MaterialSource[1] = (float)CKFF_MS_MATERIAL;
    prepared->MaterialSource[2] = (float)CKFF_MS_MATERIAL;
    prepared->MaterialSource[3] = (float)CKFF_MS_MATERIAL;
    prepared->TextureBoundMask = 0;
}

static void CKFFInitTextureBindingSet(CKFFTextureBindingSet *set)
{
    if (!set)
        return;
    set->ActiveTextureCount = 0;
    set->Hash = 0;
    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        set->Bindings[stage].Stage = stage;
        set->Bindings[stage].Uniform = 0;
        set->Bindings[stage].Texture = 0;
        set->Bindings[stage].TextureFlags = 0;
        set->Bindings[stage].Sampler = CKSamplerDesc();
    }
}

static CKDWORD CKFFSamplerTypeFromTextureFlags(CKDWORD textureFlags)
{
    if ((textureFlags & CKRST_TEXTURE_CUBEMAP) != 0)
        return CKFF_SAMPLER_CUBE;
    if ((textureFlags & CKRST_TEXTURE_VOLUMEMAP) != 0)
        return CKFF_SAMPLER_VOLUME;
    if ((textureFlags & CKRST_TEXTURE_DEPTHSTENCIL) != 0)
        return CKFF_SAMPLER_DEPTH;
    return CKFF_SAMPLER_2D;
}

static CKDWORD CKFFTextureBindingSamplerType(CKDWORD samplerType)
{
    if (samplerType == CKFF_SAMPLER_CUBE || samplerType == CKFF_SAMPLER_VOLUME)
        return samplerType;
    return CKFF_SAMPLER_2D;
}

static CKDWORD CKFFTextureBindingUniform(const CKFFUniformHandles &uniforms,
                                         CKDWORD stage, CKDWORD samplerType)
{
    if (samplerType == CKFF_SAMPLER_CUBE)
        return uniforms.s_textureCube[stage];
    if (samplerType == CKFF_SAMPLER_VOLUME)
        return uniforms.s_textureVolume[stage];
    return uniforms.s_texture[stage];
}

static void CKFFBuildTextureBindingSet(CKFFTextureBindingSet *set,
                                       const CKFFUniformHandles &uniforms,
                                       CKDWORD activeTextureCount,
                                       const CKDWORD *textureHandles,
                                       const CKDWORD *textureFlags,
                                       const CKSamplerDesc *samplers)
{
    if (!set)
        return;
    CKFFInitTextureBindingSet(set);
    set->ActiveTextureCount = activeTextureCount;
    if (set->ActiveTextureCount > CKFF_MAX_TEXTURE_STAGES)
        set->ActiveTextureCount = CKFF_MAX_TEXTURE_STAGES;
    for (CKDWORD stage = 0; stage < set->ActiveTextureCount; ++stage) {
        const CKDWORD samplerType = CKFFTextureBindingSamplerType(
            CKFFSamplerTypeFromTextureFlags(textureFlags[stage]));
        set->Bindings[stage].Stage = CKFFSamplerBindStage(stage, samplerType);
        set->Bindings[stage].Uniform = CKFFTextureBindingUniform(uniforms, stage, samplerType);
        set->Bindings[stage].Texture = textureHandles[stage];
        set->Bindings[stage].TextureFlags = textureFlags[stage];
        set->Bindings[stage].Sampler = samplers[stage];
    }
    set->Hash = CKFFHashRenderPacketTextureSet(set->ActiveTextureCount, set->Bindings);
}

static CKFFShaderKey CKFFBuildCurrentShaderKey(const CKFFPreparedState *prepared)
{
    if (!prepared)
        return CKFFShaderKey();
    return CKFFBuildShaderKey(prepared->StateDesc, prepared->TextureBoundMask);
}

static void CKFFInitVertexBufferPacketBuildResult(CKFFVertexBufferPacketBuildResult *result)
{
    if (!result)
        return;
    result->Success = FALSE;
    result->RejectReason = CKFF_RENDER_PACKET_ELIGIBLE;
    CKFFInitProgramContext(&result->ProgramContext, CKFFShaderKey(), CKFFProgramBinding());
    CKFFInitTextureBindingSet(&result->TextureBindingSet);
    memset(&result->Packet, 0, sizeof(result->Packet));
}

CKFixedFunctionPipeline::CKFixedFunctionPipeline()
    : m_Context(nullptr),
      m_DisableTextureFiltering(FALSE), m_DisableMipmaps(FALSE),
      m_ForceAnisotropicFiltering(FALSE),
      m_OpaqueInstancingEnabled(TRUE), m_InstanceLayout(0),
      m_OpaqueSortingEnabled(FALSE), m_OpaquePacketAllowed(TRUE) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const CKRenderFFPStatsConfig &settings = CKRenderDiagnosticsSettings().FFPStats;
    m_Probes.Config.StatsEnabled = settings.Enabled;
    m_Probes.Config.UniformHistEnabled = settings.UniformHistogram;
    m_Probes.Config.StatsInterval = settings.Interval;
#endif
    m_OpaqueSortingEnabled = CKRenderFFPSettings().GetBool("SortOpaqueObjects", false) ? TRUE : FALSE;
    m_OpaqueInstancingEnabled = CKRenderFFPSettings().GetBool("InstanceOpaqueObjects", true) ? TRUE : FALSE;

    m_State.Reset();
    m_PacketProgramCacheValid = FALSE;
    m_PacketProgramCacheDPFlags = 0;
    m_PacketProgramCacheFormatFlags = 0;
    m_PacketProgramCacheActiveTextureCount = 0;
    CKFFInitPreparedState(&m_PacketProgramCachePreparedState);
    memset(&m_PacketProgramCacheContext, 0, sizeof(m_PacketProgramCacheContext));
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
    m_InstanceLayout = m_VertexLayoutCache.GetLayout(CKFF_VF_TEXCOORD0 |
                                                     CKFF_VF_TEXCOORD1 |
                                                     CKFF_VF_TEXCOORD2 |
                                                     CKFF_VF_TEXCOORD3);
    m_TransientGeometry.Init(ctx, &m_VertexLayoutCache);
    m_RenderPipeline.Init(ctx);
    m_State.DirtyFlags = CKFF_DIRTY_ALL;
    m_State.MarkViewProjectionDirty();
    MarkPacketProgramDirty();
    ClearOpaqueRenderPackets();
    ResetOpaqueRenderPacketFrameState();
}

void CKFixedFunctionPipeline::Shutdown() {
    ClearOpaqueRenderPackets();
    m_TransientGeometry.Shutdown();
    m_VertexLayoutCache.Shutdown();
    m_InstanceLayout = 0;
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

// ============================================================================
// State tracking
// ============================================================================

void CKFixedFunctionPipeline::MarkStaticUniformsDirty()
{
    m_OpaquePacketQueue.MarkStaticUniformsDirty();
}

void CKFixedFunctionPipeline::MarkPacketProgramDirty()
{
    m_PacketProgramCacheValid = FALSE;
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
    if (!bindingSet)
        return;
    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();
    CKDWORD activeCount = activeTextureCount;
    if (activeCount > CKFF_MAX_TEXTURE_STAGES)
        activeCount = CKFF_MAX_TEXTURE_STAGES;
    CKSamplerDesc samplers[CKFF_MAX_TEXTURE_STAGES];
    for (CKDWORD i = 0; i < activeCount; ++i)
        samplers[i] = BuildSamplerDesc((int)i);
    CKFFBuildTextureBindingSet(bindingSet, u, activeCount,
                               m_State.TextureHandles, m_State.TextureFlags, samplers);
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
    ResetOpaqueRenderPacketFrameState();
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
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const bool collectStats = m_Probes.Config.StatsEnabled || m_Probes.Config.UniformHistEnabled;
    if (collectStats)
        ++m_Probes.Stats.SoftwareDraws;
#endif

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
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const bool statsTiming = m_Probes.Config.StatsEnabled;
    double statsStart = 0.0;
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    if (!m_TransientGeometry.Prepare(
            encoder, type, indices, indexCount, data, wrapMode,
            m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE), &pointParams,
            m_State.TexcoordComponentCounts)) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (debugLogging)
            m_DebugState.LogDrawPrimitivePrepareFailed();
        if (collectStats)
            ++m_Probes.Stats.PrepareFailures;
#endif
        return;
    }
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_Probes.Stats.PrepareUs += CKRenderPerfElapsedUs(statsStart);
    if (collectStats) {
        m_Probes.Stats.TransientVertexBytes += m_TransientGeometry.GetLastVertexBytes();
        m_Probes.Stats.TransientIndexBytes += m_TransientGeometry.GetLastIndexBytes();
    }
#endif

    // Build the fixed-function state description and select the matching program.
    const CKDWORD activeTextureCount = (CKDWORD)CKFFResolveActiveTextureCount(
        data->Flags, m_State.TextureHandles, m_State.StageStates);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    CKFFPreparedState preparedState;
    BuildCurrentPreparedState(&preparedState, data->Flags, activeTextureCount,
                              formatFlags, m_State.TexcoordComponentCounts);
    CKFFShaderKey shaderKey = CKFFBuildCurrentShaderKey(&preparedState);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_Probes.Stats.StateUs += CKRenderPerfElapsedUs(statsStart);
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    CKFFProgramBinding programBinding = m_ShaderCache.GetProgram(shaderKey);
    CKFFProgramContext programContext;
    CKFFInitProgramContext(&programContext, shaderKey, programBinding);
    CKDWORD program = programContext.Program;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_Probes.Stats.ProgramUs += CKRenderPerfElapsedUs(statsStart);
#endif
    if (program == 0) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (debugLogging)
            m_DebugState.LogDrawPrimitiveProgramMissing();
        if (collectStats)
            ++m_Probes.Stats.ProgramMisses;
#endif
        return;
    }
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (collectStats) {
        if (m_Probes.Stats.HasLastProgram && m_Probes.Stats.LastProgram == program)
            ++m_Probes.Stats.ConsecutiveProgramRepeats;
        m_Probes.Stats.LastProgram = program;
        m_Probes.Stats.HasLastProgram = TRUE;
    }

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
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    UploadUniforms(encoder, &programContext, preparedState.ActiveTextureCount);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_Probes.Stats.UniformUs += CKRenderPerfElapsedUs(statsStart);

    // Set world transform
    if (collectStats) {
        if (m_Probes.Stats.HasLastWorldMatrix && memcmp(&m_Probes.Stats.LastWorldMatrix, &m_State.World, sizeof(VxMatrix)) == 0)
            ++m_Probes.Stats.ConsecutiveWorldMatrixRepeats;
        memcpy(&m_Probes.Stats.LastWorldMatrix, &m_State.World, sizeof(VxMatrix));
        m_Probes.Stats.HasLastWorldMatrix = TRUE;
    }
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    CKDWORD transformIdx = m_Context->AllocTransform(&m_State.World, 1);
    encoder->SetTransform(transformIdx, 1);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (collectStats)
        ++m_Probes.Stats.TransformSets;
    if (statsTiming)
        m_Probes.Stats.TransformUs += CKRenderPerfElapsedUs(statsStart);

    // Set draw state
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    CKDrawState drawState = m_DrawStateCache.BuildDrawState(
        (type == VX_TRIANGLEFAN || type == VX_TRIANGLESTRIP ||
         (type == VX_POINTLIST && m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE)))
            ? VX_TRIANGLELIST
            : type);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_Probes.Stats.DrawStateBuildUs += CKRenderPerfElapsedUs(statsStart);
    if (collectStats) {
        if (m_Probes.Stats.HasLastDrawState && CKFFDrawStateEquals(m_Probes.Stats.LastDrawState, drawState))
            ++m_Probes.Stats.ConsecutiveDrawStateRepeats;
        m_Probes.Stats.LastDrawState = drawState;
        m_Probes.Stats.HasLastDrawState = TRUE;
    }
    const CKDWORD stencilRef = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILREF);
    const CKDWORD stencilReadMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILMASK);
    const CKDWORD stencilWriteMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK);
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#else
    const CKDWORD stencilRef = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILREF);
    const CKDWORD stencilReadMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILMASK);
    const CKDWORD stencilWriteMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK);
#endif
    encoder->SetState(drawState);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_Probes.Stats.EncoderStateUs += CKRenderPerfElapsedUs(statsStart);
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    encoder->SetStencilRef(stencilRef);
    encoder->SetStencilMask(stencilReadMask, stencilWriteMask);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_Probes.Stats.StencilUs += CKRenderPerfElapsedUs(statsStart);

    // Bind textures
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    BindTextures(encoder, &textureBindingSet);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_Probes.Stats.TextureUs += CKRenderPerfElapsedUs(statsStart);
#endif

    // Submit
    float depth = ComputeDepthKey();
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    encoder->Submit(view, program, *(CKDWORD *)&depth, SubmitDiscardFlags());
    CKRenderFrameCostStatsAddPrimitiveSubmit();
    CKRenderFrameCostStatsAddSubmittedDraw();
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_Probes.Stats.SubmitUs += CKRenderPerfElapsedUs(statsStart);
    if (collectStats)
        ++m_Probes.Stats.SubmittedDraws;
#endif
}

void CKFixedFunctionPipeline::DrawVertexBuffer(
    CKRasterizerEncoder *encoder, CKRenderView view,
    VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
    CKDWORD baseVertex, CKDWORD vertexCount,
    CKDWORD startIndex, CKDWORD indexCount,
    CKDWORD dpFlags, CKDWORD formatFlags,
    CKDWORD vertexLayout)
{
    const CKDWORD packetRejectReason =
        GetOpaqueVertexBufferPacketRejectReason(view, type, vb, ib, vertexLayout);
    if (packetRejectReason == CKFF_RENDER_PACKET_ELIGIBLE) {
        CKFFVertexBufferPacketBuildResult buildResult;
        BuildVertexBufferPacket(&buildResult, encoder, view, type, vb, ib,
                                baseVertex, vertexCount,
                                startIndex, indexCount,
                                dpFlags, formatFlags,
                                vertexLayout);
        if (buildResult.Success) {
            TrackOpaqueRenderPacket(buildResult.Packet);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
            if (m_Probes.Config.StatsEnabled || m_Probes.Config.UniformHistEnabled)
                ++m_Probes.Stats.QueuedRenderPackets;
#endif
            CheckOpaqueRenderPacketAdaptiveBypass(encoder);
            return;
        }
        TrackOpaqueRenderPacketReject(buildResult.RejectReason);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (m_Probes.Config.StatsEnabled || m_Probes.Config.UniformHistEnabled)
            ++m_Probes.Stats.RenderPacketFallbacks;
#endif
    } else {
        TrackOpaqueRenderPacketReject(packetRejectReason);
    }

    if (HasOpaqueRenderPackets())
        FlushOpaqueRenderPackets(encoder, FALSE, FALSE);
    SubmitVertexBufferPacketImmediate(encoder, view, type, vb, ib,
                                      baseVertex, vertexCount,
                                      startIndex, indexCount,
                                      dpFlags, formatFlags,
                                      vertexLayout);
}

void CKFixedFunctionPipeline::SubmitVertexBufferPacketImmediate(
    CKRasterizerEncoder *encoder, CKRenderView view,
    VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
    CKDWORD baseVertex, CKDWORD vertexCount,
    CKDWORD startIndex, CKDWORD indexCount,
    CKDWORD dpFlags, CKDWORD formatFlags,
    CKDWORD vertexLayout)
{
    if (!encoder || !vb) return;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const bool collectStats = m_Probes.Config.StatsEnabled || m_Probes.Config.UniformHistEnabled;
    if (collectStats)
        ++m_Probes.Stats.HardwareDraws;

    // Build the fixed-function state description from the actual mesh vertex format.
    const bool debugLogging = m_DebugState.AnyLoggingEnabled();
    const int debugDrawSerial = debugLogging ? m_DebugState.NextDrawSerial(view) : -1;
    if (debugLogging) {
        CKFFDrawDebugInfo debugInfo = {};
        debugInfo.View = view;
        debugInfo.Type = type;
        debugInfo.World = &m_State.World;
        debugInfo.ViewMatrix = &m_State.View;
        debugInfo.Projection = &m_State.Projection;
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
    }
#endif

    const CKDWORD activeTextureCount = (CKDWORD)CKFFResolveActiveTextureCount(
        dpFlags, m_State.TextureHandles, m_State.StageStates);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const bool statsTiming = m_Probes.Config.StatsEnabled;
    double statsStart = 0.0;
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    CKFFPreparedState preparedState;
    BuildCurrentPreparedState(&preparedState, dpFlags, activeTextureCount, formatFlags);
    CKFFShaderKey shaderKey = CKFFBuildCurrentShaderKey(&preparedState);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_Probes.Stats.StateUs += CKRenderPerfElapsedUs(statsStart);
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    CKFFProgramBinding programBinding = m_ShaderCache.GetProgram(shaderKey);
    CKFFProgramContext programContext;
    CKFFInitProgramContext(&programContext, shaderKey, programBinding);
    CKDWORD program = programContext.Program;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_Probes.Stats.ProgramUs += CKRenderPerfElapsedUs(statsStart);
#endif
    if (program == 0) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (collectStats)
            ++m_Probes.Stats.ProgramMisses;
#endif
        return;
    }
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (collectStats) {
        if (m_Probes.Stats.HasLastProgram && m_Probes.Stats.LastProgram == program)
            ++m_Probes.Stats.ConsecutiveProgramRepeats;
        m_Probes.Stats.LastProgram = program;
        m_Probes.Stats.HasLastProgram = TRUE;
    }

    if (debugLogging) {
        CKFFDrawDebugInfo debugInfo = {};
        debugInfo.View = view;
        debugInfo.Type = type;
        debugInfo.World = &m_State.World;
        debugInfo.ViewMatrix = &m_State.View;
        debugInfo.Projection = &m_State.Projection;
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
        m_DebugState.LogDrawVertexBufferDetails(debugInfo);
    }
#endif

    CKFFTextureBindingSet textureBindingSet;
    BuildCurrentTextureBindingSet(&textureBindingSet, preparedState.ActiveTextureCount);

    // Upload uniforms
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    UploadUniforms(encoder, &programContext, preparedState.ActiveTextureCount);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_Probes.Stats.UniformUs += CKRenderPerfElapsedUs(statsStart);

    // Set world transform
    if (collectStats) {
        if (m_Probes.Stats.HasLastWorldMatrix && memcmp(&m_Probes.Stats.LastWorldMatrix, &m_State.World, sizeof(VxMatrix)) == 0)
            ++m_Probes.Stats.ConsecutiveWorldMatrixRepeats;
        memcpy(&m_Probes.Stats.LastWorldMatrix, &m_State.World, sizeof(VxMatrix));
        m_Probes.Stats.HasLastWorldMatrix = TRUE;
    }
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    CKDWORD transformIdx = m_Context->AllocTransform(&m_State.World, 1);
    encoder->SetTransform(transformIdx, 1);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (collectStats)
        ++m_Probes.Stats.TransformSets;
    if (statsTiming)
        m_Probes.Stats.TransformUs += CKRenderPerfElapsedUs(statsStart);

    // Set draw state
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    CKDrawState drawState = m_DrawStateCache.BuildDrawState(type);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_Probes.Stats.DrawStateBuildUs += CKRenderPerfElapsedUs(statsStart);
    if (collectStats) {
        if (m_Probes.Stats.HasLastDrawState && CKFFDrawStateEquals(m_Probes.Stats.LastDrawState, drawState))
            ++m_Probes.Stats.ConsecutiveDrawStateRepeats;
        m_Probes.Stats.LastDrawState = drawState;
        m_Probes.Stats.HasLastDrawState = TRUE;
    }
    const CKDWORD stencilRef = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILREF);
    const CKDWORD stencilReadMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILMASK);
    const CKDWORD stencilWriteMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK);
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#else
    const CKDWORD stencilRef = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILREF);
    const CKDWORD stencilReadMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILMASK);
    const CKDWORD stencilWriteMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK);
#endif
    encoder->SetState(drawState);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_Probes.Stats.EncoderStateUs += CKRenderPerfElapsedUs(statsStart);
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    encoder->SetStencilRef(stencilRef);
    encoder->SetStencilMask(stencilReadMask, stencilWriteMask);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_Probes.Stats.StencilUs += CKRenderPerfElapsedUs(statsStart);
#endif

    // Set vertex layout
    if (vertexLayout) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (statsTiming)
            statsStart = CKRenderPerfNow();
#endif
        encoder->SetVertexLayout(vertexLayout);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (collectStats)
            ++m_Probes.Stats.VertexLayoutSets;
        if (statsTiming)
            m_Probes.Stats.LayoutUs += CKRenderPerfElapsedUs(statsStart);
#endif
    }

    // Bind buffers
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (collectStats) {
        if (m_Probes.Stats.HasLastVertexBuffer &&
            m_Probes.Stats.LastVertexBuffer == vb &&
            m_Probes.Stats.LastVertexLayout == vertexLayout)
            ++m_Probes.Stats.ConsecutiveVertexBufferRepeats;
        m_Probes.Stats.LastVertexBuffer = vb;
        m_Probes.Stats.LastVertexLayout = vertexLayout;
        m_Probes.Stats.HasLastVertexBuffer = TRUE;
        if (ib && m_Probes.Stats.HasLastIndexBuffer && m_Probes.Stats.LastIndexBuffer == ib)
            ++m_Probes.Stats.ConsecutiveIndexBufferRepeats;
        m_Probes.Stats.LastIndexBuffer = ib;
        m_Probes.Stats.HasLastIndexBuffer = TRUE;
    }
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    encoder->SetVertexBuffer(0, vb, baseVertex, vertexCount);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (collectStats)
        ++m_Probes.Stats.VertexBufferSets;
#endif
    if (ib) {
        encoder->SetIndexBuffer(ib, startIndex, indexCount);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (collectStats)
            ++m_Probes.Stats.IndexBufferSets;
#endif
    }
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_Probes.Stats.BufferBindUs += CKRenderPerfElapsedUs(statsStart);

    // Bind textures
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    BindTextures(encoder, &textureBindingSet);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_Probes.Stats.TextureUs += CKRenderPerfElapsedUs(statsStart);
#endif

    // Submit
    float depth = ComputeDepthKey();
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    encoder->Submit(view, program, *(CKDWORD *)&depth, SubmitDiscardFlags());
    CKRenderFrameCostStatsAddMeshSubmit();
    CKRenderFrameCostStatsAddSubmittedDraw();
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_Probes.Stats.SubmitUs += CKRenderPerfElapsedUs(statsStart);
    if (collectStats)
        ++m_Probes.Stats.SubmittedDraws;
#endif
}

// ============================================================================
// Internal methods
// ============================================================================

void CKFixedFunctionPipeline::BuildCurrentPreparedState(
    CKFFPreparedState *prepared, CKDWORD dpFlags, CKDWORD activeTextureCount,
    CKDWORD formatFlags, const CKBYTE *texcoordComponentCounts) {
    if (!prepared)
        return;
    CKFFInitPreparedState(prepared);
    CKFFStateDesc &stateDesc = prepared->StateDesc;
    prepared->ActiveTextureCount = activeTextureCount;
    if (prepared->ActiveTextureCount > CKFF_MAX_TEXTURE_STAGES)
        prepared->ActiveTextureCount = CKFF_MAX_TEXTURE_STAGES;
    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        if (m_State.TextureHandles[stage] != 0)
            prepared->TextureBoundMask |= (1u << stage);
    }

    const bool hasFormat = formatFlags != 0;
    const bool positionT = hasFormat ? ((formatFlags & CKFF_VF_POSITIONT) != 0) : ((dpFlags & CKRST_DP_TRANSFORM) == 0);
    prepared->PositionT = positionT ? TRUE : FALSE;

    // Vertex state description
    stateDesc.VS.SetHasPosition(!positionT);
    stateDesc.VS.SetHasPositionT(positionT);
    stateDesc.VS.SetHasNormal(hasFormat ? ((formatFlags & CKFF_VF_NORMAL) != 0) : ((dpFlags & CKRST_DP_LIGHT) != 0));
    stateDesc.VS.SetHasColor0(hasFormat ? ((formatFlags & CKFF_VF_COLOR0) != 0) : ((dpFlags & CKRST_DP_DIFFUSE) != 0));
    stateDesc.VS.SetHasColor1(hasFormat ? ((formatFlags & CKFF_VF_COLOR1) != 0) : ((dpFlags & CKRST_DP_SPECULAR) != 0));
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        stateDesc.VS.SetHasTexCoord(
            stage,
            hasFormat ? ((formatFlags & CKFF_VF_TEXCOORD(stage)) != 0) : (prepared->ActiveTextureCount > (CKDWORD)stage));
        const CKDWORD packedTexcoord = m_State.StageStates[stage][CKRST_TSS_TEXCOORDINDEX];
        const CKDWORD transformFlags = m_State.StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS];
        stateDesc.VS.SetTexCoordIndex(stage, CKFFTexcoordIndex(packedTexcoord));
        stateDesc.VS.SetTextureTransformFlags(stage, transformFlags);
        const CKDWORD componentCount = texcoordComponentCounts ? texcoordComponentCounts[stage] : 2;
        stateDesc.VS.SetTexcoordComponentCount(stage, CKFFTexcoordComponentCount(componentCount));
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
    prepared->LightingEnabled = stateDesc.VS.GetLightingEnabled() ? TRUE : FALSE;
    stateDesc.VS.SetSpecularEnabled(specular != 0);
    stateDesc.VS.SetNormalizeNormals(normalize != 0);
    stateDesc.VS.SetLocalViewer(stateDesc.VS.GetLightingEnabled() &&
                                m_DrawStateCache.GetRenderState(VXRENDERSTATE_LOCALVIEWER) != 0);
    stateDesc.VS.SetLightCount(stateDesc.VS.GetLightingEnabled() ? m_State.ActiveLightCount : 0);

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
    prepared->MaterialSource[0] = (float)diffuseSource;
    prepared->MaterialSource[1] = (float)ambientSource;
    prepared->MaterialSource[2] = (float)specularSource;
    prepared->MaterialSource[3] = (float)emissiveSource;

    // Fog
    CKBOOL fogEnable = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGENABLE);
    if (fogEnable) {
        CKDWORD vertexFogMode = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGVERTEXMODE);
        CKDWORD pixelFogMode = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGPIXELMODE);
        if (pixelFogMode != VXFOG_NONE)
            vertexFogMode = VXFOG_NONE;
        stateDesc.VS.SetFogMode(vertexFogMode);
        stateDesc.VS.SetRangeFog(m_DrawStateCache.GetRenderState(VXRENDERSTATE_RANGEFOGENABLE) != 0);
        stateDesc.FS.SetVertexFogMode(vertexFogMode);
        stateDesc.FS.SetPixelFogMode(pixelFogMode);
        stateDesc.FS.SetRangeFog(m_DrawStateCache.GetRenderState(VXRENDERSTATE_RANGEFOGENABLE) != 0);
    }

    // Fragment state description mirrors the active fixed-function texture-stage contract.
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const bool stageActive = (CKDWORD)stage < prepared->ActiveTextureCount;
        const bool hasTexture = stageActive && m_State.TextureHandles[stage] != 0;
        const CKDWORD colorOp = CKFFResolveStageColorOp(m_State.StageStates[stage], stageActive, hasTexture);
        const CKDWORD alphaOp = CKFFResolveStageAlphaOp(m_State.StageStates[stage], stageActive, hasTexture);
        stateDesc.FS.SetStageColorOp(stage, colorOp);
        stateDesc.FS.SetStageColorArg0(stage, CKFFResolveStageColorArg0(m_State.StageStates[stage]));
        stateDesc.FS.SetStageColorArg1(stage, CKFFResolveStageColorArg1(m_State.StageStates[stage], hasTexture));
        stateDesc.FS.SetStageColorArg2(stage, CKFFResolveStageColorArg2(m_State.StageStates[stage]));
        stateDesc.FS.SetStageAlphaOp(stage, alphaOp);
        stateDesc.FS.SetStageAlphaArg0(stage, CKFFResolveStageAlphaArg0(m_State.StageStates[stage]));
        stateDesc.FS.SetStageAlphaArg1(stage, CKFFResolveStageAlphaArg1(m_State.StageStates[stage], hasTexture));
        stateDesc.FS.SetStageAlphaArg2(stage, CKFFResolveStageAlphaArg2(m_State.StageStates[stage]));
        stateDesc.FS.SetStageResultIsTemp(stage, CKFFBaseTextureArg(CKFFResolveStageResultArg(m_State.StageStates[stage])) == CKRST_TA_TEMP);
        stateDesc.FS.SetStageProjectedSampler(stage, (m_State.StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS] & CKRST_TTF_PROJECTED) != 0);
        stateDesc.FS.SetStageSamplerType(stage, CKFFSamplerTypeFromTextureFlags(m_State.TextureFlags[stage]));
        stateDesc.FS.SetStageSamplerCompareFunc(stage, m_State.StageStates[stage][CKRST_TSS_COMPAREFUNC]);

        if (colorOp == CKRST_TOP_DISABLE)
            break;
    }

    stateDesc.FS.SetSpecularAdd(specular != 0);
    stateDesc.FS.SetFogEnabled(fogEnable != 0);
    stateDesc.FS.SetFlatShade(m_DrawStateCache.GetRenderState(VXRENDERSTATE_SHADEMODE) == VXSHADE_FLAT);

    CKBOOL alphaTest = m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHATESTENABLE);
    if (alphaTest) {
        stateDesc.FS.SetAlphaTestEnabled(true);
        stateDesc.FS.SetAlphaFunc(m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAFUNC));
    }
    stateDesc.VS.SetVertexClipping(m_DrawStateCache.GetRenderState(VXRENDERSTATE_CLIPPLANEENABLE) != 0);
}

static CKDWORD CKFFCurrentTextureMatrixUploadCount(
    const CKFFUniformEmissionContext *context,
    const CKDWORD stageStates[CKFF_MAX_TEXTURE_STAGES][CKFF_MAX_TEXTURE_STAGE_STATES])
{
    if (!context || context->PositionT || !stageStates)
        return 0;
    CKDWORD activeTextureCount = context->ActiveTextureCount;
    if (activeTextureCount > CKFF_MAX_TEXTURE_STAGES)
        activeTextureCount = CKFF_MAX_TEXTURE_STAGES;
    CKDWORD count = 0;
    for (CKDWORD stage = 0; stage < activeTextureCount; ++stage) {
        const CKDWORD flags = stageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS];
        const CKDWORD componentCount = flags & 0xFFu;
        if (componentCount > 1 && componentCount <= 4)
            count = stage + 1;
    }
    return count;
}

static bool CKFFProgramUsesBumpEnv(const CKFFShaderKey &shaderKey)
{
    const CKDWORD lastStage = shaderKey.FS.LastActiveTextureStage;
    for (CKDWORD stage = 0; stage <= lastStage && stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const CKDWORD op = shaderKey.FS.Stages[stage].ColorOp;
        if (op == CKRST_TOP_BUMPENVMAP || op == CKRST_TOP_BUMPENVMAPLUMINANCE)
            return true;
    }
    return false;
}

static bool CKFFTextureArgUsesTexFactor(CKDWORD arg)
{
    return (arg & ~(0x10u | 0x20u)) == CKRST_TA_TFACTOR;
}

static bool CKFFShaderStageUsesTexFactor(const CKFFShaderKeyFSStage &stage)
{
    if (stage.ColorOp == CKRST_TOP_BLENDFACTORALPHA || stage.AlphaOp == CKRST_TOP_BLENDFACTORALPHA)
        return true;
    if (CKFFTextureArgUsesTexFactor(stage.ColorArg0))
        return true;
    if (CKFFTextureArgUsesTexFactor(stage.ColorArg1))
        return true;
    if (CKFFTextureArgUsesTexFactor(stage.ColorArg2))
        return true;
    if (CKFFTextureArgUsesTexFactor(stage.AlphaArg0))
        return true;
    if (CKFFTextureArgUsesTexFactor(stage.AlphaArg1))
        return true;
    if (CKFFTextureArgUsesTexFactor(stage.AlphaArg2))
        return true;
    return false;
}

static bool CKFFProgramUsesTexFactor(const CKFFShaderKey &shaderKey)
{
    const CKDWORD lastStage = shaderKey.FS.LastActiveTextureStage;
    for (CKDWORD stage = 0; stage <= lastStage && stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const CKFFShaderKeyFSStage &s = shaderKey.FS.Stages[stage];
        if (CKFFShaderStageUsesTexFactor(s))
            return true;
    }
    return false;
}

static bool CKFFTextureArgUsesStageConstant(CKDWORD arg)
{
    return CKFFBaseTextureArg(arg) == CKRST_TA_CONSTANT;
}

static bool CKFFShaderStageUsesStageConstant(const CKFFShaderKeyFSStage &stage)
{
    if (CKFFTextureArgUsesStageConstant(stage.ColorArg0))
        return true;
    if (CKFFTextureArgUsesStageConstant(stage.ColorArg1))
        return true;
    if (CKFFTextureArgUsesStageConstant(stage.ColorArg2))
        return true;
    if (CKFFTextureArgUsesStageConstant(stage.AlphaArg0))
        return true;
    if (CKFFTextureArgUsesStageConstant(stage.AlphaArg1))
        return true;
    if (CKFFTextureArgUsesStageConstant(stage.AlphaArg2))
        return true;
    return false;
}

static bool CKFFProgramUsesStageConstant(const CKFFShaderKey &shaderKey)
{
    const CKDWORD lastStage = shaderKey.FS.LastActiveTextureStage;
    for (CKDWORD stage = 0; stage <= lastStage && stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const CKFFShaderKeyFSStage &s = shaderKey.FS.Stages[stage];
        if (CKFFShaderStageUsesStageConstant(s))
            return true;
    }
    return false;
}

static bool CKFFProgramUsesMaterialUniform(const CKFFShaderKey &shaderKey,
                                           CKBOOL fullSpecialized)
{
    if (shaderKey.VS.GetHasPositionT())
        return false;

    if (!fullSpecialized)
        return true;

    const uint64_t bits = shaderKey.VS.Bits;
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

static bool CKFFProgramUsesViewSpaceUniforms(const CKFFShaderKey &shaderKey,
                                             CKBOOL fullSpecialized)
{
    if (shaderKey.VS.GetHasPositionT())
        return false;

    if (!fullSpecialized)
        return true;

    const uint64_t bits = shaderKey.VS.Bits;
    if (CKFFShaderKeyVertexBlendMode(shaderKey.VS) == CKFF_VERTEX_BLEND_NORMAL)
        return true;
    if ((bits & (1ull << 13)) != 0)
        return true;

    if (shaderKey.FS.VertexFogMode != 0)
        return true;

    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        if ((shaderKey.VS.TexGen[stage] & 7u) != 0)
            return true;
    }

    return false;
}

CKDWORD CKFixedFunctionPipeline::BuildDrawParams(
    float (*drawParams)[4],
    const CKFFLightData *viewLights,
    int packedLightCount,
    const CKFFUniformEmissionContext *context) const
{
    if (!context)
        return 0;
    const CKFFShaderKey &shaderKey = context->ShaderKey;
    memset(drawParams, 0, sizeof(float) * CKFF_DRAW_PARAM_VEC4_COUNT * 4);
    memcpy(drawParams[0], m_State.Material.Diffuse, sizeof(drawParams[0]));
    memcpy(drawParams[1], m_State.Material.Ambient, sizeof(drawParams[1]));
    memcpy(drawParams[2], m_State.Material.Specular, sizeof(drawParams[2]));
    memcpy(drawParams[3], m_State.Material.Emissive, sizeof(drawParams[3]));
    drawParams[CKFF_DRAW_PARAM_MATERIAL_POWER][0] = m_State.Material.Power;
    float materialSource[4];
    CKFFShaderKeyMaterialSources(shaderKey.VS, materialSource);
    memcpy(drawParams[CKFF_DRAW_PARAM_MATERIAL_SOURCES], materialSource,
           sizeof(drawParams[CKFF_DRAW_PARAM_MATERIAL_SOURCES]));
    drawParams[CKFF_DRAW_PARAM_LIGHT_FLAGS][2] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_RANGEFOGENABLE) ? 1.0f : 0.0f;
    if (context->LightingEnabled) {
        CKDWORD ambientColor = m_DrawStateCache.GetRenderState(VXRENDERSTATE_AMBIENT);
        float ambientColorF[4];
        CKFFPackColorARGB(ambientColor, ambientColorF);
        const CKBOOL lightingEnabled = CKFFShaderKeyLightingEnabled(shaderKey.VS);
        drawParams[CKFF_DRAW_PARAM_LIGHTING][0] = lightingEnabled ? (float)packedLightCount : -1.0f;
        drawParams[CKFF_DRAW_PARAM_LIGHTING][1] = ambientColorF[0];
        drawParams[CKFF_DRAW_PARAM_LIGHTING][2] = ambientColorF[1];
        drawParams[CKFF_DRAW_PARAM_LIGHTING][3] = ambientColorF[2];
        drawParams[CKFF_DRAW_PARAM_LIGHT_FLAGS][0] = lightingEnabled &&
                            m_DrawStateCache.GetRenderState(VXRENDERSTATE_LOCALVIEWER) ? 1.0f : 0.0f;
        drawParams[CKFF_DRAW_PARAM_LIGHT_FLAGS][1] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_NORMALIZENORMALS) ? 1.0f : 0.0f;
        if (packedLightCount == 1 && viewLights) {
            memcpy(drawParams[CKFF_DRAW_PARAM_INLINE_LIGHT_BASE + 0], viewLights[0].Position, sizeof(drawParams[0]));
            memcpy(drawParams[CKFF_DRAW_PARAM_INLINE_LIGHT_BASE + 1], viewLights[0].Direction, sizeof(drawParams[0]));
            memcpy(drawParams[CKFF_DRAW_PARAM_INLINE_LIGHT_BASE + 2], viewLights[0].Diffuse, sizeof(drawParams[0]));
            memcpy(drawParams[CKFF_DRAW_PARAM_INLINE_LIGHT_BASE + 3], viewLights[0].Specular, sizeof(drawParams[0]));
            memcpy(drawParams[CKFF_DRAW_PARAM_INLINE_LIGHT_BASE + 4], viewLights[0].Ambient, sizeof(drawParams[0]));
            memcpy(drawParams[CKFF_DRAW_PARAM_INLINE_LIGHT_BASE + 5], viewLights[0].Attenuation, sizeof(drawParams[0]));
            memcpy(drawParams[CKFF_DRAW_PARAM_INLINE_LIGHT_BASE + 6], viewLights[0].SpotParams, sizeof(drawParams[0]));
            drawParams[CKFF_DRAW_PARAM_LIGHT_FLAGS][3] = 1.0f;
        }
    }
    const bool shaderUsesVertexParams = !context->PositionT &&
        (!context->FullSpecialized ||
         context->LightingEnabled ||
         CKFFProgramUsesMaterialUniform(context->ShaderKey, context->FullSpecialized));
    CKDWORD drawParamCount = shaderUsesVertexParams
        ? (context->LightingEnabled ? (packedLightCount == 1 ? 19 : 8) : 6)
        : 0;

    drawParams[CKFF_DRAW_PARAM_ALPHA][0] = (float)CKFFAlphaRefByte(m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAREF));
    drawParams[CKFF_DRAW_PARAM_ALPHA][1] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHATESTENABLE)
        ? CKFFPackAlphaFuncPrecision(m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAFUNC), m_State.AlphaTestPrecision)
        : 0.0f;
    drawParams[CKFF_DRAW_PARAM_ALPHA][2] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_SPECULARENABLE) ? 1.0f : 0.0f;
    drawParams[CKFF_DRAW_PARAM_ALPHA][3] = (float)context->PixelFogMode;

    CKDWORD tf = m_DrawStateCache.GetRenderState(VXRENDERSTATE_TEXTUREFACTOR);
    CKFFPackColorARGB(tf, drawParams[CKFF_DRAW_PARAM_TEXTURE_FACTOR]);

    drawParams[CKFF_DRAW_PARAM_FOG][0] = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_FOGSTART, 0.0f);
    drawParams[CKFF_DRAW_PARAM_FOG][1] = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_FOGEND, 1.0f);
    drawParams[CKFF_DRAW_PARAM_FOG][2] = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_FOGDENSITY, 1.0f);
    drawParams[CKFF_DRAW_PARAM_FOG][3] = (float)context->VertexFogMode;

    CKDWORD fogColor = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGCOLOR);
    CKFFPackColorARGB(fogColor, drawParams[CKFF_DRAW_PARAM_FOG_COLOR]);

    CKDWORD fragmentParamCount = 0;
    if (!context->FullSpecialized) {
        fragmentParamCount = 4;
    } else if (context->FogEnabled) {
        fragmentParamCount = 4;
    } else if (CKFFProgramUsesTexFactor(context->ShaderKey)) {
        fragmentParamCount = 2;
    } else if (shaderKey.FS.AlphaTestEnable) {
        fragmentParamCount = 1;
    }
    if (fragmentParamCount > 0 && drawParamCount < 8 + fragmentParamCount)
        drawParamCount = 8 + fragmentParamCount;
    if (!context->FullSpecialized && !context->PositionT && context->FogEnabled && drawParamCount < 8)
        drawParamCount = 8;
    return drawParamCount;
}

static void CKFFInitUniformSink(CKFFUniformSink *sink,
                                CKRasterizerEncoder *encoder,
                                CKFFRenderPacketUniformPayload *staticPayload,
                                CKFFRenderPacketUniformPayload *objectPayload,
                                CKBOOL emitStatic,
                                CKBOOL emitObject)
{
    if (!sink)
        return;
    memset(sink, 0, sizeof(CKFFUniformSink));
    sink->Encoder = encoder;
    sink->StaticPayload = staticPayload;
    sink->ObjectPayload = objectPayload;
    sink->EmitStatic = emitStatic;
    sink->EmitObject = emitObject;
}

static void CKFFInitUniformEmissionContext(CKFFUniformEmissionContext *context,
                                           CKFFUniformSink *sink,
                                           const CKFFProgramContext *programContext,
                                           CKDWORD activeTextureCount)
{
    if (!context)
        return;
    memset(context, 0, sizeof(CKFFUniformEmissionContext));
    context->Uniforms = sink;
    context->ProgramContext = programContext;
    context->ShaderKey = programContext->ShaderKey;
    context->Specialization = programContext->Specialization;
    context->ActiveTextureCount = activeTextureCount;
    context->FullSpecialized = programContext->FullSpecialized;
    context->PositionT = context->ShaderKey.VS.GetHasPositionT() ? TRUE : FALSE;
    context->LightingEnabled = (!context->PositionT &&
        (!context->FullSpecialized || ((context->ShaderKey.VS.Bits & (1ull << 13)) != 0)))
        ? TRUE : FALSE;
    context->FogEnabled = context->ShaderKey.FS.FogEnable ? TRUE : FALSE;
    context->VertexFogMode = context->FogEnabled ? context->ShaderKey.FS.VertexFogMode : 0;
    context->PixelFogMode = context->FogEnabled ? context->ShaderKey.FS.PixelFogMode : 0;
}

CKBOOL CKFixedFunctionPipeline::EmitUniform(CKFFUniformSink *sink, CKDWORD uniform,
                                            const void *data, CKDWORD count,
                                            CKDWORD vec4Count, CKBOOL objectUniform)
{
    if (!sink || !data || count == 0)
        return TRUE;
    if (objectUniform && !sink->EmitObject)
        return TRUE;
    if (!objectUniform && !sink->EmitStatic)
        return TRUE;
    if (sink->Encoder)
        UploadUniform(sink->Encoder, uniform, data, count);
    CKFFRenderPacketUniformPayload *payload = objectUniform ? sink->ObjectPayload : sink->StaticPayload;
    if (payload && !CKFFRenderPacketAddUniform(payload, uniform, data, count, vec4Count)) {
        sink->Failed = TRUE;
        return FALSE;
    }
    return TRUE;
}

void CKFFUniformEmitter::EmitObjectMatrixUniforms(
    CKFixedFunctionPipeline *pipeline,
    const CKFFUniformEmissionContext *context)
{
    if (!pipeline || !context || !context->Uniforms || context->PositionT)
        return;

    const CKFFUniformHandles &u = pipeline->m_ShaderCache.GetUniforms();
    CKFFUniformSink *sink = context->Uniforms;
    const bool viewSpaceUniforms = CKFFProgramUsesViewSpaceUniforms(context->ShaderKey,
                                                                    context->FullSpecialized);
    const bool vertexBlend = CKFFShaderKeyVertexBlendMode(context->ShaderKey.VS) == CKFF_VERTEX_BLEND_NORMAL;
    VxMatrix modelView;
    VxMatrix normalMatrix;
    VxMatrix viewNormalMatrix;
    VxMatrix viewProj;
    VxMatrix modelViewProj;
    if (viewSpaceUniforms) {
        Vx3DMultiplyMatrix4(modelView, pipeline->m_State.View, pipeline->m_State.World);
        Vx3DInverseMatrix(normalMatrix, modelView);
        Vx3DTransposeMatrix(normalMatrix, normalMatrix);
        if (vertexBlend) {
            Vx3DInverseMatrix(viewNormalMatrix, pipeline->m_State.View);
            Vx3DTransposeMatrix(viewNormalMatrix, viewNormalMatrix);
        }
    }
    Vx3DMultiplyMatrix4(viewProj, pipeline->m_State.Projection, pipeline->m_State.View);
    Vx3DMultiplyMatrix4(modelViewProj, viewProj, pipeline->m_State.World);
    VxMatrix matrices[4];
    matrices[0] = vertexBlend ? viewProj : modelViewProj;
    matrices[1] = pipeline->m_State.World;
    if (viewSpaceUniforms) {
        matrices[2] = vertexBlend ? pipeline->m_State.View : modelView;
        matrices[3] = vertexBlend ? viewNormalMatrix : normalMatrix;
    }
    if (vertexBlend) {
        VxMatrix identity;
        identity.Identity();
        VxMatrix palette[CKFF_VERTEX_BLEND_MATRIX_COUNT];
        for (int i = 0; i < CKFF_VERTEX_BLEND_MATRIX_COUNT; ++i) {
            if (pipeline->m_State.VertexBlendMatrixSet[i])
                palette[i] = pipeline->m_State.VertexBlendMatrices[i];
            else
                palette[i] = (i == 0) ? pipeline->m_State.World : identity;
        }
        pipeline->EmitUniform(sink, u.u_vertexBlendMatrices, palette,
                              CKFF_VERTEX_BLEND_MATRIX_COUNT,
                              CKFF_VERTEX_BLEND_MATRIX_COUNT * 4, TRUE);
    }
    const CKDWORD matrixCount = viewSpaceUniforms ? 4 : 2;
    pipeline->EmitUniform(sink, u.u_ffMatrices, matrices, matrixCount, matrixCount * 4, TRUE);
}

void CKFFUniformEmitter::EmitTextureMatrixUniforms(
    CKFixedFunctionPipeline *pipeline,
    const CKFFUniformEmissionContext *context)
{
    if (!pipeline || !context || !context->Uniforms)
        return;
    CKFFUniformSink *sink = context->Uniforms;
    const CKFFUniformHandles &u = pipeline->m_ShaderCache.GetUniforms();
    const CKDWORD texMatrixCount = CKFFCurrentTextureMatrixUploadCount(context, pipeline->m_State.StageStates);
    if (texMatrixCount > 0)
        pipeline->EmitUniform(sink, u.u_texMatrix, pipeline->m_State.TexMatrix,
                              texMatrixCount, texMatrixCount * 4, FALSE);
}

void CKFFUniformEmitter::EmitStageAndSpecUniforms(
    CKFixedFunctionPipeline *pipeline,
    const CKFFUniformEmissionContext *context)
{
    if (!pipeline || !context || !context->Uniforms)
        return;

    CKFFUniformSink *sink = context->Uniforms;
    const CKFFUniformHandles &u = pipeline->m_ShaderCache.GetUniforms();
    if (CKFFProgramUsesBumpEnv(context->ShaderKey)) {
        float bumpEnv[CKFF_MAX_TEXTURE_STAGES * 2][4] = {};
        CKFFPackBumpEnvUniforms(pipeline->m_State.StageStates, bumpEnv);
        pipeline->EmitUniform(sink, u.u_bumpEnv, bumpEnv,
                              CKFF_MAX_TEXTURE_STAGES * 2, CKFF_MAX_TEXTURE_STAGES * 2, FALSE);
    }

    if (context->PositionT)
        pipeline->EmitUniform(sink, u.u_viewport, pipeline->m_State.Viewport, 1, 1, FALSE);

    if (!context->FullSpecialized || CKFFProgramUsesStageConstant(context->ShaderKey)) {
        CKFFStageParamsUniform stageParams;
        CKFFPackStageParams(pipeline->m_State.StageStates, pipeline->m_State.TextureHandles,
                            context->ActiveTextureCount, stageParams);
        pipeline->EmitUniform(sink, u.u_stageParams, stageParams.Values,
                              CKFF_STAGE_PARAM_VEC4_COUNT, CKFF_STAGE_PARAM_VEC4_COUNT, FALSE);
    }

    if (!context->FullSpecialized) {
        CKFFSpecUniform ffSpec;
        CKFFPackSpecializationDwords(context->Specialization, ffSpec);
        pipeline->EmitUniform(sink, u.u_ffSpec, ffSpec.Values,
                              CKFFSpecializationInfo::MaxSpecDwords,
                              CKFFSpecializationInfo::MaxSpecDwords, FALSE);
    }
}

void CKFFUniformEmitter::EmitClipPlaneUniforms(
    CKFixedFunctionPipeline *pipeline,
    const CKFFUniformEmissionContext *context)
{
    if (!pipeline || !context || !context->Uniforms)
        return;

    CKFFUniformSink *sink = context->Uniforms;
    const CKFFUniformHandles &u = pipeline->m_ShaderCache.GetUniforms();
    CKFFClipPlaneUniform clip;
    const CKDWORD clipMask = pipeline->m_DrawStateCache.GetRenderState(VXRENDERSTATE_CLIPPLANEENABLE);
    if (!context->FullSpecialized || ((context->ShaderKey.VS.Bits & (1ull << 34)) != 0)) {
        if (clipMask != 0) {
            CKFFPackClipPlaneUniforms(pipeline->m_State.UserClipPlanes, clipMask, clip);
            pipeline->EmitUniform(sink, u.u_clipPlanes, clip.Planes, 6, 6, FALSE);
        } else {
            memset(&clip, 0, sizeof(clip));
        }
        pipeline->EmitUniform(sink, u.u_clipParams, clip.Params, 1, 1, FALSE);
    }
}

void CKFixedFunctionPipeline::EmitUniformPayloads(CKFFUniformSink *sink,
                                                  const CKFFProgramContext *programContext,
                                                  CKDWORD activeTextureCount)
{
    if (!sink || !programContext)
        return;
    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();
    CKFFUniformEmissionContext context;
    CKFFInitUniformEmissionContext(&context, sink, programContext, activeTextureCount);
    const bool emitStatic = sink->EmitStatic;
    const bool emitObject = sink->EmitObject;

    if (emitObject)
        CKFFUniformEmitter::EmitObjectMatrixUniforms(this, &context);
    if (!emitStatic)
        return;

    CKFFUniformEmitter::EmitTextureMatrixUniforms(this, &context);

    // bgfx uniform bindings are draw state. Packet replay can retain static
    // draw constants across sorted opaque packets, but immediate draws still
    // upload all constants before each submit.
    int packed = 0;
    CKFFLightData viewLights[CKFF_MAX_LIGHTS];
    if (context.LightingEnabled) {
        packed = CKFFPackViewLights(m_State.Lights, m_State.LightEnabled, m_State.ActiveLightCount,
                                    CKFFShaderKeyLightingEnabled(context.ShaderKey.VS), m_State.View, viewLights);

        if (packed > 1)
            EmitUniform(sink, u.u_lights, viewLights, packed * 7, packed * 7, FALSE);
    }

    float drawParams[CKFF_DRAW_PARAM_VEC4_COUNT][4];
    CKDWORD drawParamCount = BuildDrawParams(drawParams, viewLights, packed, &context);
    if (drawParamCount > 0)
        EmitUniform(sink, u.u_ffDrawParams, drawParams, drawParamCount, drawParamCount, FALSE);

    CKFFUniformEmitter::EmitStageAndSpecUniforms(this, &context);
    CKFFUniformEmitter::EmitClipPlaneUniforms(this, &context);
}

void CKFixedFunctionPipeline::UploadUniforms(CKRasterizerEncoder *encoder,
                                             const CKFFProgramContext *programContext,
                                             CKDWORD activeTextureCount) {
    if (!encoder || !programContext)
        return;
    UploadObjectUniforms(encoder, programContext, activeTextureCount);
    UploadStaticUniforms(encoder, programContext, activeTextureCount);
    m_State.DirtyFlags = 0;
}

void CKFixedFunctionPipeline::UploadObjectUniforms(CKRasterizerEncoder *encoder,
                                                   const CKFFProgramContext *programContext,
                                                   CKDWORD activeTextureCount)
{
    if (!encoder || !programContext)
        return;
    CKFFUniformSink sink;
    CKFFInitUniformSink(&sink, encoder, nullptr, nullptr, FALSE, TRUE);
    EmitUniformPayloads(&sink, programContext, activeTextureCount);
}

void CKFixedFunctionPipeline::UploadStaticUniforms(CKRasterizerEncoder *encoder,
                                                   const CKFFProgramContext *programContext,
                                                   CKDWORD activeTextureCount)
{
    if (!encoder || !programContext)
        return;
    CKFFUniformSink sink;
    CKFFInitUniformSink(&sink, encoder, nullptr, nullptr, TRUE, FALSE);
    EmitUniformPayloads(&sink, programContext, activeTextureCount);
}

CKBOOL CKFixedFunctionPipeline::BuildStaticUniformPayload(CKFFRenderPacketUniformPayload *payload,
                                                          const CKFFProgramContext *programContext,
                                                          CKDWORD activeTextureCount)
{
    if (!payload)
        return FALSE;
    memset(payload, 0, sizeof(CKFFRenderPacketUniformPayload));

    CKFFUniformSink sink;
    CKFFInitUniformSink(&sink, nullptr, payload, nullptr, TRUE, FALSE);
    EmitUniformPayloads(&sink, programContext, activeTextureCount);
    if (sink.Failed)
        return FALSE;

    payload->Hash = CKFFHashRenderPacketUniformPayload(*payload);
    return TRUE;
}

CKDWORD CKFixedFunctionPipeline::GetPacketObjectUniformRejectReason(const CKFFProgramContext *programContext) const
{
    if (!programContext)
        return CKFF_RENDER_PACKET_REJECT_PROGRAM_MISSING;

    const CKFFShaderKey &shaderKey = programContext->ShaderKey;
    if (shaderKey.VS.GetHasPositionT())
        return CKFF_RENDER_PACKET_ELIGIBLE;
    if (CKFFShaderKeyVertexBlendMode(shaderKey.VS) == CKFF_VERTEX_BLEND_NORMAL)
        return CKFF_RENDER_PACKET_REJECT_VERTEX_BLEND;
    return CKFF_RENDER_PACKET_ELIGIBLE;
}

CKDWORD CKFixedFunctionPipeline::GetVertexBufferPacketInstancingRejectReason(
    const CKFFProgramContext *programContext) const
{
    if (!programContext)
        return CKFF_RENDER_PACKET_REJECT_PROGRAM_MISSING;
    if (!m_OpaqueInstancingEnabled || !m_InstanceLayout)
        return CKFF_RENDER_PACKET_REJECT_INSTANCE_LAYOUT;

    const CKFFShaderKey &shaderKey = programContext->ShaderKey;
    if (shaderKey.VS.GetHasPositionT())
        return CKFF_RENDER_PACKET_REJECT_POSITIONT;
    if (CKFFShaderKeyVertexBlendMode(shaderKey.VS) == CKFF_VERTEX_BLEND_NORMAL)
        return CKFF_RENDER_PACKET_REJECT_VERTEX_BLEND;
    if (shaderKey.FS.LastActiveTextureStage >= 4)
        return CKFF_RENDER_PACKET_REJECT_TEXCOORD_RANGE;
    for (CKDWORD stage = 0; stage <= shaderKey.FS.LastActiveTextureStage; ++stage) {
        if ((shaderKey.VS.TexCoordIndex[stage] & 7u) >= 4)
            return CKFF_RENDER_PACKET_REJECT_TEXCOORD_RANGE;
        if ((shaderKey.VS.TexGen[stage] & 7u) != 0)
            return CKFF_RENDER_PACKET_REJECT_TEXGEN;
    }
    if ((shaderKey.VS.Bits & (1ull << 13)) != 0)
        return CKFF_RENDER_PACKET_REJECT_VIEW_SPACE_SHADER;
    if (shaderKey.FS.VertexFogMode != 0)
        return CKFF_RENDER_PACKET_REJECT_VERTEX_FOG;
    if (shaderKey.FS.PixelFogMode != 0)
        return CKFF_RENDER_PACKET_REJECT_PIXEL_FOG;
    if (shaderKey.FS.RangeFog)
        return CKFF_RENDER_PACKET_REJECT_RANGE_FOG;
    return CKFF_RENDER_PACKET_ELIGIBLE;
}

void CKFixedFunctionPipeline::UpdateViewProjectionCache()
{
    if (!m_State.EnsureViewProjection())
        return;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    ++m_Probes.Stats.RenderPacketViewProjectionRebuilds;
#endif
}

CKBOOL CKFixedFunctionPipeline::BuildPacketObjectUniforms(CKRenderPacketObjectUniforms *uniforms,
                                                          const CKFFProgramContext *programContext)
{
    if (!uniforms)
        return FALSE;
    memset(uniforms, 0, sizeof(CKRenderPacketObjectUniforms));

    if (!programContext)
        return FALSE;

    const CKFFShaderKey &shaderKey = programContext->ShaderKey;
    if (shaderKey.VS.GetHasPositionT())
        return TRUE;

    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();
    const bool viewSpaceUniforms = CKFFProgramUsesViewSpaceUniforms(shaderKey,
                                                                    programContext->FullSpecialized);

    VxMatrix modelView;
    VxMatrix normalMatrix;
    VxMatrix modelViewProj;
    if (viewSpaceUniforms) {
        Vx3DMultiplyMatrix4(modelView, m_State.View, m_State.World);
        Vx3DInverseMatrix(normalMatrix, modelView);
        Vx3DTransposeMatrix(normalMatrix, normalMatrix);
    }
    UpdateViewProjectionCache();
    Vx3DMultiplyMatrix4(modelViewProj, m_State.ViewProjection(), m_State.World);

    uniforms->MatrixUniform = u.u_ffMatrices;
    uniforms->MatrixCount = viewSpaceUniforms ? 4 : 2;
    uniforms->Matrices[0] = modelViewProj;
    uniforms->Matrices[1] = m_State.World;
    if (viewSpaceUniforms) {
        uniforms->Matrices[2] = modelView;
        uniforms->Matrices[3] = normalMatrix;
    }
    return TRUE;
}

void CKFixedFunctionPipeline::UploadUniform(CKRasterizerEncoder *encoder, CKDWORD uniform, const void *data, CKDWORD count) {
    if (!encoder)
        return;
    encoder->SetUniform(uniform, data, count);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (m_Probes.Config.StatsEnabled || m_Probes.Config.UniformHistEnabled) {
        ++m_Probes.Stats.UniformSets;
        m_Probes.Stats.UniformVec4s += count;
    }
    CKDWORD slot = m_Probes.Config.UniformHistEnabled
        ? CKFFUniformDebugSlot(m_ShaderCache.GetUniforms(), uniform)
        : 64;
    if (slot < 64) {
        ++m_Probes.Stats.UniformHandleSets[slot];
        m_Probes.Stats.UniformHandleVec4s[slot] += count;
    }
#endif
}

CKDWORD CKFixedFunctionPipeline::InternStaticUniformPayload(const CKFFRenderPacketUniformPayload &payload)
{
    CKBOOL interned = FALSE;
    CKDWORD index = m_OpaquePacketQueue.InternStaticUniformPayload(payload, &interned);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (interned)
        ++m_Probes.Stats.RenderPacketStaticPayloadInterns;
#endif
    return index;
}

void CKFixedFunctionPipeline::BuildRenderPacketSortKey(CKRenderPacket *packet) const
{
    m_OpaquePacketQueue.BuildSortKey(packet);
}

void CKFixedFunctionPipeline::InitVertexBufferPacketForCapture(CKRenderPacket *packet) const
{
    if (!packet)
        return;

    packet->Serial = 0;
    packet->View = CKRP_VIEW_OPAQUE3D;
    packet->Type = VX_TRIANGLELIST;
    packet->Program = 0;
    packet->Depth = 0;
    packet->DrawState.Lo = 0;
    packet->DrawState.Mid = 0;
    packet->DrawState.Hi = 0;
    packet->StencilRef = 0;
    packet->StencilReadMask = 0;
    packet->StencilWriteMask = 0;
    packet->VertexLayout = 0;
    packet->VertexBuffer = 0;
    packet->IndexBuffer = 0;
    packet->BaseVertex = 0;
    packet->VertexCount = 0;
    packet->StartIndex = 0;
    packet->IndexCount = 0;
    packet->ActiveTextureCount = 0;
    packet->TextureSetHash = 0;
    memset(packet->Textures, 0, sizeof(packet->Textures));
    packet->StaticUniformIndex = 0;
    memset(&packet->ObjectUniforms, 0, sizeof(packet->ObjectUniforms));
    memset(&packet->SortKey, 0, sizeof(packet->SortKey));
    packet->World.SetIdentity();
    packet->ViewProjection.SetIdentity();
    packet->ViewProjectionHash = 0;
    packet->CanInstance = FALSE;
    packet->InstancedProgram = 0;
    packet->Marker[0] = '\0';
}

void CKFixedFunctionPipeline::TrackOpaqueRenderPacket(const CKRenderPacket &packet)
{
    m_OpaquePacketQueue.AddPacket(packet);
    UpdateOpaqueRenderPacketAdaptiveStats();
}

void CKFixedFunctionPipeline::TrackOpaqueRenderPacketReject(CKDWORD rejectReason)
{
    if (rejectReason != CKFF_RENDER_PACKET_REJECT_ADAPTIVE_BYPASS)
        return;
    if (!m_OpaquePacketQueue.IsAdaptiveCooldownActive())
        return;
    m_OpaquePacketQueue.MarkAdaptiveCooldownBypass();
    UpdateOpaqueRenderPacketAdaptiveStats();
}

void CKFixedFunctionPipeline::UpdateOpaqueRenderPacketAdaptiveStats()
{
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    m_Probes.Stats.RenderPacketAdaptiveSamples = m_OpaquePacketQueue.GetAdaptiveSamples();
    m_Probes.Stats.RenderPacketAdaptiveSavedBindEstimate =
        m_OpaquePacketQueue.GetAdaptiveSavedBindEstimate();
    m_Probes.Stats.RenderPacketAdaptiveRunBypasses =
        m_OpaquePacketQueue.GetAdaptiveRunBypasses();
    m_Probes.Stats.RenderPacketAdaptiveSampleRuns =
        m_OpaquePacketQueue.GetAdaptiveSampleRuns();
    m_Probes.Stats.RenderPacketAdaptiveSampleMaxRun =
        m_OpaquePacketQueue.GetAdaptiveSampleMaxRun();
    m_Probes.Stats.RenderPacketAdaptiveSubmitSavedEstimate =
        m_OpaquePacketQueue.GetAdaptiveSubmitSavedEstimate();
    m_Probes.Stats.RenderPacketAdaptiveCooldownBypasses =
        m_OpaquePacketQueue.GetAdaptiveCooldownBypasses();
    m_Probes.Stats.RenderPacketAdaptiveCooldownFrames =
        m_OpaquePacketQueue.GetAdaptiveCooldownFrames();
    m_Probes.Stats.RenderPacketAdaptiveFrameEndEvaluations =
        m_OpaquePacketQueue.GetAdaptiveFrameEndEvaluations();
    m_Probes.Stats.RenderPacketAdaptiveFrameEndRunBypasses =
        m_OpaquePacketQueue.GetAdaptiveFrameEndRunBypasses();
#endif
}

CKBOOL CKFixedFunctionPipeline::CheckOpaqueRenderPacketAdaptiveBypass(CKRasterizerEncoder *encoder)
{
    if (m_OpaquePacketQueue.IsAdaptiveBypassed())
        return TRUE;
    if (!m_OpaquePacketQueue.ShouldAdaptiveBypass(m_OpaqueInstancingEnabled))
        return FALSE;

    m_OpaquePacketQueue.MarkAdaptiveBypass();
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    ++m_Probes.Stats.RenderPacketAdaptiveBypasses;
    m_Probes.Stats.RenderPacketAdaptiveRunBypasses =
        m_OpaquePacketQueue.GetAdaptiveRunBypasses();
    m_Probes.Stats.RenderPacketAdaptiveSampleRuns =
        m_OpaquePacketQueue.GetAdaptiveSampleRuns();
    m_Probes.Stats.RenderPacketAdaptiveSampleMaxRun =
        m_OpaquePacketQueue.GetAdaptiveSampleMaxRun();
    m_Probes.Stats.RenderPacketAdaptiveSubmitSavedEstimate =
        m_OpaquePacketQueue.GetAdaptiveSubmitSavedEstimate();
#endif
    UpdateOpaqueRenderPacketAdaptiveStats();
    FlushOpaqueRenderPackets(encoder, TRUE, FALSE);
    return TRUE;
}

void CKFixedFunctionPipeline::BindTextures(CKRasterizerEncoder *encoder,
                                           const CKFFTextureBindingSet *bindingSet) {
    if (!encoder || !bindingSet) return;

    CKDWORD desiredTextures[CKFF_MAX_TEXTURE_STAGES] = {};
    for (CKDWORD i = 0; i < bindingSet->ActiveTextureCount; ++i)
        desiredTextures[i] = bindingSet->Bindings[i].Texture;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const bool collectStats = m_Probes.Config.StatsEnabled || m_Probes.Config.UniformHistEnabled;
    if (collectStats) {
        if (m_Probes.Stats.HasLastTextureSet &&
            CKFFTextureSetEquals(m_Probes.Stats.LastActiveTextureCount, m_Probes.Stats.LastTextureHandles,
                                 bindingSet->ActiveTextureCount, desiredTextures))
            ++m_Probes.Stats.ConsecutiveTextureSetRepeats;
        m_Probes.Stats.LastActiveTextureCount = bindingSet->ActiveTextureCount;
        memcpy(m_Probes.Stats.LastTextureHandles, desiredTextures, sizeof(desiredTextures));
        m_Probes.Stats.HasLastTextureSet = TRUE;
    }
#endif

    for (CKDWORD i = 0; i < bindingSet->ActiveTextureCount; ++i) {
        const CKFFRenderPacketTextureBinding &binding = bindingSet->Bindings[i];
        if (binding.Texture == 0)
            continue;
        CKSamplerDesc sampler = binding.Sampler;
        encoder->SetTexture(binding.Stage, binding.Uniform, binding.Texture, &sampler);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (collectStats)
            ++m_Probes.Stats.TextureBinds;
#endif
    }
}

CKDWORD CKFixedFunctionPipeline::SubmitDiscardFlags() const {
    return CKRST_DISCARD_ALL;
}

void CKFixedFunctionPipeline::LogAndResetFrameStats() {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const bool collectStats = m_Probes.Config.StatsEnabled || m_Probes.Config.UniformHistEnabled;
    if (!collectStats)
        return;

    m_Probes.Stats.DrawStateCacheHits = m_DrawStateCache.GetBuildCacheHits();
    m_Probes.Stats.DrawStateRebuilds = m_DrawStateCache.GetBuildRebuilds();
    if (m_Probes.Config.StatsEnabled && m_Probes.Stats.FrameIndex > 0 &&
        (m_Probes.Config.StatsInterval == 1 || (m_Probes.Stats.FrameIndex % (CKDWORD)m_Probes.Config.StatsInterval) == 0)) {
        const double vec4PerDraw = m_Probes.Stats.SubmittedDraws > 0
            ? (double)m_Probes.Stats.UniformVec4s / (double)m_Probes.Stats.SubmittedDraws
            : 0.0;
        const double uniformsPerDraw = m_Probes.Stats.SubmittedDraws > 0
            ? (double)m_Probes.Stats.UniformSets / (double)m_Probes.Stats.SubmittedDraws
            : 0.0;
        const double texBindsPerDraw = m_Probes.Stats.SubmittedDraws > 0
            ? (double)m_Probes.Stats.TextureBinds / (double)m_Probes.Stats.SubmittedDraws
            : 0.0;
        CK_LOG_FMT("FFPStats.Core",
                   "frame=%u sw=%u hw=%u submitted=%u prepareFail=%u programMiss=%u uniforms=%u uniformsPerDraw=%.2f vec4=%u vec4PerDraw=%.2f texBinds=%u texBindsPerDraw=%.2f layouts=%u vbSets=%u ibSets=%u transforms=%u repeatProgram=%u repeatState=%u repeatTexSet=%u repeatVB=%u repeatIB=%u repeatWorld=%u drawStateHits=%u drawStateRebuilds=%u transientVB=%u transientIB=%u",
                   m_Probes.Stats.FrameIndex,
                   m_Probes.Stats.SoftwareDraws,
                   m_Probes.Stats.HardwareDraws,
                   m_Probes.Stats.SubmittedDraws,
                   m_Probes.Stats.PrepareFailures,
                   m_Probes.Stats.ProgramMisses,
                   m_Probes.Stats.UniformSets,
                   uniformsPerDraw,
                   m_Probes.Stats.UniformVec4s,
                   vec4PerDraw,
                   m_Probes.Stats.TextureBinds,
                   texBindsPerDraw,
                   m_Probes.Stats.VertexLayoutSets,
                   m_Probes.Stats.VertexBufferSets,
                   m_Probes.Stats.IndexBufferSets,
                   m_Probes.Stats.TransformSets,
                   m_Probes.Stats.ConsecutiveProgramRepeats,
                   m_Probes.Stats.ConsecutiveDrawStateRepeats,
                   m_Probes.Stats.ConsecutiveTextureSetRepeats,
                   m_Probes.Stats.ConsecutiveVertexBufferRepeats,
                   m_Probes.Stats.ConsecutiveIndexBufferRepeats,
                   m_Probes.Stats.ConsecutiveWorldMatrixRepeats,
                   m_Probes.Stats.DrawStateCacheHits,
                   m_Probes.Stats.DrawStateRebuilds,
                   m_Probes.Stats.TransientVertexBytes,
                   m_Probes.Stats.TransientIndexBytes);
        CK_LOG_FMT("FFPStats.Packet",
                   "frame=%u q=%u replay=%u fb=%u flush=%u overflow=%u runs=%u maxRun=%u skipState=%u skipTex=%u skipUniform=%u staticUp=%u staticSkip=%u objectUp=%u objectSkip=%u skipVB=%u skipIB=%u staticBuild=%u staticReuse=%u staticIntern=%u sortSkip=%u adaptiveSamples=%u adaptiveBypasses=%u adaptiveRunBypasses=%u adaptiveCooldownBypasses=%u adaptiveCooldownFrames=%u adaptiveFrameEndEvals=%u adaptiveFrameEndRunBypasses=%u adaptiveSaved=%u adaptiveSampleRuns=%u adaptiveSampleMaxRun=%u adaptiveSubmitSaved=%u viewProjRebuild=%u instRuns=%u instPackets=%u instSubmits=%u instBytes=%u instAllocFail=%u submitSaved=%u instFallbacks=%u",
                   m_Probes.Stats.FrameIndex,
                   m_Probes.Stats.QueuedRenderPackets,
                   m_Probes.Stats.ReplayedRenderPackets,
                   m_Probes.Stats.RenderPacketFallbacks,
                   m_Probes.Stats.RenderPacketFlushes,
                   m_Probes.Stats.RenderPacketUniformOverflows,
                   m_Probes.Stats.RenderPacketRuns,
                   m_Probes.Stats.RenderPacketMaxRunLength,
                   m_Probes.Stats.RenderPacketSkippedStates,
                   m_Probes.Stats.RenderPacketSkippedTextures,
                   m_Probes.Stats.RenderPacketSkippedUniforms,
                   m_Probes.Stats.RenderPacketStaticUniformUploads,
                   m_Probes.Stats.RenderPacketStaticUniformSkips,
                   m_Probes.Stats.RenderPacketObjectUniformUploads,
                   m_Probes.Stats.RenderPacketObjectUniformSkips,
                   m_Probes.Stats.RenderPacketSkippedVertexBuffers,
                   m_Probes.Stats.RenderPacketSkippedIndexBuffers,
                   m_Probes.Stats.RenderPacketStaticPayloadBuilds,
                   m_Probes.Stats.RenderPacketStaticPayloadReuses,
                   m_Probes.Stats.RenderPacketStaticPayloadInterns,
                   m_Probes.Stats.RenderPacketSortSkips,
                   m_Probes.Stats.RenderPacketAdaptiveSamples,
                   m_Probes.Stats.RenderPacketAdaptiveBypasses,
                   m_Probes.Stats.RenderPacketAdaptiveRunBypasses,
                   m_Probes.Stats.RenderPacketAdaptiveCooldownBypasses,
                   m_Probes.Stats.RenderPacketAdaptiveCooldownFrames,
                   m_Probes.Stats.RenderPacketAdaptiveFrameEndEvaluations,
                   m_Probes.Stats.RenderPacketAdaptiveFrameEndRunBypasses,
                   m_Probes.Stats.RenderPacketAdaptiveSavedBindEstimate,
                   m_Probes.Stats.RenderPacketAdaptiveSampleRuns,
                   m_Probes.Stats.RenderPacketAdaptiveSampleMaxRun,
                   m_Probes.Stats.RenderPacketAdaptiveSubmitSavedEstimate,
                   m_Probes.Stats.RenderPacketViewProjectionRebuilds,
                   m_Probes.Stats.RenderPacketInstancedRuns,
                   m_Probes.Stats.RenderPacketInstancedPackets,
                   m_Probes.Stats.RenderPacketInstancedSubmits,
                   m_Probes.Stats.RenderPacketInstanceBufferBytes,
                   m_Probes.Stats.RenderPacketInstanceAllocFailures,
                   m_Probes.Stats.RenderPacketSubmitSavedEstimate,
                   m_Probes.Stats.RenderPacketInstancingFallbacks);
        CK_LOG_FMT("FFPStats.Timing",
                   "frame=%u prepareUs=%.1f stateUs=%.1f programUs=%.1f uniformUs=%.1f textureUs=%.1f transformUs=%.1f drawStateBuildUs=%.1f encoderStateUs=%.1f stencilUs=%.1f layoutUs=%.1f bufferBindUs=%.1f submitUs=%.1f packetBuildUs=%.1f packetSortUs=%.1f packetReplayUs=%.1f",
                   m_Probes.Stats.FrameIndex,
                   m_Probes.Stats.PrepareUs,
                   m_Probes.Stats.StateUs,
                   m_Probes.Stats.ProgramUs,
                   m_Probes.Stats.UniformUs,
                   m_Probes.Stats.TextureUs,
                   m_Probes.Stats.TransformUs,
                   m_Probes.Stats.DrawStateBuildUs,
                   m_Probes.Stats.EncoderStateUs,
                   m_Probes.Stats.StencilUs,
                   m_Probes.Stats.LayoutUs,
                   m_Probes.Stats.BufferBindUs,
                   m_Probes.Stats.SubmitUs,
                   m_Probes.Stats.RenderPacketBuildUs,
                   m_Probes.Stats.RenderPacketSortUs,
                   m_Probes.Stats.RenderPacketReplayUs);
        if (m_Probes.Config.UniformHistEnabled) {
            const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();
            for (CKDWORD slot = 0; slot < 64; ++slot) {
                if (m_Probes.Stats.UniformHandleSets[slot] == 0)
                    continue;
                CK_LOG_FMT("FFPUniformHist",
                           "frame=%u uniform=%u name=%s sets=%u vec4=%u",
                           m_Probes.Stats.FrameIndex,
                           slot,
                           CKFFUniformDebugName(u, slot),
                           m_Probes.Stats.UniformHandleSets[slot],
                           m_Probes.Stats.UniformHandleVec4s[slot]);
            }
        }
    }

    CKDWORD nextFrame = m_Probes.Stats.FrameIndex + 1;
    m_DrawStateCache.ResetBuildStats();
    memset(&m_Probes.Stats, 0, sizeof(m_Probes.Stats));
    m_Probes.Stats.FrameIndex = nextFrame;
#endif
}

CKSamplerDesc CKFixedFunctionPipeline::BuildSamplerDesc(int stage) const {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return CKFFBuildSamplerDesc(nullptr);
    CKSamplerDesc desc = CKFFBuildSamplerDesc(m_State.StageStates[stage]);
    if (m_DisableTextureFiltering) {
        desc.MinFilter = CKRST_FILTER_NEAREST;
        desc.MagFilter = CKRST_FILTER_NEAREST;
        desc.MipFilter = m_DisableMipmaps ? CKRST_FILTER_NONE : CKRST_FILTER_NEAREST;
    } else if (m_DisableMipmaps) {
        desc.MipFilter = CKRST_FILTER_NONE;
    } else if (m_ForceAnisotropicFiltering) {
        desc.MinFilter = CKRST_FILTER_ANISOTROPIC;
        desc.MagFilter = CKRST_FILTER_ANISOTROPIC;
        desc.MipFilter = CKRST_FILTER_ANISOTROPIC;
    }
    return desc;
}

float CKFixedFunctionPipeline::ComputeDepthKey() const {
    // Depth key = distance from camera (view-space Z of the world origin)
    float z = m_State.World[3][0] * m_State.View[0][2] +
              m_State.World[3][1] * m_State.View[1][2] +
              m_State.World[3][2] * m_State.View[2][2] +
              m_State.View[3][2];
    return z;
}

CKBOOL CKFixedFunctionPipeline::ResolveVertexBufferPacketProgram(CKDWORD dpFlags,
                                                                 CKDWORD formatFlags,
                                                                 CKFFPreparedState *preparedState,
                                                                 CKFFProgramContext *programContext)
{
    if (!preparedState || !programContext)
        return FALSE;

    const CKDWORD activeTextureCount = (CKDWORD)CKFFResolveActiveTextureCount(
        dpFlags, m_State.TextureHandles, m_State.StageStates);
    if (m_PacketProgramCacheValid &&
        m_PacketProgramCacheDPFlags == dpFlags &&
        m_PacketProgramCacheFormatFlags == formatFlags &&
        m_PacketProgramCacheActiveTextureCount == (int)activeTextureCount) {
        *preparedState = m_PacketProgramCachePreparedState;
        *programContext = m_PacketProgramCacheContext;
        return programContext->Program != 0 ? TRUE : FALSE;
    }

    BuildCurrentPreparedState(preparedState, dpFlags, activeTextureCount, formatFlags);
    CKFFShaderKey shaderKey = CKFFBuildCurrentShaderKey(preparedState);
    CKFFProgramBinding programBinding = m_ShaderCache.GetProgram(shaderKey);
    CKFFInitProgramContext(programContext, shaderKey, programBinding);
    m_PacketProgramCacheDPFlags = dpFlags;
    m_PacketProgramCacheFormatFlags = formatFlags;
    m_PacketProgramCacheActiveTextureCount = (int)activeTextureCount;
    m_PacketProgramCachePreparedState = *preparedState;
    m_PacketProgramCacheContext = *programContext;
    m_PacketProgramCacheValid = TRUE;
    return programContext->Program != 0 ? TRUE : FALSE;
}

void CKFixedFunctionPipeline::CaptureVertexBufferPacketIdentity(
    CKRenderPacket *packet,
    const CKFFProgramContext *programContext,
    CKRenderView view,
    VXPRIMITIVETYPE type,
    CKDWORD vb,
    CKDWORD ib,
    CKDWORD baseVertex,
    CKDWORD vertexCount,
    CKDWORD startIndex,
    CKDWORD indexCount,
    CKDWORD vertexLayout)
{
    if (!packet || !programContext)
        return;

    packet->Serial = m_OpaquePacketQueue.NextSerial();
    packet->View = view;
    packet->Type = type;
    packet->Program = programContext->Program;
    float depth = ComputeDepthKey();
    packet->Depth = *(CKDWORD *)&depth;
    packet->DrawState = m_DrawStateCache.BuildDrawState(type);
    packet->StencilRef = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILREF);
    packet->StencilReadMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILMASK);
    packet->StencilWriteMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK);
    packet->VertexLayout = vertexLayout;
    packet->VertexBuffer = vb;
    packet->IndexBuffer = ib;
    packet->BaseVertex = baseVertex;
    packet->VertexCount = vertexCount;
    packet->StartIndex = startIndex;
    packet->IndexCount = indexCount;
    packet->ActiveTextureCount = 0;
    packet->TextureSetHash = 0;
    packet->StaticUniformIndex = 0;
    packet->CanInstance = FALSE;
    packet->InstancedProgram = 0;
    memset(&packet->ObjectUniforms, 0, sizeof(packet->ObjectUniforms));
    packet->World = m_State.World;
    packet->Marker[0] = '\0';
}

void CKFixedFunctionPipeline::CaptureVertexBufferPacketTextures(CKRenderPacket *packet,
                                                                const CKFFTextureBindingSet *bindingSet)
{
    if (!packet || !bindingSet)
        return;

    packet->ActiveTextureCount = bindingSet->ActiveTextureCount;
    packet->TextureSetHash = bindingSet->Hash;
    for (CKDWORD i = 0; i < packet->ActiveTextureCount; ++i) {
        packet->Textures[i] = bindingSet->Bindings[i];
    }
}

CKBOOL CKFixedFunctionPipeline::CaptureVertexBufferPacketObjectUniforms(
    CKRenderPacket *packet,
    const CKFFProgramContext *programContext)
{
    if (!packet)
        return FALSE;

    if (packet->CanInstance) {
        UpdateViewProjectionCache();
        memset(&packet->ObjectUniforms, 0, sizeof(packet->ObjectUniforms));
        packet->ObjectUniforms.MatrixUniform = m_ShaderCache.GetUniforms().u_ffMatrices;
    } else {
        if (!BuildPacketObjectUniforms(&packet->ObjectUniforms, programContext))
            return FALSE;
    }
    packet->ViewProjection = m_State.ViewProjection();
    packet->ViewProjectionHash = m_State.ViewProjectionHash();
    return TRUE;
}

void CKFixedFunctionPipeline::CaptureVertexBufferPacketInstancing(
    CKRenderPacket *packet,
    const CKFFProgramContext *programContext)
{
    if (!packet || !programContext)
        return;
    if (GetVertexBufferPacketInstancingRejectReason(programContext) != CKFF_RENDER_PACKET_ELIGIBLE)
        return;

    CKFFShaderKey instancedKey = programContext->ShaderKey;
    instancedKey.VS.SetInstanced(true);
    CKFFProgramBinding instancedBinding = m_ShaderCache.GetProgram(instancedKey);
    CKFFProgramContext instancedContext;
    CKFFInitProgramContext(&instancedContext, instancedKey, instancedBinding);
    if (CKFFCanUseInstancedProgramForPacket(*programContext, instancedContext)) {
        packet->CanInstance = TRUE;
        packet->InstancedProgram = instancedContext.Program;
    }
}

CKBOOL CKFixedFunctionPipeline::CaptureVertexBufferPacketStaticUniforms(
    CKRenderPacket *packet,
    const CKFFProgramContext *programContext,
    CKBOOL collectStats)
{
    if (!packet || !programContext)
        return FALSE;

    if (m_OpaquePacketQueue.TryUseCachedStaticUniform(&packet->StaticUniformIndex)) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (collectStats)
            ++m_Probes.Stats.RenderPacketStaticPayloadReuses;
#endif
    } else {
        CKFFRenderPacketUniformPayload staticPayload;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        double buildTimer = collectStats ? CKRenderPerfNow() : 0.0;
#endif
        if (!BuildStaticUniformPayload(&staticPayload, programContext, packet->ActiveTextureCount)) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
            if (collectStats)
                ++m_Probes.Stats.RenderPacketUniformOverflows;
#endif
            return FALSE;
        }
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (collectStats) {
            m_Probes.Stats.RenderPacketBuildUs += CKRenderPerfElapsedUs(buildTimer);
            ++m_Probes.Stats.RenderPacketStaticPayloadBuilds;
        }
#endif
        packet->StaticUniformIndex = InternStaticUniformPayload(staticPayload);
        m_OpaquePacketQueue.CacheStaticUniform(packet->StaticUniformIndex);
    }

    return TRUE;
}

CKDWORD CKFixedFunctionPipeline::GetOpaqueVertexBufferPacketRejectReason(CKRenderView view,
                                                                         VXPRIMITIVETYPE type,
                                                                         CKDWORD vb,
                                                                         CKDWORD ib,
                                                                         CKDWORD vertexLayout) const
{
    if (!m_OpaqueSortingEnabled)
        return CKFF_RENDER_PACKET_REJECT_SORT_DISABLED;
    if (!m_OpaquePacketAllowed)
        return CKFF_RENDER_PACKET_REJECT_PACKETS_DISALLOWED;
    if (!m_Context)
        return CKFF_RENDER_PACKET_REJECT_NO_CONTEXT;
    if (!vb)
        return CKFF_RENDER_PACKET_REJECT_MISSING_VERTEX_BUFFER;
    if (!ib)
        return CKFF_RENDER_PACKET_REJECT_MISSING_INDEX_BUFFER;
    if (!vertexLayout)
        return CKFF_RENDER_PACKET_REJECT_MISSING_VERTEX_LAYOUT;
    if (view != CKRP_VIEW_OPAQUE3D)
        return CKFF_RENDER_PACKET_REJECT_WRONG_VIEW;
    if (type != VX_TRIANGLELIST)
        return CKFF_RENDER_PACKET_REJECT_WRONG_PRIMITIVE;
    if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHABLENDENABLE))
        return CKFF_RENDER_PACKET_REJECT_ALPHA_BLEND;
    if (!m_DrawStateCache.GetRenderState(VXRENDERSTATE_ZENABLE))
        return CKFF_RENDER_PACKET_REJECT_Z_DISABLED;
    if (!m_DrawStateCache.GetRenderState(VXRENDERSTATE_ZWRITEENABLE))
        return CKFF_RENDER_PACKET_REJECT_Z_WRITE_DISABLED;
    if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_VERTEXBLEND) != VXVBLEND_DISABLE)
        return CKFF_RENDER_PACKET_REJECT_VERTEX_BLEND;
    if (m_DrawStateCache.GetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE))
        return CKFF_RENDER_PACKET_REJECT_INDEXED_VERTEX_BLEND;
    if (m_OpaquePacketQueue.IsAdaptiveBypassed())
        return CKFF_RENDER_PACKET_REJECT_ADAPTIVE_BYPASS;
    return CKFF_RENDER_PACKET_ELIGIBLE;
}

void CKFixedFunctionPipeline::BuildVertexBufferPacket(
    CKFFVertexBufferPacketBuildResult *result,
    CKRasterizerEncoder *encoder,
    CKRenderView view,
    VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
    CKDWORD baseVertex, CKDWORD vertexCount,
    CKDWORD startIndex, CKDWORD indexCount,
    CKDWORD dpFlags, CKDWORD formatFlags,
    CKDWORD vertexLayout)
{
    if (!result)
        return;
    CKFFInitVertexBufferPacketBuildResult(result);
    if (!vb) {
        result->RejectReason = CKFF_RENDER_PACKET_REJECT_MISSING_VERTEX_BUFFER;
        return;
    }

    CKBOOL collectStats = FALSE;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    collectStats = (m_Probes.Config.StatsEnabled || m_Probes.Config.UniformHistEnabled) ? TRUE : FALSE;
    if (collectStats)
        ++m_Probes.Stats.HardwareDraws;
#endif

    CKFFPreparedState preparedState;
    CKFFProgramContext programContext;
    if (!ResolveVertexBufferPacketProgram(dpFlags, formatFlags, &preparedState, &programContext)) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (collectStats)
            ++m_Probes.Stats.ProgramMisses;
#endif
        result->ProgramContext = programContext;
        result->RejectReason = CKFF_RENDER_PACKET_REJECT_PROGRAM_MISSING;
        return;
    }
    result->ProgramContext = programContext;
    BuildCurrentTextureBindingSet(&result->TextureBindingSet, preparedState.ActiveTextureCount);
    result->RejectReason = GetPacketObjectUniformRejectReason(&programContext);
    if (result->RejectReason != CKFF_RENDER_PACKET_ELIGIBLE) {
        return;
    }

    InitVertexBufferPacketForCapture(&result->Packet);
    CaptureVertexBufferPacketIdentity(&result->Packet, &programContext, view, type, vb, ib,
                                      baseVertex, vertexCount, startIndex, indexCount, vertexLayout);
    CaptureVertexBufferPacketTextures(&result->Packet, &result->TextureBindingSet);
    CaptureVertexBufferPacketInstancing(&result->Packet, &programContext);

#if CKRE_ENABLE_FFP_DIAGNOSTICS
    double buildTimer = collectStats ? CKRenderPerfNow() : 0.0;
#endif
    if (!CaptureVertexBufferPacketObjectUniforms(&result->Packet, &programContext)) {
        result->RejectReason = CKFF_RENDER_PACKET_REJECT_OBJECT_UNIFORMS;
        return;
    }
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (collectStats)
        m_Probes.Stats.RenderPacketBuildUs += CKRenderPerfElapsedUs(buildTimer);
#endif

    if (!CaptureVertexBufferPacketStaticUniforms(&result->Packet, &programContext, collectStats)) {
        result->RejectReason = CKFF_RENDER_PACKET_REJECT_STATIC_UNIFORMS;
        return;
    }

    if (encoder)
        encoder->ConsumeMarker(result->Packet.Marker, sizeof(result->Packet.Marker));
    BuildRenderPacketSortKey(&result->Packet);
    result->Success = TRUE;
    result->RejectReason = CKFF_RENDER_PACKET_ELIGIBLE;
}

void CKFixedFunctionPipeline::ClearOpaqueRenderPackets()
{
    m_OpaquePacketQueue.Clear();
}

void CKFixedFunctionPipeline::ResetOpaqueRenderPacketFrameState()
{
    m_OpaquePacketQueue.ResetFrameState();
    UpdateOpaqueRenderPacketAdaptiveStats();
}

void CKFixedFunctionPipeline::SortOpaqueRenderPackets(XArray<CKDWORD> &indices)
{
    m_OpaquePacketQueue.SortPackets(indices);
}

void CKFixedFunctionPipeline::InitRenderPacketReplayContext(CKFFRenderPacketReplayContext *context,
                                                            CKRasterizerEncoder *encoder)
{
    if (!context)
        return;

    memset(context, 0, sizeof(CKFFRenderPacketReplayContext));
    context->Encoder = encoder;
    context->Context = m_Context;
    context->Queue = &m_OpaquePacketQueue;
    context->InstanceLayout = m_InstanceLayout;
    CKFFInitRenderPacketReplayDiagnostics(&context->Diagnostics);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    context->Diagnostics.StatsEnabled =
        m_Probes.Config.StatsEnabled ? TRUE : FALSE;
    context->Diagnostics.UniformHistEnabled =
        m_Probes.Config.UniformHistEnabled ? TRUE : FALSE;
    context->Diagnostics.Uniforms = &m_ShaderCache.GetUniforms();
    context->Diagnostics.UniformSets = &m_Probes.Stats.UniformSets;
    context->Diagnostics.UniformVec4s = &m_Probes.Stats.UniformVec4s;
    context->Diagnostics.UniformHandleSets = m_Probes.Stats.UniformHandleSets;
    context->Diagnostics.UniformHandleVec4s = m_Probes.Stats.UniformHandleVec4s;
    context->Diagnostics.TextureBinds = &m_Probes.Stats.TextureBinds;
    context->Diagnostics.VertexLayoutSets = &m_Probes.Stats.VertexLayoutSets;
    context->Diagnostics.VertexBufferSets = &m_Probes.Stats.VertexBufferSets;
    context->Diagnostics.IndexBufferSets = &m_Probes.Stats.IndexBufferSets;
    context->Diagnostics.TransformSets = &m_Probes.Stats.TransformSets;
    context->Diagnostics.SubmittedDraws = &m_Probes.Stats.SubmittedDraws;
    context->Diagnostics.ReplayedRenderPackets = &m_Probes.Stats.ReplayedRenderPackets;
    context->Diagnostics.RenderPacketSkippedStates = &m_Probes.Stats.RenderPacketSkippedStates;
    context->Diagnostics.RenderPacketSkippedTextures = &m_Probes.Stats.RenderPacketSkippedTextures;
    context->Diagnostics.RenderPacketSkippedUniforms = &m_Probes.Stats.RenderPacketSkippedUniforms;
    context->Diagnostics.RenderPacketStaticUniformUploads = &m_Probes.Stats.RenderPacketStaticUniformUploads;
    context->Diagnostics.RenderPacketStaticUniformSkips = &m_Probes.Stats.RenderPacketStaticUniformSkips;
    context->Diagnostics.RenderPacketObjectUniformUploads = &m_Probes.Stats.RenderPacketObjectUniformUploads;
    context->Diagnostics.RenderPacketSkippedVertexBuffers = &m_Probes.Stats.RenderPacketSkippedVertexBuffers;
    context->Diagnostics.RenderPacketSkippedIndexBuffers = &m_Probes.Stats.RenderPacketSkippedIndexBuffers;
    context->Diagnostics.RenderPacketInstancedRuns = &m_Probes.Stats.RenderPacketInstancedRuns;
    context->Diagnostics.RenderPacketInstancedPackets = &m_Probes.Stats.RenderPacketInstancedPackets;
    context->Diagnostics.RenderPacketInstancedSubmits = &m_Probes.Stats.RenderPacketInstancedSubmits;
    context->Diagnostics.RenderPacketInstanceBufferBytes = &m_Probes.Stats.RenderPacketInstanceBufferBytes;
    context->Diagnostics.RenderPacketInstanceAllocFailures = &m_Probes.Stats.RenderPacketInstanceAllocFailures;
    context->Diagnostics.RenderPacketSubmitSavedEstimate = &m_Probes.Stats.RenderPacketSubmitSavedEstimate;
    context->Diagnostics.RenderPacketInstancingFallbacks = &m_Probes.Stats.RenderPacketInstancingFallbacks;
#endif
}

void CKFixedFunctionPipeline::FlushOpaqueRenderPackets(CKRasterizerEncoder *encoder,
                                                       CKBOOL forceDirectReplay,
                                                       CKBOOL allowAdaptiveLearning)
{
    if (!HasOpaqueRenderPackets())
        return;
    if (!encoder)
        encoder = m_RenderPipeline.GetEncoder();
    if (!encoder) {
        ClearOpaqueRenderPackets();
        return;
    }

    const int packetCount = m_OpaquePacketQueue.GetPacketCount();
    const CKBOOL directReplay = m_OpaquePacketQueue.IsDirectReplay(forceDirectReplay);
    XArray<CKDWORD> indices;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    double packetTimer = m_Probes.Config.StatsEnabled ? CKRenderPerfNow() : 0.0;
#endif
    if (directReplay) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (packetCount > 1)
            ++m_Probes.Stats.RenderPacketSortSkips;
#endif
    } else {
        SortOpaqueRenderPackets(indices);
    }
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (m_Probes.Config.StatsEnabled)
        m_Probes.Stats.RenderPacketSortUs += CKRenderPerfElapsedUs(packetTimer);
#endif

    if (allowAdaptiveLearning) {
        m_OpaquePacketQueue.EvaluateAdaptiveFrameEnd(m_OpaqueInstancingEnabled);
        UpdateOpaqueRenderPacketAdaptiveStats();
    }

    CKRenderPacketReplayCache cache;
    memset(&cache, 0, sizeof(cache));
    CKFFRenderPacketReplayContext replayContext;
    InitRenderPacketReplayContext(&replayContext, encoder);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (m_Probes.Config.StatsEnabled) {
        CKDWORD runCount = 0;
        CKDWORD maxRun = 0;
        m_OpaquePacketQueue.GetRunStats(&indices, directReplay, &runCount, &maxRun);
        m_Probes.Stats.RenderPacketRuns += runCount;
        if (maxRun > m_Probes.Stats.RenderPacketMaxRunLength)
            m_Probes.Stats.RenderPacketMaxRunLength = maxRun;
        packetTimer = CKRenderPerfNow();
    }
#endif
    XArray<CKFFRenderPacketRunPlan> runPlans;
    m_OpaquePacketQueue.BuildRunPlans(&indices, directReplay, m_OpaqueInstancingEnabled, runPlans);
    const int planCount = runPlans.Size();
    for (int planIndex = 0; planIndex < planCount; ++planIndex) {
        const CKFFRenderPacketRunPlan &plan = runPlans[planIndex];
        const CKBOOL lastPlan = (planIndex + 1 == planCount) ? TRUE : FALSE;
        if (plan.Instanced) {
            if (CKFFReplayVertexBufferPacketRunInstanced(&replayContext, &indices,
                                                         plan.Start, plan.Count,
                                                         directReplay, &cache, lastPlan)) {
                continue;
            }
        }

        CKFFReplayVertexBufferPacketRange(&replayContext, &indices, plan.Start, 0,
                                          plan.Count, directReplay, &cache, lastPlan);
    }
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (m_Probes.Config.StatsEnabled)
        m_Probes.Stats.RenderPacketReplayUs += CKRenderPerfElapsedUs(packetTimer);
#endif

    ClearOpaqueRenderPackets();
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    ++m_Probes.Stats.RenderPacketFlushes;
#endif
}
