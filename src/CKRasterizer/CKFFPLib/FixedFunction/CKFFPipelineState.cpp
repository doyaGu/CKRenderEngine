#include "CKFixedFunctionPipeline.h"
#include "CKRasterizer.h"
#include "CKFFStageState.h"
#include "CKFFUniformState.h"
#include "CKRenderFrameCostStats.h"

#include <math.h>
#include <string.h>

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
    case VXRENDERSTATE_DITHERENABLE:
    case VXRENDERSTATE_ALPHATESTENABLE:
    case VXRENDERSTATE_ALPHAFUNC:
    case VXRENDERSTATE_CLIPPING:
    case VXRENDERSTATE_CLIPPLANEENABLE:
        return TRUE;
    default:
        return FALSE;
    }
}

void CKFixedFunctionPipeline::SetRenderOptions(CKBOOL DisableTextureFiltering, CKBOOL DisableMipmaps,
                                               CKBOOL ForceAnisotropicFiltering) {
    if (m_TextureBinder.SetRenderOptions(DisableTextureFiltering, DisableMipmaps,
                                         ForceAnisotropicFiltering))
        OnFixedFunctionStateChanged(CKFF_CHANGE_DRAW_VALIDATION |
                                    CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetAlphaTestPrecision(CKDWORD precision) {
    precision &= 0xFu;
    if (m_State.AlphaTestPrecision == precision)
        return;
    m_State.AlphaTestPrecision = precision;
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

CKDWORD CKFixedFunctionPipeline::GetAlphaTestPrecision() const {
    return m_State.AlphaTestPrecision;
}

void CKFixedFunctionPipeline::SetColorTargetFormat(CKFFColorTargetFormat format) {
    if (format < CKFF_COLOR_TARGET_RGBA8 || format >= CKFF_COLOR_TARGET_COUNT)
        format = CKFF_COLOR_TARGET_RGBA8;
    if (m_State.ColorTargetFormat == format)
        return;
    m_State.ColorTargetFormat = format;
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM |
                                CKFF_CHANGE_DRAW_VALIDATION);
}

void CKFixedFunctionPipeline::SetDepthBiasFormat(CK_DEPTH_FORMAT format) {
    if ((CKDWORD)format > (CKDWORD)CKRST_DEPTHFMT_D32F)
        format = CKRST_DEPTHFMT_D24S8;
    if (m_State.DepthBiasFormat == format)
        return;
    m_State.DepthBiasFormat = format;
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

CKBOOL CKFixedFunctionPipeline::SetVertexBlendMatrix(CKDWORD index, const VxMatrix &matrix) {
    if (index >= CKFF_VERTEX_BLEND_MATRIX_COUNT)
        return FALSE;
    if (m_State.VertexBlendMatrixSet[index] &&
        m_State.VertexBlendMatrices[index] == matrix)
        return TRUE;
    m_State.VertexBlendMatrices[index] = matrix;
    m_State.VertexBlendMatrixSet[index] = TRUE;
    OnFixedFunctionStateChanged(CKFF_CHANGE_OBJECT_UNIFORM);
    return TRUE;
}

void CKFixedFunctionPipeline::ResetVertexBlendMatrices() {
    CKBOOL changed = FALSE;
    for (int i = 0; i < CKFF_VERTEX_BLEND_MATRIX_COUNT && !changed; ++i)
        changed = m_State.VertexBlendMatrixSet[i];
    if (!changed)
        return;
    for (int i = 0; i < CKFF_VERTEX_BLEND_MATRIX_COUNT; ++i) {
        Vx3DMatrixIdentity(m_State.VertexBlendMatrices[i]);
        m_State.VertexBlendMatrixSet[i] = FALSE;
    }
    OnFixedFunctionStateChanged(CKFF_CHANGE_OBJECT_UNIFORM);
}

void CKFixedFunctionPipeline::SetTexcoordComponentCount(CKDWORD stage, CKDWORD count) {
    if (stage >= CKFF_MAX_TEXTURE_STAGES)
        return;
    CKBYTE componentCount = CKFFTexcoordComponentCount(count);
    if (m_State.TexcoordComponentCounts[stage] == componentCount)
        return;
    m_State.TexcoordComponentCounts[stage] = componentCount;
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM | CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::ResetTexcoordComponentCounts() {
    CKBOOL changed = FALSE;
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES && !changed; ++stage)
        changed = m_State.TexcoordComponentCounts[stage] != 2;
    if (!changed)
        return;
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        m_State.TexcoordComponentCounts[stage] = 2;
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM | CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetRenderState(VXRENDERSTATETYPE state, CKDWORD value) {
    const CKDWORD previous = m_State.DrawState.GetRenderState(state);
    m_State.DrawState.SetRenderState(state, value);
    if (previous == value)
        return;

    CKDWORD changeMask = CKFF_CHANGE_STATIC_UNIFORM |
                         CKFF_CHANGE_DRAW_VALIDATION;
    if (CKFFRenderStateAffectsProgram(state))
        changeMask |= CKFF_CHANGE_PROGRAM;
    OnFixedFunctionStateChanged(changeMask);
}

CKDWORD CKFixedFunctionPipeline::GetRenderState(VXRENDERSTATETYPE state) const {
    return m_State.DrawState.GetRenderState(state);
}

void CKFixedFunctionPipeline::InitDefaultStates() {
    m_State.DrawState.ResetQueryDefaults();
    for (CKDWORD state = 0; state < VXRENDERSTATE_MAXSTATE; ++state) {
        const CKDWORD value = CKRSTDefaultRenderStateValue((VXRENDERSTATETYPE)state);
        if (value || state == VXRENDERSTATE_COLORWRITEENABLE)
            SetRenderState((VXRENDERSTATETYPE)state, value);
    }
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        ResetTextureStage(stage);
}

void CKFixedFunctionPipeline::SetColorWriteMask(CKBOOL r, CKBOOL g, CKBOOL b, CKBOOL a) {
    const CKDWORD previous = m_State.DrawState.GetColorWriteMask();
    m_State.DrawState.SetColorWriteMask(r, g, b, a);
    if (m_State.DrawState.GetColorWriteMask() != previous)
        OnFixedFunctionStateChanged(CKFF_CHANGE_DRAW_VALIDATION);
}

CKDWORD CKFixedFunctionPipeline::GetColorWriteMask() const {
    return m_State.DrawState.GetColorWriteMask();
}

void CKFixedFunctionPipeline::SetColorWriteMask(CKDWORD mask) {
    const CKDWORD previous = m_State.DrawState.GetColorWriteMask();
    m_State.DrawState.SetColorWriteMask(mask);
    if (m_State.DrawState.GetColorWriteMask() != previous)
        OnFixedFunctionStateChanged(CKFF_CHANGE_DRAW_VALIDATION);
}

static uint64_t TextureCombineStateMask() {
    return (1ull << CKRST_TSS_OP) |
           (1ull << CKRST_TSS_ARG1) |
           (1ull << CKRST_TSS_ARG2) |
           (1ull << CKRST_TSS_AOP) |
           (1ull << CKRST_TSS_AARG1) |
           (1ull << CKRST_TSS_AARG2) |
           (1ull << CKRST_TSS_COLORARG0) |
           (1ull << CKRST_TSS_ALPHAARG0) |
           (1ull << CKRST_TSS_RESULTARG0);
}

static void ClearExplicitTextureCombineState(CKDWORD *stageState,
                                             uint64_t *stateSetMask) {
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
    if (stateSetMask)
        *stateSetMask &= ~TextureCombineStateMask();
}

static CKBOOL ResetTextureStageValues(CKFFStateStore &state, int stage,
                                      CKBOOL markDefaultsSet) {
    CKDWORD values[CKFF_MAX_TEXTURE_STAGE_STATES] = {};
    CKDWORD queryValues[CKFF_MAX_TEXTURE_STAGE_STATES] = {};
    uint64_t setMask = 0;
    uint64_t queryMask = 0;

    values[CKRST_TSS_TEXCOORDINDEX] = (CKDWORD)stage;
    if (markDefaultsSet) {
        static_assert(CKRST_TSS_MAXSTATE < 64,
                      "stage states must fit the presence masks");
        queryMask = ((1ull << CKRST_TSS_MAXSTATE) - 1) &
                    ~((1ull << CKRST_TSS_OP) - 1);
        setMask = queryMask & ~TextureCombineStateMask() &
                  ~(1ull << CKRST_TSS_STAGEBLEND);
        queryValues[CKRST_TSS_TEXCOORDINDEX] = (CKDWORD)stage;
    } else {
        values[CKRST_TSS_TEXTURETRANSFORMFLAGS] = CKRST_TTF_NONE;
        for (CKDWORD item = CKRST_TSS_OP;
             item < CKFF_MAX_TEXTURE_STAGE_STATES; ++item) {
            queryValues[item] = CKRSTDefaultTextureStageStateValue(
                stage, (CKRST_TEXTURESTAGESTATETYPE)item);
        }
    }

    VxMatrix identity;
    identity.SetIdentity();
    const CKBOOL drawChanged =
        state.TextureHandles[stage] != 0 ||
        state.TextureFlags[stage] != 0 ||
        state.StageStateSetMasks[stage] != setMask ||
        memcmp(state.StageStates[stage], values, sizeof(values)) != 0 ||
        state.TexMatrix[stage] != identity;

    state.TextureHandles[stage] = 0;
    state.TextureFlags[stage] = 0;
    memcpy(state.StageStates[stage], values, sizeof(values));
    memcpy(state.StageQueryStates[stage], queryValues, sizeof(queryValues));
    state.StageStateSetMasks[stage] = setMask;
    state.StageStateQueryMasks[stage] = queryMask;
    state.TexMatrix[stage] = identity;
    return drawChanged;
}

void CKFixedFunctionPipeline::ResetTextureStage(int stage) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return;
    if (ResetTextureStageValues(m_State, stage, FALSE))
        OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM |
                                    CKFF_CHANGE_STATIC_UNIFORM |
                                    CKFF_CHANGE_DRAW_VALIDATION);
}

void CKFixedFunctionPipeline::DisableTextureStagesFrom(int firstStage) {
    if (firstStage < 0)
        firstStage = 0;
    CKBOOL changed = FALSE;
    for (int stage = firstStage; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        changed = ResetTextureStageValues(m_State, stage, FALSE) || changed;
    if (changed)
        OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM |
                                    CKFF_CHANGE_STATIC_UNIFORM |
                                    CKFF_CHANGE_DRAW_VALIDATION);
}

void CKFixedFunctionPipeline::ResetTextureStages(int firstStage, int stageCount) {
    if (firstStage < 0 || firstStage > CKFF_MAX_TEXTURE_STAGES || stageCount <= 0 ||
        stageCount > CKFF_MAX_TEXTURE_STAGES - firstStage)
        return;

    CKBOOL changed = FALSE;
    for (int stage = firstStage; stage < firstStage + stageCount; ++stage)
        changed = ResetTextureStageValues(m_State, stage, TRUE) || changed;
    if (changed)
        OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM |
                                    CKFF_CHANGE_STATIC_UNIFORM |
                                    CKFF_CHANGE_DRAW_VALIDATION);
}

void CKFixedFunctionPipeline::SaveTextureStage(int stage, CKFFTextureStageSnapshot &snapshot) const {
    memset(&snapshot, 0, sizeof(snapshot));
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return;

    snapshot.Texture = m_State.TextureHandles[stage];
    snapshot.TextureFlags = m_State.TextureFlags[stage];
    memcpy(snapshot.States, m_State.StageStates[stage], sizeof(snapshot.States));
    snapshot.StateSetMask = m_State.StageStateSetMasks[stage];
    snapshot.StateQueryMask = m_State.StageStateQueryMasks[stage];
    snapshot.TextureMatrix = m_State.TexMatrix[stage];
}

void CKFixedFunctionPipeline::RestoreTextureStage(int stage, const CKFFTextureStageSnapshot &snapshot) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return;

    m_State.TextureHandles[stage] = snapshot.Texture;
    m_State.TextureFlags[stage] = snapshot.TextureFlags;
    memcpy(m_State.StageStates[stage], snapshot.States, sizeof(m_State.StageStates[stage]));
    m_State.StageStateSetMasks[stage] = snapshot.StateSetMask;
    m_State.StageStateQueryMasks[stage] = snapshot.StateQueryMask;
    for (CKDWORD state = CKRST_TSS_OP; state < CKFF_MAX_TEXTURE_STAGE_STATES; ++state) {
        const uint64_t stateBit = 1ull << state;
        m_State.StageQueryStates[stage][state] = (snapshot.StateQueryMask & stateBit) != 0
            ? snapshot.States[state]
            : CKRSTDefaultTextureStageStateValue(stage, (CKRST_TEXTURESTAGESTATETYPE)state);
    }
    m_State.TexMatrix[stage] = snapshot.TextureMatrix;
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM | CKFF_CHANGE_STATIC_UNIFORM |
                                CKFF_CHANGE_DRAW_VALIDATION);
}

