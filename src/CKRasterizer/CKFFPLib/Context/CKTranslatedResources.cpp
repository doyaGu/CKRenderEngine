// CKTranslatedContext resource ownership and buffer translation.

#include "CKFFRasterizerContextInternal.h"
#include "CKTransientGeometry.h"
#include "CKVertexLayoutCache.h"
#include "CKDebugLogger.h"

#include <string.h>

// ===========================================================================
// Resources
// ===========================================================================

CKTranslatedContext::Resource *CKTranslatedContext::FindResource(CKDWORD Type, CKDWORD Handle)
{
    if (Handle == 0)
        return NULL;
    std::unordered_map<uint64_t, Resource>::iterator it = m_Resources.find(ResourceKey(Type, Handle));
    return it == m_Resources.end() ? NULL : &it->second;
}

const CKTranslatedContext::Resource *CKTranslatedContext::FindResource(CKDWORD Type, CKDWORD Handle) const
{
    if (Handle == 0)
        return NULL;
    std::unordered_map<uint64_t, Resource>::const_iterator it = m_Resources.find(ResourceKey(Type, Handle));
    return it == m_Resources.end() ? NULL : &it->second;
}

CKBOOL CKTranslatedContext::GetVertexBufferDescForTests(CKDWORD VB, CKVertexBufferDesc *Desc) const
{
    const Resource *resource = FindResource(CKRST_OBJ_VERTEXBUFFER, VB);
    if (!resource || !Desc)
        return FALSE;
    *Desc = resource->VertexBuffer;
    return TRUE;
}

int CKTranslatedContext::GetLiveResourceCountForTests(CKDWORD TypeMask) const
{
    int count = 0;
    for (std::unordered_map<uint64_t, Resource>::const_iterator it = m_Resources.begin(); it != m_Resources.end(); ++it) {
        if (it->second.Type & TypeMask)
            ++count;
    }
    return count;
}

CKBOOL CKTranslatedContext::CreateTexture(const CKTextureDesc *Desc, CKDWORD *OutHandle)
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
    // CKRST_MIPMAP_GENERATE ((CKDWORD)-1) is passed through: the backend treats
    // it as an auto-mip request and builds the chain from level 0. 0 and 1
    // both mean "no mip levels" (spec 4.5).
    CKTextureDesc deviceDesc = *Desc;
    deviceDesc.Flags |= CKRST_TEXTURE_VALID;
    if (deviceDesc.Depth == 0)
        deviceDesc.Depth = 1;
    if (deviceDesc.MipMapCount == 0)
        deviceDesc.MipMapCount = 1;

    CKDWORD handle = 0;
    if (m_Backend->CreateTexture(&deviceDesc, NULL, &handle) != CK_OK || handle == 0) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    Resource &resource = m_Resources[ResourceKey(CKRST_OBJ_TEXTURE, handle)];
    resource = Resource();
    resource.Type = CKRST_OBJ_TEXTURE;
    resource.Handle = handle;
    resource.Texture = deviceDesc;
    *OutHandle = handle;
    return TRUE;
}

CKBOOL CKTranslatedContext::LoadTexture(CKDWORD Texture, const VxImageDescEx &Image, int MipLevel,
                                        CKRST_CUBEFACE Face, const CKRECT *Region)
{
    if (!m_Created || m_ShuttingDown)
        return FALSE;
    const Resource *resource = FindResource(CKRST_OBJ_TEXTURE, Texture);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    const CKBOOL cube = (resource->Texture.Flags & CKRST_TEXTURE_CUBEMAP) != 0;
    if (!Image.Image || Image.Width <= 0 || Image.Height <= 0 || MipLevel < 0 || MipLevel >= 32) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    const CKBOOL volume = (resource->Texture.Flags & CKRST_TEXTURE_VOLUMEMAP) != 0;
    const CKDWORD layers = cube ? CKRST_CUBEFACE_COUNT :
        (volume ? XMax((CKDWORD)1, resource->Texture.Depth >> MipLevel) : 1);
    if ((CKDWORD)Face >= layers) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    const CKDWORD levels = resource->Texture.MipMapCount == CKRST_MIPMAP_GENERATE ? 1
                           : (resource->Texture.MipMapCount == 0 ? 1 : resource->Texture.MipMapCount);
    if ((CKDWORD)MipLevel >= levels) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    if (Region) {
        const CKDWORD levelWidth = XMax((CKDWORD)1, (CKDWORD)resource->Texture.Format.Width >> MipLevel);
        const CKDWORD levelHeight = XMax((CKDWORD)1, (CKDWORD)resource->Texture.Format.Height >> MipLevel);
        if (!ValidateRect(Region, levelWidth, levelHeight)) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
    }
    if (m_Backend->UpdateTexture(Texture, (CKDWORD)MipLevel, (CKDWORD)Face, Region, &Image) != CK_OK)
        return FALSE;
    ++m_FrameTextureUploads;
    return TRUE;
}

