#include "CKBgfxRasterizerContext.h"

// CKBgfxRasterizerContext resource ownership and buffer translation.

#include "CKVertexLayoutCache.h"
#include "CKDebugLogger.h"

#include <string.h>

// ===========================================================================
// Resources
// ===========================================================================

CKDWORD CKBgfxRasterizerContext::GetNativeVertexLayout(CKDWORD FormatFlags)
{
    const CKDWORD *cached = m_NativeVertexLayouts.FindPtr(FormatFlags);
    if (cached)
        return *cached;

    CKVertexElementDesc elements[20];
    CKVertexLayoutDesc desc;
    if (!CKFFVertexLayout::BuildLayout(
            FormatFlags, elements,
            (CKDWORD)(sizeof(elements) / sizeof(elements[0])), desc))
        return 0;

    CKDWORD handle = 0;
    if (CreateVertexLayout(&desc, &handle) != CK_OK || !handle)
        return 0;
    if (!m_NativeVertexLayouts.Insert(FormatFlags, handle, FALSE)) {
        DestroyObject(handle, CKRST_OBJ_VERTEXLAYOUT);
        return 0;
    }
    return handle;
}
void CKBgfxRasterizerContext::ClearNativeVertexLayouts()
{
    for (XSHashTable<CKDWORD, CKDWORD>::Iterator it =
             m_NativeVertexLayouts.Begin();
         it != m_NativeVertexLayouts.End(); ++it)
        DestroyObject(*it, CKRST_OBJ_VERTEXLAYOUT);
    m_NativeVertexLayouts.Clear();
}

CKBOOL CKBgfxRasterizerContext::GetVertexBufferDesc(CKDWORD VB, CKVertexBufferDesc *Desc) const
{
    return m_PublicResources.GetVertexBufferDesc(VB, Desc);
}

CKBOOL CKBgfxRasterizerContext::GetIndexBufferDesc(CKDWORD IB, CKIndexBufferDesc *Desc) const
{
    return m_PublicResources.GetIndexBufferDesc(IB, Desc);
}

int CKBgfxRasterizerContext::GetLiveResourceCountForTests(CKDWORD TypeMask) const
{
    return m_PublicResources.GetLiveCount((CKRST_OBJECTMASK) TypeMask);
}

