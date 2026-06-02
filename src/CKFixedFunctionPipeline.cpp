#include "CKFixedFunctionPipeline.h"
#include "CKRasterizer.h"
#include "CKFFUniformState.h"
#include "CKFFShaderABI.h"
#include "CKDebugLogger.h"
#include "CKRenderSettings.h"
#include "CKRenderPerfStats.h"

#include <cmath>
#include <cstring>

static CKDWORD CKFFShaderKeyVertexBlendMode(const CKFFShaderKeyVS &vs) {
    return (CKDWORD)((vs.Bits >> 35) & 3u);
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

CKFixedFunctionPipeline::CKFixedFunctionPipeline()
    : m_Context(nullptr), m_ActiveLightCount(0), m_CurrentActiveTextureCount(0),
      m_DisableTextureFiltering(FALSE), m_DisableMipmaps(FALSE),
      m_CurrentLightingEnabled(false), m_AlphaTestPrecision(0), m_DirtyFlags(CKFF_DIRTY_ALL),
      m_OpaqueInstancingEnabled(TRUE), m_InstanceLayout(0),
      m_OpaqueSortingEnabled(FALSE), m_OpaquePacketAllowed(TRUE) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const CKRenderFFPStatsConfig &settings = CKRenderDiagnosticsSettings().FFPStats;
    m_DiagnosticConfig.StatsEnabled = settings.Enabled;
    m_DiagnosticConfig.UniformHistEnabled = settings.UniformHistogram;
    m_DiagnosticConfig.StatsInterval = settings.Interval;
#endif
    m_OpaqueSortingEnabled = CKRenderFFPSettings().GetBool("SortOpaqueObjects", false) ? TRUE : FALSE;
    m_OpaqueInstancingEnabled = CKRenderFFPSettings().GetBool("InstanceOpaqueObjects", true) ? TRUE : FALSE;

    Vx3DMatrixIdentity(m_World);
    Vx3DMatrixIdentity(m_View);
    Vx3DMatrixIdentity(m_Projection);
    Vx3DMatrixIdentity(m_ViewProjection);
    m_ViewProjectionHash = 0;
    m_ViewProjectionDirty = TRUE;
    for (int i = 0; i < CKFF_MAX_TEXTURE_STAGES; i++)
        Vx3DMatrixIdentity(m_TexMatrix[i]);
    for (int i = 0; i < CKFF_VERTEX_BLEND_MATRIX_COUNT; ++i) {
        Vx3DMatrixIdentity(m_VertexBlendMatrices[i]);
        m_VertexBlendMatrixSet[i] = FALSE;
    }
    memset(&m_PacketTextureSetCache, 0, sizeof(m_PacketTextureSetCache));
    m_PacketTextureSetCache.Dirty = TRUE;
    m_PacketProgramCacheValid = FALSE;
    m_PacketProgramCacheDPFlags = 0;
    m_PacketProgramCacheFormatFlags = 0;
    m_PacketProgramCacheActiveTextureCount = 0;
    memset(&m_PacketProgramCacheContext, 0, sizeof(m_PacketProgramCacheContext));
    ResetMaterial();
    memset(m_Lights, 0, sizeof(m_Lights));
    memset(m_LightEnabled, 0, sizeof(m_LightEnabled));
    memset(m_TextureHandles, 0, sizeof(m_TextureHandles));
    memset(m_TextureFlags, 0, sizeof(m_TextureFlags));
    memset(m_StageStates, 0, sizeof(m_StageStates));
    memset(m_UserClipPlanes, 0, sizeof(m_UserClipPlanes));
    ResetTexcoordComponentCounts();
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
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    memset(&m_FrameStats, 0, sizeof(m_FrameStats));
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
    m_DirtyFlags = CKFF_DIRTY_ALL;
    m_ViewProjectionDirty = TRUE;
    MarkPacketTextureSetDirty();
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

void CKFixedFunctionPipeline::SetRenderOptions(CKBOOL DisableTextureFiltering, CKBOOL DisableMipmaps) {
    if (m_DisableTextureFiltering == DisableTextureFiltering &&
        m_DisableMipmaps == DisableMipmaps)
        return;
    m_DisableTextureFiltering = DisableTextureFiltering;
    m_DisableMipmaps = DisableMipmaps;
    MarkPacketTextureSetDirty();
}

void CKFixedFunctionPipeline::SetAlphaTestPrecision(CKDWORD precision) {
    precision &= 0xFu;
    if (m_AlphaTestPrecision == precision)
        return;
    m_AlphaTestPrecision = precision;
    m_DirtyFlags |= CKFF_DIRTY_ALPHATEST;
    MarkStaticUniformsDirty();
}

CKDWORD CKFixedFunctionPipeline::GetAlphaTestPrecision() const {
    return m_AlphaTestPrecision;
}

void CKFixedFunctionPipeline::SetVertexBlendMatrix(CKDWORD index, const VxMatrix &matrix) {
    if (index >= CKFF_VERTEX_BLEND_MATRIX_COUNT)
        return;
    m_VertexBlendMatrices[index] = matrix;
    m_VertexBlendMatrixSet[index] = TRUE;
    m_DirtyFlags |= CKFF_DIRTY_MATRICES;
}

void CKFixedFunctionPipeline::ResetVertexBlendMatrices() {
    for (int i = 0; i < CKFF_VERTEX_BLEND_MATRIX_COUNT; ++i) {
        Vx3DMatrixIdentity(m_VertexBlendMatrices[i]);
        m_VertexBlendMatrixSet[i] = FALSE;
    }
    m_DirtyFlags |= CKFF_DIRTY_MATRICES;
}

void CKFixedFunctionPipeline::SetTexcoordComponentCount(CKDWORD stage, CKDWORD count) {
    if (stage >= CKFF_MAX_TEXTURE_STAGES)
        return;
    CKBYTE componentCount = CKFFTexcoordComponentCount(count);
    if (m_TexcoordComponentCounts[stage] == componentCount)
        return;
    m_TexcoordComponentCounts[stage] = componentCount;
    MarkPacketProgramDirty();
}

void CKFixedFunctionPipeline::ResetTexcoordComponentCounts() {
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        m_TexcoordComponentCounts[stage] = 2;
    MarkPacketProgramDirty();
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

void CKFixedFunctionPipeline::MarkPacketTextureSetDirty()
{
    m_PacketTextureSetCache.Dirty = TRUE;
}

void CKFixedFunctionPipeline::MarkPacketProgramDirty()
{
    m_PacketProgramCacheValid = FALSE;
}

void CKFixedFunctionPipeline::UpdatePacketTextureSetCache()
{
    if (!m_PacketTextureSetCache.Dirty &&
        m_PacketTextureSetCache.ActiveTextureCount == (CKDWORD)m_CurrentActiveTextureCount)
        return;

    m_PacketTextureSetCache.ActiveTextureCount = (CKDWORD)m_CurrentActiveTextureCount;
    if (m_PacketTextureSetCache.ActiveTextureCount > CKFF_MAX_TEXTURE_STAGES)
        m_PacketTextureSetCache.ActiveTextureCount = CKFF_MAX_TEXTURE_STAGES;

    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();
    for (CKDWORD i = 0; i < m_PacketTextureSetCache.ActiveTextureCount; ++i) {
        const bool cube = (m_TextureFlags[i] & CKRST_TEXTURE_CUBEMAP) != 0;
        const bool volume = (m_TextureFlags[i] & CKRST_TEXTURE_VOLUMEMAP) != 0;
        m_PacketTextureSetCache.Textures[i].Stage =
            CKFFSamplerBindStage(i, cube ? CKFF_SAMPLER_CUBE :
                                    (volume ? CKFF_SAMPLER_VOLUME : CKFF_SAMPLER_2D));
        m_PacketTextureSetCache.Textures[i].Uniform =
            cube ? u.s_textureCube[i] :
            (volume ? u.s_textureVolume[i] : u.s_texture[i]);
        m_PacketTextureSetCache.Textures[i].Texture = m_TextureHandles[i];
        m_PacketTextureSetCache.Textures[i].TextureFlags = m_TextureFlags[i];
        m_PacketTextureSetCache.Textures[i].Sampler = BuildSamplerDesc((int)i);
    }

    m_PacketTextureSetCache.TextureSetHash =
        CKFFHashRenderPacketTextureSet(m_PacketTextureSetCache.ActiveTextureCount,
                                       m_PacketTextureSetCache.Textures);
    m_PacketTextureSetCache.Dirty = FALSE;
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
    if (CKFFRenderStateAffectsProgram(state))
        MarkPacketProgramDirty();
    MarkStaticUniformsDirty();
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

    m_TextureHandles[stage] = 0;
    m_TextureFlags[stage] = 0;
    memset(m_StageStates[stage], 0, sizeof(m_StageStates[stage]));
    m_StageStates[stage][CKRST_TSS_TEXCOORDINDEX] = (CKDWORD)stage;
    m_StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS] = CKRST_TTF_NONE;
    Vx3DMatrixIdentity(m_TexMatrix[stage]);
    MarkPacketTextureSetDirty();
    MarkPacketProgramDirty();
    MarkStaticUniformsDirty();
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
    snapshot.TextureFlags = m_TextureFlags[stage];
    memcpy(snapshot.States, m_StageStates[stage], sizeof(snapshot.States));
    snapshot.TextureMatrix = m_TexMatrix[stage];
}

void CKFixedFunctionPipeline::RestoreTextureStage(int stage, const CKFFTextureStageSnapshot &snapshot) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return;

    m_TextureHandles[stage] = snapshot.Texture;
    m_TextureFlags[stage] = snapshot.TextureFlags;
    memcpy(m_StageStates[stage], snapshot.States, sizeof(m_StageStates[stage]));
    m_TexMatrix[stage] = snapshot.TextureMatrix;
    MarkPacketTextureSetDirty();
    MarkPacketProgramDirty();
    MarkStaticUniformsDirty();
}

