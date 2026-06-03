#ifndef CKFFUNIFORMEMITTER_H
#define CKFFUNIFORMEMITTER_H

#include "CKFFDrawProbes.h"
#include "CKFFDrawTypes.h"
#include "CKFFStateStore.h"

class CKDrawStateCache;
class CKRasterizerEncoder;

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

    void UploadUniforms(CKRasterizerEncoder *encoder,
                        const CKFFProgramContext *programContext,
                        CKDWORD activeTextureCount);
    CKBOOL BuildStaticUniformPayload(CKFFRenderPacketUniformPayload *payload,
                                     const CKFFProgramContext *programContext,
                                     CKDWORD activeTextureCount);
    CKBOOL BuildObjectUniforms(CKRenderPacketObjectUniforms *uniforms,
                               const CKFFProgramContext *programContext);

private:
    void UploadObjectUniforms(CKRasterizerEncoder *encoder,
                              const CKFFProgramContext *programContext,
                              CKDWORD activeTextureCount);
    void UploadStaticUniforms(CKRasterizerEncoder *encoder,
                              const CKFFProgramContext *programContext,
                              CKDWORD activeTextureCount);
    void UploadUniform(CKRasterizerEncoder *encoder, CKDWORD uniform, const void *data, CKDWORD count);
    CKBOOL Emit(CKFFUniformSink *sink, CKDWORD uniform, const void *data,
                CKDWORD count, CKDWORD vec4Count, CKBOOL objectUniform);
    void EmitPayloads(CKFFUniformSink *sink,
                      const CKFFProgramContext *programContext,
                      CKDWORD activeTextureCount);
    void EmitObjectMatrixUniforms(const CKFFUniformEmissionContext *context);
    void EmitTextureMatrixUniforms(const CKFFUniformEmissionContext *context);
    void EmitStageAndSpecUniforms(const CKFFUniformEmissionContext *context);
    void EmitClipPlaneUniforms(const CKFFUniformEmissionContext *context);

    CKFFStateStore &m_State;
    const CKDrawStateCache &m_DrawState;
    CKFFShaderCache &m_ShaderCache;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    CKFFDrawProbes &m_Probes;
#endif
};

#endif // CKFFUNIFORMEMITTER_H
