#include "CKFFResourceStore.h"

CKTextureDesc *CKFFResourceStore::FindTexture(CKDWORD Handle)
{
    return Handle ? m_Textures.FindPtr(Handle) : NULL;
}

const CKTextureDesc *CKFFResourceStore::FindTexture(CKDWORD Handle) const
{
    return Handle ? m_Textures.FindPtr(Handle) : NULL;
}

CKFFVertexBufferData *CKFFResourceStore::FindVertexBuffer(CKDWORD Handle)
{
    return Handle ? m_VertexBuffers.FindPtr(Handle) : NULL;
}

const CKFFVertexBufferData *CKFFResourceStore::FindVertexBuffer(
    CKDWORD Handle) const
{
    return Handle ? m_VertexBuffers.FindPtr(Handle) : NULL;
}

CKFFIndexBufferData *CKFFResourceStore::FindIndexBuffer(CKDWORD Handle)
{
    return Handle ? m_IndexBuffers.FindPtr(Handle) : NULL;
}

const CKFFIndexBufferData *CKFFResourceStore::FindIndexBuffer(
    CKDWORD Handle) const
{
    return Handle ? m_IndexBuffers.FindPtr(Handle) : NULL;
}

CKBOOL CKFFResourceStore::InsertTexture(CKDWORD Handle,
                                        const CKTextureDesc &Desc)
{
    return m_Textures.Insert(Handle, Desc, FALSE);
}

CKBOOL CKFFResourceStore::InsertVertexBuffer(
    CKDWORD Handle, const CKFFVertexBufferData &Buffer)
{
    return m_VertexBuffers.Insert(Handle, Buffer, FALSE);
}

CKBOOL CKFFResourceStore::InsertIndexBuffer(
    CKDWORD Handle, const CKFFIndexBufferData &Buffer)
{
    return m_IndexBuffers.Insert(Handle, Buffer, FALSE);
}

CKBOOL CKFFResourceStore::IsAlive(CKDWORD Handle,
                                  CKRST_OBJECTTYPE Type) const
{
    switch (Type) {
    case CKRST_OBJ_TEXTURE:
        return FindTexture(Handle) != NULL;
    case CKRST_OBJ_VERTEXBUFFER:
        return FindVertexBuffer(Handle) != NULL;
    case CKRST_OBJ_INDEXBUFFER:
        return FindIndexBuffer(Handle) != NULL;
    default:
        return FALSE;
    }
}

CKBOOL CKFFResourceStore::GetTextureDesc(CKDWORD Handle,
                                         CKTextureDesc *Desc) const
{
    const CKTextureDesc *texture = FindTexture(Handle);
    if (!texture || !Desc)
        return FALSE;
    *Desc = *texture;
    return TRUE;
}

CKBOOL CKFFResourceStore::GetVertexBufferDesc(
    CKDWORD Handle, CKVertexBufferDesc *Desc) const
{
    const CKFFVertexBufferData *buffer = FindVertexBuffer(Handle);
    if (!buffer || !Desc)
        return FALSE;
    *Desc = buffer->Desc;
    return TRUE;
}

CKBOOL CKFFResourceStore::GetIndexBufferDesc(
    CKDWORD Handle, CKIndexBufferDesc *Desc) const
{
    const CKFFIndexBufferData *buffer = FindIndexBuffer(Handle);
    if (!buffer || !Desc)
        return FALSE;
    *Desc = buffer->Desc;
    return TRUE;
}

int CKFFResourceStore::GetLiveCount(CKRST_OBJECTMASK TypeMask) const
{
    int count = 0;
    if (TypeMask & CKRST_OBJ_TEXTURE)
        count += m_Textures.Size();
    if (TypeMask & CKRST_OBJ_VERTEXBUFFER)
        count += m_VertexBuffers.Size();
    if (TypeMask & CKRST_OBJ_INDEXBUFFER)
        count += m_IndexBuffers.Size();
    return count;
}

void CKFFResourceStore::CollectHandles(CKRST_OBJECTTYPE Type,
                                       XArray<CKDWORD> &Handles) const
{
    Handles.Resize(0);
    if (Type == CKRST_OBJ_TEXTURE) {
        for (TextureTable::ConstIterator it = m_Textures.Begin();
             it != m_Textures.End(); ++it)
            Handles.PushBack(it.GetKey());
    } else if (Type == CKRST_OBJ_VERTEXBUFFER) {
        for (VertexBufferTable::ConstIterator it = m_VertexBuffers.Begin();
             it != m_VertexBuffers.End(); ++it)
            Handles.PushBack(it.GetKey());
    } else if (Type == CKRST_OBJ_INDEXBUFFER) {
        for (IndexBufferTable::ConstIterator it = m_IndexBuffers.Begin();
             it != m_IndexBuffers.End(); ++it)
            Handles.PushBack(it.GetKey());
    }
}

void CKFFResourceStore::CollectHandles(
    CKRST_OBJECTMASK TypeMask,
    XClassArray<CKFFResourceHandle> &Handles) const
{
    Handles.Clear();
    if (TypeMask & CKRST_OBJ_TEXTURE) {
        for (TextureTable::ConstIterator it = m_Textures.Begin();
             it != m_Textures.End(); ++it)
            Handles.PushBack({it.GetKey(), CKRST_OBJ_TEXTURE});
    }
    if (TypeMask & CKRST_OBJ_VERTEXBUFFER) {
        for (VertexBufferTable::ConstIterator it = m_VertexBuffers.Begin();
             it != m_VertexBuffers.End(); ++it)
            Handles.PushBack({it.GetKey(), CKRST_OBJ_VERTEXBUFFER});
    }
    if (TypeMask & CKRST_OBJ_INDEXBUFFER) {
        for (IndexBufferTable::ConstIterator it = m_IndexBuffers.Begin();
             it != m_IndexBuffers.End(); ++it)
            Handles.PushBack({it.GetKey(), CKRST_OBJ_INDEXBUFFER});
    }
}

void CKFFResourceStore::Remove(CKDWORD Handle, CKRST_OBJECTTYPE Type)
{
    if (Type == CKRST_OBJ_TEXTURE) {
        m_Textures.Remove(Handle);
    } else if (Type == CKRST_OBJ_VERTEXBUFFER) {
        CKFFVertexBufferData *buffer = FindVertexBuffer(Handle);
        if (buffer) {
            buffer->LockData.Clear();
            buffer->ConvertedVertices.Clear();
        }
        m_VertexBuffers.Remove(Handle);
    } else if (Type == CKRST_OBJ_INDEXBUFFER) {
        CKFFIndexBufferData *buffer = FindIndexBuffer(Handle);
        if (buffer)
            buffer->LockData.Clear();
        m_IndexBuffers.Remove(Handle);
    }
}

void CKFFResourceStore::Clear()
{
    for (VertexBufferTable::Iterator it = m_VertexBuffers.Begin();
         it != m_VertexBuffers.End(); ++it) {
        (*it).LockData.Clear();
        (*it).ConvertedVertices.Clear();
    }
    for (IndexBufferTable::Iterator it = m_IndexBuffers.Begin();
         it != m_IndexBuffers.End(); ++it)
        (*it).LockData.Clear();
    m_Textures.Clear();
    m_VertexBuffers.Clear();
    m_IndexBuffers.Clear();
}
