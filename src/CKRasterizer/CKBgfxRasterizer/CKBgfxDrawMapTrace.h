#ifndef CKBGFX_DRAWMAP_TRACE_H
#define CKBGFX_DRAWMAP_TRACE_H

#include "CKRasterizer.h"

#define CKBGFX_DRAWMAP_SCHEMA 2

#define CKBGFX_DRAWMAP_TAG_SUBMIT_MAP  "SubmitMap"
#define CKBGFX_DRAWMAP_TAG_SUBMIT_MISS "SubmitMiss"
#define CKBGFX_DRAWMAP_TAG_PROGRAM_MAP "ProgramMap"
#define CKBGFX_DRAWMAP_TAG_TEXTURE_MAP "TextureMap"
#define CKBGFX_DRAWMAP_TAG_BUFFER_MAP  "BufferMap"
#define CKBGFX_DRAWMAP_TAG_STATE_MAP   "StateMap"

struct CKBgfxDrawMapSubmitTrace {
    CKDWORD Frame;
    CKDWORD Submit;
    CKDWORD View;
    CKDWORD ViewSubmit;
    CKSTRING Kind;
    CKSTRING ViewMode;
    CKDWORD OrderGeneration;
    CKDWORD OrderSequential;
    CKDWORD Depth;
    CKDWORD Program;
    CKDWORD BgfxProgram;
    CKDWORD ProgramHash;
    CKDWORD SpecHash;
    CKSTRING ShaderProfile;
    CKDWORD StateHash;
    CKDWORD BgfxStateLo;
    CKDWORD BgfxStateHi;
    CKDWORD StencilHash;
    CKDWORD Layout;
    CKDWORD VertexBufferMask;
    CKDWORD IndexBuffer;
    CKDWORD IndexStart;
    CKDWORD IndexCount;
    CKDWORD IndexBgfxHandle;
    CKDWORD TextureMask;
    CKDWORD Discard;
    CKDWORD Extra0;
    CKDWORD Extra1;
    CKDWORD Extra2;
    CKDWORD Parse;
    CKDWORD Token;
    CKSTRING Source;
    CKDWORD ObjectId;
    CKSTRING ObjectName;
    CKDWORD EntityId;
    CKSTRING EntityName;
    CKDWORD MeshId;
    CKSTRING MeshName;
    CKDWORD MaterialId;
    CKSTRING MaterialName;
    CKSTRING Path;
    int GroupIndex;
    int PrimitiveIndex;
    int PrimitiveType;
    CKDWORD IndexTotal;
    CKDWORD VertexTotal;
    CKSTRING VertexFields;
    CKSTRING TextureFields;
    CKSTRING Label;
};

struct CKBgfxDrawMapStateTrace {
    CKDWORD Frame;
    CKDWORD Submit;
    CKDWORD StateHash;
    CKDWORD Low;
    CKDWORD Mid;
    CKDWORD High;
    CKDWORD BgfxStateLo;
    CKDWORD BgfxStateHi;
    CKDWORD StencilHash;
    CKDWORD StencilRef;
    CKDWORD StencilReadMask;
    CKDWORD StencilWriteMask;
    CKDWORD PointSize;
};

struct CKBgfxDrawMapProgramTrace {
    CKSTRING Event;
    CKDWORD Frame;
    CKDWORD Program;
    CKDWORD Bgfx;
    CKDWORD VertexShader;
    CKDWORD PixelShader;
    CKSTRING Profile;
    CKDWORD SpecCount;
    CKDWORD SpecHash;
    CKSTRING Spec;
};

struct CKBgfxDrawMapTextureTrace {
    CKSTRING Event;
    CKDWORD Frame;
    CKDWORD Texture;
    CKDWORD Bgfx;
    CKDWORD SamplerBase;
    CKSTRING Kind;
    CKDWORD Width;
    CKDWORD Height;
    CKDWORD Depth;
    int Format;
    CKDWORD Mips;
    CKDWORD Flags;
    CKDWORD AutoMip;
    CKDWORD BaseSampler;
    CKDWORD IsDepth;
    CKDWORD BitsPerPixel;
};

struct CKBgfxDrawMapBufferTrace {
    CKSTRING Event;
    CKDWORD Frame;
    CKSTRING Kind;
    CKDWORD Buffer;
    CKDWORD Bgfx;
    CKDWORD Layout;
    CKDWORD Stride;
    CKDWORD Count;
    CKDWORD Index32;
    CKDWORD Flags;
};

CKBOOL CKBgfxDrawMapAppend(char *Buffer, CKDWORD BufferSize,
                           CKDWORD *Offset, CKSTRING Format, ...);
CKBOOL CKBgfxDrawMapAppendTextureBinding(char *Buffer, CKDWORD BufferSize,
                                         CKDWORD *Offset, CKDWORD Stage,
                                         CKDWORD Texture, CKDWORD Uniform,
                                         CKDWORD Bgfx, CKDWORD SamplerFlags);
CKBOOL CKBgfxDrawMapAppendVertexBinding(char *Buffer, CKDWORD BufferSize,
                                        CKDWORD *Offset, CKDWORD Stage,
                                        CKDWORD BufferId, CKDWORD Start,
                                        CKDWORD Count, CKDWORD Bgfx,
                                        CKDWORD Layout);
void CKBgfxDrawMapTraceSubmitMiss(CKSTRING Reason, CKDWORD Frame,
                                  CKDWORD Submit, CKDWORD View,
                                  CKDWORD Program, CKSTRING Kind,
                                  CKSTRING Label);
void CKBgfxDrawMapTraceState(const CKBgfxDrawMapStateTrace *Trace);
void CKBgfxDrawMapTraceSubmit(const CKBgfxDrawMapSubmitTrace *Trace);
void CKBgfxDrawMapTraceProgram(const CKBgfxDrawMapProgramTrace *Trace);
void CKBgfxDrawMapTraceTexture(const CKBgfxDrawMapTextureTrace *Trace);
void CKBgfxDrawMapTraceBuffer(const CKBgfxDrawMapBufferTrace *Trace);

#endif
