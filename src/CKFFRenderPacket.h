#ifndef CKFFRENDERPACKET_H
#define CKFFRENDERPACKET_H

#include "VxMath.h"
#include "CKRenderEngineTypes.h"
#include "CKRenderEngineEnums.h"
#include "CKRasterizerTypes.h"
#include "CKFFConstants.h"

#define CKFF_RENDER_PACKET_MAX_UNIFORMS 16
#define CKFF_RENDER_PACKET_MAX_UNIFORM_VEC4S 192
#define CKFF_RENDER_PACKET_MARKER_SIZE 512
#define CKFF_RENDER_PACKET_MIN_INSTANCE_COUNT 4

#define CKFF_RENDER_PACKET_RUN_FALLBACK_NONE 0
#define CKFF_RENDER_PACKET_RUN_FALLBACK_NOT_INSTANCEABLE 1

enum CKFFRenderPacketEligibilityReason {
    CKFF_RENDER_PACKET_ELIGIBLE = 0,
    CKFF_RENDER_PACKET_REJECT_SORT_DISABLED,
    CKFF_RENDER_PACKET_REJECT_PACKETS_DISALLOWED,
    CKFF_RENDER_PACKET_REJECT_ADAPTIVE_BYPASS,
    CKFF_RENDER_PACKET_REJECT_NO_CONTEXT,
    CKFF_RENDER_PACKET_REJECT_MISSING_VERTEX_BUFFER,
    CKFF_RENDER_PACKET_REJECT_MISSING_INDEX_BUFFER,
    CKFF_RENDER_PACKET_REJECT_MISSING_VERTEX_LAYOUT,
    CKFF_RENDER_PACKET_REJECT_WRONG_VIEW,
    CKFF_RENDER_PACKET_REJECT_WRONG_PRIMITIVE,
    CKFF_RENDER_PACKET_REJECT_ALPHA_BLEND,
    CKFF_RENDER_PACKET_REJECT_Z_DISABLED,
    CKFF_RENDER_PACKET_REJECT_Z_WRITE_DISABLED,
    CKFF_RENDER_PACKET_REJECT_VERTEX_BLEND,
    CKFF_RENDER_PACKET_REJECT_INDEXED_VERTEX_BLEND,
    CKFF_RENDER_PACKET_REJECT_POSITIONT,
    CKFF_RENDER_PACKET_REJECT_TEXCOORD_RANGE,
    CKFF_RENDER_PACKET_REJECT_TEXGEN,
    CKFF_RENDER_PACKET_REJECT_VIEW_SPACE_SHADER,
    CKFF_RENDER_PACKET_REJECT_VERTEX_FOG,
    CKFF_RENDER_PACKET_REJECT_PIXEL_FOG,
    CKFF_RENDER_PACKET_REJECT_RANGE_FOG,
    CKFF_RENDER_PACKET_REJECT_INSTANCE_LAYOUT,
    CKFF_RENDER_PACKET_REJECT_PROGRAM_MISSING,
    CKFF_RENDER_PACKET_REJECT_OBJECT_UNIFORMS,
    CKFF_RENDER_PACKET_REJECT_STATIC_UNIFORMS
};

struct CKFFRenderPacketUniformEntry {
    CKDWORD Uniform;
    CKDWORD Offset;
    CKDWORD Count;
    CKDWORD Vec4Count;
};

struct CKFFRenderPacketUniformPayload {
    CKDWORD EntryCount;
    CKDWORD Vec4Count;
    CKDWORD Hash;
    CKFFRenderPacketUniformEntry Entries[CKFF_RENDER_PACKET_MAX_UNIFORMS];
    float Values[CKFF_RENDER_PACKET_MAX_UNIFORM_VEC4S][4];
};

struct CKFFRenderPacketTextureBinding {
    CKDWORD Stage;
    CKDWORD Uniform;
    CKDWORD Texture;
    CKDWORD TextureFlags;
    CKSamplerDesc Sampler;
};

struct CKRenderPacketObjectUniforms {
    CKDWORD MatrixUniform;
    CKDWORD MatrixCount;
    VxMatrix Matrices[4];
};

struct CKRenderPacketSortKey {
    CKDWORD Program;
    CKDWORD DrawStateLo;
    CKDWORD DrawStateMid;
    CKDWORD DrawStateHi;
    CKDWORD StencilRef;
    CKDWORD StencilReadMask;
    CKDWORD StencilWriteMask;
    CKDWORD StaticUniformHash;
    CKDWORD TextureSetHash;
    CKDWORD ActiveTextureCount;
    CKDWORD VertexLayout;
    CKDWORD VertexBuffer;
    CKDWORD BaseVertex;
    CKDWORD VertexCount;
    CKDWORD IndexBuffer;
    CKDWORD StartIndex;
    CKDWORD IndexCount;
};

