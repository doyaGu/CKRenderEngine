#ifndef CKFFUNIFORMEMITTER_H
#define CKFFUNIFORMEMITTER_H

#include "CKFFDrawProbes.h"
#include "CKFFDrawTypes.h"
#include "CKFFStateStore.h"

class CKDrawStateCache;
class CKFFTextureBinder;

class CKFFUniformEmitter {
public:
    CKFFUniformEmitter(CKFFStateStore &state,
                       const CKDrawStateCache &drawState,
                       const CKFFTextureBinder &textureBinder,
                       const CKDWORD &shaderTargetFlags,
                       CKFFDrawProbes &probes);

    // Prepares producer-owned blocks for this draw, retaining unchanged revisions.
    CKBOOL UploadUniforms(CKFFConstantSet *constants,
                          const CKFFProgramContext *programContext,
                          CKDWORD activeTextureCount,
                          uint64_t staticUniformRevision);
    CKBOOL UploadObjectUniforms(CKFFConstantSet *constants, const CKFFProgramContext *programContext, CKDWORD activeTextureCount);
    CKBOOL UploadStaticUniforms(CKFFConstantSet *constants, const CKFFProgramContext *programContext, CKDWORD activeTextureCount);
    void ResetCache();

private:
    CKBOOL UploadUniform(CKFFConstantSet *constants, CKFFConstantBlock block, const void *data,
                         CKDWORD vec4Count);
    CKBOOL Emit(CKFFUniformSink *sink, CKFFConstantBlock block, const void *data,
                CKDWORD count, CKDWORD vec4Count, CKBOOL objectUniform);
    void EmitPayloads(CKFFUniformSink *sink,
                      const CKFFProgramContext *programContext,
                      CKDWORD activeTextureCount);
    void EmitObjectMatrixUniforms(const CKFFUniformEmissionContext *context);
    void EmitTextureMatrixUniforms(const CKFFUniformEmissionContext *context);
    void EmitStageAndSpecUniforms(const CKFFUniformEmissionContext *context);
    void EmitClipPlaneUniforms(const CKFFUniformEmissionContext *context);
    CKBOOL RenderTargetOriginFlip() const;

    CKFFStateStore &m_State;
    const CKDrawStateCache &m_DrawState;
    const CKFFTextureBinder &m_TextureBinder;
    const CKDWORD &m_ShaderTargetFlags;
    CKFFDrawProbes &m_Probes;
    CKBOOL m_StaticUniformCacheValid;
    uint64_t m_LastStaticConstantsIdentity;
    uint64_t m_LastStaticUniformRevision;
    CKDWORD m_LastStaticActiveTextureCount;
    CKFFShaderKey m_LastStaticShaderKey;
    CKFFSpecializationInfo m_LastStaticSpecialization;
};

#endif // CKFFUNIFORMEMITTER_H
