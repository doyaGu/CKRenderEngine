#include "CKFFRenderPacketReplay.h"
#include "CKFFDebug.h"
#include "CKRasterizer.h"

#include <cstring>

static void CKFFReplayIncrement(CKDWORD *counter, CKDWORD amount)
{
    if (counter)
        *counter += amount;
}

static void CKFFReplayRecordUniform(CKFFRenderPacketReplayDiagnostics *diagnostics,
                                    CKDWORD uniform,
                                    CKDWORD count)
{
    if (!diagnostics)
        return;
    if (diagnostics->StatsEnabled || diagnostics->UniformHistEnabled) {
        CKFFReplayIncrement(diagnostics->UniformSets, 1);
        CKFFReplayIncrement(diagnostics->UniformVec4s, count);
    }
    if (diagnostics->UniformHistEnabled &&
        diagnostics->Uniforms &&
        diagnostics->UniformHandleSets &&
        diagnostics->UniformHandleVec4s) {
        CKDWORD slot = CKFFUniformDebugSlot(*diagnostics->Uniforms, uniform);
        if (slot < 64) {
            ++diagnostics->UniformHandleSets[slot];
            diagnostics->UniformHandleVec4s[slot] += count;
        }
    }
}

static void CKFFReplayUploadObjectUniforms(CKFFRenderPacketReplayContext *context,
                                           const CKRenderPacketObjectUniforms &uniforms)
{
    if (!context || !context->Encoder ||
        uniforms.MatrixUniform == 0 ||
        uniforms.MatrixCount == 0)
        return;
    context->Encoder->SetUniform(uniforms.MatrixUniform, uniforms.Matrices,
                                 uniforms.MatrixCount);
    CKFFReplayRecordUniform(&context->Diagnostics, uniforms.MatrixUniform,
                            uniforms.MatrixCount);
}

static void CKFFReplayUploadUniformPayload(CKFFRenderPacketReplayContext *context,
                                           const CKFFRenderPacketUniformPayload &payload)
{
    if (!context || !context->Encoder)
        return;
    for (CKDWORD i = 0; i < payload.EntryCount; ++i) {
        const CKFFRenderPacketUniformEntry &entry = payload.Entries[i];
        context->Encoder->SetUniform(entry.Uniform,
                                     &payload.Values[entry.Offset][0],
                                     entry.Count);
        CKFFReplayRecordUniform(&context->Diagnostics, entry.Uniform, entry.Count);
    }
}

void CKFFInitRenderPacketReplayDiagnostics(CKFFRenderPacketReplayDiagnostics *diagnostics)
{
    if (diagnostics)
        memset(diagnostics, 0, sizeof(CKFFRenderPacketReplayDiagnostics));
}

