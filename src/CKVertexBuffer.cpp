#include "RCKVertexBuffer.h"

#include "CKRasterizer.h"
#include "RCKRenderManager.h"
#include "RCKRenderContext.h"

namespace {

int GetActiveTexcoordCount(CKRST_DPFLAGS flags) {
    CKDWORD stageFlags = flags & CKRST_DP_STAGESMASK;
    if (!stageFlags)
        return 0;

    int count = 0;
    for (int i = 0; i < CKRST_MAX_STAGES; ++i) {
        if (stageFlags & CKRST_DP_STAGE(i))
            count = i + 1;
    }
    return count;
}

void ClearVertexBufferStaging(VxDrawPrimitiveData &data) {
    VxDeleteAligned(data.PositionPtr);
    VxDeleteAligned(data.NormalPtr);
    VxDeleteAligned(data.ColorPtr);
    VxDeleteAligned(data.SpecularColorPtr);
    VxDeleteAligned(data.TexCoordPtr);
    for (int i = 0; i < CKRST_MAX_STAGES - 1; ++i)
        VxDeleteAligned(data.TexCoordPtrs[i]);
    memset(&data, 0, sizeof(data));
}

CKDWORD ComputeVertexStagingSize(CKRST_DPFLAGS flags) {
    CKDWORD size = (flags & CKRST_DP_TRANSFORM) ? sizeof(VxVector) : sizeof(VxVector4);
    if (flags & CKRST_DP_LIGHT)
        size += sizeof(VxVector);
    if (flags & CKRST_DP_DIFFUSE)
        size += sizeof(CKDWORD);
    if (flags & CKRST_DP_SPECULAR)
        size += sizeof(CKDWORD);
    size += GetActiveTexcoordCount(flags) * sizeof(Vx2DVector);
    return size;
}

CKBOOL AllocateVertexBufferStaging(VxDrawPrimitiveData &data, CKRST_DPFLAGS flags, CKDWORD maxVertexCount) {
    ClearVertexBufferStaging(data);

    data.Flags = flags & ~CKRST_DP_VBUFFER;
    data.VertexCount = (int)maxVertexCount;
    data.PositionStride = (flags & CKRST_DP_TRANSFORM) ? sizeof(VxVector) : sizeof(VxVector4);
    data.PositionPtr = VxNewAligned(data.PositionStride * maxVertexCount, 16);
    if (!data.PositionPtr)
        return FALSE;

    if (flags & CKRST_DP_LIGHT) {
        data.NormalStride = sizeof(VxVector);
        data.NormalPtr = VxNewAligned(data.NormalStride * maxVertexCount, 16);
        if (!data.NormalPtr)
            return FALSE;
    }

    if (flags & CKRST_DP_DIFFUSE) {
        data.ColorStride = sizeof(CKDWORD);
        data.ColorPtr = VxNewAligned(data.ColorStride * maxVertexCount, 16);
        if (!data.ColorPtr)
            return FALSE;
    }

    if (flags & CKRST_DP_SPECULAR) {
        data.SpecularColorStride = sizeof(CKDWORD);
        data.SpecularColorPtr = VxNewAligned(data.SpecularColorStride * maxVertexCount, 16);
        if (!data.SpecularColorPtr)
            return FALSE;
    }

    const int texcoordCount = GetActiveTexcoordCount(flags);
    if (texcoordCount > 0) {
        data.TexCoordStride = sizeof(Vx2DVector);
        data.TexCoordPtr = VxNewAligned(data.TexCoordStride * maxVertexCount, 16);
        if (!data.TexCoordPtr)
            return FALSE;

        for (int stage = 1; stage < texcoordCount; ++stage) {
            data.TexCoordStrides[stage - 1] = sizeof(Vx2DVector);
            data.TexCoordPtrs[stage - 1] = VxNewAligned(sizeof(Vx2DVector) * maxVertexCount, 16);
            if (!data.TexCoordPtrs[stage - 1])
                return FALSE;
        }
    }

    return TRUE;
}

} // namespace

RCKVertexBuffer::RCKVertexBuffer(CKContext *context) : CKVertexBuffer(), m_Desc(), m_MemoryPool() {
    m_CKContext = context;
    RCKRenderManager *rm = (RCKRenderManager *) m_CKContext->GetRenderManager();
    m_ObjectIndex = rm->CreateObjectIndex(CKRST_OBJ_VERTEXBUFFER);
    m_DpData.Flags = 0;
    memset(&m_LockedData, 0, sizeof(m_LockedData));
    m_Valid = FALSE;
}

RCKVertexBuffer::~RCKVertexBuffer() {
    ClearVertexBufferStaging(m_DpData);
    RCKRenderManager *rm = (RCKRenderManager *) m_CKContext->GetRenderManager();
    rm->ReleaseObjectIndex(m_ObjectIndex, CKRST_OBJ_VERTEXBUFFER);
}

void RCKVertexBuffer::Destroy() {
    ClearVertexBufferStaging(m_DpData);
    m_Valid = FALSE;
    RCKRenderManager *rm = (RCKRenderManager *) m_CKContext->GetRenderManager();
    rm->DestroyVertexBuffer(this);
}

