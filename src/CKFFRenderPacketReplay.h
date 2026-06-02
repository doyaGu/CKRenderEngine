#ifndef CKFFRENDERPACKETREPLAY_H
#define CKFFRENDERPACKETREPLAY_H

#include "CKFFRenderPacketQueue.h"
#include "CKFFShaderCache.h"

class CKRasterizerContext;
class CKRasterizerEncoder;

struct CKFFRenderPacketReplayDiagnostics {
    CKBOOL StatsEnabled;
    CKBOOL UniformHistEnabled;
    const CKFFUniformHandles *Uniforms;
    CKDWORD *UniformSets;
    CKDWORD *UniformVec4s;
    CKDWORD *UniformHandleSets;
    CKDWORD *UniformHandleVec4s;
    CKDWORD *TextureBinds;
    CKDWORD *VertexLayoutSets;
    CKDWORD *VertexBufferSets;
    CKDWORD *IndexBufferSets;
    CKDWORD *TransformSets;
    CKDWORD *SubmittedDraws;
    CKDWORD *ReplayedRenderPackets;
    CKDWORD *RenderPacketSkippedStates;
    CKDWORD *RenderPacketSkippedTextures;
    CKDWORD *RenderPacketSkippedUniforms;
    CKDWORD *RenderPacketStaticUniformUploads;
    CKDWORD *RenderPacketStaticUniformSkips;
    CKDWORD *RenderPacketObjectUniformUploads;
    CKDWORD *RenderPacketSkippedVertexBuffers;
    CKDWORD *RenderPacketSkippedIndexBuffers;
    CKDWORD *RenderPacketInstancedRuns;
    CKDWORD *RenderPacketInstancedPackets;
    CKDWORD *RenderPacketInstancedSubmits;
    CKDWORD *RenderPacketInstanceBufferBytes;
    CKDWORD *RenderPacketInstanceAllocFailures;
    CKDWORD *RenderPacketSubmitSavedEstimate;
    CKDWORD *RenderPacketInstancingFallbacks;
};

struct CKFFRenderPacketReplayContext {
    CKRasterizerEncoder *Encoder;
    CKRasterizerContext *Context;
    CKFFRenderPacketQueue *Queue;
    CKDWORD InstanceLayout;
    CKFFRenderPacketReplayDiagnostics Diagnostics;
};

void CKFFInitRenderPacketReplayDiagnostics(CKFFRenderPacketReplayDiagnostics *diagnostics);
void CKFFBindRenderPacketSharedState(CKFFRenderPacketReplayContext *context,
                                     const CKRenderPacket &packet,
                                     CKRenderPacketReplayCache *cache);
void CKFFReplayVertexBufferPacket(CKFFRenderPacketReplayContext *context,
                                  const CKRenderPacket &packet,
                                  CKRenderPacketReplayCache *cache,
                                  CKBOOL lastPacket);
CKBOOL CKFFReplayVertexBufferPacketRunInstanced(CKFFRenderPacketReplayContext *context,
                                                const XArray<CKDWORD> *indices,
                                                int start,
                                                int packetCount,
                                                CKBOOL directReplay,
                                                CKRenderPacketReplayCache *cache,
                                                CKBOOL lastRun);

#endif