void CKFFBindRenderPacketSharedState(CKFFRenderPacketReplayContext *context,
                                     const CKRenderPacket &packet,
                                     CKRenderPacketReplayCache *cache)
{
    if (!context || !context->Encoder || !context->Queue || !cache)
        return;

    CKRasterizerEncoder *encoder = context->Encoder;
    CKFFRenderPacketReplayDiagnostics *diagnostics = &context->Diagnostics;

    if (!cache->HasState || !CKFFDrawStateEquals(cache->DrawState, packet.DrawState)) {
        encoder->SetState(packet.DrawState);
        cache->DrawState = packet.DrawState;
        cache->HasState = TRUE;
    } else {
        CKFFReplayIncrement(diagnostics->RenderPacketSkippedStates, 1);
    }

    if (!cache->HasStencil ||
        cache->StencilRef != packet.StencilRef ||
        cache->StencilReadMask != packet.StencilReadMask ||
        cache->StencilWriteMask != packet.StencilWriteMask) {
        encoder->SetStencilRef(packet.StencilRef);
        encoder->SetStencilMask(packet.StencilReadMask, packet.StencilWriteMask);
        cache->StencilRef = packet.StencilRef;
        cache->StencilReadMask = packet.StencilReadMask;
        cache->StencilWriteMask = packet.StencilWriteMask;
        cache->HasStencil = TRUE;
    }

    if (packet.VertexLayout &&
        (!cache->HasVertexLayout || cache->VertexLayout != packet.VertexLayout)) {
        encoder->SetVertexLayout(packet.VertexLayout);
        cache->VertexLayout = packet.VertexLayout;
        cache->HasVertexLayout = TRUE;
        CKFFReplayIncrement(diagnostics->VertexLayoutSets, 1);
    }

    if (!cache->HasVertexBuffer ||
        cache->VertexBuffer != packet.VertexBuffer ||
        cache->BaseVertex != packet.BaseVertex ||
        cache->VertexCount != packet.VertexCount) {
        encoder->SetVertexBuffer(0, packet.VertexBuffer,
                                 packet.BaseVertex, packet.VertexCount);
        cache->VertexBuffer = packet.VertexBuffer;
        cache->BaseVertex = packet.BaseVertex;
        cache->VertexCount = packet.VertexCount;
        cache->HasVertexBuffer = TRUE;
        CKFFReplayIncrement(diagnostics->VertexBufferSets, 1);
    } else {
        CKFFReplayIncrement(diagnostics->RenderPacketSkippedVertexBuffers, 1);
    }

    if (packet.IndexBuffer) {
        if (!cache->HasIndexBuffer ||
            cache->IndexBuffer != packet.IndexBuffer ||
            cache->StartIndex != packet.StartIndex ||
            cache->IndexCount != packet.IndexCount) {
            encoder->SetIndexBuffer(packet.IndexBuffer,
                                    packet.StartIndex,
                                    packet.IndexCount);
            cache->IndexBuffer = packet.IndexBuffer;
            cache->StartIndex = packet.StartIndex;
            cache->IndexCount = packet.IndexCount;
            cache->HasIndexBuffer = TRUE;
            CKFFReplayIncrement(diagnostics->IndexBufferSets, 1);
        } else {
            CKFFReplayIncrement(diagnostics->RenderPacketSkippedIndexBuffers, 1);
        }
    } else {
        cache->HasIndexBuffer = FALSE;
    }

    CKBOOL sameTextures = cache->HasTextures &&
                          cache->ActiveTextureCount == packet.ActiveTextureCount &&
                          cache->TextureSetHash == packet.SortKey.TextureSetHash;
    if (sameTextures) {
        for (CKDWORD i = 0; i < packet.ActiveTextureCount; ++i) {
            if (!CKFFRenderPacketTextureEquals(cache->Textures[i], packet.Textures[i])) {
                sameTextures = FALSE;
                break;
            }
        }
    }
    if (!sameTextures) {
        for (CKDWORD i = 0; i < packet.ActiveTextureCount; ++i) {
            if (packet.Textures[i].Texture == 0)
                continue;
            encoder->SetTexture(packet.Textures[i].Stage,
                                packet.Textures[i].Uniform,
                                packet.Textures[i].Texture,
                                (CKSamplerDesc *)&packet.Textures[i].Sampler);
            CKFFReplayIncrement(diagnostics->TextureBinds, 1);
        }
        cache->ActiveTextureCount = packet.ActiveTextureCount;
        cache->TextureSetHash = packet.SortKey.TextureSetHash;
        for (CKDWORD i = 0; i < packet.ActiveTextureCount; ++i)
            cache->Textures[i] = packet.Textures[i];
        cache->HasTextures = TRUE;
    } else {
        CKFFReplayIncrement(diagnostics->RenderPacketSkippedTextures, 1);
    }

    if (!cache->HasStaticUniforms ||
        cache->StaticUniformIndex != packet.StaticUniformIndex) {
        const CKFFRenderPacketUniformPayload &staticUniforms =
            context->Queue->GetStaticUniformPayload(packet.StaticUniformIndex);
        CKFFReplayUploadUniformPayload(context, staticUniforms);
        cache->StaticUniformIndex = packet.StaticUniformIndex;
        cache->HasStaticUniforms = TRUE;
        CKFFReplayIncrement(diagnostics->RenderPacketStaticUniformUploads, 1);
    } else {
        CKFFReplayIncrement(diagnostics->RenderPacketSkippedUniforms, 1);
        CKFFReplayIncrement(diagnostics->RenderPacketStaticUniformSkips, 1);
    }
}

