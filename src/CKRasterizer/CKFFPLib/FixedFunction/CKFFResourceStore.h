#ifndef CKFFRESOURCESTORE_H
#define CKFFRESOURCESTORE_H

#include "CKFFBufferData.h"
#include "XClassArray.h"
#include "XSHashTable.h"

struct CKFFResourceHandle {
    CKDWORD Handle;
    CKRST_OBJECTTYPE Type;
};

// Canonical public resource records. These tables contain only caller-visible
// descriptors and CPU conversion/lock storage; concrete contexts own all
// native resource records and destruction policy.
class CKFFResourceStore {
public:
    CKTextureDesc *FindTexture(CKDWORD Handle);
    const CKTextureDesc *FindTexture(CKDWORD Handle) const;
    CKFFVertexBufferData *FindVertexBuffer(CKDWORD Handle);
    const CKFFVertexBufferData *FindVertexBuffer(CKDWORD Handle) const;
    CKFFIndexBufferData *FindIndexBuffer(CKDWORD Handle);
    const CKFFIndexBufferData *FindIndexBuffer(CKDWORD Handle) const;

    CKBOOL InsertTexture(CKDWORD Handle, const CKTextureDesc &Desc);
    CKBOOL InsertVertexBuffer(CKDWORD Handle,
                              const CKFFVertexBufferData &Buffer);
    CKBOOL InsertIndexBuffer(CKDWORD Handle,
                             const CKFFIndexBufferData &Buffer);

    CKBOOL IsAlive(CKDWORD Handle, CKRST_OBJECTTYPE Type) const;
    CKBOOL GetTextureDesc(CKDWORD Handle, CKTextureDesc *Desc) const;
    CKBOOL GetVertexBufferDesc(CKDWORD Handle,
                               CKVertexBufferDesc *Desc) const;
    CKBOOL GetIndexBufferDesc(CKDWORD Handle,
                              CKIndexBufferDesc *Desc) const;
    int GetLiveCount(CKRST_OBJECTMASK TypeMask) const;
    void CollectHandles(CKRST_OBJECTTYPE Type,
                        XArray<CKDWORD> &Handles) const;
    void CollectHandles(CKRST_OBJECTMASK TypeMask,
                        XClassArray<CKFFResourceHandle> &Handles) const;

    void Remove(CKDWORD Handle, CKRST_OBJECTTYPE Type);
    void Clear();

private:
    typedef XSHashTable<CKTextureDesc, CKDWORD> TextureTable;
    typedef XSHashTable<CKFFVertexBufferData, CKDWORD> VertexBufferTable;
    typedef XSHashTable<CKFFIndexBufferData, CKDWORD> IndexBufferTable;

    TextureTable m_Textures;
    VertexBufferTable m_VertexBuffers;
    IndexBufferTable m_IndexBuffers;
};

#endif