void CKFixedFunctionPipeline::SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type, CKDWORD value) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return;
    if ((int)type >= CKFF_MAX_TEXTURE_STAGE_STATES) return;
    if (m_StageStates[stage][(int)type] == value)
        return;

    if (type == CKRST_TSS_STAGEBLEND && value == 0 && stage > 0) {
        DisableTextureStagesFrom(stage);
        return;
    }

    m_StageStates[stage][(int)type] = value;

    if (type == CKRST_TSS_TEXTUREMAPBLEND) {
        ClearExplicitTextureCombineState(m_StageStates[stage]);
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
            m_StageStates[stage][CKRST_TSS_OP] = colorOp;
            m_StageStates[stage][CKRST_TSS_ARG1] = colorArg1;
            m_StageStates[stage][CKRST_TSS_ARG2] = colorArg2;
            m_StageStates[stage][CKRST_TSS_AOP] = alphaOp;
            m_StageStates[stage][CKRST_TSS_AARG1] = alphaArg1;
            m_StageStates[stage][CKRST_TSS_AARG2] = alphaArg2;
        }
    }
    MarkPacketTextureSetDirty();
    MarkPacketProgramDirty();
    MarkStaticUniformsDirty();
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
    MarkStaticUniformsDirty();
}

void CKFixedFunctionPipeline::SetUserClipPlane(int index, const VxPlane &plane) {
    if (index < 0 || index >= 6)
        return;
    m_UserClipPlanes[index] = plane;
    MarkStaticUniformsDirty();
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
        m_ViewProjectionDirty = TRUE;
        MarkStaticUniformsDirty();
        break;
    case VXMATRIX_PROJECTION:
        m_Projection = matrix;
        m_DirtyFlags |= CKFF_DIRTY_MATRICES;
        m_ViewProjectionDirty = TRUE;
        MarkStaticUniformsDirty();
        break;
    default:
        if (type >= VXMATRIX_TEXTURE0 && type <= VXMATRIX_TEXTURE7) {
            int idx = type - VXMATRIX_TEXTURE0;
            if (idx < CKFF_MAX_TEXTURE_STAGES) {
                m_TexMatrix[idx] = matrix;
                MarkStaticUniformsDirty();
            }
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
    MarkStaticUniformsDirty();
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
    MarkStaticUniformsDirty();
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
    MarkStaticUniformsDirty();
}

void CKFixedFunctionPipeline::EnableLight(int index, CKBOOL enable) {
    if (index < 0 || index >= CKFF_MAX_LIGHTS) return;
    if (m_LightEnabled[index] == enable)
        return;
    m_LightEnabled[index] = enable;

    m_ActiveLightCount = 0;
    for (int i = 0; i < CKFF_MAX_LIGHTS; i++) {
        if (m_LightEnabled[i]) m_ActiveLightCount++;
    }
    m_DirtyFlags |= CKFF_DIRTY_LIGHTS;
    MarkPacketProgramDirty();
    MarkStaticUniformsDirty();
}

void CKFixedFunctionPipeline::SetTexture(int stage, CKDWORD textureHandle) {
    SetTexture(stage, textureHandle, textureHandle != 0 ? CKRST_TEXTURE_VALID : 0);
}

void CKFixedFunctionPipeline::SetTexture(int stage, CKDWORD textureHandle, CKDWORD textureFlags) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return;
    const CKDWORD normalizedFlags = textureHandle != 0 ? textureFlags : 0;
    if (m_TextureHandles[stage] == textureHandle && m_TextureFlags[stage] == normalizedFlags)
        return;
    const CKBOOL oldHasTexture = m_TextureHandles[stage] != 0 ? TRUE : FALSE;
    const CKBOOL newHasTexture = textureHandle != 0 ? TRUE : FALSE;
    const CKDWORD oldStaticFlags = CKFFStaticTextureFlags(m_TextureFlags[stage]);
    const CKDWORD newStaticFlags = CKFFStaticTextureFlags(normalizedFlags);
    m_TextureHandles[stage] = textureHandle;
    m_TextureFlags[stage] = normalizedFlags;
    MarkPacketTextureSetDirty();
    if (oldHasTexture != newHasTexture || oldStaticFlags != newStaticFlags) {
        MarkPacketProgramDirty();
        MarkStaticUniformsDirty();
    }
}

CKDWORD CKFixedFunctionPipeline::GetTexture(int stage) const {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return 0;
    return m_TextureHandles[stage];
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
    const bool collectStats = m_DiagnosticConfig.StatsEnabled || m_DiagnosticConfig.UniformHistEnabled;
    if (collectStats)
        ++m_FrameStats.SoftwareDraws;
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
        debugInfo.World = &m_World;
        debugInfo.ViewMatrix = &m_View;
        debugInfo.Projection = &m_Projection;
        debugInfo.Viewport = m_Viewport;
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
    pointParams.World = m_World;
    pointParams.View = m_View;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const bool statsTiming = m_DiagnosticConfig.StatsEnabled;
    double statsStart = 0.0;
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    if (!m_TransientGeometry.Prepare(
            encoder, type, indices, indexCount, data, wrapMode,
            m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE), &pointParams,
            m_TexcoordComponentCounts)) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (debugLogging)
            m_DebugState.LogDrawPrimitivePrepareFailed();
        if (collectStats)
            ++m_FrameStats.PrepareFailures;
#endif
        return;
    }
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_FrameStats.PrepareUs += CKRenderPerfElapsedUs(statsStart);
    if (collectStats) {
        m_FrameStats.TransientVertexBytes += m_TransientGeometry.GetLastVertexBytes();
        m_FrameStats.TransientIndexBytes += m_TransientGeometry.GetLastIndexBytes();
    }
#endif

    // Build the fixed-function state description and select the matching program.
    m_CurrentActiveTextureCount = CKFFResolveActiveTextureCount(data->Flags, m_TextureHandles, m_StageStates);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    CKFFStateDesc stateDesc = BuildCurrentStateDesc(data->Flags, formatFlags, m_TexcoordComponentCounts);
    CKFFShaderKey shaderKey = BuildCurrentShaderKey(stateDesc);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_FrameStats.StateUs += CKRenderPerfElapsedUs(statsStart);
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    CKFFProgramBinding programBinding = m_ShaderCache.GetProgram(shaderKey);
    SetCurrentProgramBinding(shaderKey, programBinding);
    CKDWORD program = programBinding.Program;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_FrameStats.ProgramUs += CKRenderPerfElapsedUs(statsStart);
#endif
    if (program == 0) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (debugLogging)
            m_DebugState.LogDrawPrimitiveProgramMissing();
        if (collectStats)
            ++m_FrameStats.ProgramMisses;
#endif
        return;
    }
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (collectStats) {
        if (m_FrameStats.HasLastProgram && m_FrameStats.LastProgram == program)
            ++m_FrameStats.ConsecutiveProgramRepeats;
        m_FrameStats.LastProgram = program;
        m_FrameStats.HasLastProgram = TRUE;
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
        debugInfo.World = &m_World;
        debugInfo.ViewMatrix = &m_View;
        debugInfo.Projection = &m_Projection;
        debugInfo.Viewport = m_Viewport;
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
    }
#endif

    // Upload uniforms
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    UploadUniforms(encoder);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
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
#endif
    CKDWORD transformIdx = m_Context->AllocTransform(&m_World, 1);
    encoder->SetTransform(transformIdx, 1);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (collectStats)
        ++m_FrameStats.TransformSets;
    if (statsTiming)
        m_FrameStats.TransformUs += CKRenderPerfElapsedUs(statsStart);

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
#else
    const CKDWORD stencilRef = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILREF);
    const CKDWORD stencilReadMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILMASK);
    const CKDWORD stencilWriteMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK);