struct CKRenderPacket {
    CKDWORD Serial;
    CKRenderView View;
    VXPRIMITIVETYPE Type;
    CKDWORD Program;
    CKDWORD Depth;
    CKDrawState DrawState;
    CKDWORD StencilRef;
    CKDWORD StencilReadMask;
    CKDWORD StencilWriteMask;
    CKDWORD VertexLayout;
    CKDWORD VertexBuffer;
    CKDWORD IndexBuffer;
    CKDWORD BaseVertex;
    CKDWORD VertexCount;
    CKDWORD StartIndex;
    CKDWORD IndexCount;
    CKDWORD ActiveTextureCount;
    CKDWORD TextureSetHash;
    CKFFRenderPacketTextureBinding Textures[CKFF_MAX_TEXTURE_STAGES];
    CKDWORD StaticUniformIndex;
    CKRenderPacketObjectUniforms ObjectUniforms;
    CKRenderPacketSortKey SortKey;
    VxMatrix World;
    VxMatrix ViewProjection;
    CKDWORD ViewProjectionHash;
    CKBOOL CanInstance;
    CKDWORD InstancedProgram;
    char Marker[CKFF_RENDER_PACKET_MARKER_SIZE];
};

struct CKRenderPacketReplayCache {
    CKBOOL HasState;
    CKBOOL HasStencil;
    CKBOOL HasTextures;
    CKBOOL HasStaticUniforms;
    CKBOOL HasObjectUniforms;
    CKBOOL HasVertexLayout;
    CKBOOL HasVertexBuffer;
    CKBOOL HasIndexBuffer;
    CKDrawState DrawState;
    CKDWORD StencilRef;
    CKDWORD StencilReadMask;
    CKDWORD StencilWriteMask;
    CKDWORD ActiveTextureCount;
    CKDWORD TextureSetHash;
    CKFFRenderPacketTextureBinding Textures[CKFF_MAX_TEXTURE_STAGES];
    CKDWORD StaticUniformIndex;
    CKDWORD VertexLayout;
    CKDWORD VertexBuffer;
    CKDWORD BaseVertex;
    CKDWORD VertexCount;
    CKDWORD IndexBuffer;
    CKDWORD StartIndex;
    CKDWORD IndexCount;
};

struct CKFFRenderPacketRunPlan {
    int Start;
    int Count;
    CKBOOL Instanced;
    CKDWORD FallbackReason;
};

CKBOOL CKFFDrawStateEquals(const CKDrawState &a, const CKDrawState &b);
CKDWORD CKFFHashBytes(const void *data, CKDWORD size, CKDWORD hash);
CKDWORD CKFFStaticTextureFlags(CKDWORD flags);
CKBOOL CKFFRenderPacketTextureEquals(const CKFFRenderPacketTextureBinding &a,
                                     const CKFFRenderPacketTextureBinding &b);
CKBOOL CKFFRenderPacketUniformPayloadEquals(const CKFFRenderPacketUniformPayload &a,
                                            const CKFFRenderPacketUniformPayload &b);
int CKFFCompareRenderPacketSortKey(const CKRenderPacketSortKey &a,
                                   const CKRenderPacketSortKey &b);
CKBOOL CKFFRenderPacketSortKeyEquals(const CKRenderPacketSortKey &a,
                                     const CKRenderPacketSortKey &b);
int CKFFCompareRenderPacket(const CKRenderPacket &a,
                            const CKRenderPacket &b);
CKBOOL CKFFRenderPacketSameRunKey(const CKRenderPacket &a,
                                  const CKRenderPacket &b);
CKBOOL CKFFRenderPacketCanInstanceRun(const CKRenderPacket &a,
                                      const CKRenderPacket &b);
CKBOOL CKFFRenderPacketAddUniform(CKFFRenderPacketUniformPayload *payload,
                                  CKDWORD uniform,
                                  const void *data,
                                  CKDWORD count,
                                  CKDWORD vec4Count);
CKBOOL CKFFRenderPacketAddUniform(CKFFRenderPacketUniformPayload *payload,
                                  CKDWORD uniform,
                                  const void *data,
                                  CKDWORD count);
CKDWORD CKFFHashRenderPacketUniformPayload(const CKFFRenderPacketUniformPayload &payload);
CKDWORD CKFFHashRenderPacketTextureSet(CKDWORD activeTextureCount,
                                       const CKFFRenderPacketTextureBinding *textures);
CKDWORD CKFFHashRenderPacketTextureSet(const CKRenderPacket &packet);

#endif // CKFFRENDERPACKET_H