CKVB_STATE RCKVertexBuffer::Check(CKRenderContext *Ctx, CKDWORD MaxVertexCount, CKRST_DPFLAGS Format, CKBOOL Dynamic) {
    (void)Ctx;

    CKRST_DPFLAGS cpuFormat = (CKRST_DPFLAGS)(Format & ~CKRST_DP_VBUFFER);
    CKDWORD vertexSize = ComputeVertexStagingSize(cpuFormat);
    bool incompatible =
        !m_Valid ||
        cpuFormat != (m_DpData.Flags & ~CKRST_DP_VBUFFER) ||
        vertexSize != m_Desc.m_VertexSize ||
        MaxVertexCount > m_Desc.m_MaxVertexCount;

    if (!incompatible)
        return CK_VB_OK;

    m_Desc.m_VertexFormat = cpuFormat;
    m_Desc.m_VertexSize = vertexSize;
    m_Desc.m_MaxVertexCount = MaxVertexCount;
    m_Desc.m_CurrentVCount = 0;
    m_Desc.m_Flags = Dynamic ? 0x8 : 0;

    if (!AllocateVertexBufferStaging(m_DpData, cpuFormat, MaxVertexCount)) {
        ClearVertexBufferStaging(m_DpData);
        m_Valid = FALSE;
        return CK_VB_FAILED;
    }

    m_Valid = TRUE;
    return CK_VB_LOST;
}

VxDrawPrimitiveData *RCKVertexBuffer::Lock(CKRenderContext *Ctx, CKDWORD StartVertex, CKDWORD VertexCount, CKLOCKFLAGS LockFlags) {
    (void) Ctx;
    (void) LockFlags;

    if (!m_Valid || StartVertex >= m_Desc.m_MaxVertexCount)
        return nullptr;

    if (StartVertex + VertexCount > m_Desc.m_MaxVertexCount)
        VertexCount = m_Desc.m_MaxVertexCount - StartVertex;

    m_Desc.m_CurrentVCount = StartVertex + VertexCount;
    m_LockedData = m_DpData;
    m_LockedData.VertexCount = (int) VertexCount;

    VxDrawPrimitiveData *locked = &m_LockedData;
    if (locked->PositionPtr)
        locked->PositionPtr = (CKBYTE *)locked->PositionPtr + StartVertex * locked->PositionStride;
    if (locked->NormalPtr)
        locked->NormalPtr = (CKBYTE *)locked->NormalPtr + StartVertex * locked->NormalStride;
    if (locked->ColorPtr)
        locked->ColorPtr = (CKBYTE *)locked->ColorPtr + StartVertex * locked->ColorStride;
    if (locked->SpecularColorPtr)
        locked->SpecularColorPtr = (CKBYTE *)locked->SpecularColorPtr + StartVertex * locked->SpecularColorStride;
    if (locked->TexCoordPtr)
        locked->TexCoordPtr = (CKBYTE *)locked->TexCoordPtr + StartVertex * locked->TexCoordStride;
    for (int i = 0; i < CKRST_MAX_STAGES - 1; ++i) {
        if (locked->TexCoordPtrs[i])
            locked->TexCoordPtrs[i] = (CKBYTE *)locked->TexCoordPtrs[i] + StartVertex * locked->TexCoordStrides[i];
    }

    return locked;
}

void RCKVertexBuffer::Unlock(CKRenderContext *Ctx) {
    (void) Ctx;
}

CKBOOL RCKVertexBuffer::Draw(CKRenderContext *Ctx, VXPRIMITIVETYPE pType, CKWORD *Indices, int IndexCount, CKDWORD StartVertex, CKDWORD VertexCount) {
    if (!Ctx || !m_Valid || VertexCount == 0 || StartVertex >= m_Desc.m_CurrentVCount)
        return FALSE;

    if (StartVertex + VertexCount > m_Desc.m_CurrentVCount)
        VertexCount = m_Desc.m_CurrentVCount - StartVertex;

    if (!Indices)
        IndexCount = (int) VertexCount;

    VxDrawPrimitiveData drawData = m_DpData;
    drawData.VertexCount = (int)VertexCount;
    if (drawData.PositionPtr)
        drawData.PositionPtr = (CKBYTE *)drawData.PositionPtr + StartVertex * drawData.PositionStride;
    if (drawData.NormalPtr)
        drawData.NormalPtr = (CKBYTE *)drawData.NormalPtr + StartVertex * drawData.NormalStride;
    if (drawData.ColorPtr)
        drawData.ColorPtr = (CKBYTE *)drawData.ColorPtr + StartVertex * drawData.ColorStride;
    if (drawData.SpecularColorPtr)
        drawData.SpecularColorPtr = (CKBYTE *)drawData.SpecularColorPtr + StartVertex * drawData.SpecularColorStride;
    if (drawData.TexCoordPtr)
        drawData.TexCoordPtr = (CKBYTE *)drawData.TexCoordPtr + StartVertex * drawData.TexCoordStride;
    for (int i = 0; i < CKRST_MAX_STAGES - 1; ++i) {
        if (drawData.TexCoordPtrs[i])
            drawData.TexCoordPtrs[i] = (CKBYTE *)drawData.TexCoordPtrs[i] + StartVertex * drawData.TexCoordStrides[i];
    }

    return Ctx->DrawPrimitive(pType, Indices, IndexCount, &drawData);
}
