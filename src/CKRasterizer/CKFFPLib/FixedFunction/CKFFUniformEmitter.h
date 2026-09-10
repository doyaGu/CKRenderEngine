#ifndef CKFFUNIFORMEMITTER_H
#define CKFFUNIFORMEMITTER_H

#include "CKFFDrawProbes.h"
#include "CKFFDrawTypes.h"
#include "CKFFStateStore.h"

class CKDrawStateCache;
class CKRasterizerBackend;

class CKFFUniformEmitter {
public:
    CKFFUniformEmitter(CKFFStateStore &state,
                       const CKDrawStateCache &drawState,
                       CKFFShaderCache &shaderCache,
                       CKFFDrawProbes &probes);

    // Prepares producer-owned blocks for this draw, retaining unchanged revisions.
    CKBOOL UploadUniforms(CKBackendConstants *constants,
                          const CKFFProgramContext *programContext,
                          CKDWORD activeTextureCount);

private:
    CKBOOL UploadObjectUniforms(CKBackendConstants *constants,
                                const CKFFProgramContext *programContext,
                                CKDWORD activeTextureCount);
    CKBOOL UploadStaticUniforms(CKBackendConstants *constants,
                                const CKFFProgramContext *programContext,
                                CKDWORD activeTextureCount);
    CKBOOL UploadUniform(CKBackendConstants *constants, CKFFConstantBlock block, const void *data,
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
    CKFFShaderCache &m_ShaderCache;
    CKFFDrawProbes &m_Probes;
};

#endif // CKFFUNIFORMEMITTER_H
