#include "CKFixedFunctionPipeline.h"
#include "CKFFRenderPacket.h"
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
    case VXRENDERSTATE_ALPHATESTENABLE:
    case VXRENDERSTATE_ALPHAFUNC:
    case VXRENDERSTATE_CLIPPLANEENABLE:
        return TRUE;
    default:
        return FALSE;
    }
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
    OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::ResetVertexBlendMatrices() {
    for (int i = 0; i < CKFF_VERTEX_BLEND_MATRIX_COUNT; ++i) {
        Vx3DMatrixIdentity(m_State.VertexBlendMatrices[i]);
        m_State.VertexBlendMatrixSet[i] = FALSE;
    }
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

void CKFixedFunctionPipeline::SetRenderState(VXRENDERSTATETYPE state, CKDWORD value) {
    if (m_DrawStateCache.GetRenderState(state) == value)
        return;
    m_DrawStateCache.SetRenderState(state, value);

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

void CKFixedFunctionPipeline::ResetTextureStage(int stage) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return;

    m_State.TextureHandles[stage] = 0;
    m_State.TextureFlags[stage] = 0;
    memset(m_State.StageStates[stage], 0, sizeof(m_State.StageStates[stage]));
    m_State.StageStateSetMasks[stage] = 0;
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
    snapshot.StateSetMask = m_State.StageStateSetMasks[stage];
    snapshot.TextureMatrix = m_State.TexMatrix[stage];
}

void CKFixedFunctionPipeline::RestoreTextureStage(int stage, const CKFFTextureStageSnapshot &snapshot) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return;

    m_State.TextureHandles[stage] = snapshot.Texture;
    m_State.TextureFlags[stage] = snapshot.TextureFlags;
    memcpy(m_State.StageStates[stage], snapshot.States, sizeof(m_State.StageStates[stage]));
    m_State.StageStateSetMasks[stage] = snapshot.StateSetMask;
    m_State.TexMatrix[stage] = snapshot.TextureMatrix;
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM | CKFF_CHANGE_STATIC_UNIFORM);
}

void CKFixedFunctionPipeline::SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type, CKDWORD value) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return;
    if ((int)type < 0 || (int)type >= CKFF_MAX_TEXTURE_STAGE_STATES) return;

    if (type == CKRST_TSS_STAGEBLEND && value == 0 && stage > 0) {
        DisableTextureStagesFrom(stage);
        return;
    }

    const uint64_t stateBit = 1ull << (CKDWORD)type;
    if (m_State.StageStates[stage][(int)type] == value &&
        (m_State.StageStateSetMasks[stage] & stateBit) != 0)
        return;

    m_State.StageStates[stage][(int)type] = value;
    m_State.StageStateSetMasks[stage] |= stateBit;

    if (type == CKRST_TSS_TEXTUREMAPBLEND) {
        ClearExplicitTextureCombineState(m_State.StageStates[stage],
                                         &m_State.StageStateSetMasks[stage]);
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
            m_State.StageStateSetMasks[stage] |=
                (1ull << CKRST_TSS_OP) |
                (1ull << CKRST_TSS_ARG1) |
                (1ull << CKRST_TSS_ARG2) |
                (1ull << CKRST_TSS_AOP) |
                (1ull << CKRST_TSS_AARG1) |
                (1ull << CKRST_TSS_AARG2);
        }
    }
    OnFixedFunctionStateChanged(CKFF_CHANGE_PROGRAM | CKFF_CHANGE_STATIC_UNIFORM);
}

CKDWORD CKFixedFunctionPipeline::GetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type) const {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return 0;
    if ((int)type < 0 || (int)type >= CKFF_MAX_TEXTURE_STAGE_STATES) return 0;
    return m_State.StageStates[stage][(int)type];
}

void CKFixedFunctionPipeline::SetViewport(const CKViewportData &viewport) {
    CK_FRAME_COST_ADD_VIEWPORT_SET();

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
        OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
        break;
    case VXMATRIX_VIEW:
        m_State.View = matrix;
        m_State.MarkViewProjectionDirty();
        OnFixedFunctionStateChanged(CKFF_CHANGE_STATIC_UNIFORM);
        break;
    case VXMATRIX_PROJECTION:
        m_State.Projection = matrix;
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
