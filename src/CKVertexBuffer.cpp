#include "RCKVertexBuffer.h"

#include "CKRasterizer.h"
#include "RCKRenderManager.h"
#include "RCKRenderContext.h"
#include "CKTransientGeometry.h"
#include "CKVertexLayoutCache.h"

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

void OffsetDrawPrimitiveData(VxDrawPrimitiveData &data, CKDWORD startVertex, CKDWORD vertexCount) {
    data.VertexCount = (int)vertexCount;
    if (data.PositionPtr)
        data.PositionPtr = (CKBYTE *)data.PositionPtr + startVertex * data.PositionStride;
    if (data.NormalPtr)
        data.NormalPtr = (CKBYTE *)data.NormalPtr + startVertex * data.NormalStride;
    if (data.ColorPtr)
        data.ColorPtr = (CKBYTE *)data.ColorPtr + startVertex * data.ColorStride;
    if (data.SpecularColorPtr)
        data.SpecularColorPtr = (CKBYTE *)data.SpecularColorPtr + startVertex * data.SpecularColorStride;
    if (data.TexCoordPtr)
        data.TexCoordPtr = (CKBYTE *)data.TexCoordPtr + startVertex * data.TexCoordStride;
    for (int i = 0; i < CKRST_MAX_STAGES - 1; ++i) {
        if (data.TexCoordPtrs[i])
            data.TexCoordPtrs[i] = (CKBYTE *)data.TexCoordPtrs[i] + startVertex * data.TexCoordStrides[i];
    }
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
    m_FormatFlags = 0;
    m_VertexLayout = 0;
    m_HardwareValid = FALSE;
    m_LockedStart = 0;
    m_LockedCount = 0;
    m_LockFlags = CK_LOCK_DEFAULT;
    m_DirtyStart = 0;
    m_DirtyCount = 0;
}

RCKVertexBuffer::~RCKVertexBuffer() {
    ClearVertexBufferStaging(m_DpData);
    RCKRenderManager *rm = (RCKRenderManager *) m_CKContext->GetRenderManager();
    rm->ReleaseObjectIndex(m_ObjectIndex, CKRST_OBJ_VERTEXBUFFER);
}

void RCKVertexBuffer::Destroy() {
    ClearVertexBufferStaging(m_DpData);
    m_Valid = FALSE;
    m_HardwareValid = FALSE;
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
    m_HardwareValid = FALSE;
    m_FormatFlags = 0;
    m_VertexLayout = 0;
    m_DirtyStart = 0;
    m_DirtyCount = 0;
    return CK_VB_LOST;
}

VxDrawPrimitiveData *RCKVertexBuffer::Lock(CKRenderContext *Ctx, CKDWORD StartVertex, CKDWORD VertexCount, CKLOCKFLAGS LockFlags) {
    (void) Ctx;

    if (!m_Valid || StartVertex >= m_Desc.m_MaxVertexCount)
        return nullptr;

    if (StartVertex + VertexCount > m_Desc.m_MaxVertexCount)
        VertexCount = m_Desc.m_MaxVertexCount - StartVertex;

    m_Desc.m_CurrentVCount = StartVertex + VertexCount;
    m_LockedData = m_DpData;
    OffsetDrawPrimitiveData(m_LockedData, StartVertex, VertexCount);
    m_LockedStart = StartVertex;
    m_LockedCount = VertexCount;
    m_LockFlags = LockFlags;

    return &m_LockedData;
}

