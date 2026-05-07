#ifndef CKTRANSIENTGEOMETRY_H
#define CKTRANSIENTGEOMETRY_H

#include "VxDefines.h"
#include "CKRasterizerEnums.h"
#include "CKRasterizerTypes.h"

#include <vector>

class CKRasterizerContext;
class CKRasterizerEncoder;
class CKVertexLayoutCache;

class CKTransientGeometry {
public:
    CKTransientGeometry();
    ~CKTransientGeometry();

    void Init(CKRasterizerContext *ctx, CKVertexLayoutCache *layoutCache);
    void Shutdown();

    // Pack VxDrawPrimitiveData (scattered attribute pointers with varying strides)
    // into an interleaved transient vertex buffer, optionally with an index buffer,
    // and bind them to the encoder ready for Submit.
    CKBOOL Prepare(
        CKRasterizerEncoder *encoder,
        VXPRIMITIVETYPE primType,
        CKWORD *indices,
        int indexCount,
        VxDrawPrimitiveData *data,
        CKDWORD wrapMode = 0,
        CKBOOL pointSprites = FALSE,
        float pointSize = 1.0f);

    // Get the layout handle for the last Prepare call
    CKDWORD GetLayoutHandle() const { return m_LastLayout; }

    // Convert triangle fan/strip indices to triangle list.
    // Returns the number of output indices written to dst.
    static int ConvertPrimitiveToTriangleList(VXPRIMITIVETYPE srcType,
                                              CKWORD *srcIndices, int srcCount,
                                              CKWORD *dst);

    static void AdjustTriangleWrapTexcoords(float uv[3][2], CKDWORD wrapMode);

    // Interleave canonical fixed-function vertex data. This is shared by
    // transient and hardware VB paths so both obey the same missing-attribute
    // default rules.
    static void InterleaveVertices(void *dst, CKDWORD stride, CKDWORD vertexCount,
                                   CKDWORD formatFlags, VxDrawPrimitiveData *data);
    static void InterleaveVertex(void *dst, CKDWORD stride, CKDWORD dstIndex,
                                 CKDWORD srcIndex, CKDWORD formatFlags,
                                 VxDrawPrimitiveData *data,
                                 const float *texcoord0Override = nullptr,
                                 const float *positionOverride = nullptr);

private:
    CKRasterizerContext *m_Context;
    CKVertexLayoutCache *m_LayoutCache;
    CKDWORD m_LastLayout;
    std::vector<CKWORD> m_TempIndices;

    int ConvertToTriangleList(VXPRIMITIVETYPE srcType,
                              CKWORD *srcIndices, int srcCount,
                              CKWORD *dst);
};

#endif // CKTRANSIENTGEOMETRY_H
