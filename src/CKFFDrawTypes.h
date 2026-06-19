#ifndef CKFFDRAWTYPES_H
#define CKFFDRAWTYPES_H

#include "CKFFStateDesc.h"
#include "CKFFShaderKey.h"
#include "CKFFRenderPacket.h"
#include "CKFFShaderCache.h"
#include "CKRasterizerTypes.h"

class CKDrawStateCache;
class CKRasterizerEncoder;
struct CKFFStateStore;

struct CKFFUniformSink {
    CKRasterizerEncoder *Encoder;
    CKFFRenderPacketUniformPayload *StaticPayload;
    CKFFRenderPacketUniformPayload *ObjectPayload;
    CKBOOL EmitStatic;
    CKBOOL EmitObject;
    CKBOOL Failed;
};

struct CKFFPreparedState {
    CKFFStateDesc StateDesc;
    CKDWORD ActiveTextureCount;
    CKBOOL PositionT;
    CKBOOL LightingEnabled;
    float MaterialSource[4];
    CKDWORD TextureBoundMask;
};

struct CKFFTextureBindingSet {
    CKDWORD ActiveTextureCount;
    CKDWORD Hash;
    CKFFRenderPacketTextureBinding Bindings[CKFF_MAX_TEXTURE_STAGES];
};

struct CKFFUniformEmissionContext {
    CKFFUniformSink *Uniforms;
    const CKFFProgramContext *ProgramContext;
    CKFFShaderKey ShaderKey;
    CKFFSpecializationInfo Specialization;
    CKDWORD ActiveTextureCount;
    CKBOOL FullSpecialized;
    CKBOOL PositionT;
    CKBOOL LightingEnabled;
    CKBOOL FogEnabled;
    CKDWORD VertexFogMode;
    CKDWORD PixelFogMode;
};

struct CKFFVertexBufferPacketBuildResult {
    CKBOOL Success;
    CKDWORD RejectReason;
    CKFFProgramContext ProgramContext;
    CKFFTextureBindingSet TextureBindingSet;
    CKRenderPacket Packet;
};

inline void CKFFInitPreparedState(CKFFPreparedState *prepared)
{
    if (!prepared)
        return;
    prepared->StateDesc = CKFFStateDesc();
    prepared->ActiveTextureCount = 0;
    prepared->PositionT = FALSE;
    prepared->LightingEnabled = FALSE;
    prepared->MaterialSource[0] = (float)CKFF_MS_MATERIAL;
    prepared->MaterialSource[1] = (float)CKFF_MS_MATERIAL;
    prepared->MaterialSource[2] = (float)CKFF_MS_MATERIAL;
    prepared->MaterialSource[3] = (float)CKFF_MS_MATERIAL;
    prepared->TextureBoundMask = 0;
}

inline void CKFFInitTextureBindingSet(CKFFTextureBindingSet *set)
{
    if (!set)
        return;
    set->ActiveTextureCount = 0;
    set->Hash = 0;
    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        set->Bindings[stage].Stage = stage;
        set->Bindings[stage].Uniform = 0;
        set->Bindings[stage].Texture = 0;
        set->Bindings[stage].TextureFlags = 0;
        set->Bindings[stage].Sampler = CKSamplerDesc();
    }
}

float CKFFComputeDepthKey(const CKFFStateStore &state, const CKDrawStateCache &drawState);
CKDWORD CKFFSubmitDiscardFlags(const CKFFStateStore &state, const CKDrawStateCache &drawState);

inline CKFFShaderKey CKFFBuildShaderKeyFromPreparedState(const CKFFPreparedState *prepared)
{
    if (!prepared)
        return CKFFShaderKey();
    return CKFFBuildShaderKey(prepared->StateDesc, prepared->TextureBoundMask);
}

#endif // CKFFDRAWTYPES_H