#endif
    encoder->SetState(drawState);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_FrameStats.EncoderStateUs += CKRenderPerfElapsedUs(statsStart);
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    encoder->SetStencilRef(stencilRef);
    encoder->SetStencilMask(stencilReadMask, stencilWriteMask);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_FrameStats.StencilUs += CKRenderPerfElapsedUs(statsStart);

    // Bind textures
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    BindTextures(encoder);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_FrameStats.TextureUs += CKRenderPerfElapsedUs(statsStart);
#endif

    // Submit
    float depth = ComputeDepthKey();
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    encoder->Submit(view, program, *(CKDWORD *)&depth, SubmitDiscardFlags());
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_FrameStats.SubmitUs += CKRenderPerfElapsedUs(statsStart);
    if (collectStats)
        ++m_FrameStats.SubmittedDraws;
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
        CKRenderPacket packet;
        InitVertexBufferPacketForCapture(&packet);
        if (BuildVertexBufferPacket(encoder, &packet, view, type, vb, ib,
                                    baseVertex, vertexCount,
                                    startIndex, indexCount,
                                    dpFlags, formatFlags,
                                    vertexLayout)) {
            TrackOpaqueRenderPacket(packet);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
            if (m_DiagnosticConfig.StatsEnabled || m_DiagnosticConfig.UniformHistEnabled)
                ++m_FrameStats.QueuedRenderPackets;
#endif
            CheckOpaqueRenderPacketAdaptiveBypass(encoder);
            return;
        }
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (m_DiagnosticConfig.StatsEnabled || m_DiagnosticConfig.UniformHistEnabled)
            ++m_FrameStats.RenderPacketFallbacks;
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
    const bool collectStats = m_DiagnosticConfig.StatsEnabled || m_DiagnosticConfig.UniformHistEnabled;
    if (collectStats)
        ++m_FrameStats.HardwareDraws;

    // Build the fixed-function state description from the actual mesh vertex format.
    const bool debugLogging = m_DebugState.AnyLoggingEnabled();
    const int debugDrawSerial = debugLogging ? m_DebugState.NextDrawSerial(view) : -1;
    if (debugLogging) {
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
    }
#endif

    m_CurrentActiveTextureCount = CKFFResolveActiveTextureCount(dpFlags, m_TextureHandles, m_StageStates);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const bool statsTiming = m_DiagnosticConfig.StatsEnabled;
    double statsStart = 0.0;
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    CKFFStateDesc stateDesc = BuildCurrentStateDesc(dpFlags, formatFlags);
    CKFFShaderKey shaderKey = BuildCurrentShaderKey(stateDesc);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_FrameStats.StateUs += CKRenderPerfElapsedUs(statsStart);
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    CKFFProgramBinding programBinding = m_ShaderCache.GetProgram(shaderKey);
    SetCurrentProgramBinding(shaderKey, programBinding);
    CKDWORD program = programBinding.Program;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_FrameStats.ProgramUs += CKRenderPerfElapsedUs(statsStart);
#endif
    if (program == 0) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (collectStats)
            ++m_FrameStats.ProgramMisses;
#endif
        return;
    }
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (collectStats) {
        if (m_FrameStats.HasLastProgram && m_FrameStats.LastProgram == program)
            ++m_FrameStats.ConsecutiveProgramRepeats;
        m_FrameStats.LastProgram = program;
        m_FrameStats.HasLastProgram = TRUE;
    }

    if (debugLogging) {
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
    }
#endif

    // Upload uniforms
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    UploadUniforms(encoder);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
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
#endif
    CKDWORD transformIdx = m_Context->AllocTransform(&m_World, 1);
    encoder->SetTransform(transformIdx, 1);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (collectStats)
        ++m_FrameStats.TransformSets;
    if (statsTiming)
        m_FrameStats.TransformUs += CKRenderPerfElapsedUs(statsStart);

    // Set draw state
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    CKDrawState drawState = m_DrawStateCache.BuildDrawState(type);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
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
#else
    const CKDWORD stencilRef = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILREF);
    const CKDWORD stencilReadMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILMASK);
    const CKDWORD stencilWriteMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK);
#endif
    encoder->SetState(drawState);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_FrameStats.EncoderStateUs += CKRenderPerfElapsedUs(statsStart);
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    encoder->SetStencilRef(stencilRef);
    encoder->SetStencilMask(stencilReadMask, stencilWriteMask);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_FrameStats.StencilUs += CKRenderPerfElapsedUs(statsStart);
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
            ++m_FrameStats.VertexLayoutSets;
        if (statsTiming)
            m_FrameStats.LayoutUs += CKRenderPerfElapsedUs(statsStart);
#endif
    }

    // Bind buffers
#if CKRE_ENABLE_FFP_DIAGNOSTICS
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
#endif
    encoder->SetVertexBuffer(0, vb, baseVertex, vertexCount);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (collectStats)
        ++m_FrameStats.VertexBufferSets;
#endif
    if (ib) {
        encoder->SetIndexBuffer(ib, startIndex, indexCount);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (collectStats)
            ++m_FrameStats.IndexBufferSets;
#endif
    }
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_FrameStats.BufferBindUs += CKRenderPerfElapsedUs(statsStart);

    // Bind textures
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    BindTextures(encoder);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_FrameStats.TextureUs += CKRenderPerfElapsedUs(statsStart);
#endif

    // Submit
    float depth = ComputeDepthKey();
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        statsStart = CKRenderPerfNow();
#endif
    encoder->Submit(view, program, *(CKDWORD *)&depth, SubmitDiscardFlags());
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (statsTiming)
        m_FrameStats.SubmitUs += CKRenderPerfElapsedUs(statsStart);
    if (collectStats)
        ++m_FrameStats.SubmittedDraws;
#endif
}

// ============================================================================
// Internal methods
// ============================================================================

CKFFStateDesc CKFixedFunctionPipeline::BuildCurrentStateDesc(
    CKDWORD dpFlags, CKDWORD formatFlags, const CKBYTE *texcoordComponentCounts) {
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
    m_CurrentLightingEnabled = stateDesc.VS.GetLightingEnabled();
    stateDesc.VS.SetSpecularEnabled(specular != 0);
    stateDesc.VS.SetNormalizeNormals(normalize != 0);
    stateDesc.VS.SetLocalViewer(stateDesc.VS.GetLightingEnabled() &&
                                m_DrawStateCache.GetRenderState(VXRENDERSTATE_LOCALVIEWER) != 0);
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
        const bool stageActive = stage < m_CurrentActiveTextureCount;
        const bool hasTexture = stageActive && m_TextureHandles[stage] != 0;
        const CKDWORD colorOp = CKFFResolveStageColorOp(m_StageStates[stage], stageActive, hasTexture);
        const CKDWORD alphaOp = CKFFResolveStageAlphaOp(m_StageStates[stage], stageActive, hasTexture);
        stateDesc.FS.SetStageColorOp(stage, colorOp);
        stateDesc.FS.SetStageColorArg0(stage, CKFFResolveStageColorArg0(m_StageStates[stage]));
        stateDesc.FS.SetStageColorArg1(stage, CKFFResolveStageColorArg1(m_StageStates[stage], hasTexture));
        stateDesc.FS.SetStageColorArg2(stage, CKFFResolveStageColorArg2(m_StageStates[stage]));
        stateDesc.FS.SetStageAlphaOp(stage, alphaOp);
        stateDesc.FS.SetStageAlphaArg0(stage, CKFFResolveStageAlphaArg0(m_StageStates[stage]));
        stateDesc.FS.SetStageAlphaArg1(stage, CKFFResolveStageAlphaArg1(m_StageStates[stage], hasTexture));
        stateDesc.FS.SetStageAlphaArg2(stage, CKFFResolveStageAlphaArg2(m_StageStates[stage]));
        stateDesc.FS.SetStageResultIsTemp(stage, CKFFBaseTextureArg(CKFFResolveStageResultArg(m_StageStates[stage])) == CKRST_TA_TEMP);
        stateDesc.FS.SetStageProjectedSampler(stage, (m_StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS] & CKRST_TTF_PROJECTED) != 0);
        if ((m_TextureFlags[stage] & CKRST_TEXTURE_CUBEMAP) != 0)
            stateDesc.FS.SetStageSamplerType(stage, CKFF_SAMPLER_CUBE);
        else if ((m_TextureFlags[stage] & CKRST_TEXTURE_VOLUMEMAP) != 0)
            stateDesc.FS.SetStageSamplerType(stage, CKFF_SAMPLER_VOLUME);
        else if ((m_TextureFlags[stage] & CKRST_TEXTURE_DEPTHSTENCIL) != 0)
            stateDesc.FS.SetStageSamplerType(stage, CKFF_SAMPLER_DEPTH);
        stateDesc.FS.SetStageSamplerCompareFunc(stage, m_StageStates[stage][CKRST_TSS_COMPAREFUNC]);

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
    if (m_CurrentShaderKey.VS.GetHasPositionT())
        return 0;
    CKDWORD count = 0;
    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const CKDWORD flags = m_StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS];
        const CKDWORD componentCount = flags & 0xFFu;
        if (componentCount > 1 && componentCount <= 4)
            count = stage + 1;
    }
    return count;
}

