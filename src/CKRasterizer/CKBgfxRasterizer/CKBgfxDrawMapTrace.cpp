#include "CKBgfxDrawMapTrace.h"
#include "CKBgfxInternal.h"

#include <cstdarg>
#include <cstdio>

static CKSTRING CKBgfxDrawMapSafeString(CKSTRING Text)
{
    return Text ? Text : (CKSTRING)"";
}

CKBOOL CKBgfxDrawMapAppend(char *Buffer, CKDWORD BufferSize,
                           CKDWORD *Offset, CKSTRING Format, ...)
{
    int written;
    va_list args;
    if (!Buffer || !Offset || !Format || BufferSize == 0)
        return FALSE;
    if (*Offset >= BufferSize) {
        Buffer[BufferSize - 1] = '\0';
        return FALSE;
    }
    va_start(args, Format);
    written = vsnprintf(Buffer + *Offset, BufferSize - *Offset, Format, args);
    va_end(args);
    if (written <= 0)
        return FALSE;
    if ((CKDWORD)written >= BufferSize - *Offset) {
        *Offset = BufferSize - 1;
        Buffer[BufferSize - 1] = '\0';
        return FALSE;
    }
    *Offset += (CKDWORD)written;
    return TRUE;
}

CKBOOL CKBgfxDrawMapAppendTextureBinding(char *Buffer, CKDWORD BufferSize,
                                         CKDWORD *Offset, CKDWORD Stage,
                                         CKDWORD Texture, CKDWORD Uniform,
                                         CKDWORD Bgfx, CKDWORD SamplerFlags)
{
    return CKBgfxDrawMapAppend(Buffer, BufferSize, Offset,
                               (CKSTRING)" tex%u=%u:%u:%u:0x%08X",
                               Stage, Texture, Uniform, Bgfx, SamplerFlags);
}

CKBOOL CKBgfxDrawMapAppendVertexBinding(char *Buffer, CKDWORD BufferSize,
                                        CKDWORD *Offset, CKDWORD Stage,
                                        CKDWORD BufferId, CKDWORD Start,
                                        CKDWORD Count, CKDWORD Bgfx,
                                        CKDWORD Layout)
{
    return CKBgfxDrawMapAppend(Buffer, BufferSize, Offset,
                               (CKSTRING)" vb%u=%u:%u:%u:%u:%u",
                               Stage, BufferId, Start, Count, Bgfx, Layout);
}

void CKBgfxDrawMapTraceSubmitMiss(CKSTRING Reason, CKDWORD Frame,
                                  CKDWORD Submit, CKDWORD View,
                                  CKDWORD Program, CKSTRING Kind,
                                  CKSTRING Label)
{
    if (Label && Label[0] != '\0') {
        CKBgfxLogf(CKBGFX_DRAWMAP_TAG_SUBMIT_MISS,
                   "reason=%s frame=%u submit=%u view=%u program=%u kind=%s label=\"%s\"",
                   CKBgfxDrawMapSafeString(Reason), Frame, Submit, View,
                   Program, CKBgfxDrawMapSafeString(Kind), Label);
    } else {
        CKBgfxLogf(CKBGFX_DRAWMAP_TAG_SUBMIT_MISS,
                   "reason=%s frame=%u submit=%u view=%u program=%u kind=%s",
                   CKBgfxDrawMapSafeString(Reason), Frame, Submit, View,
                   Program, CKBgfxDrawMapSafeString(Kind));
    }
}

void CKBgfxDrawMapTraceState(const CKBgfxDrawMapStateTrace *Trace)
{
    if (!Trace)
        return;
    CKBgfxLogf(CKBGFX_DRAWMAP_TAG_STATE_MAP,
               "event=use frame=%u submit=%u stateHash=0x%08X low=0x%08X mid=0x%08X high=0x%08X bgfxStateLo=0x%08X bgfxStateHi=0x%08X stencilHash=0x%08X stencilRef=%u stencilRead=0x%02X stencilWrite=0x%02X pointSize=%u",
               Trace->Frame, Trace->Submit, Trace->StateHash, Trace->Low,
               Trace->Mid, Trace->High, Trace->BgfxStateLo,
               Trace->BgfxStateHi, Trace->StencilHash, Trace->StencilRef,
               Trace->StencilReadMask, Trace->StencilWriteMask,
               Trace->PointSize);
}

