#ifndef CKFFDRAWTYPES_H
#define CKFFDRAWTYPES_H

#include "CKFFStateDesc.h"
#include "CKFFShaderKey.h"
#include "CKFFRenderPacket.h"
#include "CKFFShaderCache.h"
#include "CKRasterizerTypes.h"

class CKRasterizerEncoder;

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

#endif // CKFFDRAWTYPES_H