bool CKFixedFunctionPipeline::ProgramUsesBumpEnv(const CKFFProgramContext *programContext) const {
    const CKFFShaderKey &shaderKey = programContext ? programContext->ShaderKey : m_CurrentShaderKey;
    const CKDWORD lastStage = shaderKey.FS.LastActiveTextureStage;
    for (CKDWORD stage = 0; stage <= lastStage && stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const CKDWORD op = shaderKey.FS.Stages[stage].ColorOp;
        if (op == CKRST_TOP_BUMPENVMAP || op == CKRST_TOP_BUMPENVMAPLUMINANCE)
            return true;
    }
    return false;
}

bool CKFixedFunctionPipeline::ProgramUsesTexFactor(const CKFFProgramContext *programContext) const {
    const CKFFShaderKey &shaderKey = programContext ? programContext->ShaderKey : m_CurrentShaderKey;
    const CKDWORD lastStage = shaderKey.FS.LastActiveTextureStage;
    for (CKDWORD stage = 0; stage <= lastStage && stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const CKFFShaderKeyFSStage &s = shaderKey.FS.Stages[stage];
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

bool CKFixedFunctionPipeline::ProgramUsesStageConstant(const CKFFProgramContext *programContext) const {
    const CKFFShaderKey &shaderKey = programContext ? programContext->ShaderKey : m_CurrentShaderKey;
    const CKDWORD lastStage = shaderKey.FS.LastActiveTextureStage;
    for (CKDWORD stage = 0; stage <= lastStage && stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const CKFFShaderKeyFSStage &s = shaderKey.FS.Stages[stage];
        const CKDWORD args[] = { s.ColorArg0, s.ColorArg1, s.ColorArg2, s.AlphaArg0, s.AlphaArg1, s.AlphaArg2 };
        for (CKDWORD arg : args) {
            if (CKFFBaseTextureArg(arg) == CKRST_TA_CONSTANT)
                return true;
        }
    }
    return false;
}

bool CKFixedFunctionPipeline::ProgramUsesMaterialUniform(const CKFFProgramContext *programContext) const {
    const CKFFShaderKey &shaderKey = programContext ? programContext->ShaderKey : m_CurrentShaderKey;
    const CKBOOL fullSpecialized = programContext ? programContext->FullSpecialized :
        (m_CurrentProgramBinding.FullSpecialized ? TRUE : FALSE);

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

bool CKFixedFunctionPipeline::ProgramUsesViewSpaceUniforms(const CKFFProgramContext *programContext) const {
    const CKFFShaderKey &shaderKey = programContext ? programContext->ShaderKey : m_CurrentShaderKey;
    const CKBOOL fullSpecialized = programContext ? programContext->FullSpecialized :
        (m_CurrentProgramBinding.FullSpecialized ? TRUE : FALSE);

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

CKBOOL CKFixedFunctionPipeline::EmitUniform(CKFFUniformSink *sink, CKDWORD uniform,
                                            const void *data, CKDWORD count,
                                            CKDWORD vec4Count, CKBOOL objectUniform)
{
    if (!sink || !data || count == 0)
        return TRUE;
    if (sink->Encoder)
        UploadUniform(sink->Encoder, uniform, data, count);
    if (objectUniform && !sink->EmitObject)
        return TRUE;
    if (!objectUniform && !sink->EmitStatic)
        return TRUE;
    CKFFRenderPacketUniformPayload *payload = objectUniform ? sink->ObjectPayload : sink->StaticPayload;
    if (payload && !CKFFRenderPacketAddUniform(payload, uniform, data, count, vec4Count)) {
        sink->Failed = TRUE;
        return FALSE;
    }
    return TRUE;
}

void CKFixedFunctionPipeline::EmitUniformPayloads(CKFFUniformSink *sink,
                                                  const CKFFProgramContext *programContext)
{
    if (!sink)
        return;
    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();
    const CKFFShaderKey &shaderKey = programContext ? programContext->ShaderKey : m_CurrentShaderKey;
    const CKBOOL fullSpecialized = programContext ? programContext->FullSpecialized :
        (m_CurrentProgramBinding.FullSpecialized ? TRUE : FALSE);
    const bool positionT = shaderKey.VS.GetHasPositionT();
    const bool emitStatic = sink->Encoder || sink->EmitStatic;
    const bool emitObject = sink->Encoder || sink->EmitObject;

    VxMatrix modelView;
    VxMatrix normalMatrix;
    VxMatrix viewNormalMatrix;
    VxMatrix viewProj;
    VxMatrix modelViewProj;
    if (emitObject && !positionT) {
        const bool viewSpaceUniforms = ProgramUsesViewSpaceUniforms(programContext);
        const bool vertexBlend = CKFFShaderKeyVertexBlendMode(shaderKey.VS) == CKFF_VERTEX_BLEND_NORMAL;
        if (viewSpaceUniforms) {
            Vx3DMultiplyMatrix4(modelView, m_View, m_World);
            Vx3DInverseMatrix(normalMatrix, modelView);
            Vx3DTransposeMatrix(normalMatrix, normalMatrix);
            if (vertexBlend) {
                Vx3DInverseMatrix(viewNormalMatrix, m_View);
                Vx3DTransposeMatrix(viewNormalMatrix, viewNormalMatrix);
            }
        }
        Vx3DMultiplyMatrix4(viewProj, m_Projection, m_View);
        Vx3DMultiplyMatrix4(modelViewProj, viewProj, m_World);
        VxMatrix matrices[4];
        matrices[0] = vertexBlend ? viewProj : modelViewProj;
        matrices[1] = m_World;
        matrices[2] = vertexBlend ? m_View : modelView;
        matrices[3] = vertexBlend ? viewNormalMatrix : normalMatrix;
        if (vertexBlend) {
            VxMatrix identity;
            identity.Identity();
            VxMatrix palette[CKFF_VERTEX_BLEND_MATRIX_COUNT];
            for (int i = 0; i < CKFF_VERTEX_BLEND_MATRIX_COUNT; ++i) {
                if (m_VertexBlendMatrixSet[i])
                    palette[i] = m_VertexBlendMatrices[i];
                else
                    palette[i] = (i == 0) ? m_World : identity;
            }
            EmitUniform(sink, u.u_vertexBlendMatrices, palette,
                        CKFF_VERTEX_BLEND_MATRIX_COUNT,
                        CKFF_VERTEX_BLEND_MATRIX_COUNT * 4, TRUE);
        }
        const CKDWORD matrixCount = viewSpaceUniforms ? 4 : 2;
        EmitUniform(sink, u.u_ffMatrices, matrices, matrixCount, matrixCount * 4, TRUE);
    }
    if (!emitStatic)
        return;

    const CKDWORD texMatrixCount = CurrentTextureMatrixUploadCount();
    if (texMatrixCount > 0)
        EmitUniform(sink, u.u_texMatrix, m_TexMatrix, texMatrixCount, texMatrixCount * 4, FALSE);

    // bgfx uniform bindings are draw state. Packet replay can retain static
    // draw constants across sorted opaque packets, but immediate draws still
    // upload all constants before each submit.
    int packed = 0;
    CKFFLightData viewLights[CKFF_MAX_LIGHTS];
    const bool shaderUsesLighting = !positionT && (!fullSpecialized || ((shaderKey.VS.Bits & (1ull << 13)) != 0));
    if (shaderUsesLighting) {
        packed = CKFFPackViewLights(m_Lights, m_LightEnabled, m_ActiveLightCount,
                                    m_CurrentLightingEnabled ? TRUE : FALSE, m_View, viewLights);

        if (packed > 1)
            EmitUniform(sink, u.u_lights, viewLights, packed * 7, packed * 7, FALSE);
    }

    const bool fogEnabled = shaderKey.FS.FogEnable;
    const CKDWORD vertexFogMode = fogEnabled ? shaderKey.FS.VertexFogMode : 0;
    const CKDWORD pixelFogMode = fogEnabled ? shaderKey.FS.PixelFogMode : 0;

    float drawParams[CKFF_DRAW_PARAM_VEC4_COUNT][4];
    memset(drawParams, 0, sizeof(drawParams));
    memcpy(drawParams[0], m_Material.Diffuse, sizeof(drawParams[0]));
    memcpy(drawParams[1], m_Material.Ambient, sizeof(drawParams[1]));
    memcpy(drawParams[2], m_Material.Specular, sizeof(drawParams[2]));
    memcpy(drawParams[3], m_Material.Emissive, sizeof(drawParams[3]));
    drawParams[CKFF_DRAW_PARAM_MATERIAL_POWER][0] = m_Material.Power;
    memcpy(drawParams[CKFF_DRAW_PARAM_MATERIAL_SOURCES], m_MaterialSource,
           sizeof(drawParams[CKFF_DRAW_PARAM_MATERIAL_SOURCES]));
    drawParams[CKFF_DRAW_PARAM_LIGHT_FLAGS][2] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_RANGEFOGENABLE) ? 1.0f : 0.0f;
    if (shaderUsesLighting) {
        CKDWORD ambientColor = m_DrawStateCache.GetRenderState(VXRENDERSTATE_AMBIENT);
        float ambientColorF[4];
        CKFFPackColorARGB(ambientColor, ambientColorF);
        drawParams[CKFF_DRAW_PARAM_LIGHTING][0] = m_CurrentLightingEnabled ? (float)packed : -1.0f;
        drawParams[CKFF_DRAW_PARAM_LIGHTING][1] = ambientColorF[0];
        drawParams[CKFF_DRAW_PARAM_LIGHTING][2] = ambientColorF[1];
        drawParams[CKFF_DRAW_PARAM_LIGHTING][3] = ambientColorF[2];
        drawParams[CKFF_DRAW_PARAM_LIGHT_FLAGS][0] = m_CurrentLightingEnabled &&
                            m_DrawStateCache.GetRenderState(VXRENDERSTATE_LOCALVIEWER) ? 1.0f : 0.0f;
        drawParams[CKFF_DRAW_PARAM_LIGHT_FLAGS][1] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_NORMALIZENORMALS) ? 1.0f : 0.0f;
        if (packed == 1) {
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
    const bool shaderUsesVertexParams = !positionT && (!fullSpecialized || shaderUsesLighting || ProgramUsesMaterialUniform(programContext));
    CKDWORD drawParamCount = shaderUsesVertexParams ? (shaderUsesLighting ? (packed == 1 ? 19 : 8) : 6) : 0;

    drawParams[CKFF_DRAW_PARAM_ALPHA][0] = (float)CKFFAlphaRefByte(m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAREF));
    drawParams[CKFF_DRAW_PARAM_ALPHA][1] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHATESTENABLE)
        ? CKFFPackAlphaFuncPrecision(m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAFUNC), m_AlphaTestPrecision)
        : 0.0f;
    drawParams[CKFF_DRAW_PARAM_ALPHA][2] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_SPECULARENABLE) ? 1.0f : 0.0f;
    drawParams[CKFF_DRAW_PARAM_ALPHA][3] = (float)pixelFogMode;

    CKDWORD tf = m_DrawStateCache.GetRenderState(VXRENDERSTATE_TEXTUREFACTOR);
    CKFFPackColorARGB(tf, drawParams[CKFF_DRAW_PARAM_TEXTURE_FACTOR]);

    drawParams[CKFF_DRAW_PARAM_FOG][0] = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_FOGSTART, 0.0f);
    drawParams[CKFF_DRAW_PARAM_FOG][1] = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_FOGEND, 1.0f);
    drawParams[CKFF_DRAW_PARAM_FOG][2] = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_FOGDENSITY, 1.0f);
    drawParams[CKFF_DRAW_PARAM_FOG][3] = (float)vertexFogMode;

    CKDWORD fogColor = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGCOLOR);
    CKFFPackColorARGB(fogColor, drawParams[CKFF_DRAW_PARAM_FOG_COLOR]);

    CKDWORD fragmentParamCount = 0;
    if (!fullSpecialized) {
        fragmentParamCount = 4;
    } else if (fogEnabled) {
        fragmentParamCount = 4;
    } else if (ProgramUsesTexFactor(programContext)) {
        fragmentParamCount = 2;
    } else if (shaderKey.FS.AlphaTestEnable) {
        fragmentParamCount = 1;
    }
    if (fragmentParamCount > 0 && drawParamCount < 8 + fragmentParamCount)
        drawParamCount = 8 + fragmentParamCount;
    if (!fullSpecialized && !positionT && fogEnabled && drawParamCount < 8)
        drawParamCount = 8;
    if (drawParamCount > 0)
        EmitUniform(sink, u.u_ffDrawParams, drawParams, drawParamCount, drawParamCount, FALSE);

    if (ProgramUsesBumpEnv(programContext)) {
        float bumpEnv[CKFF_MAX_TEXTURE_STAGES * 2][4] = {};
        CKFFPackBumpEnvUniforms(m_StageStates, bumpEnv);
        EmitUniform(sink, u.u_bumpEnv, bumpEnv,
                    CKFF_MAX_TEXTURE_STAGES * 2, CKFF_MAX_TEXTURE_STAGES * 2, FALSE);
    }

    if (positionT)
        EmitUniform(sink, u.u_viewport, m_Viewport, 1, 1, FALSE);

    if (!fullSpecialized || ProgramUsesStageConstant(programContext)) {
        CKFFStageParamsUniform stageParams;
        CKFFPackStageParams(m_StageStates, m_TextureHandles, m_CurrentActiveTextureCount, stageParams);
        EmitUniform(sink, u.u_stageParams, stageParams.Values,
                    CKFF_STAGE_PARAM_VEC4_COUNT, CKFF_STAGE_PARAM_VEC4_COUNT, FALSE);
    }

    if (!fullSpecialized) {
        CKFFSpecUniform ffSpec;
        const CKFFSpecializationInfo &specialization = programContext
            ? programContext->Specialization
            : m_CurrentProgramBinding.Specialization;
        CKFFPackSpecializationDwords(specialization, ffSpec);
        EmitUniform(sink, u.u_ffSpec, ffSpec.Values,
                    CKFFSpecializationInfo::MaxSpecDwords,
                    CKFFSpecializationInfo::MaxSpecDwords, FALSE);
    }

    CKFFClipPlaneUniform clip;
    const CKDWORD clipMask = m_DrawStateCache.GetRenderState(VXRENDERSTATE_CLIPPLANEENABLE);
    if (!fullSpecialized || ((shaderKey.VS.Bits & (1ull << 34)) != 0)) {
        if (clipMask != 0) {
            CKFFPackClipPlaneUniforms(m_UserClipPlanes, clipMask, clip);
            EmitUniform(sink, u.u_clipPlanes, clip.Planes, 6, 6, FALSE);
        } else {
            memset(&clip, 0, sizeof(clip));
        }
        EmitUniform(sink, u.u_clipParams, clip.Params, 1, 1, FALSE);
    }
}

