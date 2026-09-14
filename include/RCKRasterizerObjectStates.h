#ifndef RCKRASTERIZEROBJECTSTATES_H
#define RCKRASTERIZEROBJECTSTATES_H

#include "CKTypes.h"

class CKRasterizerContext;

struct RCKVertexBufferObjectState {
    CKRasterizerContext *Context;
    CKDWORD ObjectIndex;
    CKDWORD FormatFlags;
    CKDWORD ContentVersion;
    CKBOOL Valid;
};

struct RCKMeshBufferState {
    CKRasterizerContext *Context;
    CKDWORD VertexBufferReady;
    CKBOOL IndexBufferReady;
    CKDWORD VertexBuffer;
    CKDWORD IndexBuffer;
    CKDWORD IndexBufferIndexCount;
    CKDWORD VertexBufferDpFlags;
    CKDWORD VertexBufferVertexFormat;
    CKDWORD VertexBufferStride;
    CKDWORD VertexBufferVertexCount;
    CKBOOL VertexBufferWrapAware;
};

#endif // RCKRASTERIZEROBJECTSTATES_H