CKBOOL CKBgfxRasterizerContext::CreateTexture(const CKTextureDesc *Desc, CKDWORD *OutHandle)
{
    if (OutHandle)
        *OutHandle = 0;
    if (!m_Created || m_ShuttingDown || !Desc || !OutHandle)
        return FALSE;
    if (Desc->Format.Width <= 0 || Desc->Format.Height <= 0 ||
        ((Desc->Flags & CKRST_TEXTURE_CUBEMAP) && Desc->Format.Width != Desc->Format.Height)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    // CKRST_MIPMAP_GENERATE ((CKDWORD)-1) is passed through: the concrete context treats
    // it as an auto-mip request and builds the chain from level 0. 0 and 1
    // Both values mean that only the base level exists.
    CKTextureDesc deviceDesc = *Desc;
    deviceDesc.Flags |= CKRST_TEXTURE_VALID;
    if (deviceDesc.Depth == 0)
        deviceDesc.Depth = 1;
    if (deviceDesc.MipMapCount == 0)
        deviceDesc.MipMapCount = 1;

    CKDWORD handle = 0;
    if (CreateTexture(&deviceDesc, NULL, &handle) != CK_OK || handle == 0) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (!m_PublicResources.InsertTexture(handle, deviceDesc)) {
        DestroyObject(handle, CKRST_OBJ_TEXTURE);
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    *OutHandle = handle;
    return TRUE;
}

CKBOOL CKBgfxRasterizerContext::LoadTexture(CKDWORD Texture, const VxImageDescEx &Image, int MipLevel,
                                        CKRST_CUBEFACE Face, const CKRECT *Region)
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    const CKTextureDesc *texture = m_PublicResources.FindTexture(Texture);
    if (!texture) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    const CKBOOL cube = (texture->Flags & CKRST_TEXTURE_CUBEMAP) != 0;
    if (!Image.Image || Image.Width <= 0 || Image.Height <= 0 || MipLevel < 0 || MipLevel >= 32) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    const CKBOOL volume = (texture->Flags & CKRST_TEXTURE_VOLUMEMAP) != 0;
    const CKDWORD layers = cube ? CKRST_CUBEFACE_COUNT :
        (volume ? XMax((CKDWORD)1, texture->Depth >> MipLevel) : 1);
    if ((CKDWORD)Face >= layers) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    const CKDWORD levels = texture->MipMapCount == CKRST_MIPMAP_GENERATE ? 1
                           : (texture->MipMapCount == 0 ? 1 : texture->MipMapCount);
    if ((CKDWORD)MipLevel >= levels) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (Region) {
        const CKDWORD levelWidth = XMax((CKDWORD)1, (CKDWORD)texture->Format.Width >> MipLevel);
        const CKDWORD levelHeight = XMax((CKDWORD)1, (CKDWORD)texture->Format.Height >> MipLevel);
        if (!ValidateRect(Region, levelWidth, levelHeight)) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
    }
    if (UpdateTexture(Texture, (CKDWORD)MipLevel, (CKDWORD)Face, Region, &Image) != CK_OK)
        return FALSE;
    ++m_FrameTextureUploads;
    return TRUE;
}

CKBOOL CKBgfxRasterizerContext::GetTextureDesc(CKDWORD Texture, CKTextureDesc *Desc) const
{
    return m_PublicResources.GetTextureDesc(Texture, Desc);
}

CKBOOL CKBgfxRasterizerContext::CreateVertexBuffer(const CKVertexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle)
{
    if (OutHandle)
        *OutHandle = 0;
    if (!m_Created || m_ShuttingDown || !Desc || !OutHandle)
        return FALSE;
    VertexBufferData buffer;
    XArray<CKBYTE> converted;
    if (buffer.Initialize(*Desc, Data, converted) != CK_OK) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    const CKDWORD deviceLayout = GetNativeVertexLayout(buffer.FormatFlags);
    if (deviceLayout == 0) {
        if (!m_LayoutMismatchLogged) {
            m_LayoutMismatchLogged = TRUE;
            CK_LOG_FMT("Rasterizer", "no device vertex layout for vertex format 0x%08X (ffp 0x%08X)",
                       Desc->m_VertexFormat, buffer.FormatFlags);
        }
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }

    CKBufferDesc deviceDesc;
    deviceDesc.Kind = CKRST_BUFFER_VERTEX;
    deviceDesc.Size = Desc->m_MaxVertexCount * buffer.NativeStride;
    deviceDesc.Stride = buffer.NativeStride;
    deviceDesc.Layout = deviceLayout;
    deviceDesc.Dynamic = (Desc->m_Flags & CKRST_VB_DYNAMIC) != 0 ? TRUE : FALSE;

    CKDWORD handle = 0;
    deviceDesc.InitialData = converted.IsEmpty() ? NULL : converted.Begin();
    if (CreateBuffer(&deviceDesc, &handle) != CK_OK || handle == 0) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (!m_PublicResources.InsertVertexBuffer(handle, buffer)) {
        DestroyObject(handle, CKRST_OBJ_VERTEXBUFFER);
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (Data)
        ++m_FrameBufferUploads;
    *OutHandle = handle;
    return TRUE;
}

CKBOOL CKBgfxRasterizerContext::CreateIndexBuffer(const CKIndexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle)
{
    if (OutHandle)
        *OutHandle = 0;
    if (!m_Created || m_ShuttingDown || !Desc || !OutHandle)
        return FALSE;
    IndexBufferData buffer;
    if (buffer.Initialize(*Desc, Data) != CK_OK) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    CKBufferDesc deviceDesc;
    deviceDesc.Kind = CKRST_BUFFER_INDEX;
    deviceDesc.Size = Desc->m_MaxIndexCount * 2;
    deviceDesc.Index32 = FALSE;
    deviceDesc.Dynamic = (Desc->m_Flags & CKRST_VB_DYNAMIC) != 0 ? TRUE : FALSE;
    deviceDesc.InitialData = Data;
    CKDWORD handle = 0;
    if (CreateBuffer(&deviceDesc, &handle) != CK_OK || handle == 0) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (Data) {
        ++m_FrameBufferUploads;
    }
    if (!m_PublicResources.InsertIndexBuffer(handle, buffer)) {
        DestroyObject(handle, CKRST_OBJ_INDEXBUFFER);
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    *OutHandle = handle;
    return TRUE;
}

void *CKBgfxRasterizerContext::LockVertexBuffer(CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKRST_LOCKFLAGS Flags)
{
    VertexBufferData *buffer = m_PublicResources.FindVertexBuffer(VB);
    if (!buffer) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return NULL;
    }
    CKERROR error = CK_OK;
    void *data = buffer->Lock(StartVertex, VertexCount, Flags, error);
    if (error != CK_OK) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return NULL;
    }
    return data;
}

CKBOOL CKBgfxRasterizerContext::UnlockVertexBuffer(CKDWORD VB)
{
    VertexBufferData *buffer = m_PublicResources.FindVertexBuffer(VB);
    if (!buffer) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    CKFFBufferUpload source;
    if (buffer->PrepareUnlock(source) != CK_OK) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    CKBufferUpdateDesc update;
    if (m_BufferUses.PrepareUpdate(
            GetCompletedSubmitId(), CKRST_BUFFER_VERTEX, VB,
            buffer->Desc.m_MaxVertexCount * buffer->NativeStride,
            source, update) != CK_OK) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (UpdateBuffer(&update) != CK_OK)
        return FALSE;
    m_BufferUses.CommitUpdate(update);
    ++m_FrameBufferUploads;
    return TRUE;
}

void *CKBgfxRasterizerContext::LockIndexBuffer(CKDWORD IB, CKDWORD StartIndex, CKDWORD IndexCount, CKRST_LOCKFLAGS Flags)
{
    IndexBufferData *buffer = m_PublicResources.FindIndexBuffer(IB);
    if (!buffer) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return NULL;
    }
    CKERROR error = CK_OK;
    void *data = buffer->Lock(StartIndex, IndexCount, Flags, error);
    if (error != CK_OK) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return NULL;
    }
    return data;
}