void CKFixedFunctionPipeline::UploadUniforms(CKRasterizerEncoder *encoder) {
    if (!encoder)
        return;
    CKFFProgramContext programContext;
    CKFFInitProgramContext(&programContext, m_CurrentShaderKey, m_CurrentProgramBinding);
    CKFFUniformSink sink;
    memset(&sink, 0, sizeof(sink));
    sink.Encoder = encoder;
    sink.EmitStatic = TRUE;
    sink.EmitObject = TRUE;
    EmitUniformPayloads(&sink, &programContext);
    m_DirtyFlags = 0;
}

CKBOOL CKFixedFunctionPipeline::BuildUniformPayloads(CKFFRenderPacketUniformPayload *staticPayload,
                                                     CKFFRenderPacketUniformPayload *objectPayload,
                                                     const CKFFProgramContext *programContext)
{
    if (!staticPayload || !objectPayload)
        return FALSE;
    memset(staticPayload, 0, sizeof(CKFFRenderPacketUniformPayload));
    memset(objectPayload, 0, sizeof(CKFFRenderPacketUniformPayload));

    CKFFUniformSink sink;
    memset(&sink, 0, sizeof(sink));
    sink.StaticPayload = staticPayload;
    sink.ObjectPayload = objectPayload;
    sink.EmitStatic = TRUE;
    sink.EmitObject = TRUE;
    EmitUniformPayloads(&sink, programContext);
    if (sink.Failed)
        return FALSE;

    staticPayload->Hash = CKFFHashRenderPacketUniformPayload(*staticPayload);
    objectPayload->Hash = CKFFHashRenderPacketUniformPayload(*objectPayload);
    return TRUE;
}

CKBOOL CKFixedFunctionPipeline::BuildStaticUniformPayload(CKFFRenderPacketUniformPayload *payload,
                                                          const CKFFProgramContext *programContext)
{
    if (!payload)
        return FALSE;
    memset(payload, 0, sizeof(CKFFRenderPacketUniformPayload));

    CKFFUniformSink sink;
    memset(&sink, 0, sizeof(sink));
    sink.StaticPayload = payload;
    sink.EmitStatic = TRUE;
    sink.EmitObject = FALSE;
    EmitUniformPayloads(&sink, programContext);
    if (sink.Failed)
        return FALSE;

    payload->Hash = CKFFHashRenderPacketUniformPayload(*payload);
    return TRUE;
}

CKBOOL CKFixedFunctionPipeline::BuildObjectUniformPayload(CKFFRenderPacketUniformPayload *payload,
                                                          const CKFFProgramContext *programContext)
{
    if (!payload)
        return FALSE;
    memset(payload, 0, sizeof(CKFFRenderPacketUniformPayload));

    CKFFUniformSink sink;
    memset(&sink, 0, sizeof(sink));
    sink.ObjectPayload = payload;
    sink.EmitStatic = FALSE;
    sink.EmitObject = TRUE;
    EmitUniformPayloads(&sink, programContext);
    if (sink.Failed)
        return FALSE;

    payload->Hash = CKFFHashRenderPacketUniformPayload(*payload);
    return TRUE;
}