CKBOOL CKTranslatedContext::GetTextureDesc(CKDWORD Texture, CKTextureDesc *Desc) const
{
    if (!Desc)
        return FALSE;
    const Resource *resource = FindResource(CKRST_OBJ_TEXTURE, Texture);
    if (!resource)
        return FALSE;
    *Desc = resource->Texture;
    return TRUE;
}

CKBOOL CKTranslatedContext::CreateVertexBuffer(const CKVertexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle)
{
    if (OutHandle)
        *OutHandle = 0;
    if (!m_Created || m_ShuttingDown || !Desc || !OutHandle)
        return FALSE;
    if (Desc->m_MaxVertexCount == 0) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }

    Resource resource;
    resource.Type = CKRST_OBJ_VERTEXBUFFER;
    resource.VertexBuffer = *Desc;
    const CKDWORD canonicalStride = CKRSTGetVertexLayout(Desc->m_VertexFormat, Desc->m_TexcoordDims, &resource.Layout);
    if (canonicalStride == 0 || (Desc->m_VertexSize != 0 && Desc->m_VertexSize != canonicalStride)) {
        // The engine writes Lock memory in the canonical layout (spec 4.5); a
        // different explicit vertex size cannot be honoured.
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    resource.VertexBuffer.m_VertexSize = canonicalStride;

    // The backend keeps the fixed-function pipeline's own interleaved layout;
    // Unlock converts from the canonical layout the engine writes.
    const bool hasNormal = resource.Layout.NormalOffset >= 0;
    const bool hasUV = resource.Layout.TexcoordCount > 0;
    CKDWORD formatFlags = CKVertexLayoutCache::DPFlagsToFormatFlags(Desc->m_VertexFormat, hasNormal, hasUV);
    if (resource.Layout.TweenPositionOffset >= 0) {
        formatFlags |= CKFF_VF_TWEENPOSITION;
        if (resource.Layout.TweenNormalOffset >= 0)
            formatFlags |= CKFF_VF_TWEENNORMAL;
    }
    resource.FormatFlags = formatFlags;
    resource.DeviceStride = CKVertexLayoutCache::ComputeStride(formatFlags);
    resource.DeviceLayout = m_FFP.ResolveVertexLayout(formatFlags);
    if (resource.DeviceLayout == 0 || resource.DeviceStride == 0) {
        if (!m_LayoutMismatchLogged) {
            m_LayoutMismatchLogged = TRUE;
            CK_LOG_FMT("Rasterizer", "no device vertex layout for vertex format 0x%08X (ffp 0x%08X)",
                       Desc->m_VertexFormat, formatFlags);
        }
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }

    CKBackendBufferDesc deviceDesc;
    deviceDesc.Kind = CKRST_BACKEND_BUFFER_VERTEX;
    deviceDesc.Size = Desc->m_MaxVertexCount * resource.DeviceStride;
    deviceDesc.Stride = resource.DeviceStride;
    deviceDesc.Layout = resource.DeviceLayout;
    deviceDesc.Dynamic = (Desc->m_Flags & CKRST_VB_DYNAMIC) != 0 ? TRUE : FALSE;
    std::vector<CKBYTE> converted;
    const void *deviceData = NULL;
    if (Data) {
        resource.Shadow.resize((size_t)Desc->m_MaxVertexCount * canonicalStride);
        memcpy(resource.Shadow.data(), Data, (size_t)Desc->m_MaxVertexCount * canonicalStride);
        resource.LockStart = 0;
        resource.LockCount = Desc->m_MaxVertexCount;
        resource.Locked = TRUE;
        // Reuse the Unlock conversion path on the whole buffer.
        converted.resize((size_t)Desc->m_MaxVertexCount * resource.DeviceStride);
        VxDrawPrimitiveData dp;
        memset(&dp, 0, sizeof(dp));
        const CKRSTVertexLayout &l = resource.Layout;
        const CKBYTE *base = resource.Shadow.data();
        dp.VertexCount = (int)Desc->m_MaxVertexCount;
        dp.Flags = Desc->m_VertexFormat;
        dp.PositionPtr = (void *)(base + l.PositionOffset);
        dp.PositionStride = canonicalStride;
        if (l.NormalOffset >= 0) { dp.NormalPtr = (void *)(base + l.NormalOffset); dp.NormalStride = canonicalStride; }
        if (l.DiffuseOffset >= 0) { dp.ColorPtr = (void *)(base + l.DiffuseOffset); dp.ColorStride = canonicalStride; }
        if (l.SpecularOffset >= 0) { dp.SpecularColorPtr = (void *)(base + l.SpecularOffset); dp.SpecularColorStride = canonicalStride; }
        if (l.TexcoordCount > 0) { dp.TexCoordPtr = (void *)(base + l.TexcoordOffset[0]); dp.TexCoordStride = canonicalStride; }
        for (int i = 1; i < l.TexcoordCount; ++i) {
            dp.TexCoordPtrs[i - 1] = (void *)(base + l.TexcoordOffset[i]);
            dp.TexCoordStrides[i - 1] = canonicalStride;
        }
        if (l.TweenPositionOffset >= 0) { dp.TweenPositionPtr = (void *)(base + l.TweenPositionOffset); dp.TweenPositionStride = canonicalStride; }
        if (l.TweenNormalOffset >= 0) { dp.TweenNormalPtr = (void *)(base + l.TweenNormalOffset); dp.TweenNormalStride = canonicalStride; }
        CKBYTE dims[CKRST_MAX_TEXTURE_STAGES];
        for (int i = 0; i < CKRST_MAX_TEXTURE_STAGES; ++i)
            dims[i] = (CKBYTE)(i < l.TexcoordCount ? l.TexcoordDims[i] : 2);
        CKTransientGeometry::InterleaveVertices(converted.data(), resource.DeviceStride, Desc->m_MaxVertexCount,
                                                formatFlags, &dp, dims);
        resource.Locked = FALSE;
        resource.LockCount = 0;
        deviceData = converted.data();
    }

    CKDWORD handle = 0;
    deviceDesc.InitialData = deviceData;
    if (m_Backend->CreateBuffer(&deviceDesc, &handle) != CK_OK || handle == 0) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    resource.Handle = handle;
    m_Resources[ResourceKey(CKRST_OBJ_VERTEXBUFFER, handle)] = resource;
    if (Data)
        ++m_FrameBufferUploads;
    *OutHandle = handle;
    return TRUE;
}

