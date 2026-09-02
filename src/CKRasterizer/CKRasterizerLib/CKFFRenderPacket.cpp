#include "CKFFRenderPacket.h"

#include <string.h>

CKBOOL CKFFDrawStateEquals(const CKDrawState &a, const CKDrawState &b)
{
    return a.Lo == b.Lo && a.Mid == b.Mid && a.Hi == b.Hi ? TRUE : FALSE;
}

CKDWORD CKFFHashBytes(const void *data, CKDWORD size, CKDWORD hash)
{
    const CKBYTE *bytes = (const CKBYTE *)data;
    for (CKDWORD i = 0; i < size; ++i) {
        hash ^= (CKDWORD)bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

CKDWORD CKFFStaticTextureFlags(CKDWORD flags)
{
    return flags & (CKRST_TEXTURE_CUBEMAP |
                    CKRST_TEXTURE_VOLUMEMAP |
                    CKRST_TEXTURE_DEPTHSTENCIL);
}

static int CKFFCompareDword(CKDWORD a, CKDWORD b)
{
    if (a < b)
        return -1;
    if (a > b)
        return 1;
    return 0;
}

static CKBOOL CKFFSamplerEquals(const CKSamplerDesc &a, const CKSamplerDesc &b)
{
    return memcmp(&a, &b, sizeof(CKSamplerDesc)) == 0 ? TRUE : FALSE;
}

CKBOOL CKFFRenderPacketTextureEquals(const CKFFRenderPacketTextureBinding &a,
                                     const CKFFRenderPacketTextureBinding &b)
{
    if (a.Stage != b.Stage || a.Uniform != b.Uniform ||
        a.Texture != b.Texture || a.TextureFlags != b.TextureFlags)
        return FALSE;
    return CKFFSamplerEquals(a.Sampler, b.Sampler);
}

CKBOOL CKFFRenderPacketUniformPayloadEquals(const CKFFRenderPacketUniformPayload &a,
                                            const CKFFRenderPacketUniformPayload &b)
{
    if (a.Hash != b.Hash || a.EntryCount != b.EntryCount || a.Vec4Count != b.Vec4Count)
        return FALSE;
    if (a.EntryCount > 0 &&
        memcmp(a.Entries, b.Entries, a.EntryCount * sizeof(CKFFRenderPacketUniformEntry)) != 0)
        return FALSE;
    if (a.Vec4Count > 0 &&
        memcmp(a.Values, b.Values, a.Vec4Count * sizeof(float) * 4) != 0)
        return FALSE;
    return TRUE;
}

int CKFFCompareRenderPacketSortKey(const CKRenderPacketSortKey &a,
                                   const CKRenderPacketSortKey &b)
{
    int cmp = CKFFCompareDword(a.Program, b.Program);
    if (cmp)
        return cmp;
    cmp = CKFFCompareDword(a.DrawStateLo, b.DrawStateLo);
    if (cmp)
        return cmp;
    cmp = CKFFCompareDword(a.DrawStateMid, b.DrawStateMid);
    if (cmp)
        return cmp;
    cmp = CKFFCompareDword(a.DrawStateHi, b.DrawStateHi);
    if (cmp)
        return cmp;
    cmp = CKFFCompareDword(a.StencilRef, b.StencilRef);
    if (cmp)
        return cmp;
    cmp = CKFFCompareDword(a.StencilReadMask, b.StencilReadMask);
    if (cmp)
        return cmp;
    cmp = CKFFCompareDword(a.StencilWriteMask, b.StencilWriteMask);
    if (cmp)
        return cmp;
    cmp = CKFFCompareDword(a.StaticUniformHash, b.StaticUniformHash);
    if (cmp)
        return cmp;
    cmp = CKFFCompareDword(a.TextureSetHash, b.TextureSetHash);
    if (cmp)
        return cmp;
    cmp = CKFFCompareDword(a.ActiveTextureCount, b.ActiveTextureCount);
    if (cmp)
        return cmp;
    cmp = CKFFCompareDword(a.VertexLayout, b.VertexLayout);
    if (cmp)
        return cmp;
    cmp = CKFFCompareDword(a.VertexBuffer, b.VertexBuffer);
    if (cmp)
        return cmp;
    cmp = CKFFCompareDword(a.BaseVertex, b.BaseVertex);
    if (cmp)
        return cmp;
    cmp = CKFFCompareDword(a.VertexCount, b.VertexCount);
    if (cmp)
        return cmp;
    cmp = CKFFCompareDword(a.IndexBuffer, b.IndexBuffer);
    if (cmp)
        return cmp;
    cmp = CKFFCompareDword(a.StartIndex, b.StartIndex);
    if (cmp)
        return cmp;
    return CKFFCompareDword(a.IndexCount, b.IndexCount);
}

CKBOOL CKFFRenderPacketSortKeyEquals(const CKRenderPacketSortKey &a,
                                     const CKRenderPacketSortKey &b)
{
    return CKFFCompareRenderPacketSortKey(a, b) == 0 ? TRUE : FALSE;
}

static int CKFFComparePacketTextureSet(const CKRenderPacket &a,
                                       const CKRenderPacket &b)
{
    int cmp = CKFFCompareDword(a.ActiveTextureCount, b.ActiveTextureCount);
    if (cmp)
        return cmp;
    for (CKDWORD i = 0; i < a.ActiveTextureCount && i < CKFF_MAX_TEXTURE_STAGES; ++i) {
        cmp = CKFFCompareDword(a.Textures[i].Stage, b.Textures[i].Stage);
        if (cmp)
            return cmp;
        cmp = CKFFCompareDword(a.Textures[i].Texture, b.Textures[i].Texture);
        if (cmp)
            return cmp;
        cmp = CKFFCompareDword(a.Textures[i].Uniform, b.Textures[i].Uniform);
        if (cmp)
            return cmp;
        cmp = CKFFCompareDword(a.Textures[i].TextureFlags, b.Textures[i].TextureFlags);
        if (cmp)
            return cmp;
        cmp = memcmp(&a.Textures[i].Sampler, &b.Textures[i].Sampler, sizeof(CKSamplerDesc));
        if (cmp < 0)
            return -1;
        if (cmp > 0)
            return 1;
    }
    return 0;
}

int CKFFCompareRenderPacket(const CKRenderPacket &a,
                            const CKRenderPacket &b)
{
    int cmp = CKFFCompareRenderPacketSortKey(a.SortKey, b.SortKey);
    if (cmp)
        return cmp;
    return CKFFCompareDword(a.Serial, b.Serial);
}

CKBOOL CKFFRenderPacketSameRunKey(const CKRenderPacket &a,
                                  const CKRenderPacket &b)
{
    return CKFFRenderPacketSortKeyEquals(a.SortKey, b.SortKey);
}

CKBOOL CKFFRenderPacketCanInstanceRun(const CKRenderPacket &a,
                                       const CKRenderPacket &b)
{
    if (!a.CanInstance || !b.CanInstance)
        return FALSE;
    if (a.InstancedProgram != b.InstancedProgram)
        return FALSE;
    if (!CKFFRenderPacketSameRunKey(a, b))
        return FALSE;
    if (a.StaticUniformIndex != b.StaticUniformIndex)
        return FALSE;
    if (a.ActiveTextureCount != b.ActiveTextureCount)
        return FALSE;
    for (CKDWORD i = 0; i < a.ActiveTextureCount && i < CKFF_MAX_TEXTURE_STAGES; ++i) {
        if (!CKFFRenderPacketTextureEquals(a.Textures[i], b.Textures[i]))
            return FALSE;
    }
    if (a.ViewProjectionHash != b.ViewProjectionHash)
        return FALSE;
    return memcmp(&a.ViewProjection, &b.ViewProjection, sizeof(VxMatrix)) == 0 ? TRUE : FALSE;
}

CKBOOL CKFFRenderPacketAddUniform(CKFFRenderPacketUniformPayload *payload,
                                  CKDWORD uniform,
                                  const void *data,
                                  CKDWORD count,
                                  CKDWORD vec4Count)
{
    if (!payload || !data || count == 0)
        return TRUE;
    if (payload->EntryCount >= CKFF_RENDER_PACKET_MAX_UNIFORMS)
        return FALSE;
    if (payload->Vec4Count + vec4Count > CKFF_RENDER_PACKET_MAX_UNIFORM_VEC4S)
        return FALSE;

    CKFFRenderPacketUniformEntry &entry = payload->Entries[payload->EntryCount];
    entry.Uniform = uniform;
    entry.Offset = payload->Vec4Count;
    entry.Count = count;
    entry.Vec4Count = vec4Count;
    memcpy(&payload->Values[payload->Vec4Count][0], data, vec4Count * sizeof(float) * 4);
    ++payload->EntryCount;
    payload->Vec4Count += vec4Count;
    return TRUE;
}

CKBOOL CKFFRenderPacketAddUniform(CKFFRenderPacketUniformPayload *payload,
                                  CKDWORD uniform,
                                  const void *data,
                                  CKDWORD count)
{
    return CKFFRenderPacketAddUniform(payload, uniform, data, count, count);
}

CKDWORD CKFFHashRenderPacketUniformPayload(const CKFFRenderPacketUniformPayload &payload)
{
    CKDWORD hash = 2166136261u;
    hash = CKFFHashBytes(&payload.EntryCount, sizeof(payload.EntryCount), hash);
    hash = CKFFHashBytes(&payload.Vec4Count, sizeof(payload.Vec4Count), hash);
    for (CKDWORD i = 0; i < payload.EntryCount; ++i) {
        hash = CKFFHashBytes(&payload.Entries[i].Uniform, sizeof(CKDWORD), hash);
        hash = CKFFHashBytes(&payload.Entries[i].Count, sizeof(CKDWORD), hash);
        hash = CKFFHashBytes(&payload.Entries[i].Vec4Count, sizeof(CKDWORD), hash);
        hash = CKFFHashBytes(&payload.Values[payload.Entries[i].Offset][0],
                             payload.Entries[i].Vec4Count * sizeof(float) * 4, hash);
    }
    return hash;
}

CKDWORD CKFFHashRenderPacketTextureSet(CKDWORD activeTextureCount,
                                       const CKFFRenderPacketTextureBinding *textures)
{
    CKDWORD hash = 2166136261u;
    hash = CKFFHashBytes(&activeTextureCount, sizeof(activeTextureCount), hash);
    for (CKDWORD i = 0; textures && i < activeTextureCount && i < CKFF_MAX_TEXTURE_STAGES; ++i) {
        const CKFFRenderPacketTextureBinding &binding = textures[i];
        hash = CKFFHashBytes(&binding.Stage, sizeof(binding.Stage), hash);
        hash = CKFFHashBytes(&binding.Uniform, sizeof(binding.Uniform), hash);
        hash = CKFFHashBytes(&binding.Texture, sizeof(binding.Texture), hash);
        hash = CKFFHashBytes(&binding.TextureFlags, sizeof(binding.TextureFlags), hash);
        hash = CKFFHashBytes(&binding.Sampler, sizeof(binding.Sampler), hash);
    }
    return hash;
}

CKDWORD CKFFHashRenderPacketTextureSet(const CKRenderPacket &packet)
{
    return CKFFHashRenderPacketTextureSet(packet.ActiveTextureCount, packet.Textures);
}