CKBOOL CKFixedFunctionPipeline::CanBuildPacketObjectUniforms() const
{
    return GetPacketObjectUniformRejectReason() == CKFF_RENDER_PACKET_ELIGIBLE ? TRUE : FALSE;
}

CKDWORD CKFixedFunctionPipeline::GetPacketObjectUniformRejectReason() const
{
    if (m_CurrentShaderKey.VS.GetHasPositionT())
        return CKFF_RENDER_PACKET_ELIGIBLE;
    if (CKFFShaderKeyVertexBlendMode(m_CurrentShaderKey.VS) == CKFF_VERTEX_BLEND_NORMAL)
        return CKFF_RENDER_PACKET_REJECT_VERTEX_BLEND;
    return CKFF_RENDER_PACKET_ELIGIBLE;
}

CKBOOL CKFixedFunctionPipeline::CanInstanceVertexBufferPacket() const
{
    return GetVertexBufferPacketInstancingRejectReason() == CKFF_RENDER_PACKET_ELIGIBLE ? TRUE : FALSE;
}

CKDWORD CKFixedFunctionPipeline::GetVertexBufferPacketInstancingRejectReason() const
{
    if (!m_OpaqueInstancingEnabled || !m_InstanceLayout)
        return CKFF_RENDER_PACKET_REJECT_INSTANCE_LAYOUT;
    if (m_CurrentShaderKey.VS.GetHasPositionT())
        return CKFF_RENDER_PACKET_REJECT_POSITIONT;
    if (CKFFShaderKeyVertexBlendMode(m_CurrentShaderKey.VS) == CKFF_VERTEX_BLEND_NORMAL)
        return CKFF_RENDER_PACKET_REJECT_VERTEX_BLEND;
    if (m_CurrentShaderKey.FS.LastActiveTextureStage >= 4)
        return CKFF_RENDER_PACKET_REJECT_TEXCOORD_RANGE;
    for (CKDWORD stage = 0; stage <= m_CurrentShaderKey.FS.LastActiveTextureStage; ++stage) {
        if ((m_CurrentShaderKey.VS.TexCoordIndex[stage] & 7u) >= 4)
            return CKFF_RENDER_PACKET_REJECT_TEXCOORD_RANGE;
        if ((m_CurrentShaderKey.VS.TexGen[stage] & 7u) != 0)
            return CKFF_RENDER_PACKET_REJECT_TEXGEN;
    }
    if ((m_CurrentShaderKey.VS.Bits & (1ull << 13)) != 0)
        return CKFF_RENDER_PACKET_REJECT_VIEW_SPACE_SHADER;
    if (m_CurrentShaderKey.FS.VertexFogMode != 0)
        return CKFF_RENDER_PACKET_REJECT_VERTEX_FOG;
    if (m_CurrentShaderKey.FS.PixelFogMode != 0)
        return CKFF_RENDER_PACKET_REJECT_PIXEL_FOG;
    if (m_CurrentShaderKey.FS.RangeFog)
        return CKFF_RENDER_PACKET_REJECT_RANGE_FOG;
    return CKFF_RENDER_PACKET_ELIGIBLE;
}

void CKFixedFunctionPipeline::UpdateViewProjectionCache()
{
    if (!m_ViewProjectionDirty)
        return;
    Vx3DMultiplyMatrix4(m_ViewProjection, m_Projection, m_View);
    m_ViewProjectionHash = CKFFHashBytes(&m_ViewProjection,
                                         sizeof(m_ViewProjection),
                                         2166136261u);
    m_ViewProjectionDirty = FALSE;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    ++m_FrameStats.RenderPacketViewProjectionRebuilds;
#endif
}

CKBOOL CKFixedFunctionPipeline::BuildPacketObjectUniforms(CKRenderPacketObjectUniforms *uniforms,
                                                          const CKFFProgramContext *programContext)
{
    if (!uniforms)
        return FALSE;
    memset(uniforms, 0, sizeof(CKRenderPacketObjectUniforms));

    const CKFFShaderKey &shaderKey = programContext ? programContext->ShaderKey : m_CurrentShaderKey;
    if (shaderKey.VS.GetHasPositionT())
        return TRUE;

    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();
    const bool viewSpaceUniforms = ProgramUsesViewSpaceUniforms(programContext);

    VxMatrix modelView;
    VxMatrix normalMatrix;
    VxMatrix modelViewProj;
    if (viewSpaceUniforms) {
        Vx3DMultiplyMatrix4(modelView, m_View, m_World);
        Vx3DInverseMatrix(normalMatrix, modelView);
        Vx3DTransposeMatrix(normalMatrix, normalMatrix);
    }
    UpdateViewProjectionCache();
    Vx3DMultiplyMatrix4(modelViewProj, m_ViewProjection, m_World);

    uniforms->MatrixUniform = u.u_ffMatrices;
    uniforms->MatrixCount = viewSpaceUniforms ? 4 : 2;
    uniforms->Matrices[0] = modelViewProj;
    uniforms->Matrices[1] = m_World;
    uniforms->Matrices[2] = modelView;
    uniforms->Matrices[3] = normalMatrix;
    return TRUE;
}

void CKFixedFunctionPipeline::UploadUniform(CKRasterizerEncoder *encoder, CKDWORD uniform, const void *data, CKDWORD count) {
    if (!encoder)
        return;
    encoder->SetUniform(uniform, data, count);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
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
#endif
}

CKDWORD CKFixedFunctionPipeline::InternStaticUniformPayload(const CKFFRenderPacketUniformPayload &payload)
{
    CKBOOL interned = FALSE;
    CKDWORD index = m_OpaquePacketQueue.InternStaticUniformPayload(payload, &interned);
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (interned)
        ++m_FrameStats.RenderPacketStaticPayloadInterns;
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
    m_FrameStats.RenderPacketAdaptiveSamples = m_OpaquePacketQueue.GetAdaptiveSamples();
    m_FrameStats.RenderPacketAdaptiveSavedBindEstimate =
        m_OpaquePacketQueue.GetAdaptiveSavedBindEstimate();
    m_FrameStats.RenderPacketAdaptiveRunBypasses =
        m_OpaquePacketQueue.GetAdaptiveRunBypasses();
    m_FrameStats.RenderPacketAdaptiveSampleRuns =
        m_OpaquePacketQueue.GetAdaptiveSampleRuns();
    m_FrameStats.RenderPacketAdaptiveSampleMaxRun =
        m_OpaquePacketQueue.GetAdaptiveSampleMaxRun();
    m_FrameStats.RenderPacketAdaptiveSubmitSavedEstimate =
        m_OpaquePacketQueue.GetAdaptiveSubmitSavedEstimate();
    m_FrameStats.RenderPacketAdaptiveCooldownBypasses =
        m_OpaquePacketQueue.GetAdaptiveCooldownBypasses();
    m_FrameStats.RenderPacketAdaptiveCooldownFrames =
        m_OpaquePacketQueue.GetAdaptiveCooldownFrames();
    m_FrameStats.RenderPacketAdaptiveFrameEndEvaluations =
        m_OpaquePacketQueue.GetAdaptiveFrameEndEvaluations();
    m_FrameStats.RenderPacketAdaptiveFrameEndRunBypasses =
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
    ++m_FrameStats.RenderPacketAdaptiveBypasses;
    m_FrameStats.RenderPacketAdaptiveRunBypasses =
        m_OpaquePacketQueue.GetAdaptiveRunBypasses();
    m_FrameStats.RenderPacketAdaptiveSampleRuns =
        m_OpaquePacketQueue.GetAdaptiveSampleRuns();
    m_FrameStats.RenderPacketAdaptiveSampleMaxRun =
        m_OpaquePacketQueue.GetAdaptiveSampleMaxRun();
    m_FrameStats.RenderPacketAdaptiveSubmitSavedEstimate =
        m_OpaquePacketQueue.GetAdaptiveSubmitSavedEstimate();
#endif
    UpdateOpaqueRenderPacketAdaptiveStats();
    FlushOpaqueRenderPackets(encoder, TRUE, FALSE);
    return TRUE;
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
#if CKRE_ENABLE_FFP_DIAGNOSTICS
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
        const bool cube = (m_TextureFlags[i] & CKRST_TEXTURE_CUBEMAP) != 0;
        const bool volume = (m_TextureFlags[i] & CKRST_TEXTURE_VOLUMEMAP) != 0;
        const CKDWORD samplerStage = CKFFSamplerBindStage(i, cube ? CKFF_SAMPLER_CUBE :
                                                             (volume ? CKFF_SAMPLER_VOLUME : CKFF_SAMPLER_2D));
        const CKDWORD samplerUniform = cube ? u.s_textureCube[i] :
                                       (volume ? u.s_textureVolume[i] : u.s_texture[i]);
        encoder->SetTexture(samplerStage, samplerUniform, texture, &sampler);
        if (collectStats)
            ++m_FrameStats.TextureBinds;
    }