void CKFixedFunctionPipeline::SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type, CKDWORD value) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return;
    if ((int)type < 0 || (int)type >= CKFF_MAX_TEXTURE_STAGE_STATES) return;

    if (type == CKRST_TSS_STAGEBLEND && value == 0 && stage > 0) {
        DisableTextureStagesFrom(stage);
        return;
    }

    const uint64_t stateBit = 1ull << (CKDWORD)type;
    m_State.StageStateQueryMasks[stage] |= stateBit;
    m_State.StageQueryStates[stage][type] = value;
    CKBOOL changed = m_State.StageStates[stage][type] != value ||
                     (m_State.StageStateSetMasks[stage] & stateBit) == 0;
    m_State.StageStates[stage][(int)type] = value;
    m_State.StageStateSetMasks[stage] |= stateBit;

    if (type == CKRST_TSS_TEXTUREMAPBLEND) {
        changed = changed ||
                  (m_State.StageStateSetMasks[stage] & TextureCombineStateMask()) != 0;
        ClearExplicitTextureCombineState(m_State.StageStates[stage],
                                         &m_State.StageStateSetMasks[stage]);
        m_State.StageStateQueryMasks[stage] |= TextureCombineStateMask();
        ClearExplicitTextureCombineState(m_State.StageQueryStates[stage], nullptr);
    } else if (type == CKRST_TSS_ADDRESS) {
        constexpr uint64_t addressMask = (1ull << CKRST_TSS_ADDRESSU) |
                                         (1ull << CKRST_TSS_ADDRESSV) |
                                         (1ull << CKRST_TSS_ADDRESW);
        changed = changed ||
                  (m_State.StageStateSetMasks[stage] & addressMask) != addressMask ||
                  m_State.StageStates[stage][CKRST_TSS_ADDRESSU] != value ||
                  m_State.StageStates[stage][CKRST_TSS_ADDRESSV] != value ||
                  m_State.StageStates[stage][CKRST_TSS_ADDRESW] != value;
        m_State.StageStates[stage][CKRST_TSS_ADDRESSU] = value;
        m_State.StageStates[stage][CKRST_TSS_ADDRESSV] = value;
        m_State.StageStates[stage][CKRST_TSS_ADDRESW] = value;
        m_State.StageQueryStates[stage][CKRST_TSS_ADDRESSU] = value;
        m_State.StageQueryStates[stage][CKRST_TSS_ADDRESSV] = value;
        m_State.StageQueryStates[stage][CKRST_TSS_ADDRESW] = value;
        m_State.StageStateSetMasks[stage] |= addressMask;
        m_State.StageStateQueryMasks[stage] |= addressMask;
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
            m_State.StageQueryStates[stage][CKRST_TSS_OP] = colorOp;
            m_State.StageQueryStates[stage][CKRST_TSS_ARG1] = colorArg1;
            m_State.StageQueryStates[stage][CKRST_TSS_ARG2] = colorArg2;
            m_State.StageQueryStates[stage][CKRST_TSS_AOP] = alphaOp;
            m_State.StageQueryStates[stage][CKRST_TSS_AARG1] = alphaArg1;
            m_State.StageQueryStates[stage][CKRST_TSS_AARG2] = alphaArg2;
            constexpr uint64_t derivedMask =
                (1ull << CKRST_TSS_OP) |
                (1ull << CKRST_TSS_ARG1) |
                (1ull << CKRST_TSS_ARG2) |
                (1ull << CKRST_TSS_AOP) |
                (1ull << CKRST_TSS_AARG1) |
                (1ull << CKRST_TSS_AARG2);
            changed = changed ||
                      (m_State.StageStateSetMasks[stage] & derivedMask) != derivedMask ||
                      m_State.StageStates[stage][CKRST_TSS_OP] != colorOp ||
                      m_State.StageStates[stage][CKRST_TSS_ARG1] != colorArg1 ||
                      m_State.StageStates[stage][CKRST_TSS_ARG2] != colorArg2 ||
                      m_State.StageStates[stage][CKRST_TSS_AOP] != alphaOp ||
                      m_State.StageStates[stage][CKRST_TSS_AARG1] != alphaArg1 ||
                      m_State.StageStates[stage][CKRST_TSS_AARG2] != alphaArg2;
            m_State.StageStateSetMasks[stage] |= derivedMask;
            m_State.StageStateQueryMasks[stage] |= derivedMask;
        }
    }
    if (changed) {
        OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM |
                                    CKFF_CHANGE_STATIC_UNIFORM |
                                    CKFF_CHANGE_DRAW_VALIDATION);
    }
}

