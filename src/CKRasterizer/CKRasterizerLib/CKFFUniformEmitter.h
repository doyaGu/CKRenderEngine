#ifndef CKFFUNIFORMEMITTER_H
#define CKFFUNIFORMEMITTER_H

#include "CKFFDrawProbes.h"
#include "CKFFDrawTypes.h"
#include "CKFFStateStore.h"

class CKDrawStateCache;
class CKRasterizerBackend;

class CKFFUniformEmitter {
public:
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    CKFFUniformEmitter(CKFFStateStore &state,
                       const CKDrawStateCache &drawState,
                       CKFFShaderCache &shaderCache,
                       CKFFDrawProbes &probes);
#else
    CKFFUniformEmitter(CKFFStateStore &state,
                       const CKDrawStateCache &drawState,
                       CKFFShaderCache &shaderCache);
#endif

    // Pushes every constant block of the draw; FALSE when the backend refused
    // an upload (the draw is then rejected).
    CKBOOL UploadUniforms(CKRasterizerBackend *backend,
                          const CKFFProgramContext *programContext,
                          CKDWORD activeTextureCount);

private:
    CKBOOL UploadObjectUniforms(CKRasterizerBackend *backend,
                                const CKFFProgramContext *programContext,
                                CKDWORD activeTextureCount);
    CKBOOL UploadStaticUniforms(CKRasterizerBackend *backend,
                                const CKFFProgramContext *programContext,
                                CKDWORD activeTextureCount);
    CKBOOL UploadUniform(CKRasterizerBackend *backend, CKBackendConstantBlock block, const void *data,
                         CKDWORD vec4Count);
    CKBOOL Emit(CKFFUniformSink *sink, CKBackendConstantBlock block, const void *data,
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
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    CKFFDrawProbes &m_Probes;
#endif
};

#endif // CKFFUNIFORMEMITTER_H