CKBOOL CKTranslatedContext::CreateIndexBuffer(const CKIndexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle)
{
    if (OutHandle)
        *OutHandle = 0;
    if (!m_Created || m_ShuttingDown || !Desc || !OutHandle)
        return FALSE;
    if (Desc->m_MaxIndexCount == 0) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    CKBackendBufferDesc deviceDesc;
    deviceDesc.Kind = CKRST_BACKEND_BUFFER_INDEX;
    deviceDesc.Size = Desc->m_MaxIndexCount * 2;
    deviceDesc.Index32 = FALSE;
    deviceDesc.Dynamic = (Desc->m_Flags & CKRST_VB_DYNAMIC) != 0 ? TRUE : FALSE;
    deviceDesc.InitialData = Data;
    CKDWORD handle = 0;
    if (m_Backend->CreateBuffer(&deviceDesc, &handle) != CK_OK || handle == 0) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    Resource &resource = m_Resources[ResourceKey(CKRST_OBJ_INDEXBUFFER, handle)];
    resource = Resource();
    resource.Type = CKRST_OBJ_INDEXBUFFER;
    resource.Handle = handle;
    resource.IndexBuffer = *Desc;
    if (Data) {
        resource.Shadow.resize((size_t)Desc->m_MaxIndexCount * 2);
        memcpy(resource.Shadow.data(), Data, (size_t)Desc->m_MaxIndexCount * 2);
        ++m_FrameBufferUploads;
    }
    *OutHandle = handle;
    return TRUE;
}