void CKBgfxDrawMapTraceSubmit(const CKBgfxDrawMapSubmitTrace *Trace)
{
    if (!Trace)
        return;
    CKBgfxLogf(CKBGFX_DRAWMAP_TAG_SUBMIT_MAP,
               "schema=%u frame=%u submit=%u view=%u viewSubmit=%u kind=%s viewMode=%s orderGen=%u orderSequential=%u depth=%u program=%u bgfxProgram=%u programHash=0x%08X specHash=0x%08X shaderProfile=%s stateHash=0x%08X bgfxStateLo=0x%08X bgfxStateHi=0x%08X stencilHash=0x%08X layout=%u vbMask=0x%X ib=%u:%u:%u:%u textureMask=0x%X discard=0x%02X extra0=%u extra1=%u extra2=%u parse=%u token=%u source=%s object=%u:%s entity=%u:%s mesh=%u:%s material=%u:%s path=%s group=%d prim=%d type=%d indices=%u verts=%u%s%s label=\"%s\"",
               CKBGFX_DRAWMAP_SCHEMA,
               Trace->Frame,
               Trace->Submit,
               Trace->View,
               Trace->ViewSubmit,
               CKBgfxDrawMapSafeString(Trace->Kind),
               CKBgfxDrawMapSafeString(Trace->ViewMode),
               Trace->OrderGeneration,
               Trace->OrderSequential,
               Trace->Depth,
               Trace->Program,
               Trace->BgfxProgram,
               Trace->ProgramHash,
               Trace->SpecHash,
               CKBgfxDrawMapSafeString(Trace->ShaderProfile),
               Trace->StateHash,
               Trace->BgfxStateLo,
               Trace->BgfxStateHi,
               Trace->StencilHash,
               Trace->Layout,
               Trace->VertexBufferMask,
               Trace->IndexBuffer,
               Trace->IndexStart,
               Trace->IndexCount,
               Trace->IndexBgfxHandle,
               Trace->TextureMask,
               Trace->Discard,
               Trace->Extra0,
               Trace->Extra1,
               Trace->Extra2,
               Trace->Parse,
               Trace->Token,
               CKBgfxDrawMapSafeString(Trace->Source),
               Trace->ObjectId,
               CKBgfxDrawMapSafeString(Trace->ObjectName),
               Trace->EntityId,
               CKBgfxDrawMapSafeString(Trace->EntityName),
               Trace->MeshId,
               CKBgfxDrawMapSafeString(Trace->MeshName),
               Trace->MaterialId,
               CKBgfxDrawMapSafeString(Trace->MaterialName),
               CKBgfxDrawMapSafeString(Trace->Path),
               Trace->GroupIndex,
               Trace->PrimitiveIndex,
               Trace->PrimitiveType,
               Trace->IndexTotal,
               Trace->VertexTotal,
               CKBgfxDrawMapSafeString(Trace->VertexFields),
               CKBgfxDrawMapSafeString(Trace->TextureFields),
               CKBgfxDrawMapSafeString(Trace->Label));
}

void CKBgfxDrawMapTraceProgram(const CKBgfxDrawMapProgramTrace *Trace)
{
    if (!Trace)
        return;
    CKBgfxLogf(CKBGFX_DRAWMAP_TAG_PROGRAM_MAP,
               "event=%s frame=%u program=%u bgfx=%u vs=%u ps=%u profile=%s specCount=%u specHash=0x%08X spec=\"%s\"",
               CKBgfxDrawMapSafeString(Trace->Event), Trace->Frame,
               Trace->Program, Trace->Bgfx, Trace->VertexShader,
               Trace->PixelShader, CKBgfxDrawMapSafeString(Trace->Profile),
               Trace->SpecCount, Trace->SpecHash,
               CKBgfxDrawMapSafeString(Trace->Spec));
}

void CKBgfxDrawMapTraceTexture(const CKBgfxDrawMapTextureTrace *Trace)
{
    if (!Trace)
        return;
    CKBgfxLogf(CKBGFX_DRAWMAP_TAG_TEXTURE_MAP,
               "event=%s frame=%u texture=%u bgfx=%u samplerBase=%u kind=%s size=%ux%ux%u format=%d mips=%u flags=0x%08X autoMip=%u baseSampler=%u depth=%u bpp=%u",
               CKBgfxDrawMapSafeString(Trace->Event), Trace->Frame,
               Trace->Texture, Trace->Bgfx, Trace->SamplerBase,
               CKBgfxDrawMapSafeString(Trace->Kind), Trace->Width,
               Trace->Height, Trace->Depth, Trace->Format, Trace->Mips,
               Trace->Flags, Trace->AutoMip, Trace->BaseSampler,
               Trace->IsDepth, Trace->BitsPerPixel);
}

void CKBgfxDrawMapTraceBuffer(const CKBgfxDrawMapBufferTrace *Trace)
{
    if (!Trace)
        return;
    CKBgfxLogf(CKBGFX_DRAWMAP_TAG_BUFFER_MAP,
               "event=%s frame=%u kind=%s buffer=%u bgfx=%u layout=%u stride=%u count=%u index32=%u flags=0x%08X",
               CKBgfxDrawMapSafeString(Trace->Event), Trace->Frame,
               CKBgfxDrawMapSafeString(Trace->Kind), Trace->Buffer,
               Trace->Bgfx, Trace->Layout, Trace->Stride, Trace->Count,
               Trace->Index32, Trace->Flags);
}
