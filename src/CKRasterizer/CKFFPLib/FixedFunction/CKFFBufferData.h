#ifndef CKFFBUFFERDATA_H
#define CKFFBUFFERDATA_H

#include "CKRasterizer.h"
#include "CKRasterizerContextData.h"

enum CKFFBufferUpdateMode {
    CKFF_BUFFER_UPDATE_PRESERVE = 0,
    CKFF_BUFFER_UPDATE_DISCARD,
    CKFF_BUFFER_UPDATE_NOOVERWRITE
};

CKBufferUpdateMode CKFFToContextBufferUpdateMode(
    CKFFBufferUpdateMode Mode);

struct CKFFBufferUpload {
    CKFFBufferUpdateMode Mode;
    CKDWORD Offset;
    CKDWORD Size;
    const void *Data;

    CKFFBufferUpload()
        : Mode(CKFF_BUFFER_UPDATE_PRESERVE), Offset(0), Size(0),
          Data(NULL) {}
};

struct CKFFVertexBufferData {
    CKVertexBufferDesc Desc;
    CKRSTVertexLayout Layout;
    CKDWORD FormatFlags;
    CKDWORD NativeStride;
    XArray<CKBYTE> LockData;
    XArray<CKBYTE> ConvertedVertices;
    CKBOOL Locked;
    CKRST_LOCKFLAGS LockFlags;
    CKDWORD LockStart;
    CKDWORD LockCount;

    CKFFVertexBufferData();

    CKERROR Initialize(const CKVertexBufferDesc &Source,
                       const void *Data,
                       XArray<CKBYTE> &InitialData);
    void *Lock(CKDWORD StartVertex, CKDWORD VertexCount,
               CKRST_LOCKFLAGS Flags, CKERROR &Error);
    CKERROR PrepareUnlock(CKFFBufferUpload &Upload);
};

struct CKFFIndexBufferData {
    CKIndexBufferDesc Desc;
    XArray<CKBYTE> LockData;
    CKBOOL Locked;
    CKRST_LOCKFLAGS LockFlags;
    CKDWORD LockStart;
    CKDWORD LockCount;

    CKFFIndexBufferData();

    CKERROR Initialize(const CKIndexBufferDesc &Source, const void *Data);
    void *Lock(CKDWORD StartIndex, CKDWORD IndexCount,
               CKRST_LOCKFLAGS Flags, CKERROR &Error);
    CKERROR PrepareUnlock(CKFFBufferUpload &Upload);
};

#endif