void *CKTranslatedContext::LockVertexBuffer(CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKRST_LOCKFLAGS Flags)
{
    (void)Flags;
    Resource *resource = FindResource(CKRST_OBJ_VERTEXBUFFER, VB);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return NULL;
    }
    const CKDWORD maxCount = resource->VertexBuffer.m_MaxVertexCount;
    if (VertexCount == 0)
        VertexCount = StartVertex < maxCount ? maxCount - StartVertex : 0;
    if (resource->Locked || StartVertex >= maxCount || VertexCount == 0 || StartVertex + VertexCount > maxCount) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return NULL;
    }
    const CKDWORD stride = resource->VertexBuffer.m_VertexSize;
    if (resource->Shadow.size() < (size_t)maxCount * stride)
        resource->Shadow.resize((size_t)maxCount * stride, 0);
    resource->Locked = TRUE;
    resource->LockStart = StartVertex;
    resource->LockCount = VertexCount;
    return resource->Shadow.data() + (size_t)StartVertex * stride;
}

CKBOOL CKTranslatedContext::UnlockVertexBuffer(CKDWORD VB)
{
    Resource *resource = FindResource(CKRST_OBJ_VERTEXBUFFER, VB);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    if (!resource->Locked) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    resource->Locked = FALSE;
    const CKDWORD stride = resource->VertexBuffer.m_VertexSize;
    const CKDWORD start = resource->LockStart;
    const CKDWORD count = resource->LockCount;
    const CKRSTVertexLayout &l = resource->Layout;
    const CKBYTE *base = resource->Shadow.data() + (size_t)start * stride;

    VxDrawPrimitiveData dp;
    memset(&dp, 0, sizeof(dp));
    dp.VertexCount = (int)count;
    dp.Flags = resource->VertexBuffer.m_VertexFormat;
    dp.PositionPtr = (void *)(base + l.PositionOffset);
    dp.PositionStride = stride;
    if (l.NormalOffset >= 0) { dp.NormalPtr = (void *)(base + l.NormalOffset); dp.NormalStride = stride; }
    if (l.DiffuseOffset >= 0) { dp.ColorPtr = (void *)(base + l.DiffuseOffset); dp.ColorStride = stride; }
    if (l.SpecularOffset >= 0) { dp.SpecularColorPtr = (void *)(base + l.SpecularOffset); dp.SpecularColorStride = stride; }
    if (l.TexcoordCount > 0) { dp.TexCoordPtr = (void *)(base + l.TexcoordOffset[0]); dp.TexCoordStride = stride; }
    for (int i = 1; i < l.TexcoordCount; ++i) {
        dp.TexCoordPtrs[i - 1] = (void *)(base + l.TexcoordOffset[i]);
        dp.TexCoordStrides[i - 1] = stride;
    }
    if (l.TweenPositionOffset >= 0) { dp.TweenPositionPtr = (void *)(base + l.TweenPositionOffset); dp.TweenPositionStride = stride; }
    if (l.TweenNormalOffset >= 0) { dp.TweenNormalPtr = (void *)(base + l.TweenNormalOffset); dp.TweenNormalStride = stride; }
    CKBYTE dims[CKRST_MAX_TEXTURE_STAGES];
    for (int i = 0; i < CKRST_MAX_TEXTURE_STAGES; ++i)
        dims[i] = (CKBYTE)(i < l.TexcoordCount ? l.TexcoordDims[i] : 2);

    const size_t deviceBytes = (size_t)count * resource->DeviceStride;
    if (resource->Scratch.size() < deviceBytes)
        resource->Scratch.resize(deviceBytes);
    CKTransientGeometry::InterleaveVertices(resource->Scratch.data(), resource->DeviceStride, count,
                                            resource->FormatFlags, &dp, dims);
    if (m_Backend->UpdateBuffer(CKRST_BACKEND_BUFFER_VERTEX, VB,
                                start * resource->DeviceStride, (CKDWORD)deviceBytes,
                                resource->Scratch.data()) != CK_OK)
        return FALSE;
    ++m_FrameBufferUploads;
    return TRUE;
}