void CKFixedFunctionPipeline::ClearTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return;
    if ((int)type < 0 || (int)type >= CKFF_MAX_TEXTURE_STAGE_STATES) return;
    const uint64_t stateBit = 1ull << (CKDWORD)type;
    m_State.StageStateQueryMasks[stage] |= stateBit;
    m_State.StageQueryStates[stage][type] = 0;
    if (m_State.StageStates[stage][(int)type] == 0 &&
        (m_State.StageStateSetMasks[stage] & stateBit) == 0)
        return;
    m_State.StageStates[stage][(int)type] = 0;
    m_State.StageStateSetMasks[stage] &= ~stateBit;
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM | CKFF_CHANGE_STATIC_UNIFORM |
                                CKFF_CHANGE_DRAW_VALIDATION);
}

CKDWORD CKFixedFunctionPipeline::GetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type) const {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return 0;
    if ((int)type < 0 || (int)type >= CKFF_MAX_TEXTURE_STAGE_STATES) return 0;
    return m_State.StageStates[stage][(int)type];
}

CKDWORD CKFixedFunctionPipeline::QueryTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type) const {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES || !CKRSTIsValidTextureStageStateType(type))
        return 0;
    return m_State.StageQueryStates[stage][type];
}

CKBOOL CKFixedFunctionPipeline::GetTransform(VXMATRIX_TYPE type, VxMatrix &matrix) const {
    const int slot = CKRSTMatrixSlot(type);
    if (slot < 0)
        return FALSE;
    if (slot == 0)
        matrix = m_State.World;
    else if (slot < CKRST_MAX_WORLD_MATRICES)
        matrix = m_State.VertexBlendMatrices[slot];
    else if (type == VXMATRIX_VIEW)
        matrix = m_State.View;
    else if (type == VXMATRIX_PROJECTION)
        matrix = m_State.Projection;
    else
        matrix = m_State.TexMatrix[type - VXMATRIX_TEXTURE0];
    return TRUE;
}