void CKFFReplayVertexBufferPacket(CKFFRenderPacketReplayContext *context,
                                  const CKRenderPacket &packet,
                                  CKRenderPacketReplayCache *cache,
                                  CKBOOL lastPacket)
{
    if (!context || !context->Encoder || !context->Context || !cache)
        return;

    CKFFBindRenderPacketSharedState(context, packet, cache);
    CKFFReplayUploadObjectUniforms(context, packet.ObjectUniforms);
    CKFFReplayIncrement(context->Diagnostics.RenderPacketObjectUniformUploads, 1);

    CKDWORD transformIdx = context->Context->AllocTransform((VxMatrix *)&packet.World, 1);
    context->Encoder->SetTransform(transformIdx, 1);
    CKFFReplayIncrement(context->Diagnostics.TransformSets, 1);
    if (packet.Marker[0] != '\0')
        context->Encoder->SetMarker((CKSTRING)packet.Marker);
    context->Encoder->Submit(packet.View, packet.Program, packet.Depth,
                             lastPacket ? CKRST_DISCARD_ALL : CKRST_DISCARD_NONE);
    CKFFReplayIncrement(context->Diagnostics.SubmittedDraws, 1);
    CKFFReplayIncrement(context->Diagnostics.ReplayedRenderPackets, 1);
}

void CKFFReplayVertexBufferPacketRange(CKFFRenderPacketReplayContext *context,
                                       const XArray<CKDWORD> *indices,
                                       int start,
                                       int offset,
                                       int packetCount,
                                       CKBOOL directReplay,
                                       CKRenderPacketReplayCache *cache,
                                       CKBOOL lastRange)
{
    if (!context || !context->Queue)
        return;
    if (!directReplay && !indices)
        return;

    for (int i = 0; i < packetCount; ++i) {
        const int packetIndex = directReplay
            ? start + offset + i
            : (int)(*indices)[start + offset + i];
        const CKRenderPacket &packet = context->Queue->GetPacket(packetIndex);
        CKFFReplayVertexBufferPacket(context, packet, cache,
                                     lastRange && i + 1 == packetCount ? TRUE : FALSE);
    }
}

