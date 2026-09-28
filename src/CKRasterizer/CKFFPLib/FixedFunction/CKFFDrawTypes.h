#ifndef CKFFDRAWTYPES_H
#define CKFFDRAWTYPES_H

#include "CKFFStateDesc.h"
#include "CKFFConstants.h"
#include "CKFFShaderKey.h"
#include "CKFFProgram.h"
#include "CKFFShaderABI.h"
#include "CKRasterizerContextTypes.h"
#include "CKFFPipelineState.h"

class CKDrawStateCache;
class CKFFConstantSet;
struct CKFFStateStore;
struct CKFFLinePatternSpan;

struct CKFFUniformSink {
    CKFFConstantSet *Constants;
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
    CKFFSamplerShaderState ShaderState;
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
    CKFFSamplerLayoutPlan SamplerLayoutPlan;
    CKFFTextureBinding Bindings[CKFF_MAX_TEXTURE_STAGES];
};

enum CKFFDrawSource {
    CKFF_DRAW_PRIMITIVE,
    CKFF_DRAW_VERTEX_BUFFER
};

// Fully resolved FFP output consumed immediately by a concrete rasterizer.
// CPU geometry points into CKFixedFunctionPipeline scratch arrays and remains
// valid until that pipeline prepares another draw.
struct CKFFDraw {
    CKFFPipelineState Pipeline;
    CKFFTextureBindingSet Textures;
    const CKFFConstantSet *Constants;
    const char *Marker;
    CKFFShaderKey ShaderKey;
    CKFFFragmentProgram FragmentProgram;
    CKDWORD VertexFormat;
    CKDWORD VertexBuffer;
    const CKBYTE *Vertices;
    CKDWORD VertexStride;
    CKDWORD StartVertex;
    CKDWORD VertexCount;
    CKDWORD IndexBuffer;
    const CKBYTE *Indices;
    CKBOOL Index32;
    CKDWORD StartIndex;
    CKDWORD IndexCount;
    CKDWORD SortKey;
    const CKFFLinePatternSpan *LinePatternSpans;
    CKDWORD LinePatternSpanCount;
    CKFFDrawSource Source;
    CKBOOL SkipSubmit;

    CKFFDraw()
    {
        ResetSubmissionFields();
    }

    void ResetSubmissionFields()
    {
        Constants = NULL;
        Marker = NULL;
        VertexFormat = 0;
        VertexBuffer = 0;
        Vertices = NULL;
        VertexStride = 0;
        StartVertex = 0;
        VertexCount = 0;
        IndexBuffer = 0;
        Indices = NULL;
        Index32 = FALSE;
        StartIndex = 0;
        IndexCount = 0;
        SortKey = 0;
        LinePatternSpans = NULL;
        LinePatternSpanCount = 0;
        Source = CKFF_DRAW_PRIMITIVE;
        SkipSubmit = FALSE;
    }
};

struct CKFFUniformEmissionContext {
    CKFFUniformSink *Uniforms;
    const CKFFProgramContext *ProgramContext;
    const CKFFTextureBindingSet *Textures;
    CKDWORD ActiveTextureCount;
    CKBOOL PositionT;
    CKBOOL LightingEnabled;
    CKBOOL FogEnabled;
    CKDWORD VertexFogMode;
    CKDWORD PixelFogMode;
    CKBOOL PolygonDepthBias;
    CKBOOL PatternedLines;
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
    set->SamplerLayoutPlan = CKFFSamplerLayoutPlan();
    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        set->Bindings[stage].Stage = stage;
        set->Bindings[stage].Texture = 0;
        set->Bindings[stage].TextureFlags = 0;
        set->Bindings[stage].Sampler = CKSamplerDesc();
        set->Bindings[stage].ShaderState = CKFFSamplerShaderState();
    }
}

float CKFFComputeDepthKey(const CKFFStateStore &state, const CKDrawStateCache &drawState);
CKDWORD CKFFEncodeDepthKey(float depth);
CKBOOL CKFFDrawStateEquals(const CKDrawState &a, const CKDrawState &b);
CKDWORD CKFFHashBytes(const void *data, CKDWORD size, CKDWORD hash);
// Texture flags that select the sampler type (and therefore the program).
CKDWORD CKFFStaticTextureFlags(CKDWORD flags);
CKDWORD CKFFHashTextureBindingSet(CKDWORD activeTextureCount, const CKFFTextureBinding *textures);
CKFFFragmentSamplingMode CKFFResolveFragmentSamplingMode(
    const CKFFTextureBindingSet &textures);

inline CKFFShaderKey CKFFBuildShaderKeyFromPreparedState(const CKFFPreparedState *prepared)
{
    if (!prepared)
        return CKFFShaderKey();
    return CKFFBuildShaderKey(prepared->StateDesc, prepared->TextureBoundMask);
}

#endif // CKFFDRAWTYPES_H