void RCKVertexBuffer::Unlock(CKRenderContext *Ctx) {
    if (!Ctx || !m_Valid || m_LockedCount == 0)
        return;

    RCKRenderContext *rctx = static_cast<RCKRenderContext *>(Ctx);
    if (!rctx->m_RasterizerContext)
        return;

    const bool hasNormal = m_DpData.NormalPtr != nullptr;
    const bool hasUV = m_DpData.TexCoordPtr != nullptr;
    CKDWORD formatFlags = CKVertexLayoutCache::DPFlagsToFormatFlags(m_DpData.Flags, hasNormal, hasUV);
    CKDWORD stride = 0;
    CKDWORD layout = rctx->m_FFPipeline.GetVertexLayoutCache().GetLayout(formatFlags, &stride);
    if (stride == 0 || layout == 0)
        return;

    CKDWORD updateStart = m_LockedStart;
    CKDWORD updateCount = m_LockedCount;
    if ((m_LockFlags & CK_LOCK_DISCARD) != 0 || !m_HardwareValid ||
        formatFlags != m_FormatFlags || layout != m_VertexLayout) {
        updateStart = 0;
        updateCount = m_Desc.m_CurrentVCount;
    }

    VxDrawPrimitiveData updateData = m_DpData;
    OffsetDrawPrimitiveData(updateData, updateStart, updateCount);
    const CKDWORD updateSize = updateCount * stride;
    CKBYTE *interleaved = (CKBYTE *)VxMalloc(updateSize);
    if (!interleaved)
        return;
    CKTransientGeometry::InterleaveVertices(interleaved, stride, updateCount, formatFlags, &updateData);

    if (!m_HardwareValid || formatFlags != m_FormatFlags || layout != m_VertexLayout ||
        (m_LockFlags & CK_LOCK_DISCARD) != 0) {
        rctx->m_RasterizerContext->DeleteObject(m_ObjectIndex, CKRST_OBJ_VERTEXBUFFER);
        CKVertexBufferDesc desc = m_Desc;
        desc.m_Flags = CKRST_VB_VALID | CKRST_VB_WRITEONLY;
        desc.m_VertexSize = stride;
        desc.m_CurrentVCount = m_Desc.m_CurrentVCount;
        if (rctx->m_RasterizerContext->CreateVertexBuffer(m_ObjectIndex, &desc, interleaved) == CK_OK) {
            m_HardwareValid = TRUE;
            m_FormatFlags = formatFlags;
            m_VertexLayout = layout;
        } else {
            m_HardwareValid = FALSE;
        }
    } else {
        if (rctx->m_RasterizerContext->UpdateVertexBuffer(m_ObjectIndex, updateStart * stride, updateSize, interleaved) != CK_OK)
            m_HardwareValid = FALSE;
    }

    VxFree(interleaved);
    m_DirtyStart = updateStart;
    m_DirtyCount = updateCount;
    m_LockedCount = 0;
}

CKBOOL RCKVertexBuffer::Draw(CKRenderContext *Ctx, VXPRIMITIVETYPE pType, CKWORD *Indices, int IndexCount, CKDWORD StartVertex, CKDWORD VertexCount) {
    if (!Ctx || !m_Valid || VertexCount == 0 || StartVertex >= m_Desc.m_CurrentVCount)
        return FALSE;

    if (StartVertex + VertexCount > m_Desc.m_CurrentVCount)
        VertexCount = m_Desc.m_CurrentVCount - StartVertex;

    if (!Indices)
        IndexCount = (int) VertexCount;

    RCKRenderContext *rctx = static_cast<RCKRenderContext *>(Ctx);
    if (m_HardwareValid && !Indices &&
        rctx && rctx->m_RasterizerContext &&
        rctx->m_FFPipeline.GetRenderState(VXRENDERSTATE_WRAP0) == 0 &&
        !(pType == VX_POINTLIST && rctx->m_FFPipeline.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE))) {
        CKRenderView view = (m_DpData.Flags & CKRST_DP_TRANSFORM)
            ? rctx->m_Current3DView
            : rctx->m_Current2DView;
        rctx->m_FFPipeline.DrawVertexBuffer(
            rctx->m_FFPipeline.GetRenderPipeline().GetEncoder(),
            view,
            pType,
            m_ObjectIndex,
            0,
            StartVertex,
            VertexCount,
            0,
            VertexCount,
            m_DpData.Flags,
            m_FormatFlags,
            m_VertexLayout);
        return TRUE;
    }

    VxDrawPrimitiveData drawData = m_DpData;
    OffsetDrawPrimitiveData(drawData, StartVertex, VertexCount);
    return Ctx->DrawPrimitive(pType, Indices, IndexCount, &drawData);
}
