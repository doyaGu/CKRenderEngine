#ifndef CKFFSTATESTORE_H
#define CKFFSTATESTORE_H

#include "VxMath.h"
#include "CKRasterizerTypes.h"
#include "CKFFStateDesc.h"
#include "CKFFConstants.h"

struct CKFFStateStore {
    VxMatrix World;
    VxMatrix View;
    VxMatrix Projection;
    VxMatrix TexMatrix[CKFF_MAX_TEXTURE_STAGES];
    VxMatrix VertexBlendMatrices[CKFF_VERTEX_BLEND_MATRIX_COUNT];
    CKBOOL VertexBlendMatrixSet[CKFF_VERTEX_BLEND_MATRIX_COUNT];

    CKFFMaterialData Material;
    CKFFLightData Lights[CKFF_MAX_LIGHTS];
    CKBOOL LightEnabled[CKFF_MAX_LIGHTS];
    int ActiveLightCount;

    CKDWORD TextureHandles[CKFF_MAX_TEXTURE_STAGES];
    CKDWORD TextureFlags[CKFF_MAX_TEXTURE_STAGES];
    CKDWORD StageStates[CKFF_MAX_TEXTURE_STAGES][CKFF_MAX_TEXTURE_STAGE_STATES];
    CKBYTE TexcoordComponentCounts[CKFF_MAX_TEXTURE_STAGES];

    float Viewport[4];
    VxPlane UserClipPlanes[6];
    CKDWORD AlphaTestPrecision;

    CKBOOL EnsureViewProjection();
    const VxMatrix &ViewProjection() const { return m_ViewProjection; }
    CKDWORD ViewProjectionHash() const { return m_ViewProjectionHash; }
    void MarkViewProjectionDirty() { m_ViewProjectionDirty = TRUE; }

    void Reset();

private:
    VxMatrix m_ViewProjection;
    CKDWORD m_ViewProjectionHash;
    CKBOOL m_ViewProjectionDirty;
};

#endif // CKFFSTATESTORE_H