CKBOOL CKFFReplayVertexBufferPacketRunInstanced(CKFFRenderPacketReplayContext *context,
                                                const XArray<CKDWORD> *indices,
                                                int start,
                                                int packetCount,
                                                CKBOOL directReplay,
                                                CKRenderPacketReplayCache *cache,
                                                CKBOOL lastRun)
{
    if (!context || !context->Encoder || !context->Context ||
        !context->Queue || !cache || !context->InstanceLayout)
        return FALSE;
    if (packetCount < CKFF_RENDER_PACKET_MIN_INSTANCE_COUNT)
        return FALSE;

    int pos = 0;
    while (pos < packetCount) {
        int remaining = packetCount - pos;
        if (remaining < CKFF_RENDER_PACKET_MIN_INSTANCE_COUNT) {
            CKFFReplayVertexBufferPacketRange(context, indices, start, pos, remaining,
                                              directReplay, cache, lastRun);
            return TRUE;
        }

        const int firstIndex = directReplay
            ? start + pos
            : (int)(*indices)[start + pos];
        const CKRenderPacket &first = context->Queue->GetPacket(firstIndex);
        CKDWORD instanceCount = (CKDWORD)remaining;
        CKDWORD available = context->Context->GetAvailTransientInstanceBuffer(
            instanceCount, context->InstanceLayout);
        if (available > instanceCount)
            available = instanceCount;
        if (available < CKFF_RENDER_PACKET_MIN_INSTANCE_COUNT) {
            CKFFReplayIncrement(context->Diagnostics.RenderPacketInstanceAllocFailures, 1);
            CKFFReplayIncrement(context->Diagnostics.RenderPacketInstancingFallbacks, 1);
            if (pos == 0)
                return FALSE;
            CKFFReplayVertexBufferPacketRange(context, indices, start, pos, remaining,
                                              directReplay, cache, lastRun);
            return TRUE;
        }

        CKTransientInstanceBuffer instanceBuffer;
        memset(&instanceBuffer, 0, sizeof(instanceBuffer));
        if (!context->Context->AllocTransientInstanceBuffer(&instanceBuffer, available,
                                                           context->InstanceLayout)) {
            CKFFReplayIncrement(context->Diagnostics.RenderPacketInstanceAllocFailures, 1);
            CKFFReplayIncrement(context->Diagnostics.RenderPacketInstancingFallbacks, 1);
            if (pos == 0)
                return FALSE;
            CKFFReplayVertexBufferPacketRange(context, indices, start, pos, remaining,
                                              directReplay, cache, lastRun);
            return TRUE;
        }

        for (CKDWORD i = 0; i < available; ++i) {
            const int packetIndex = directReplay
                ? start + pos + (int)i
                : (int)(*indices)[start + pos + (int)i];
            const CKRenderPacket &packet = context->Queue->GetPacket(packetIndex);
            CKBYTE *dst = (CKBYTE *)instanceBuffer.Data + instanceBuffer.Stride * i;
            memcpy(dst, &packet.World, sizeof(VxMatrix));
            if (instanceBuffer.Stride > sizeof(VxMatrix))
                memset(dst + sizeof(VxMatrix), 0, instanceBuffer.Stride - sizeof(VxMatrix));
        }

        CKFFBindRenderPacketSharedState(context, first, cache);
        if (first.ObjectUniforms.MatrixUniform) {
            context->Encoder->SetUniform(first.ObjectUniforms.MatrixUniform,
                                         &first.ViewProjection, 1);
            CKFFReplayRecordUniform(&context->Diagnostics,
                                    first.ObjectUniforms.MatrixUniform, 1);
        }
        CKFFReplayIncrement(context->Diagnostics.RenderPacketObjectUniformUploads, 1);
        CKFFReplayIncrement(context->Diagnostics.RenderPacketInstanceBufferBytes,
                            instanceBuffer.Stride * available);
        context->Encoder->SetTransientInstanceBuffer(0, &instanceBuffer);
        if (first.Marker[0] != '\0')
            context->Encoder->SetMarker((CKSTRING)first.Marker);

        const CKBOOL chunkIsLast = (pos + (int)available == packetCount) ? TRUE : FALSE;
        CKDWORD discardFlags = (lastRun && chunkIsLast)
            ? CKRST_DISCARD_ALL
            : CKRST_DISCARD_INSTANCEDATA;
        context->Encoder->Submit(first.View, first.InstancedProgram, first.Depth, discardFlags);
        CKFFReplayIncrement(context->Diagnostics.SubmittedDraws, 1);
        CKFFReplayIncrement(context->Diagnostics.ReplayedRenderPackets, available);
        CKFFReplayIncrement(context->Diagnostics.RenderPacketInstancedRuns, 1);
        CKFFReplayIncrement(context->Diagnostics.RenderPacketInstancedPackets, available);
        CKFFReplayIncrement(context->Diagnostics.RenderPacketInstancedSubmits, 1);
        if (available > 0)
            CKFFReplayIncrement(context->Diagnostics.RenderPacketSubmitSavedEstimate,
                                available - 1);
        pos += (int)available;
    }

    return TRUE;
}
