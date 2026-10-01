#include "CKFFStateStore.h"
#include "CKFFDrawTypes.h"
#include "CKRasterizer.h"
#include <string.h>

CKBOOL CKFFStateStore::EnsureViewProjection()
{
    if (!m_ViewProjectionDirty)
        return FALSE;
    Vx3DMultiplyMatrix4(m_ViewProjection, Projection, View);
    m_ViewProjectionHash = CKFFHashBytes(&m_ViewProjection,
                                         sizeof(m_ViewProjection),
                                         2166136261u);
    m_ViewProjectionDirty = FALSE;
    return TRUE;
}

CKBOOL CKFFStateStore::EnsureObjectViewProjection(CKBOOL originFlip)
{
    EnsureViewProjection();
    originFlip = originFlip ? TRUE : FALSE;
    if (!m_ObjectViewProjectionDirty &&
        m_ObjectViewProjectionOriginFlip == originFlip) {
        return FALSE;
    }

    VxMatrix remap;
    Vx3DMatrixIdentity(remap);
    remap[0][0] = ViewportRemap[0];
    remap[1][1] = ViewportRemap[1];
    remap[3][0] = ViewportRemap[2] +
        0.5f * Viewport[0] * ViewportRemap[0];
    remap[3][1] = ViewportRemap[3] +
        0.5f * Viewport[1] * ViewportRemap[1];
    Vx3DMultiplyMatrix4(m_ObjectViewProjection, remap, m_ViewProjection);
    if (originFlip) {
        VxMatrix flip;
        Vx3DMatrixIdentity(flip);
        flip[1][1] = -1.0f;
        VxMatrix flipped;
        Vx3DMultiplyMatrix4(flipped, flip, m_ObjectViewProjection);
        m_ObjectViewProjection = flipped;
    }
    m_ObjectViewProjectionDirty = FALSE;
    m_ObjectViewProjectionOriginFlip = originFlip;
    return TRUE;
}

void CKFFStateStore::Reset()
{
    DrawState.Reset();
    ActiveLightCount = 0;
    AlphaTestPrecision = 0;
    ColorTargetFormat = CKFF_COLOR_TARGET_RGBA8;
    DepthBiasFormat = CKRST_DEPTHFMT_D24S8;
    Vx3DMatrixIdentity(World);
    Vx3DMatrixIdentity(View);
    Vx3DMatrixIdentity(Projection);
    Vx3DMatrixIdentity(m_ViewProjection);
    Vx3DMatrixIdentity(m_ObjectViewProjection);
    m_ViewProjectionHash = 0;
    m_ViewProjectionDirty = TRUE;
    m_ObjectViewProjectionDirty = TRUE;
    m_ObjectViewProjectionOriginFlip = FALSE;
    for (int i = 0; i < CKFF_MAX_TEXTURE_STAGES; i++)
        Vx3DMatrixIdentity(TexMatrix[i]);
    for (int i = 0; i < CKFF_VERTEX_BLEND_MATRIX_COUNT; ++i) {
        Vx3DMatrixIdentity(VertexBlendMatrices[i]);
        VertexBlendMatrixSet[i] = FALSE;
    }
    RenderTargetActive = FALSE;

    memset(&MaterialConstants, 0, sizeof(MaterialConstants));
    Material = CKMaterialData();
    Material.Diffuse = Material.Ambient = VxColor(1.0f, 1.0f, 1.0f, 1.0f);
    MaterialConstants.Diffuse[0] = 1.0f;
    MaterialConstants.Diffuse[1] = 1.0f;
    MaterialConstants.Diffuse[2] = 1.0f;
    MaterialConstants.Diffuse[3] = 1.0f;
    MaterialConstants.Ambient[0] = 1.0f;
    MaterialConstants.Ambient[1] = 1.0f;
    MaterialConstants.Ambient[2] = 1.0f;
    MaterialConstants.Ambient[3] = 1.0f;

    memset(Lights, 0, sizeof(Lights));
    memset(LightConstants, 0, sizeof(LightConstants));
    memset(LightEnabled, 0, sizeof(LightEnabled));
    memset(TextureHandles, 0, sizeof(TextureHandles));
    memset(TextureFlags, 0, sizeof(TextureFlags));
    memset(StageStates, 0, sizeof(StageStates));
    memset(StageQueryStates, 0, sizeof(StageQueryStates));
    memset(StageStateSetMasks, 0, sizeof(StageStateSetMasks));
    memset(StageStateQueryMasks, 0, sizeof(StageStateQueryMasks));
    memset(ResetStageMasks, 0, sizeof(ResetStageMasks));
    memset(UserClipPlanes, 0, sizeof(UserClipPlanes));

    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        StageStates[stage][CKRST_TSS_TEXCOORDINDEX] = (CKDWORD)stage;
        for (CKDWORD state = CKRST_TSS_OP; state < CKFF_MAX_TEXTURE_STAGE_STATES; ++state)
            StageQueryStates[stage][state] = CKRSTDefaultTextureStageStateValue(
                stage, (CKRST_TEXTURESTAGESTATETYPE)state);
        TexcoordComponentCounts[stage] = 2;
    }

    Viewport[0] = 2.0f / 800.0f;
    Viewport[1] = -2.0f / 600.0f;
    Viewport[2] = -1.0f;
    Viewport[3] = 1.0f;
    memset(&ViewportData, 0, sizeof(ViewportData));
    ViewportData.ViewWidth = 800;
    ViewportData.ViewHeight = 600;
    ViewportData.ViewZMax = 1.0f;
    TargetLogicalWidth = TargetLogicalHeight = 0;
    TargetPhysicalWidth = TargetPhysicalHeight = 0;
    ViewportRemap[0] = ViewportRemap[1] = 1.0f;
    ViewportRemap[2] = ViewportRemap[3] = 0.0f;
    ViewportRemapIdentity = TRUE;
    Scissor.left = Scissor.top = Scissor.right = Scissor.bottom = 0;
    ScissorEnabled = FALSE;
}
