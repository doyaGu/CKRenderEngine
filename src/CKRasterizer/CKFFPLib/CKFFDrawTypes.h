#ifndef CKFFDRAWTYPES_H
#define CKFFDRAWTYPES_H

#include "CKFFStateDesc.h"
#include "CKFFShaderKey.h"
#include "CKFFShaderCache.h"
#include "CKRasterizerBackendTypes.h"

class CKDrawStateCache;
class CKBackendConstants;
struct CKFFStateStore;

struct CKFFUniformSink {
    CKBackendConstants *Constants;
    CKBOOL EmitStatic;
    CKBOOL EmitObject;
    CKBOOL Failed;
};

// One texture stage resolved for the backend: the sampler slot it lands on
// (Stage, fixed layout of CKFFShaderABI.h), the texture handle and its
// sampler state.
struct CKFFTextureBinding {
    CKDWORD Stage;          // backend sampler slot
    CKDWORD Texture;
    CKDWORD TextureFlags;
    CKSamplerDesc Sampler;
};

struct CKFFPreparedState {
    CKFFStateDesc StateDesc;
    CKDWORD ActiveTextureCount;
    CKBOOL PositionT;
    CKBOOL LightingEnabled;
    float MaterialSource[4];
    CKDWORD TextureBoundMask;
};

enum CKFFProgramPrepareStatus {
    CKFF_PROGRAM_PREPARE_OK = 0,
    CKFF_PROGRAM_PREPARE_INVALID_INPUT,
    CKFF_PROGRAM_PREPARE_PROGRAM_MISSING
};

struct CKFFProgramPreparation {
    CKFFPreparedState PreparedState;
    CKFFProgramContext ProgramContext;
};

struct CKFFTextureBindingSet {
    CKDWORD ActiveStageCount;
    CKDWORD ActiveTextureCount;
    CKDWORD Hash;
    CKFFTextureBinding Bindings[CKFF_MAX_TEXTURE_STAGES];
};

struct CKFFUniformEmissionContext {
    CKFFUniformSink *Uniforms;
    const CKFFProgramContext *ProgramContext;
    CKFFShaderKey ShaderKey;
    CKFFSpecializationInfo Specialization;
    CKDWORD ActiveTextureCount;
    CKBOOL PositionT;
    CKBOOL LightingEnabled;
    CKBOOL FogEnabled;
    CKDWORD VertexFogMode;
    CKDWORD PixelFogMode;
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
    set->ActiveStageCount = 0;
    set->ActiveTextureCount = 0;
    set->Hash = 0;
    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        set->Bindings[stage].Stage = stage;
        set->Bindings[stage].Texture = 0;
        set->Bindings[stage].TextureFlags = 0;
        set->Bindings[stage].Sampler = CKSamplerDesc();
    }
}

float CKFFComputeDepthKey(const CKFFStateStore &state, const CKDrawStateCache &drawState);
CKDWORD CKFFEncodeDepthKey(float depth);
CKBOOL CKFFDrawStateEquals(const CKDrawState &a, const CKDrawState &b);
CKDWORD CKFFHashBytes(const void *data, CKDWORD size, CKDWORD hash);
// Texture flags that select the sampler type (and therefore the program).
CKDWORD CKFFStaticTextureFlags(CKDWORD flags);
CKDWORD CKFFHashTextureBindingSet(CKDWORD activeTextureCount, const CKFFTextureBinding *textures);

inline CKFFShaderKey CKFFBuildShaderKeyFromPreparedState(const CKFFPreparedState *prepared)
{
    if (!prepared)
        return CKFFShaderKey();
    return CKFFBuildShaderKey(prepared->StateDesc, prepared->TextureBoundMask);
}

#endif // CKFFDRAWTYPES_H