void *CKTranslatedContext::LockIndexBuffer(CKDWORD IB, CKDWORD StartIndex, CKDWORD IndexCount, CKRST_LOCKFLAGS Flags)
{
    (void)Flags;
    Resource *resource = FindResource(CKRST_OBJ_INDEXBUFFER, IB);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return NULL;
    }
    const CKDWORD maxCount = resource->IndexBuffer.m_MaxIndexCount;
    if (IndexCount == 0)
        IndexCount = StartIndex < maxCount ? maxCount - StartIndex : 0;
    if (resource->Locked || StartIndex >= maxCount || IndexCount == 0 || StartIndex + IndexCount > maxCount) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return NULL;
    }
    if (resource->Shadow.size() < (size_t)maxCount * 2)
        resource->Shadow.resize((size_t)maxCount * 2, 0);
    resource->Locked = TRUE;
    resource->LockStart = StartIndex;
    resource->LockCount = IndexCount;
    return resource->Shadow.data() + (size_t)StartIndex * 2;
}

CKBOOL CKTranslatedContext::UnlockIndexBuffer(CKDWORD IB)
{
    Resource *resource = FindResource(CKRST_OBJ_INDEXBUFFER, IB);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    if (!resource->Locked) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    resource->Locked = FALSE;
    if (m_Backend->UpdateBuffer(CKRST_BACKEND_BUFFER_INDEX, IB,
                                resource->LockStart * 2, resource->LockCount * 2,
                                resource->Shadow.data() + (size_t)resource->LockStart * 2) != CK_OK)
        return FALSE;
    ++m_FrameBufferUploads;
    return TRUE;
}

CKBOOL CKTranslatedContext::DeleteObject(CKDWORD Handle, CKDWORD Type)
{
    if (Type != CKRST_OBJ_TEXTURE && Type != CKRST_OBJ_VERTEXBUFFER && Type != CKRST_OBJ_INDEXBUFFER) {
        Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    Resource *resource = FindResource(Type, Handle);
    if (!resource) {
        Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    if (Type == CKRST_OBJ_TEXTURE) {
        if (m_Target == Handle)
            ReleaseTarget();
        for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) {
            if (m_FFP.GetTexture(stage) == Handle) {
                m_FFP.SetTexture(stage, 0, 0);
            }
        }
    }
    if (m_Backend)
        m_Backend->DestroyObject(Handle, Type);
    m_Resources.erase(ResourceKey(Type, Handle));
    return TRUE;
}

CKBOOL CKTranslatedContext::FlushObjects(CKDWORD TypeMask)
{
    std::vector<uint64_t> keys;
    for (std::unordered_map<uint64_t, Resource>::const_iterator it = m_Resources.begin(); it != m_Resources.end(); ++it) {
        if (it->second.Type & TypeMask)
            keys.push_back(it->first);
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        std::unordered_map<uint64_t, Resource>::iterator it = m_Resources.find(keys[i]);
        if (it == m_Resources.end())
            continue;
        DeleteObject(it->second.Handle, it->second.Type);
    }
    return TRUE;
}

void CKTranslatedContext::SetResourceName(CKDWORD Handle, CKDWORD Type, CKSTRING Name)
{
    if (m_Backend && FindResource(Type, Handle))
        m_Backend->SetObjectName(Handle, Type, Name);
}
