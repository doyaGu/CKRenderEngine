#include "CKFFBufferData.h"

#include "CKTransientGeometry.h"
#include "CKVertexLayoutCache.h"

#include <string.h>

static CKFFBufferUpdateMode CKFFGetBufferUpdateMode(CKRST_LOCKFLAGS Flags)
{
    if (Flags == CKRST_LOCK_DISCARD)
        return CKFF_BUFFER_UPDATE_DISCARD;
    if (Flags == CKRST_LOCK_NOOVERWRITE)
        return CKFF_BUFFER_UPDATE_NOOVERWRITE;
    return CKFF_BUFFER_UPDATE_PRESERVE;
}

CKBufferUpdateMode CKFFToContextBufferUpdateMode(
    CKFFBufferUpdateMode Mode)
{
    if (Mode == CKFF_BUFFER_UPDATE_DISCARD)
        return CKRST_BUFFER_UPDATE_DISCARD;
    if (Mode == CKFF_BUFFER_UPDATE_NOOVERWRITE)
        return CKRST_BUFFER_UPDATE_NOOVERWRITE;
    return CKRST_BUFFER_UPDATE_PRESERVE;
}

static CKBOOL CKFFValidLockFlags(CKRST_LOCKFLAGS Flags)
{
    return Flags == CKRST_LOCK_DEFAULT || Flags == CKRST_LOCK_NOOVERWRITE ||
           Flags == CKRST_LOCK_DISCARD;
}

static void CKFFSetupVertexBufferDrawData(
    VxDrawPrimitiveData &Data, const CKRSTVertexLayout &Layout,
    CKDWORD VertexFormat, CKBYTE *Vertices, CKDWORD VertexStride,
    CKDWORD VertexCount)
{
    memset(&Data, 0, sizeof(Data));
    Data.VertexCount = (int)VertexCount;
    Data.Flags = VertexFormat;
    Data.PositionPtr = Vertices + Layout.PositionOffset;
    Data.PositionStride = VertexStride;
    if (Layout.NormalOffset >= 0) {
        Data.NormalPtr = Vertices + Layout.NormalOffset;
        Data.NormalStride = VertexStride;
    }
    if (Layout.DiffuseOffset >= 0) {
        Data.ColorPtr = Vertices + Layout.DiffuseOffset;
        Data.ColorStride = VertexStride;
    }
    if (Layout.SpecularOffset >= 0) {
        Data.SpecularColorPtr = Vertices + Layout.SpecularOffset;
        Data.SpecularColorStride = VertexStride;
    }
    if (Layout.TexcoordCount > 0) {
        Data.TexCoordPtr = Vertices + Layout.TexcoordOffset[0];
        Data.TexCoordStride = VertexStride;
    }
    for (int stage = 1; stage < Layout.TexcoordCount; ++stage) {
        Data.TexCoordPtrs[stage - 1] = Vertices + Layout.TexcoordOffset[stage];
        Data.TexCoordStrides[stage - 1] = VertexStride;
    }
    if (Layout.TweenPositionOffset >= 0) {
        Data.TweenPositionPtr = Vertices + Layout.TweenPositionOffset;
        Data.TweenPositionStride = VertexStride;
    }
    if (Layout.TweenNormalOffset >= 0) {
        Data.TweenNormalPtr = Vertices + Layout.TweenNormalOffset;
        Data.TweenNormalStride = VertexStride;
    }
}

static void CKFFInterleaveVertexBuffer(const CKFFVertexBufferData &Buffer,
                                       CKBYTE *Source, CKDWORD Count,
                                       CKBYTE *Destination)
{
    VxDrawPrimitiveData data;
    CKFFSetupVertexBufferDrawData(
        data, Buffer.Layout, Buffer.Desc.m_VertexFormat, Source,
        Buffer.Desc.m_VertexSize, Count);
    CKBYTE dimensions[CKRST_MAX_TEXTURE_STAGES];
    for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) {
        dimensions[stage] = (CKBYTE)(stage < Buffer.Layout.TexcoordCount
                                         ? Buffer.Layout.TexcoordDims[stage]
                                         : 2);
    }
    CKTransientGeometry::InterleaveVertices(
        Destination, Buffer.NativeStride, Count, Buffer.FormatFlags,
        &data, dimensions);
}