#else
    for (CKDWORD i = 0; i < activeCount; ++i) {
        const CKDWORD texture = desiredTextures[i];
        if (texture == 0)
            continue;
        CKSamplerDesc sampler = desiredSamplers[i];
        const bool cube = (m_TextureFlags[i] & CKRST_TEXTURE_CUBEMAP) != 0;
        const bool volume = (m_TextureFlags[i] & CKRST_TEXTURE_VOLUMEMAP) != 0;
        const CKDWORD samplerStage = CKFFSamplerBindStage(i, cube ? CKFF_SAMPLER_CUBE :
                                                             (volume ? CKFF_SAMPLER_VOLUME : CKFF_SAMPLER_2D));
        const CKDWORD samplerUniform = cube ? u.s_textureCube[i] :
                                       (volume ? u.s_textureVolume[i] : u.s_texture[i]);
        encoder->SetTexture(samplerStage, samplerUniform, texture, &sampler);
    }
#endif
}

CKDWORD CKFixedFunctionPipeline::SubmitDiscardFlags() const {
    return CKRST_DISCARD_ALL;
}

void CKFixedFunctionPipeline::LogAndResetFrameStats() {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
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
        CK_LOG_FMT("FFPStats.Core",
                   "frame=%u sw=%u hw=%u submitted=%u prepareFail=%u programMiss=%u uniforms=%u uniformsPerDraw=%.2f vec4=%u vec4PerDraw=%.2f texBinds=%u texBindsPerDraw=%.2f layouts=%u vbSets=%u ibSets=%u transforms=%u repeatProgram=%u repeatState=%u repeatTexSet=%u repeatVB=%u repeatIB=%u repeatWorld=%u drawStateHits=%u drawStateRebuilds=%u transientVB=%u transientIB=%u",
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
                   m_FrameStats.TransientIndexBytes);
        CK_LOG_FMT("FFPStats.Packet",
                   "frame=%u q=%u replay=%u fb=%u flush=%u overflow=%u runs=%u maxRun=%u skipState=%u skipTex=%u skipUniform=%u staticUp=%u staticSkip=%u objectUp=%u objectSkip=%u skipVB=%u skipIB=%u staticBuild=%u staticReuse=%u staticIntern=%u sortSkip=%u adaptiveSamples=%u adaptiveBypasses=%u adaptiveRunBypasses=%u adaptiveCooldownBypasses=%u adaptiveCooldownFrames=%u adaptiveFrameEndEvals=%u adaptiveFrameEndRunBypasses=%u adaptiveSaved=%u adaptiveSampleRuns=%u adaptiveSampleMaxRun=%u adaptiveSubmitSaved=%u viewProjRebuild=%u instRuns=%u instPackets=%u instSubmits=%u instBytes=%u instAllocFail=%u submitSaved=%u instFallbacks=%u",
                   m_FrameStats.FrameIndex,
                   m_FrameStats.QueuedRenderPackets,
                   m_FrameStats.ReplayedRenderPackets,
                   m_FrameStats.RenderPacketFallbacks,
                   m_FrameStats.RenderPacketFlushes,
                   m_FrameStats.RenderPacketUniformOverflows,
                   m_FrameStats.RenderPacketRuns,
                   m_FrameStats.RenderPacketMaxRunLength,
                   m_FrameStats.RenderPacketSkippedStates,
                   m_FrameStats.RenderPacketSkippedTextures,
                   m_FrameStats.RenderPacketSkippedUniforms,
                   m_FrameStats.RenderPacketStaticUniformUploads,
                   m_FrameStats.RenderPacketStaticUniformSkips,
                   m_FrameStats.RenderPacketObjectUniformUploads,
                   m_FrameStats.RenderPacketObjectUniformSkips,
                   m_FrameStats.RenderPacketSkippedVertexBuffers,
                   m_FrameStats.RenderPacketSkippedIndexBuffers,
                   m_FrameStats.RenderPacketStaticPayloadBuilds,
                   m_FrameStats.RenderPacketStaticPayloadReuses,
                   m_FrameStats.RenderPacketStaticPayloadInterns,
                   m_FrameStats.RenderPacketSortSkips,
                   m_FrameStats.RenderPacketAdaptiveSamples,
                   m_FrameStats.RenderPacketAdaptiveBypasses,
                   m_FrameStats.RenderPacketAdaptiveRunBypasses,
                   m_FrameStats.RenderPacketAdaptiveCooldownBypasses,
                   m_FrameStats.RenderPacketAdaptiveCooldownFrames,
                   m_FrameStats.RenderPacketAdaptiveFrameEndEvaluations,
                   m_FrameStats.RenderPacketAdaptiveFrameEndRunBypasses,
                   m_FrameStats.RenderPacketAdaptiveSavedBindEstimate,
                   m_FrameStats.RenderPacketAdaptiveSampleRuns,
                   m_FrameStats.RenderPacketAdaptiveSampleMaxRun,
                   m_FrameStats.RenderPacketAdaptiveSubmitSavedEstimate,
                   m_FrameStats.RenderPacketViewProjectionRebuilds,
                   m_FrameStats.RenderPacketInstancedRuns,
                   m_FrameStats.RenderPacketInstancedPackets,
                   m_FrameStats.RenderPacketInstancedSubmits,
                   m_FrameStats.RenderPacketInstanceBufferBytes,
                   m_FrameStats.RenderPacketInstanceAllocFailures,
                   m_FrameStats.RenderPacketSubmitSavedEstimate,
                   m_FrameStats.RenderPacketInstancingFallbacks);
        CK_LOG_FMT("FFPStats.Timing",
                   "frame=%u prepareUs=%.1f stateUs=%.1f programUs=%.1f uniformUs=%.1f textureUs=%.1f transformUs=%.1f drawStateBuildUs=%.1f encoderStateUs=%.1f stencilUs=%.1f layoutUs=%.1f bufferBindUs=%.1f submitUs=%.1f packetBuildUs=%.1f packetSortUs=%.1f packetReplayUs=%.1f",
                   m_FrameStats.FrameIndex,
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
                   m_FrameStats.SubmitUs,
                   m_FrameStats.RenderPacketBuildUs,
                   m_FrameStats.RenderPacketSortUs,
                   m_FrameStats.RenderPacketReplayUs);
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
#endif
}

CKSamplerDesc CKFixedFunctionPipeline::BuildSamplerDesc(int stage) const {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return CKFFBuildSamplerDesc(nullptr);
    CKSamplerDesc desc = CKFFBuildSamplerDesc(m_StageStates[stage]);
    if (m_DisableTextureFiltering) {
        desc.MinFilter = CKRST_FILTER_NEAREST;
        desc.MagFilter = CKRST_FILTER_NEAREST;
        desc.MipFilter = m_DisableMipmaps ? CKRST_FILTER_NONE : CKRST_FILTER_NEAREST;
    } else if (m_DisableMipmaps) {
        desc.MipFilter = CKRST_FILTER_NONE;
    }
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

CKBOOL CKFixedFunctionPipeline::ResolveVertexBufferPacketProgram(CKDWORD dpFlags,
                                                                 CKDWORD formatFlags,
                                                                 CKFFProgramContext *programContext)
{
    if (!programContext)
        return FALSE;

    m_CurrentActiveTextureCount = CKFFResolveActiveTextureCount(dpFlags, m_TextureHandles, m_StageStates);
    if (m_PacketProgramCacheValid &&
        m_PacketProgramCacheDPFlags == dpFlags &&
        m_PacketProgramCacheFormatFlags == formatFlags &&
        m_PacketProgramCacheActiveTextureCount == m_CurrentActiveTextureCount) {
        *programContext = m_PacketProgramCacheContext;
        SetCurrentProgramBinding(programContext->ShaderKey, programContext->Binding);
        return programContext->Program != 0 ? TRUE : FALSE;
    }

    CKFFStateDesc stateDesc = BuildCurrentStateDesc(dpFlags, formatFlags);
    CKFFShaderKey shaderKey = BuildCurrentShaderKey(stateDesc);
    CKFFProgramBinding programBinding = m_ShaderCache.GetProgram(shaderKey);
    CKFFInitProgramContext(programContext, shaderKey, programBinding);
    SetCurrentProgramBinding(programContext->ShaderKey, programContext->Binding);
    m_PacketProgramCacheDPFlags = dpFlags;
    m_PacketProgramCacheFormatFlags = formatFlags;
    m_PacketProgramCacheActiveTextureCount = m_CurrentActiveTextureCount;
    m_PacketProgramCacheContext = *programContext;
    m_PacketProgramCacheValid = TRUE;
    return programContext->Program != 0 ? TRUE : FALSE;
}

void CKFixedFunctionPipeline::CaptureVertexBufferPacketIdentity(
    CKRasterizerEncoder *encoder,
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
    packet->World = m_World;
    packet->Marker[0] = '\0';
    if (encoder)
        encoder->ConsumeMarker(packet->Marker, sizeof(packet->Marker));
}

void CKFixedFunctionPipeline::CaptureVertexBufferPacketTextures(CKRenderPacket *packet)
{
    if (!packet)
        return;

    UpdatePacketTextureSetCache();
    packet->ActiveTextureCount = m_PacketTextureSetCache.ActiveTextureCount;
    packet->TextureSetHash = m_PacketTextureSetCache.TextureSetHash;
    for (CKDWORD i = 0; i < packet->ActiveTextureCount; ++i) {
        packet->Textures[i] = m_PacketTextureSetCache.Textures[i];
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
    packet->ViewProjection = m_ViewProjection;
    packet->ViewProjectionHash = m_ViewProjectionHash;
    return TRUE;
}

void CKFixedFunctionPipeline::CaptureVertexBufferPacketInstancing(
    CKRenderPacket *packet,
    const CKFFProgramContext *programContext)
{
    if (!packet || !programContext)
        return;
    if (!CanInstanceVertexBufferPacket())
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
            ++m_FrameStats.RenderPacketStaticPayloadReuses;
#endif
    } else {
        CKFFRenderPacketUniformPayload staticPayload;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        double buildTimer = collectStats ? CKRenderPerfNow() : 0.0;
#endif
        if (!BuildStaticUniformPayload(&staticPayload, programContext)) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
            if (collectStats)
                ++m_FrameStats.RenderPacketUniformOverflows;
#endif
            return FALSE;
        }
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (collectStats) {
            m_FrameStats.RenderPacketBuildUs += CKRenderPerfElapsedUs(buildTimer);
            ++m_FrameStats.RenderPacketStaticPayloadBuilds;
        }
#endif
        packet->StaticUniformIndex = InternStaticUniformPayload(staticPayload);
        m_OpaquePacketQueue.CacheStaticUniform(packet->StaticUniformIndex);
    }

    return TRUE;
}

