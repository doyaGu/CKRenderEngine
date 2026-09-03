#ifndef CKTRANSIENTGEOMETRY_H
#define CKTRANSIENTGEOMETRY_H

#include "VxDefines.h"
#include "VxMatrix.h"
#include "CKRasterizerBackendEnums.h"
#include "CKRasterizerBackendTypes.h"
#include "CKRasterizerBackend.h"
#include "XArray.h"

class CKVertexLayoutCache;

struct CKFFPointSpriteParams {
    float Size;
    float MinSize;
    float MaxSize;
    CKBOOL ScaleEnable;
    float ScaleA;
    float ScaleB;
    float ScaleC;
    VxMatrix World;
    VxMatrix View;
    VxMatrix Projection;
    float ViewportWidth;
    float ViewportHeight;
};

class CKTransientGeometry {
public:
    CKTransientGeometry();
    ~CKTransientGeometry();

    void Init(CKRasterizerBackend *backend, CKVertexLayoutCache *layoutCache);
    void Shutdown();

    // Pack VxDrawPrimitiveData (scattered attribute pointers with varying strides)
    // into transient backend vertices, optionally with transient indices; the
    // results (GetVertices / GetIndices) go into the CKBackendDraw of this draw.
    CKBOOL Prepare(
        VXPRIMITIVETYPE primType,
        CKWORD *indices,
        int indexCount,
        VxDrawPrimitiveData *data,
        CKDWORD wrapMode = 0,
        CKBOOL pointSprites = FALSE,
        const CKFFPointSpriteParams *pointParams = nullptr,
        const CKBYTE *texcoordComponentCounts = nullptr,
        const CKDWORD *wrapModes = nullptr);

    // Get the layout handle for the last Prepare call
    CKDWORD GetLayoutHandle() const { return m_LastLayout; }
    CKDWORD GetLastVertexBytes() const { return m_LastVertexBytes; }
    CKDWORD GetLastIndexBytes() const { return m_LastIndexBytes; }
    // Geometry of the last successful Prepare (indices NULL when non-indexed).
    const CKBackendTransientVertices *GetVertices() const { return &m_Vertices; }
    const CKBackendTransientIndices *GetIndices() const { return m_HasIndices ? &m_Indices : NULL; }

    // Convert triangle fan/strip indices to triangle list.
    // Returns the number of output indices written to dst.
    static int ConvertPrimitiveToTriangleList(VXPRIMITIVETYPE srcType,
                                              CKWORD *srcIndices, int srcCount,
                                              CKWORD *dst);

    static void AdjustTriangleWrapTexcoords(float uv[3][2], CKDWORD wrapMode);
    static void AdjustTriangleWrapTexcoords(float texcoords[3][4], CKDWORD wrapMode,
                                            CKDWORD componentCount);
    static float ComputePointSpriteSizeForDistance(float size, float minSize, float maxSize,
                                                   CKBOOL scaleEnable,
                                                   float scaleA, float scaleB, float scaleC,
                                                   float distance,
                                                   float viewportHeight = 1.0f);

    // Interleave canonical fixed-function vertex data. This is shared by
    // transient and hardware VB paths so both obey the same missing-attribute
    // default rules.
    static void InterleaveVertices(void *dst, CKDWORD stride, CKDWORD vertexCount,
                                   CKDWORD formatFlags, VxDrawPrimitiveData *data,
                                   const CKBYTE *texcoordComponentCounts = nullptr);
    static void InterleaveVertex(void *dst, CKDWORD stride, CKDWORD dstIndex,
                                 CKDWORD srcIndex, CKDWORD formatFlags,
                                 VxDrawPrimitiveData *data,
                                 const float *texcoord0Override = nullptr,
                                 const float *positionOverride = nullptr,
                                 const CKBYTE *texcoordComponentCounts = nullptr,
                                 const float *texcoordOverrides = nullptr);

private:
    CKRasterizerBackend *m_Backend;
    CKVertexLayoutCache *m_LayoutCache;
    CKBackendTransientVertices m_Vertices;
    CKBackendTransientIndices m_Indices;
    CKBOOL m_HasIndices;
    CKDWORD m_LastLayout;
    CKDWORD m_LastVertexBytes;
    CKDWORD m_LastIndexBytes;
    XArray<CKWORD> m_TempIndices;

    int ConvertToTriangleList(VXPRIMITIVETYPE srcType,
                              CKWORD *srcIndices, int srcCount,
                              CKWORD *dst);
};

#endif // CKTRANSIENTGEOMETRY_H