CKFFVertexBufferData::CKFFVertexBufferData()
    : FormatFlags(0), NativeStride(0), Locked(FALSE),
      LockFlags(CKRST_LOCK_DEFAULT), LockStart(0), LockCount(0)
{
    memset(&Layout, 0, sizeof(Layout));
}

CKERROR CKFFVertexBufferData::Initialize(const CKVertexBufferDesc &Source,
                                         const void *Data,
                                         XArray<CKBYTE> &InitialData)
{
    if (Source.m_MaxVertexCount == 0)
        return CKERR_INVALIDPARAMETER;

    Desc = Source;
    const CKDWORD canonicalStride = CKRSTGetVertexLayout(
        Source.m_VertexFormat, Source.m_TexcoordDims, &Layout);
    if (canonicalStride == 0 ||
        (Source.m_VertexSize != 0 && Source.m_VertexSize != canonicalStride))
        return CKERR_INVALIDPARAMETER;
    Desc.m_VertexSize = canonicalStride;

    const bool hasNormal = Layout.NormalOffset >= 0;
    const bool hasUV = Layout.TexcoordCount > 0;
    FormatFlags = CKFFVertexLayout::DPFlagsToFormatFlags(
        Source.m_VertexFormat, hasNormal, hasUV);
    if (Layout.TweenPositionOffset >= 0) {
        FormatFlags |= CKFF_VF_TWEENPOSITION;
        if (Layout.TweenNormalOffset >= 0)
            FormatFlags |= CKFF_VF_TWEENNORMAL;
    }
    NativeStride = CKFFVertexLayout::ComputeStride(FormatFlags);
    if (NativeStride == 0 ||
        Source.m_MaxVertexCount > 0x7FFFFFFFu / canonicalStride ||
        Source.m_MaxVertexCount > 0x7FFFFFFFu / NativeStride)
        return CKERR_INVALIDPARAMETER;

    InitialData.Clear();
    if (!Data)
        return CK_OK;

    const int canonicalBytes =
        (int)(Source.m_MaxVertexCount * canonicalStride);
    CKBYTE *vertices = (CKBYTE *)Data;
    if ((Source.m_Flags & CKRST_VB_WRITEONLY) == 0) {
        LockData.Resize(canonicalBytes);
        memcpy(LockData.Begin(), Data, canonicalBytes);
        vertices = LockData.Begin();
    }
    InitialData.Resize((int)(Source.m_MaxVertexCount * NativeStride));
    CKFFInterleaveVertexBuffer(*this, vertices, Source.m_MaxVertexCount,
                               InitialData.Begin());
    return CK_OK;
}

void *CKFFVertexBufferData::Lock(CKDWORD StartVertex, CKDWORD VertexCount,
                                 CKRST_LOCKFLAGS Flags, CKERROR &Error)
{
    Error = CKERR_INVALIDPARAMETER;
    const CKDWORD maxCount = Desc.m_MaxVertexCount;
    if (VertexCount == 0)
        VertexCount = StartVertex < maxCount ? maxCount - StartVertex : 0;
    if (!CKFFValidLockFlags(Flags) || Locked || StartVertex >= maxCount ||
        VertexCount == 0 || VertexCount > maxCount - StartVertex)
        return NULL;

    const CKDWORD stride = Desc.m_VertexSize;
    const CKBOOL writeOnly = (Desc.m_Flags & CKRST_VB_WRITEONLY) != 0;
    const int requiredBytes =
        (int)((writeOnly ? VertexCount : maxCount) * stride);
    const int oldBytes = LockData.Size();
    if (oldBytes < requiredBytes) {
        LockData.Resize(requiredBytes);
        if (!writeOnly)
            memset(LockData.Begin() + oldBytes, 0, requiredBytes - oldBytes);
    } else {
        LockData.Resize(requiredBytes);
    }
    Locked = TRUE;
    LockFlags = Flags;
    LockStart = StartVertex;
    LockCount = VertexCount;
    Error = CK_OK;
    return LockData.Begin() + (writeOnly ? 0 : StartVertex * stride);
}

