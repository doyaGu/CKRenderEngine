#include "CKFFStateStore.h"
#include "CKFFRenderPacket.h"

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

void CKFFStateStore::Reset()
{
    ActiveLightCount = 0;
    AlphaTestPrecision = 0;
    Vx3DMatrixIdentity(World);
    Vx3DMatrixIdentity(View);
    Vx3DMatrixIdentity(Projection);
    Vx3DMatrixIdentity(m_ViewProjection);
    m_ViewProjectionHash = 0;
    m_ViewProjectionDirty = TRUE;
    for (int i = 0; i < CKFF_MAX_TEXTURE_STAGES; i++)
        Vx3DMatrixIdentity(TexMatrix[i]);
    for (int i = 0; i < CKFF_VERTEX_BLEND_MATRIX_COUNT; ++i) {
        Vx3DMatrixIdentity(VertexBlendMatrices[i]);
        VertexBlendMatrixSet[i] = FALSE;
    }
    VertexBlendPaletteOverflow = FALSE;

    memset(&Material, 0, sizeof(Material));
    Material.Diffuse[0] = 1.0f;
    Material.Diffuse[1] = 1.0f;
    Material.Diffuse[2] = 1.0f;
    Material.Diffuse[3] = 1.0f;
    Material.Ambient[0] = 1.0f;
    Material.Ambient[1] = 1.0f;
    Material.Ambient[2] = 1.0f;
    Material.Ambient[3] = 1.0f;

    memset(Lights, 0, sizeof(Lights));
    memset(LightEnabled, 0, sizeof(LightEnabled));
    memset(TextureHandles, 0, sizeof(TextureHandles));
    memset(TextureFlags, 0, sizeof(TextureFlags));
    memset(StageStates, 0, sizeof(StageStates));
    memset(StageStateSetMasks, 0, sizeof(StageStateSetMasks));
    memset(UserClipPlanes, 0, sizeof(UserClipPlanes));

    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        StageStates[stage][CKRST_TSS_TEXCOORDINDEX] = (CKDWORD)stage;
        TexcoordComponentCounts[stage] = 2;
    }

    Viewport[0] = 2.0f / 800.0f;
    Viewport[1] = -2.0f / 600.0f;
    Viewport[2] = -1.0f;
    Viewport[3] = 1.0f;
}