CKBOOL CKFixedFunctionPipeline::CanQueueOpaqueVertexBufferPacket(CKRenderView view,
                                                                 VXPRIMITIVETYPE type,
                                                                 CKDWORD vb,
                                                                 CKDWORD ib,
                                                                 CKDWORD vertexLayout) const
{
    return GetOpaqueVertexBufferPacketRejectReason(view, type, vb, ib, vertexLayout) ==
        CKFF_RENDER_PACKET_ELIGIBLE ? TRUE : FALSE;
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
    if (m_OpaquePacketQueue.IsAdaptiveBypassed())
        return CKFF_RENDER_PACKET_REJECT_ADAPTIVE_BYPASS;
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
    return CKFF_RENDER_PACKET_ELIGIBLE;
}

CKBOOL CKFixedFunctionPipeline::BuildVertexBufferPacket(
    CKRasterizerEncoder *encoder,
    CKRenderPacket *packet, CKRenderView view,
    VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
    CKDWORD baseVertex, CKDWORD vertexCount,
    CKDWORD startIndex, CKDWORD indexCount,
    CKDWORD dpFlags, CKDWORD formatFlags,
    CKDWORD vertexLayout)
{
    if (!packet || !vb)
        return FALSE;

    CKBOOL collectStats = FALSE;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    collectStats = (m_DiagnosticConfig.StatsEnabled || m_DiagnosticConfig.UniformHistEnabled) ? TRUE : FALSE;
    if (collectStats)
        ++m_FrameStats.HardwareDraws;
#endif

    CKFFProgramContext programContext;
    if (!ResolveVertexBufferPacketProgram(dpFlags, formatFlags, &programContext)) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (collectStats)
            ++m_FrameStats.ProgramMisses;
#endif
        return FALSE;
    }
    if (!CanBuildPacketObjectUniforms())
        return FALSE;

    CaptureVertexBufferPacketIdentity(encoder, packet, &programContext, view, type, vb, ib,
                                      baseVertex, vertexCount, startIndex, indexCount, vertexLayout);
    CaptureVertexBufferPacketTextures(packet);
    CaptureVertexBufferPacketInstancing(packet, &programContext);

#if CKRE_ENABLE_FFP_DIAGNOSTICS
    double buildTimer = collectStats ? CKRenderPerfNow() : 0.0;
#endif
    if (!CaptureVertexBufferPacketObjectUniforms(packet, &programContext))
        return FALSE;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (collectStats)
        m_FrameStats.RenderPacketBuildUs += CKRenderPerfElapsedUs(buildTimer);
#endif

    if (!CaptureVertexBufferPacketStaticUniforms(packet, &programContext, collectStats))
        return FALSE;

    BuildRenderPacketSortKey(packet);
    return TRUE;
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
        m_DiagnosticConfig.StatsEnabled ? TRUE : FALSE;
    context->Diagnostics.UniformHistEnabled =
        m_DiagnosticConfig.UniformHistEnabled ? TRUE : FALSE;
    context->Diagnostics.Uniforms = &m_ShaderCache.GetUniforms();
    context->Diagnostics.UniformSets = &m_FrameStats.UniformSets;
    context->Diagnostics.UniformVec4s = &m_FrameStats.UniformVec4s;
    context->Diagnostics.UniformHandleSets = m_FrameStats.UniformHandleSets;
    context->Diagnostics.UniformHandleVec4s = m_FrameStats.UniformHandleVec4s;
    context->Diagnostics.TextureBinds = &m_FrameStats.TextureBinds;
    context->Diagnostics.VertexLayoutSets = &m_FrameStats.VertexLayoutSets;
    context->Diagnostics.VertexBufferSets = &m_FrameStats.VertexBufferSets;
    context->Diagnostics.IndexBufferSets = &m_FrameStats.IndexBufferSets;
    context->Diagnostics.TransformSets = &m_FrameStats.TransformSets;
    context->Diagnostics.SubmittedDraws = &m_FrameStats.SubmittedDraws;
    context->Diagnostics.ReplayedRenderPackets = &m_FrameStats.ReplayedRenderPackets;
    context->Diagnostics.RenderPacketSkippedStates = &m_FrameStats.RenderPacketSkippedStates;
    context->Diagnostics.RenderPacketSkippedTextures = &m_FrameStats.RenderPacketSkippedTextures;
    context->Diagnostics.RenderPacketSkippedUniforms = &m_FrameStats.RenderPacketSkippedUniforms;
    context->Diagnostics.RenderPacketStaticUniformUploads = &m_FrameStats.RenderPacketStaticUniformUploads;
    context->Diagnostics.RenderPacketStaticUniformSkips = &m_FrameStats.RenderPacketStaticUniformSkips;
    context->Diagnostics.RenderPacketObjectUniformUploads = &m_FrameStats.RenderPacketObjectUniformUploads;
    context->Diagnostics.RenderPacketSkippedVertexBuffers = &m_FrameStats.RenderPacketSkippedVertexBuffers;
    context->Diagnostics.RenderPacketSkippedIndexBuffers = &m_FrameStats.RenderPacketSkippedIndexBuffers;
    context->Diagnostics.RenderPacketInstancedRuns = &m_FrameStats.RenderPacketInstancedRuns;
    context->Diagnostics.RenderPacketInstancedPackets = &m_FrameStats.RenderPacketInstancedPackets;
    context->Diagnostics.RenderPacketInstancedSubmits = &m_FrameStats.RenderPacketInstancedSubmits;
    context->Diagnostics.RenderPacketInstanceBufferBytes = &m_FrameStats.RenderPacketInstanceBufferBytes;
    context->Diagnostics.RenderPacketInstanceAllocFailures = &m_FrameStats.RenderPacketInstanceAllocFailures;
    context->Diagnostics.RenderPacketSubmitSavedEstimate = &m_FrameStats.RenderPacketSubmitSavedEstimate;
    context->Diagnostics.RenderPacketInstancingFallbacks = &m_FrameStats.RenderPacketInstancingFallbacks;
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
    double packetTimer = m_DiagnosticConfig.StatsEnabled ? CKRenderPerfNow() : 0.0;
#endif
    if (directReplay) {
#if CKRE_ENABLE_FFP_DIAGNOSTICS
        if (packetCount > 1)
            ++m_FrameStats.RenderPacketSortSkips;
#endif
    } else {
        SortOpaqueRenderPackets(indices);
    }
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    if (m_DiagnosticConfig.StatsEnabled)
        m_FrameStats.RenderPacketSortUs += CKRenderPerfElapsedUs(packetTimer);
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
    if (m_DiagnosticConfig.StatsEnabled) {
        CKDWORD runCount = 0;
        CKDWORD maxRun = 0;
        m_OpaquePacketQueue.GetRunStats(&indices, directReplay, &runCount, &maxRun);
        m_FrameStats.RenderPacketRuns += runCount;
        if (maxRun > m_FrameStats.RenderPacketMaxRunLength)
            m_FrameStats.RenderPacketMaxRunLength = maxRun;
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
    if (m_DiagnosticConfig.StatsEnabled)
        m_FrameStats.RenderPacketReplayUs += CKRenderPerfElapsedUs(packetTimer);
#endif

    ClearOpaqueRenderPackets();
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    ++m_FrameStats.RenderPacketFlushes;
#endif
}