void CKFixedFunctionPipeline::SetRenderTargetActive(CKBOOL active) {
    active = active ? TRUE : FALSE;
    if (m_State.RenderTargetActive == active)
        return;
    m_State.RenderTargetActive = active;
    m_State.DrawState.SetWindingFlip(RenderTargetOriginFlip());
    UpdateViewportMapping();
    OnFixedFunctionStateChanged(CKFF_CHANGE_OBJECT_UNIFORM | CKFF_CHANGE_STATIC_UNIFORM);
}

CKBOOL CKFixedFunctionPipeline::RenderTargetOriginFlip() const {
    return m_State.RenderTargetActive &&
           (m_ShaderTargetFlags & CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT) != 0;
}

void CKFixedFunctionPipeline::SetViewport(const CKViewportData &viewport) {
    CK_FRAME_COST_ADD_VIEWPORT_SET();

    const CKViewportData &current = m_State.ViewportData;
    if (current.ViewX == viewport.ViewX && current.ViewY == viewport.ViewY &&
        current.ViewWidth == viewport.ViewWidth &&
        current.ViewHeight == viewport.ViewHeight &&
        current.ViewZMin == viewport.ViewZMin &&
        current.ViewZMax == viewport.ViewZMax)
        return;

    const float w = viewport.ViewWidth > 0 ? (float)viewport.ViewWidth : 1.0f;
    const float h = viewport.ViewHeight > 0 ? (float)viewport.ViewHeight : 1.0f;
    const float x = (float)viewport.ViewX;
    const float y = (float)viewport.ViewY;

    m_State.Viewport[0] = 2.0f / w;
    m_State.Viewport[1] = -2.0f / h;
    m_State.Viewport[2] = -1.0f - (2.0f * x / w);
    m_State.Viewport[3] = 1.0f + (2.0f * y / h);
    m_State.ViewportData = viewport;
    UpdateViewportMapping();
    OnFixedFunctionStateChanged(CKFF_CHANGE_OBJECT_UNIFORM | CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetTargetExtents(CKDWORD logicalWidth, CKDWORD logicalHeight,
                                               CKDWORD physicalWidth, CKDWORD physicalHeight) {
    if (m_State.TargetLogicalWidth == logicalWidth && m_State.TargetLogicalHeight == logicalHeight &&
        m_State.TargetPhysicalWidth == physicalWidth && m_State.TargetPhysicalHeight == physicalHeight)
        return;
    m_State.TargetLogicalWidth = logicalWidth;
    m_State.TargetLogicalHeight = logicalHeight;
    m_State.TargetPhysicalWidth = physicalWidth;
    m_State.TargetPhysicalHeight = physicalHeight;
    UpdateViewportMapping();
    OnFixedFunctionStateChanged(CKFF_CHANGE_OBJECT_UNIFORM | CKFF_CHANGE_STATIC_UNIFORM);
}

CKBOOL CKFixedFunctionPipeline::GetViewportScissor(CKRECT *rect) const {
    if (rect)
        *rect = m_State.Scissor;
    return m_State.ScissorEnabled;
}

// Viewport-relative clip space -> target clip space, and the scissor that
// clips to the viewport on the physical target. Identity / no scissor when the
// viewport covers the whole logical target or the extents are unknown.
void CKFixedFunctionPipeline::UpdateViewportMapping() {
    CKFFStateStore &st = m_State;
    st.ViewportRemap[0] = st.ViewportRemap[1] = 1.0f;
    st.ViewportRemap[2] = st.ViewportRemap[3] = 0.0f;
    st.ViewportRemapIdentity = TRUE;
    st.ScissorEnabled = FALSE;
    st.Scissor.left = st.Scissor.top = st.Scissor.right = st.Scissor.bottom = 0;

    const CKDWORD lw = st.TargetLogicalWidth, lh = st.TargetLogicalHeight;
    const CKDWORD pw = st.TargetPhysicalWidth, ph = st.TargetPhysicalHeight;
    if (lw == 0 || lh == 0 || pw == 0 || ph == 0)
        return;
    const CKViewportData &vp = st.ViewportData;
    if (vp.ViewWidth == 0 || vp.ViewHeight == 0)
        return;
    const CKBOOL fullViewport = vp.ViewX == 0 && vp.ViewY == 0 && vp.ViewWidth == lw && vp.ViewHeight == lh;
    if (fullViewport)
        return;

    const float fw = (float)vp.ViewWidth, fh = (float)vp.ViewHeight;
    const float fx = (float)vp.ViewX, fy = (float)vp.ViewY;
    st.ViewportRemap[0] = fw / (float)lw;
    st.ViewportRemap[1] = fh / (float)lh;
    st.ViewportRemap[2] = (2.0f * fx + fw) / (float)lw - 1.0f;
    st.ViewportRemap[3] = 1.0f - (2.0f * fy + fh) / (float)lh;
    st.ViewportRemapIdentity = FALSE;

    // Scissor on the physical target, rounded outwards so scaled edges do not
    // leave gaps; mirrored when the target renders upside down (RTT origin).
    const double sx = (double)pw / (double)lw;
    const double sy = (double)ph / (double)lh;
    double left = fx * sx, top = fy * sy;
    double right = (fx + fw) * sx, bottom = (fy + fh) * sy;
    int l = (int)floor(left), t = (int)floor(top);
    int r = (int)ceil(right), b = (int)ceil(bottom);
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (r > (int)pw) r = (int)pw;
    if (b > (int)ph) b = (int)ph;
    if (r <= l || b <= t) {
        // Viewport outside the target: clip everything.
        l = t = 0;
        r = b = 1;
        if (pw < 1 || ph < 1)
            return;
    }
    if (RenderTargetOriginFlip()) {
        const int mirroredTop = (int)ph - b;
        const int mirroredBottom = (int)ph - t;
        t = mirroredTop;
        b = mirroredBottom;
    }
    st.Scissor.left = l;
    st.Scissor.top = t;
    st.Scissor.right = r;
    st.Scissor.bottom = b;
    st.ScissorEnabled = TRUE;
}

void CKFixedFunctionPipeline::SetUserClipPlane(int index, const VxPlane &plane) {
    if (index < 0 || index >= 6)
        return;
    if (m_State.UserClipPlanes[index] == plane)
        return;
    m_State.UserClipPlanes[index] = plane;
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetTransform(VXMATRIX_TYPE type, const VxMatrix &matrix) {
    switch (type) {
    case VXMATRIX_WORLD:
        if (m_State.World == matrix)
            return;
        m_State.World = matrix;
        OnFixedFunctionStateChanged(CKFF_CHANGE_OBJECT_UNIFORM);
        break;
    case VXMATRIX_VIEW:
        if (m_State.View == matrix)
            return;
        m_State.View = matrix;
        m_State.MarkViewProjectionDirty();
        OnFixedFunctionStateChanged(CKFF_CHANGE_OBJECT_UNIFORM | CKFF_CHANGE_STATIC_UNIFORM);
        break;
    case VXMATRIX_PROJECTION:
        if (m_State.Projection == matrix)
            return;
        m_State.Projection = matrix;
        m_State.MarkViewProjectionDirty();
        OnFixedFunctionStateChanged(CKFF_CHANGE_OBJECT_UNIFORM);
        break;
    default:
        if (type >= VXMATRIX_TEXTURE0 && type <= VXMATRIX_TEXTURE7) {
            int idx = type - VXMATRIX_TEXTURE0;
            if (idx < CKFF_MAX_TEXTURE_STAGES) {
                if (m_State.TexMatrix[idx] == matrix)
                    return;
                m_State.TexMatrix[idx] = matrix;
                OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
            }
        }
        break;
    }
}

void CKFixedFunctionPipeline::ApplyMaterial(const CKMaterialRenderState &state) {
    SetMaterial(&state.Material);
    SetRenderState(VXRENDERSTATE_CULLMODE, state.CullMode);
    SetRenderState(VXRENDERSTATE_FILLMODE, state.FillMode);
    SetRenderState(VXRENDERSTATE_SHADEMODE, state.ShadeMode);
    SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, state.AlphaBlend ? TRUE : FALSE);
    if (state.AlphaBlend) {
        SetRenderState(VXRENDERSTATE_SRCBLEND, state.SourceBlend);
        SetRenderState(VXRENDERSTATE_DESTBLEND, state.DestBlend);
    }
    SetRenderState(VXRENDERSTATE_ZENABLE, TRUE);
    SetRenderState(VXRENDERSTATE_ZWRITEENABLE, state.ZWrite ? TRUE : FALSE);
    SetRenderState(VXRENDERSTATE_ZFUNC, state.ZFunc);
}

void CKFixedFunctionPipeline::ResetMaterial() {
    m_State.Material = CKMaterialData();
    m_State.Material.Diffuse = m_State.Material.Ambient = VxColor(1.0f, 1.0f, 1.0f, 1.0f);
    memset(&m_State.MaterialConstants, 0, sizeof(m_State.MaterialConstants));
    m_State.MaterialConstants.Diffuse[0] = 1.0f;
    m_State.MaterialConstants.Diffuse[1] = 1.0f;
    m_State.MaterialConstants.Diffuse[2] = 1.0f;
    m_State.MaterialConstants.Diffuse[3] = 1.0f;
    m_State.MaterialConstants.Ambient[0] = 1.0f;
    m_State.MaterialConstants.Ambient[1] = 1.0f;
    m_State.MaterialConstants.Ambient[2] = 1.0f;
    m_State.MaterialConstants.Ambient[3] = 1.0f;
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetMaterial(const CKMaterialData *mat) {
    if (!mat) return;
    if (memcmp(&m_State.Material, mat, sizeof(*mat)) == 0)
        return;
    m_State.Material = *mat;
    m_State.MaterialConstants.Diffuse[0] = mat->Diffuse.r;
    m_State.MaterialConstants.Diffuse[1] = mat->Diffuse.g;
    m_State.MaterialConstants.Diffuse[2] = mat->Diffuse.b;
    m_State.MaterialConstants.Diffuse[3] = mat->Diffuse.a;
    m_State.MaterialConstants.Ambient[0] = mat->Ambient.r;
    m_State.MaterialConstants.Ambient[1] = mat->Ambient.g;
    m_State.MaterialConstants.Ambient[2] = mat->Ambient.b;
    m_State.MaterialConstants.Ambient[3] = mat->Ambient.a;
    m_State.MaterialConstants.Specular[0] = mat->Specular.r;
    m_State.MaterialConstants.Specular[1] = mat->Specular.g;
    m_State.MaterialConstants.Specular[2] = mat->Specular.b;
    m_State.MaterialConstants.Specular[3] = mat->Specular.a;
    m_State.MaterialConstants.Emissive[0] = mat->Emissive.r;
    m_State.MaterialConstants.Emissive[1] = mat->Emissive.g;
    m_State.MaterialConstants.Emissive[2] = mat->Emissive.b;
    m_State.MaterialConstants.Emissive[3] = mat->Emissive.a;
    m_State.MaterialConstants.Power = mat->SpecularPower;
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetLight(int index, const CKLightData *light) {
    if (index < 0 || index >= CKFF_MAX_LIGHTS || !light) return;
    if (memcmp(&m_State.Lights[index], light, sizeof(*light)) == 0)
        return;
    m_State.Lights[index] = *light;

    CKFFLightData &dst = m_State.LightConstants[index];

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

    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::EnableLight(int index, CKBOOL enable) {
    if (index < 0 || index >= CKFF_MAX_LIGHTS) return;
    enable = enable ? TRUE : FALSE;
    if (m_State.LightEnabled[index] == enable)
        return;
    m_State.LightEnabled[index] = enable;

    m_State.ActiveLightCount = 0;
    for (int i = 0; i < CKFF_MAX_LIGHTS; i++) {
        if (m_State.LightEnabled[i]) m_State.ActiveLightCount++;
    }
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
    const CKDWORD oldFlags = m_State.TextureFlags[stage];
    const CKDWORD oldStaticFlags = CKFFStaticTextureFlags(m_State.TextureFlags[stage]);
    const CKDWORD newStaticFlags = CKFFStaticTextureFlags(normalizedFlags);
    m_State.TextureHandles[stage] = textureHandle;
    m_State.TextureFlags[stage] = normalizedFlags;
    if (oldHasTexture != newHasTexture || oldStaticFlags != newStaticFlags) {
        OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM | CKFF_CHANGE_STATIC_UNIFORM |
                                    CKFF_CHANGE_DRAW_VALIDATION);
    } else if (oldFlags != normalizedFlags) {
        // Non-program texture flags such as BUMPLUMINANCE are packed into the
        // stage-parameter uniform even when the sampler dimension is unchanged.
        OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM |
                                    CKFF_CHANGE_DRAW_VALIDATION);
    }
}

CKDWORD CKFixedFunctionPipeline::GetTexture(int stage) const {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return 0;
    return m_State.TextureHandles[stage];
}
