#ifndef CKTRANSIENTGEOMETRY_H
#define CKTRANSIENTGEOMETRY_H

#include "VxDefines.h"
#include "VxMatrix.h"
#include "CKRasterizerContextEnums.h"
#include "XArray.h"

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

struct CKFFPointFillCullParams {
    VxMatrix World;
    VxMatrix ViewProjection;
    VxMatrix BlendMatrices[4];
    CKDWORD BlendMode;
    CKDWORD BlendCount;
    CKBOOL IndexedBlend;
    float TweenFactor;
    CKDWORD CullMode;
    CKBOOL InverseWinding;
};

class CKTransientGeometry {
public:
    CKTransientGeometry();
    ~CKTransientGeometry();

    void Clear();

    // Pack VxDrawPrimitiveData (scattered attribute pointers with varying strides)
    // into reusable CPU arrays. The concrete rasterizer copies these bytes to
    // its own transient storage immediately before submitting the draw.
    CKBOOL Prepare(
        VXPRIMITIVETYPE primType,
        CKWORD *indices,
        int indexCount,
        VxDrawPrimitiveData *data,
        CKDWORD wrapMode = 0,
        CKBOOL pointSprites = FALSE,
        const CKFFPointSpriteParams *pointParams = nullptr,
        const CKBYTE *texcoordComponentCounts = nullptr,
        const CKDWORD *wrapModes = nullptr,
        CKBOOL trackSourceIndices = FALSE);

    // Point fill uses point topology, which has no hardware face culling.
    // Cull the source triangles before that topology conversion. The output
    // flag reports triangles that cross clip planes or cannot be classified.
    void CullPointFilledTriangles(const CKFFPointFillCullParams &params,
                                  CKBOOL *approximate);

    // Expand the vertices of point-filled triangles to screen-sized quads.
    // The source data supplies per-vertex PSIZE, which is not part of the
    // backend's interleaved vertex layout. Each duplicate retains the point
    // centre and carries an internal pixel offset applied after vertex work.
    // Returns FALSE for vertex blending and tweening until their centre
    // distance calculation is available here.
    CKBOOL ExpandPointFilledTriangles(const CKFFPointSpriteParams &params,
                                      CKBOOL pointSprites,
                                      const VxDrawPrimitiveData *sourceData,
                                      CKBOOL *approximate);

    CKDWORD GetFormatFlags() const { return m_FormatFlags; }
    CKDWORD GetVertexCount() const { return m_VertexCount; }
    CKDWORD GetVertexStride() const { return m_VertexStride; }
    CKDWORD GetIndexCount() const { return m_IndexCount; }
    CKBOOL IsIndex32() const { return m_Index32; }
    CKDWORD GetLastVertexBytes() const { return m_LastVertexBytes; }
    CKDWORD GetLastIndexBytes() const { return m_LastIndexBytes; }
    const CKBYTE *GetVertices() const { return m_VertexData.IsEmpty() ? NULL : m_VertexData.Begin(); }
    const CKBYTE *GetIndices() const { return m_IndexData.IsEmpty() ? NULL : m_IndexData.Begin(); }

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
    XArray<CKBYTE> m_VertexData;
    XArray<CKBYTE> m_IndexData;
    CKDWORD m_FormatFlags;
    CKDWORD m_VertexCount;
    CKDWORD m_VertexStride;
    CKDWORD m_IndexCount;
    CKBOOL m_Index32;
    CKDWORD m_LastVertexBytes;
    CKDWORD m_LastIndexBytes;
    XArray<CKWORD> m_TempIndices;
    XArray<CKDWORD> m_SourceVertexIndices;

    int ConvertToTriangleList(VXPRIMITIVETYPE srcType,
                              CKWORD *srcIndices, int srcCount,
                              CKWORD *dst);
};

#endif // CKTRANSIENTGEOMETRY_H
