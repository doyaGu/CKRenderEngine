#include "RCKVertexBuffer.h"

#include "CKRasterizer.h"
#include "CKVertexPacking.h"
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

CKBOOL HasTextureCoordinateWrap(RCKRenderContext *rctx) {
    for (int stage = 0; stage < CKRST_MAX_STAGES; ++stage) {
        if ((rctx->GetRasterizerRenderState(
                 (VXRENDERSTATETYPE)(VXRENDERSTATE_WRAP0 + stage)) & VXWRAP_MASK) != 0) {
            return TRUE;
        }
    }
    return FALSE;
}

void ClearVertexBufferStaging(VxDrawPrimitiveData &data) {
    VxDeleteAligned(data.PositionPtr);
    VxDeleteAligned(data.NormalPtr);
    VxDeleteAligned(data.TweenPositionPtr);
    VxDeleteAligned(data.TweenNormalPtr);
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
    if (data.TweenPositionPtr)
        data.TweenPositionPtr = (CKBYTE *)data.TweenPositionPtr +
                                startVertex * data.TweenPositionStride;
    if (data.TweenNormalPtr)
        data.TweenNormalPtr = (CKBYTE *)data.TweenNormalPtr +
                              startVertex * data.TweenNormalStride;
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
    if ((flags & CKRST_DP_TRANSFORM) && (flags & CKRST_DP_WEIGHTMASK))
        size = CKRSTGetBlendVertexSize(flags);
    if (flags & CKRST_DP_LIGHT)
        size += sizeof(VxVector);
    if (flags & CKRST_DP_TWEEN) {
        size += sizeof(VxVector);
        if (flags & CKRST_DP_LIGHT)
            size += sizeof(VxVector);
    }
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
    if (flags & CKRST_DP_TRANSFORM) {
        data.PositionStride = (flags & CKRST_DP_WEIGHTMASK)
            ? CKRSTGetBlendVertexSize(flags)
            : sizeof(VxVector);
    } else {
        data.PositionStride = sizeof(VxVector4);
    }
    data.PositionPtr = VxNewAligned(data.PositionStride * maxVertexCount, 16);
    if (!data.PositionPtr)
        return FALSE;

    if (flags & CKRST_DP_LIGHT) {
        data.NormalStride = sizeof(VxVector);
        data.NormalPtr = VxNewAligned(data.NormalStride * maxVertexCount, 16);
        if (!data.NormalPtr)
            return FALSE;
    }

    if (flags & CKRST_DP_TWEEN) {
        data.TweenPositionStride = sizeof(VxVector);
        data.TweenPositionPtr = VxNewAligned(
            data.TweenPositionStride * maxVertexCount, 16);
        if (!data.TweenPositionPtr)
            return FALSE;
        if (flags & CKRST_DP_LIGHT) {
            data.TweenNormalStride = sizeof(VxVector);
            data.TweenNormalPtr = VxNewAligned(
                data.TweenNormalStride * maxVertexCount, 16);
            if (!data.TweenNormalPtr)
                return FALSE;
        }
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
    RCKVertexBufferObjectState rasterizerState = {};
    SetRasterizerObjectState(rasterizerState);
    memset(&m_DpData, 0, sizeof(m_DpData));
    memset(&m_LockedData, 0, sizeof(m_LockedData));
    m_Valid = FALSE;
    m_ContentVersion = 0;
    m_LockedStart = 0;
    m_LockedCount = 0;
    m_LockFlags = CK_LOCK_DEFAULT;
    m_DirtyStart = 0;
    m_DirtyCount = 0;
}

RCKVertexBuffer::~RCKVertexBuffer() {
    ClearVertexBufferStaging(m_DpData);
    RCKRenderManager *rm = m_CKContext
        ? static_cast<RCKRenderManager *>(m_CKContext->GetRenderManager())
        : nullptr;
    if (rm) {
        rm->DeleteVertexBufferObjects(this, FALSE);
    } else if (m_RasterizerContext && m_ObjectIndex) {
        m_RasterizerContext->DeleteObject(m_ObjectIndex, CKRST_OBJ_VERTEXBUFFER);
    }
    RCKVertexBufferObjectState rasterizerState = {};
    SetRasterizerObjectState(rasterizerState);
}

void RCKVertexBuffer::GetRasterizerObjectState(RCKVertexBufferObjectState &State) const {
    State.Context = m_RasterizerContext;
    State.ObjectIndex = m_ObjectIndex;
    State.FormatFlags = m_FormatFlags;
    State.ContentVersion = m_HardwareVersion;
    State.Valid = m_HardwareValid;
}

void RCKVertexBuffer::SetRasterizerObjectState(const RCKVertexBufferObjectState &State) {
    m_RasterizerContext = State.Context;
    m_ObjectIndex = State.ObjectIndex;
    m_FormatFlags = State.FormatFlags;
    m_HardwareVersion = State.ContentVersion;
    m_HardwareValid = State.Valid;
}

void RCKVertexBuffer::Destroy() {
    ClearVertexBufferStaging(m_DpData);
    m_Valid = FALSE;
    m_HardwareValid = FALSE;
    RCKRenderManager *rm = (RCKRenderManager *) m_CKContext->GetRenderManager();
    rm->DestroyVertexBuffer(this);
}

CKVB_STATE RCKVertexBuffer::Check(CKRenderContext *Ctx, CKDWORD MaxVertexCount, CKRST_DPFLAGS Format, CKBOOL Dynamic) {
    CKRST_DPFLAGS cpuFormat = (CKRST_DPFLAGS)(Format & ~CKRST_DP_VBUFFER);
    const CKDWORD vertexSize = ComputeVertexStagingSize(cpuFormat);
    if (MaxVertexCount == 0 || vertexSize == 0)
        return CK_VB_FAILED;

    const CKDWORD bufferFlags = Dynamic ? CKRST_VB_DYNAMIC : 0;
    bool incompatible =
        !m_Valid ||
        cpuFormat != (m_DpData.Flags & ~CKRST_DP_VBUFFER) ||
        vertexSize != m_Desc.m_VertexSize ||
        MaxVertexCount > m_Desc.m_MaxVertexCount ||
        bufferFlags != (m_Desc.m_Flags & CKRST_VB_DYNAMIC);

    if (!incompatible)
        return m_ContentVersion != 0 && Upload(
            Ctx, 0, m_Desc.m_CurrentVCount, CKRST_LOCK_DEFAULT)
            ? CK_VB_OK
            : CK_VB_LOST;

    RCKRenderManager *rm = m_CKContext
        ? static_cast<RCKRenderManager *>(m_CKContext->GetRenderManager())
        : nullptr;
    if (rm && !rm->DeleteVertexBufferObjects(this))
        return CK_VB_FAILED;

    m_Desc.m_VertexFormat = cpuFormat;
    m_Desc.m_VertexSize = vertexSize;
    m_Desc.m_MaxVertexCount = MaxVertexCount;
    m_Desc.m_CurrentVCount = 0;
    m_Desc.m_Flags = bufferFlags;

    if (!AllocateVertexBufferStaging(m_DpData, cpuFormat, MaxVertexCount)) {
        ClearVertexBufferStaging(m_DpData);
        m_Valid = FALSE;
        return CK_VB_FAILED;
    }

    m_Valid = TRUE;
    m_HardwareValid = FALSE;
    m_FormatFlags = 0;
    m_ContentVersion = 0;
    m_HardwareVersion = 0;
    m_DirtyStart = 0;
    m_DirtyCount = 0;
    return CK_VB_LOST;
}

VxDrawPrimitiveData *RCKVertexBuffer::Lock(CKRenderContext *Ctx, CKDWORD StartVertex, CKDWORD VertexCount, CKLOCKFLAGS LockFlags) {
    (void) Ctx;

    if (!m_Valid || m_LockedCount != 0 || VertexCount == 0 ||
        (LockFlags != CK_LOCK_DEFAULT &&
         LockFlags != CK_LOCK_NOOVERWRITE &&
         LockFlags != CK_LOCK_DISCARD) ||
        StartVertex >= m_Desc.m_MaxVertexCount)
        return nullptr;

    if (VertexCount > m_Desc.m_MaxVertexCount - StartVertex)
        VertexCount = m_Desc.m_MaxVertexCount - StartVertex;

    const CKDWORD endVertex = StartVertex + VertexCount;
    if (endVertex > m_Desc.m_CurrentVCount)
        m_Desc.m_CurrentVCount = endVertex;
    m_LockedData = m_DpData;
    OffsetDrawPrimitiveData(m_LockedData, StartVertex, VertexCount);
    m_LockedStart = StartVertex;
    m_LockedCount = VertexCount;
    m_LockFlags = LockFlags;

    return &m_LockedData;
}

CKBOOL RCKVertexBuffer::Upload(CKRenderContext *Ctx, CKDWORD StartVertex,
                               CKDWORD VertexCount,
                               CKRST_LOCKFLAGS LockFlags) {
    if (!Ctx || !m_Valid || m_ContentVersion == 0 || VertexCount == 0)
        return FALSE;

    RCKRenderContext *rctx = static_cast<RCKRenderContext *>(Ctx);
    CKRasterizerContext *rst = rctx->m_RasterizerContext;
    RCKRenderManager *rm = m_CKContext
        ? static_cast<RCKRenderManager *>(m_CKContext->GetRenderManager())
        : nullptr;
    if (!rst || !rm || StartVertex >= m_Desc.m_CurrentVCount ||
        VertexCount > m_Desc.m_CurrentVCount - StartVertex)
        return FALSE;

    rm->SelectVertexBufferObject(this, rst);

    if (m_HardwareValid && m_ObjectIndex &&
        m_HardwareVersion == m_ContentVersion) {
        return TRUE;
    }

    const CKDWORD vertexFormat = CKRSTVertexFormatFromDrawData(&m_DpData);
    CKRSTVertexLayout layout;
    const CKDWORD stride = CKRSTGetVertexLayout(vertexFormat, nullptr, &layout);
    if (stride == 0)
        return FALSE;

    if (m_ObjectIndex && m_FormatFlags != vertexFormat) {
        if (!rm->DeleteVertexBufferObjects(this))
            return FALSE;
        m_RasterizerContext = rst;
    }

    const CKBOOL create = m_ObjectIndex == 0;
    if (create) {
        CKVertexBufferDesc desc = m_Desc;
        desc.m_Flags = CKRST_VB_VALID | CKRST_VB_WRITEONLY | (m_Desc.m_Flags & CKRST_VB_DYNAMIC);
        desc.m_VertexFormat = vertexFormat;
        desc.m_VertexSize = stride;
        desc.m_CurrentVCount = m_Desc.m_CurrentVCount;
        if (!rst->CreateVertexBuffer(&desc, nullptr, &m_ObjectIndex) ||
            !m_ObjectIndex) {
            m_ObjectIndex = 0;
            m_HardwareValid = FALSE;
            return FALSE;
        }

        m_RasterizerContext = rst;
        m_FormatFlags = vertexFormat;
        m_HardwareVersion = 0;
        m_HardwareValid = FALSE;
        if (!rm->VertexBufferObjectUpdated(this)) {
            rst->DeleteObject(m_ObjectIndex, CKRST_OBJ_VERTEXBUFFER);
            m_ObjectIndex = 0;
            m_FormatFlags = 0;
            return FALSE;
        }

        StartVertex = 0;
        VertexCount = m_Desc.m_CurrentVCount;
        LockFlags = CKRST_LOCK_DEFAULT;
    }

    // DISCARD replaces the native buffer contents. The engine's CPU staging
    // still contains every current vertex, so upload the complete snapshot
    // even when the caller only changed a subrange.
    if (LockFlags == CKRST_LOCK_DISCARD) {
        StartVertex = 0;
        VertexCount = m_Desc.m_CurrentVCount;
    }

    VxDrawPrimitiveData updateData = m_DpData;
    OffsetDrawPrimitiveData(updateData, StartVertex, VertexCount);
    CKBYTE *dst = static_cast<CKBYTE *>(rst->LockVertexBuffer(
        m_ObjectIndex, StartVertex, VertexCount, LockFlags));
    if (dst) {
        CKRSTPackVertices(layout, dst, VertexCount, &updateData);
        m_HardwareValid = rst->UnlockVertexBuffer(m_ObjectIndex);
    } else {
        m_HardwareValid = FALSE;
    }

    if (m_HardwareValid)
        m_HardwareVersion = m_ContentVersion;
    rm->VertexBufferObjectUpdated(this);
    return m_HardwareValid;
}

void RCKVertexBuffer::Unlock(CKRenderContext *Ctx) {
    if (!m_Valid || m_LockedCount == 0)
        return;

    const CKDWORD updateStart = m_LockedStart;
    const CKDWORD updateCount = m_LockedCount;
    const CKRST_LOCKFLAGS lockFlags =
        static_cast<CKRST_LOCKFLAGS>(m_LockFlags);
    m_LockedCount = 0;

    ++m_ContentVersion;
    if (m_ContentVersion == 0)
        ++m_ContentVersion;

    Upload(Ctx, updateStart, updateCount, lockFlags);
    m_DirtyStart = updateStart;
    m_DirtyCount = updateCount;
}

CKBOOL RCKVertexBuffer::Draw(CKRenderContext *Ctx, VXPRIMITIVETYPE pType, CKWORD *Indices, int IndexCount, CKDWORD StartVertex, CKDWORD VertexCount) {
    if (!Ctx || !m_Valid || VertexCount == 0 || StartVertex >= m_Desc.m_CurrentVCount)
        return FALSE;

    if (VertexCount > m_Desc.m_CurrentVCount - StartVertex)
        VertexCount = m_Desc.m_CurrentVCount - StartVertex;

    if (!Indices)
        IndexCount = (int) VertexCount;

    RCKRenderContext *rctx = static_cast<RCKRenderContext *>(Ctx);
    if (!Indices && rctx && rctx->m_RasterizerContext &&
        !HasTextureCoordinateWrap(rctx) &&
        !rctx->GetRasterizerRenderState(VXRENDERSTATE_INDEXVBLENDENABLE) &&
        pType != VX_POINTLIST) {
        Upload(Ctx, 0, m_Desc.m_CurrentVCount, CKRST_LOCK_DEFAULT);
        if (m_HardwareValid && m_ObjectIndex &&
            m_RasterizerContext == rctx->m_RasterizerContext &&
            m_HardwareVersion == m_ContentVersion) {
            return rctx->m_RasterizerContext->DrawPrimitiveVB(
                pType, m_ObjectIndex, StartVertex, VertexCount, nullptr, 0);
        }
    }

    VxDrawPrimitiveData drawData = m_DpData;
    OffsetDrawPrimitiveData(drawData, StartVertex, VertexCount);
    return Ctx->DrawPrimitive(pType, Indices, IndexCount, &drawData);
}
