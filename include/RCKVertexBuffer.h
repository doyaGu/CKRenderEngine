#ifndef RCKVERTEXBUFFER_H
#define RCKVERTEXBUFFER_H

#include "VxMemoryPool.h"
#include "CKVertexBuffer.h"
#include "CKRasterizerTypes.h"

struct RCKVertexBuffer : public CKVertexBuffer {
public:
    explicit RCKVertexBuffer(CKContext *context);

    ~RCKVertexBuffer() override;

    void Destroy() override;

    CKVB_STATE Check(CKRenderContext *Ctx, CKDWORD MaxVertexCount, CKRST_DPFLAGS Format, CKBOOL Dynamic) override;

    VxDrawPrimitiveData *Lock(CKRenderContext *Ctx, CKDWORD StartVertex, CKDWORD VertexCount, CKLOCKFLAGS LockFlags) override;

    void Unlock(CKRenderContext *Ctx) override;

    CKBOOL Draw(CKRenderContext *Ctx, VXPRIMITIVETYPE pType, CKWORD *Indices, int IndexCount, CKDWORD StartVertex, CKDWORD VertexCount) override;

    void InvalidateHardwareBuffer();

protected:
    CKDWORD m_ObjectIndex;
    CKVertexBufferDesc m_Desc;
    CKBOOL m_Valid;
    VxMemoryPool m_MemoryPool;
    CKContext *m_CKContext;
    VxDrawPrimitiveData m_DpData;
    VxDrawPrimitiveData m_LockedData;
    CKDWORD m_FormatFlags;
    CKDWORD m_VertexLayout;
    CKBOOL m_HardwareValid;
    CKDWORD m_LockedStart;
    CKDWORD m_LockedCount;
    CKLOCKFLAGS m_LockFlags;
    CKDWORD m_DirtyStart;
    CKDWORD m_DirtyCount;
};


#endif // RCKVERTEXBUFFER_H