CKERROR CKFFVertexBufferData::PrepareUnlock(CKFFBufferUpload &Upload)
{
    if (!Locked)
        return CKERR_INVALIDPARAMETER;
    Locked = FALSE;

    const CKDWORD stride = Desc.m_VertexSize;
    const CKBOOL writeOnly = (Desc.m_Flags & CKRST_VB_WRITEONLY) != 0;
    CKBYTE *source = LockData.Begin() + (writeOnly ? 0 : LockStart * stride);
    const CKDWORD nativeBytes = LockCount * NativeStride;
    ConvertedVertices.Resize((int)nativeBytes);
    CKFFInterleaveVertexBuffer(*this, source, LockCount,
                               ConvertedVertices.Begin());

    Upload.Mode = CKFFGetBufferUpdateMode(LockFlags);
    Upload.Offset = LockStart * NativeStride;
    Upload.Size = nativeBytes;
    Upload.Data = ConvertedVertices.Begin();
    return CK_OK;
}

CKFFIndexBufferData::CKFFIndexBufferData()
    : Locked(FALSE), LockFlags(CKRST_LOCK_DEFAULT), LockStart(0),
      LockCount(0)
{
}

CKERROR CKFFIndexBufferData::Initialize(const CKIndexBufferDesc &Source,
                                        const void *Data)
{
    if (Source.m_MaxIndexCount == 0 ||
        Source.m_MaxIndexCount > 0x3FFFFFFFu)
        return CKERR_INVALIDPARAMETER;
    Desc = Source;
    if (Data && (Source.m_Flags & CKRST_VB_WRITEONLY) == 0) {
        LockData.Resize((int)(Source.m_MaxIndexCount * 2));
        memcpy(LockData.Begin(), Data, Source.m_MaxIndexCount * 2);
    }
    return CK_OK;
}

void *CKFFIndexBufferData::Lock(CKDWORD StartIndex, CKDWORD IndexCount,
                                CKRST_LOCKFLAGS Flags, CKERROR &Error)
{
    Error = CKERR_INVALIDPARAMETER;
    const CKDWORD maxCount = Desc.m_MaxIndexCount;
    if (IndexCount == 0)
        IndexCount = StartIndex < maxCount ? maxCount - StartIndex : 0;
    if (!CKFFValidLockFlags(Flags) || Locked || StartIndex >= maxCount ||
        IndexCount == 0 || IndexCount > maxCount - StartIndex)
        return NULL;

    const CKBOOL writeOnly = (Desc.m_Flags & CKRST_VB_WRITEONLY) != 0;
    const int requiredBytes =
        (int)(2 * (writeOnly ? IndexCount : maxCount));
    const int oldBytes = LockData.Size();
    if (oldBytes < requiredBytes) {
        LockData.Resize(requiredBytes);
        if (!writeOnly)
            memset(LockData.Begin() + oldBytes, 0, requiredBytes - oldBytes);
    } else {
        LockData.Resize(requiredBytes);
    }
    Locked = TRUE;
    LockFlags = Flags;
    LockStart = StartIndex;
    LockCount = IndexCount;
    Error = CK_OK;
    return LockData.Begin() + (writeOnly ? 0 : StartIndex * 2);
}

CKERROR CKFFIndexBufferData::PrepareUnlock(CKFFBufferUpload &Upload)
{
    if (!Locked)
        return CKERR_INVALIDPARAMETER;
    Locked = FALSE;
    const CKBOOL writeOnly = (Desc.m_Flags & CKRST_VB_WRITEONLY) != 0;
    Upload.Mode = CKFFGetBufferUpdateMode(LockFlags);
    Upload.Offset = LockStart * 2;
    Upload.Size = LockCount * 2;
    Upload.Data = LockData.Begin() + (writeOnly ? 0 : LockStart * 2);
    return CK_OK;
}
