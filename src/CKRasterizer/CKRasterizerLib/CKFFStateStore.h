#ifndef CKFFSTATESTORE_H
#define CKFFSTATESTORE_H

#include "VxMath.h"
#include "CKRasterizerBackendTypes.h"
#include "CKRasterizerTypes.h"
#include "CKFFStateDesc.h"
#include "CKFFConstants.h"
#include "CKDrawStateCache.h"

struct CKFFStateStore {
    CKDrawStateCache DrawState;
    VxMatrix World;
    VxMatrix View;
    VxMatrix Projection;
    VxMatrix TexMatrix[CKFF_MAX_TEXTURE_STAGES];
    VxMatrix VertexBlendMatrices[CKFF_VERTEX_BLEND_MATRIX_COUNT];
    CKBOOL VertexBlendMatrixSet[CKFF_VERTEX_BLEND_MATRIX_COUNT];
    CKBOOL VertexBlendPaletteOverflow;
    // A render-target texture is bound. On bottom-left-origin backends the
    // pipeline then renders upside down so the texture memory matches the
    // D3D layout (spec 5.9, RTT origin) and sampling needs no flip.
    CKBOOL RenderTargetActive;

    CKMaterialData Material;
    CKLightData Lights[CKFF_MAX_LIGHTS];
    // Derived shader data, rebuilt only when the corresponding source changes.
    CKFFMaterialData MaterialConstants;
    CKFFLightData LightConstants[CKFF_MAX_LIGHTS];
    CKBOOL LightEnabled[CKFF_MAX_LIGHTS];
    int ActiveLightCount;

    CKDWORD TextureHandles[CKFF_MAX_TEXTURE_STAGES];
    CKDWORD TextureFlags[CKFF_MAX_TEXTURE_STAGES];
    CKDWORD StageStates[CKFF_MAX_TEXTURE_STAGES][CKFF_MAX_TEXTURE_STAGE_STATES];
    uint64_t StageStateSetMasks[CKFF_MAX_TEXTURE_STAGES];
    // Query defaults differ from unresolved FFP values before the first write.
    // A bit selects the single stored value, including an explicitly cleared
    // zero. This is independent of the combine-expression explicit mask.
    uint64_t StageStateQueryMasks[CKFF_MAX_TEXTURE_STAGES];
    CKBYTE TexcoordComponentCounts[CKFF_MAX_TEXTURE_STAGES];

    float Viewport[4];               // POSITIONT screen -> viewport-relative clip mapping
    // D3D viewport emulation (spec 4.4): the engine's viewport is a sub
    // rectangle of the logical target (window pixels or texture size); every
    // draw remaps viewport-relative clip space into the target and clips with
    // a scissor scaled to the physical target (window x RenderScale).
    CKViewportData ViewportData;
    CKDWORD TargetLogicalWidth, TargetLogicalHeight;
    CKDWORD TargetPhysicalWidth, TargetPhysicalHeight;
    float ViewportRemap[4];          // clip x,y scale (0,1) and offset (2,3); identity = 1,1,0,0
    CKBOOL ViewportRemapIdentity;
    CKRECT Scissor;                  // physical target pixels (already mirrored for a flipped target)
    CKBOOL ScissorEnabled;
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