CKBOOL CKBgfxRasterizerContext::UnlockIndexBuffer(CKDWORD IB)
{
    IndexBufferData *buffer = m_PublicResources.FindIndexBuffer(IB);
    if (!buffer) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    CKFFBufferUpload source;
    if (buffer->PrepareUnlock(source) != CK_OK) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    CKBufferUpdateDesc update;
    if (m_BufferUses.PrepareUpdate(
            GetCompletedSubmitId(), CKRST_BUFFER_INDEX, IB,
            buffer->Desc.m_MaxIndexCount * 2, source, update) != CK_OK) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (UpdateBuffer(&update) != CK_OK)
        return FALSE;
    m_BufferUses.CommitUpdate(update);
    ++m_FrameBufferUploads;
    return TRUE;
}

CKBOOL CKBgfxRasterizerContext::DeleteObject(CKRST_HANDLE Handle, CKRST_OBJECTTYPE Type)
{
    if (!CKFFIsPublicResourceType(Type)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (!m_PublicResources.IsAlive(Handle, Type)) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    const CKBOOL wasTarget =
        Type == CKRST_OBJ_TEXTURE && m_Target.Texture == Handle;
    if (wasTarget && !ReleaseTarget())
        return FALSE;
    if (DestroyObject(Handle, Type) != CK_OK)
        return FALSE;
    CKFFDetachDeletedResource(m_FFP, m_BufferUses, Handle, Type);
    m_PublicResources.Remove(Handle, Type);
    return TRUE;
}

CKBOOL CKBgfxRasterizerContext::FlushObjects(CKRST_OBJECTMASK TypeMask)
{
    if (!CKFFIsPublicResourceMask(TypeMask)) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    XClassArray<CKFFResourceHandle> handles;
    CKBOOL success = TRUE;
    m_PublicResources.CollectHandles(TypeMask, handles);
    for (int i = 0; i < handles.Size(); ++i)
        if (!DeleteObject(handles[i].Handle, handles[i].Type))
            success = FALSE;
    return success;
}

CKBOOL CKBgfxRasterizerContext::SetResourceName(
    CKRST_HANDLE Handle, CKRST_OBJECTTYPE Type, CKSTRING Name)
{
    if (!Name || !m_PublicResources.IsAlive(Handle, Type))
        return FALSE;
    return SetObjectName(Handle, Type, Name) == CK_OK ? TRUE : FALSE;
}

