#ifndef CKFFUNIFORMEMITTER_H
#define CKFFUNIFORMEMITTER_H

#include "CKFFDrawProbes.h"
#include "CKFFDrawTypes.h"
#include "CKFFStateStore.h"

class CKDrawStateCache;

class CKFFUniformEmitter {
public:
    CKFFUniformEmitter(CKFFStateStore &state,
                       const CKDrawStateCache &drawState,
                       const CKDWORD &shaderTargetFlags,
                       CKFFDrawProbes &probes);

    // Prepares producer-owned blocks for this draw, retaining unchanged revisions.
    CKBOOL UploadUniforms(CKFFConstantSet *constants,
                          const CKFFProgramContext *programContext,
                          const CKFFTextureBindingSet &textures,
                          uint64_t objectUniformRevision,
                          uint64_t staticUniformRevision,
                          CKBOOL polygonDepthBias = TRUE,
                          CKBOOL patternedLines = FALSE);
    CKBOOL UploadObjectUniforms(CKFFConstantSet *constants, const CKFFProgramContext *programContext, CKDWORD activeTextureCount);
    CKBOOL UploadStaticUniforms(CKFFConstantSet *constants, const CKFFProgramContext *programContext,
                                const CKFFTextureBindingSet &textures, CKBOOL polygonDepthBias = TRUE,
                                CKBOOL patternedLines = FALSE);
    void ResetCache();

private:
    CKBOOL UploadUniform(CKFFConstantSet *constants, CKFFConstantBlock block, const void *data,
                         CKDWORD vec4Count);
    CKBOOL Emit(CKFFUniformSink *sink, CKFFConstantBlock block, const void *data,
                CKDWORD count, CKDWORD vec4Count, CKBOOL objectUniform);
    void EmitPayloads(CKFFUniformSink *sink,
                      const CKFFProgramContext *programContext,
                      const CKFFTextureBindingSet *textures,
                      CKDWORD activeTextureCount,
                      CKBOOL polygonDepthBias,
                      CKBOOL patternedLines);
    void EmitObjectMatrixUniforms(const CKFFUniformEmissionContext *context);
    void EmitTextureMatrixUniforms(const CKFFUniformEmissionContext *context);
    void EmitStageAndFragmentProgramUniforms(const CKFFUniformEmissionContext *context);
    void EmitClipPlaneUniforms(const CKFFUniformEmissionContext *context);
    CKBOOL RenderTargetOriginFlip() const;

    CKFFStateStore &m_State;
    const CKDrawStateCache &m_DrawState;
    const CKDWORD &m_ShaderTargetFlags;
    CKFFDrawProbes &m_Probes;
    CKBOOL m_ObjectUniformCacheValid;
    uint64_t m_LastObjectConstantsIdentity;
    uint64_t m_LastObjectUniformRevision;
    CKDWORD m_LastObjectProgramKey;
    CKBOOL m_StaticUniformCacheValid;
    uint64_t m_LastStaticConstantsIdentity;
    uint64_t m_LastStaticUniformRevision;
    CKDWORD m_LastStaticTextureBindingHash;
    CKDWORD m_LastStaticActiveTextureCount;
    CKBOOL m_LastStaticPolygonDepthBias;
    CKBOOL m_LastStaticPatternedLines;
    CKFFShaderKey m_LastStaticShaderKey;
    CKFFFragmentProgram m_LastStaticFragmentProgram;
};

#endif // CKFFUNIFORMEMITTER_H
