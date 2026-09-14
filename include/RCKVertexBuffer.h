#ifndef RCKVERTEXBUFFER_H
#define RCKVERTEXBUFFER_H

#include "VxMemoryPool.h"
#include "CKVertexBuffer.h"
#include "CKRasterizer.h"
#include "RCKRasterizerObjectStates.h"

struct RCKVertexBuffer : public CKVertexBuffer {
public:
    explicit RCKVertexBuffer(CKContext *context);

    ~RCKVertexBuffer() override;

    void Destroy() override;

    CKVB_STATE Check(CKRenderContext *Ctx, CKDWORD MaxVertexCount, CKRST_DPFLAGS Format, CKBOOL Dynamic) override;

    VxDrawPrimitiveData *Lock(CKRenderContext *Ctx, CKDWORD StartVertex, CKDWORD VertexCount, CKLOCKFLAGS LockFlags) override;

    void Unlock(CKRenderContext *Ctx) override;

    CKBOOL Draw(CKRenderContext *Ctx, VXPRIMITIVETYPE pType, CKWORD *Indices, int IndexCount, CKDWORD StartVertex, CKDWORD VertexCount) override;

    void GetRasterizerObjectState(RCKVertexBufferObjectState &State) const;
    void SetRasterizerObjectState(const RCKVertexBufferObjectState &State);

protected:
    CKBOOL Upload(CKRenderContext *Ctx, CKDWORD StartVertex,
                  CKDWORD VertexCount, CKRST_LOCKFLAGS LockFlags);

    CKDWORD m_ObjectIndex;
    CKVertexBufferDesc m_Desc;
    CKBOOL m_Valid;
    VxMemoryPool m_MemoryPool;
    CKContext *m_CKContext;
    VxDrawPrimitiveData m_DpData;
    VxDrawPrimitiveData m_LockedData;
    CKDWORD m_FormatFlags;
    // Source-compatible view of the object selected for the last context.
    // RCKRenderManager owns the complete per-context list.
    CKRasterizerContext *m_RasterizerContext;
    CKBOOL m_HardwareValid;
    CKDWORD m_ContentVersion;
    CKDWORD m_HardwareVersion;
    CKDWORD m_LockedStart;
    CKDWORD m_LockedCount;
    CKLOCKFLAGS m_LockFlags;
    CKDWORD m_DirtyStart;
    CKDWORD m_DirtyCount;
};


#endif // RCKVERTEXBUFFER_H
