#ifndef CKFIXEDFUNCTIONPIPELINE_H
#define CKFIXEDFUNCTIONPIPELINE_H

#include "VxMath.h"
#include "CKRenderEngineTypes.h"
#include "CKRenderEngineEnums.h"
#include "CKRasterizerEnums.h"
#include "CKRasterizerTypes.h"
#include "CKFFStateDesc.h"
#include "CKFFShaderKey.h"
#include "CKFFDebug.h"
#include "CKFFDrawProbes.h"
#include "CKFFStageState.h"
#include "CKFFConstants.h"
#include "CKFFDrawTypes.h"
#include "CKFFStateStore.h"
#include "CKFFTextureBinder.h"
#include "CKFFUniformEmitter.h"
#include "CKFFOpaquePacketCoordinator.h"
#include "CKFFRenderPacketReplay.h"
#include "CKFFShaderCache.h"
#include "CKDrawStateCache.h"
#include "CKVertexLayoutCache.h"
#include "CKTransientGeometry.h"
#include "CKRenderPipeline.h"
#include "CKFrustumCuller.h"

#ifndef CKRE_ENABLE_TEST_ACCESS
#define CKRE_ENABLE_TEST_ACCESS 0
#endif

class CKRasterizerContext;
class CKRasterizerEncoder;

struct CKLightData;

struct CKFFPipelineTestAccess;

enum CKFFDrawRejectReason {
    CKFF_DRAW_REJECT_NONE = 0,
    CKFF_DRAW_REJECT_INVALID_INPUT,
    CKFF_DRAW_REJECT_PREPARE_FAILED,
    CKFF_DRAW_REJECT_PROGRAM_MISSING,
    CKFF_DRAW_REJECT_STENCIL_WRITE_MASK,
    CKFF_DRAW_REJECT_VERTEX_TWEEN,
    CKFF_DRAW_REJECT_VERTEX_BLEND_INPUT,
    CKFF_DRAW_REJECT_VERTEX_BLEND_PALETTE,
    CKFF_DRAW_REJECT_AFFINE_TEXCOORD,
    CKFF_DRAW_REJECT_TEXTURE_OP,
    CKFF_DRAW_REJECT_RENDER_TARGET_TYPE,
    CKFF_DRAW_REJECT_BORDER_PALETTE,
    CKFF_DRAW_REJECT_DEPTH_COMPARE_FILTER,
    CKFF_DRAW_REJECT_DITHER,
    CKFF_DRAW_REJECT_ZBIAS,
    CKFF_DRAW_REJECT_LINE_PATTERN,
    CKFF_DRAW_REJECT_EDGE_ANTIALIAS,
    CKFF_DRAW_REJECT_CLIPPING_DISABLED,
    CKFF_DRAW_REJECT_STAGE_BLEND,
    CKFF_DRAW_REJECT_SAMPLER_LOD_CONTROL,
    CKFF_DRAW_REJECT_SAMPLER_ANISOTROPY_LIMIT,
    CKFF_DRAW_REJECT_SAMPLER_LAYOUT,
    CKFF_DRAW_REJECT_STATE_VALUE,
    CKFF_DRAW_REJECT_ENCODER_ERROR,
    CKFF_DRAW_REJECT_POINT_VERTEX_BUFFER,
    CKFF_DRAW_REJECT_COUNT
};

class CKFixedFunctionPipeline {
public:
    CKFixedFunctionPipeline();
    ~CKFixedFunctionPipeline();

    bool Init(CKRasterizerContext *ctx);
    CKERROR PrepareShutdown();
    CKERROR Shutdown();
    void SetRenderOptions(CKBOOL DisableTextureFiltering, CKBOOL DisableMipmaps,
                          CKBOOL ForceAnisotropicFiltering = FALSE);

    // === State tracking ===
    void SetRenderState(VXRENDERSTATETYPE state, CKDWORD value);
    CKDWORD GetRenderState(VXRENDERSTATETYPE state) const;
    void SetColorWriteMask(CKBOOL r, CKBOOL g, CKBOOL b, CKBOOL a);
    CKDWORD GetColorWriteMask() const;
    void SetColorWriteMask(CKDWORD mask);
    void ResetTextureStage(int stage);
    void DisableTextureStagesFrom(int firstStage);
    void SaveTextureStage(int stage, CKFFTextureStageSnapshot &snapshot) const;
    void RestoreTextureStage(int stage, const CKFFTextureStageSnapshot &snapshot);
    void SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type, CKDWORD value);
    CKDWORD GetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type) const;
    void SetTransform(VXMATRIX_TYPE type, const VxMatrix &matrix);
    void ResetMaterial();
    void SetMaterial(const CKMaterialData *mat);
    void SetLight(int index, const CKLightData *light);
    void EnableLight(int index, CKBOOL enable);
    void SetTexture(int stage, CKDWORD textureHandle);
    void SetTexture(int stage, CKDWORD textureHandle, CKDWORD textureFlags);
    CKDWORD GetTexture(int stage) const;
    void SetViewport(const CKViewportData &viewport);
    void SetUserClipPlane(int index, const VxPlane &plane);
    void SetAlphaTestPrecision(CKDWORD precision);
    CKDWORD GetAlphaTestPrecision() const;
    CKBOOL SetVertexBlendMatrix(CKDWORD index, const VxMatrix &matrix);
    void ResetVertexBlendMatrices();
    void SetTexcoordComponentCount(CKDWORD stage, CKDWORD count);
    void ResetTexcoordComponentCounts();
    void BeginDebugFrame();

    // === Drawing ===
    // Draw using VxDrawPrimitiveData (software vertex path)
    CKBOOL DrawPrimitive(CKRasterizerEncoder *encoder, CKRenderView view,
                         VXPRIMITIVETYPE type, CKWORD *indices, int indexCount,
                         VxDrawPrimitiveData *data);

    // Draw using persistent vertex/index buffer handles
    CKBOOL DrawVertexBuffer(CKRasterizerEncoder *encoder, CKRenderView view,
                            VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
                            CKDWORD baseVertex, CKDWORD vertexCount,
                            CKDWORD startIndex, CKDWORD indexCount,
                            CKDWORD dpFlags, CKDWORD formatFlags,
                            CKDWORD vertexLayout);
    CKFFDrawRejectReason GetLastDrawRejectReason() const { return m_LastDrawRejectReason; }
    CKDWORD GetRejectedDrawCount(CKFFDrawRejectReason reason) const {
        return reason > CKFF_DRAW_REJECT_NONE && reason < CKFF_DRAW_REJECT_COUNT
            ? m_DrawRejectCounts[reason]
            : 0;
    }

    CKBOOL HasOpaqueRenderPackets() const { return m_OpaquePackets.HasPackets(); }
    void FlushOpaqueRenderPackets(CKRasterizerEncoder *encoder = nullptr,
                                  CKBOOL forceDirectReplay = FALSE,
                                  CKBOOL allowAdaptiveLearning = TRUE);
    void SetOpaqueSortingEnabled(CKBOOL enabled);
    void SetOpaqueInstancingEnabled(CKBOOL enabled) { m_OpaquePackets.SetInstancingEnabled(enabled); }
    void SetOpaqueRenderPacketsAllowed(CKBOOL allowed) { m_OpaquePackets.SetPacketsAllowed(allowed); }
    CKBOOL GetOpaqueRenderPacketsAllowed() const { return m_OpaquePackets.PacketsAllowed(); }
    CKDWORD GetOpaquePacketAdaptiveSamples() const { return m_OpaquePackets.GetAdaptiveSamples(); }
    CKDWORD GetOpaquePacketAdaptiveBypasses() const { return m_OpaquePackets.GetAdaptiveBypasses(); }
    CKDWORD GetOpaquePacketAdaptiveSavedBindEstimate() const { return m_OpaquePackets.GetAdaptiveSavedBindEstimate(); }
    CKDWORD GetOpaquePacketAdaptiveRunBypasses() const { return m_OpaquePackets.GetAdaptiveRunBypasses(); }
    CKDWORD GetOpaquePacketAdaptiveSampleRuns() const { return m_OpaquePackets.GetAdaptiveSampleRuns(); }
    CKDWORD GetOpaquePacketAdaptiveSampleMaxRun() const { return m_OpaquePackets.GetAdaptiveSampleMaxRun(); }
    CKDWORD GetOpaquePacketAdaptiveSubmitSavedEstimate() const { return m_OpaquePackets.GetAdaptiveSubmitSavedEstimate(); }
    CKDWORD GetOpaquePacketAdaptiveCooldownBypasses() const { return m_OpaquePackets.GetAdaptiveCooldownBypasses(); }
    CKDWORD GetOpaquePacketAdaptiveCooldownFrames() const { return m_OpaquePackets.GetAdaptiveCooldownFrames(); }
    CKDWORD GetOpaquePacketAdaptiveFrameEndEvaluations() const { return m_OpaquePackets.GetAdaptiveFrameEndEvaluations(); }
    CKDWORD GetOpaquePacketAdaptiveFrameEndRunBypasses() const { return m_OpaquePackets.GetAdaptiveFrameEndRunBypasses(); }

    // === Subsystem access ===
    CKDrawStateCache &GetDrawStateCache() { return m_DrawStateCache; }
    const CKDrawStateCache &GetDrawStateCache() const { return m_DrawStateCache; }
    CKVertexLayoutCache &GetVertexLayoutCache() { return m_VertexLayoutCache; }
    CKTransientGeometry &GetTransientGeometry() { return m_TransientGeometry; }
    CKFFShaderCache &GetShaderCache() { return m_ShaderCache; }
    CKRenderPipeline &GetRenderPipeline() { return m_RenderPipeline; }
    CKFrustumCuller &GetFrustumCuller() { return m_FrustumCuller; }
    CKRasterizerContext *GetContext() const { return m_Context; }
    const CKFFStateStore &GetStateStore() const { return m_State; }
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    CKFFDrawProbes &GetProbes() { return m_Probes; }
#endif

    // === Matrix access ===
    const VxMatrix &GetWorldMatrix() const { return m_State.World; }
    const VxMatrix &GetViewMatrix() const { return m_State.View; }
    const VxMatrix &GetProjectionMatrix() const { return m_State.Projection; }
    CKSamplerDesc BuildSamplerDesc(int stage) const;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const CKFFFrameStats &GetFrameStats() const { return m_Probes.Stats; }
#else
    const CKFFFrameStats &GetFrameStats() const;
#endif

    // === Packet-build support ===
    void BuildCurrentPreparedState(CKFFPreparedState *prepared, CKDWORD dpFlags, CKDWORD activeTextureCount,
                                   CKDWORD formatFlags = 0,
                                   const CKBYTE *texcoordComponentCounts = nullptr,
                                   CKBOOL pointSprite = FALSE);
    CKBOOL BuildCurrentTextureBindingSet(CKFFTextureBindingSet *bindingSet,
                                         CKDWORD activeTextureCount,
                                         const CKFFShaderKey &shaderKey);
    CKBOOL ValidateProgramSupport(const CKFFShaderKey &shaderKey);
    CKBOOL BuildStaticUniformPayload(CKFFRenderPacketUniformPayload *payload,
                                     const CKFFProgramContext *programContext,
                                     CKDWORD activeTextureCount);
    CKBOOL BuildPacketObjectUniforms(CKRenderPacketObjectUniforms *uniforms,
                                     const CKFFProgramContext *programContext);
    void UpdateViewProjectionCache();
    float ComputeDepthKey() const;
    CKBOOL SubmitVertexBufferImmediate(CKRasterizerEncoder *encoder, CKRenderView view,
                                       VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
                                       CKDWORD baseVertex, CKDWORD vertexCount,
                                       CKDWORD startIndex, CKDWORD indexCount,
                                       CKDWORD dpFlags, CKDWORD formatFlags,
                                       CKDWORD vertexLayout);

private:
#if CKRE_ENABLE_TEST_ACCESS
    friend struct CKFFPipelineTestAccess;
#endif

    enum CKFFStateChange { CKFF_CHANGE_STATIC_UNIFORM = 0x1, CKFF_CHANGE_PROGRAM = 0x2 };
    enum CKFFSubmitSource { CKFF_SUBMIT_PRIMITIVE, CKFF_SUBMIT_VERTEX_BUFFER };

    struct CKFFDrawSubmission {
        CKRenderView View;
        VXPRIMITIVETYPE DrawStateType;
        const CKFFProgramContext *ProgramContext;
        const CKFFTextureBindingSet *Textures;
        CKDWORD VertexBuffer;
        CKDWORD IndexBuffer;
        CKDWORD BaseVertex;
        CKDWORD VertexCount;
        CKDWORD StartIndex;
        CKDWORD IndexCount;
        CKDWORD VertexLayout;
        CKFFSubmitSource Source;
    };

    CKRasterizerContext *m_Context;
    CKBOOL m_DisableTextureFiltering;
    CKBOOL m_DisableMipmaps;
    CKBOOL m_ForceAnisotropicFiltering;

    // Subsystems
    CKFFShaderCache m_ShaderCache;
    CKDrawStateCache m_DrawStateCache;
    CKVertexLayoutCache m_VertexLayoutCache;
    CKTransientGeometry m_TransientGeometry;
    CKRenderPipeline m_RenderPipeline;
    CKFrustumCuller m_FrustumCuller;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    CKFFDebugState m_DebugState;
#endif
    CKFFStateStore m_State;

#if CKRE_ENABLE_FFP_DIAGNOSTICS
    CKFFDrawProbes m_Probes;
#endif
    CKFFTextureBinder m_TextureBinder;
    CKFFUniformEmitter m_UniformEmitter;
    CKFFOpaquePacketCoordinator m_OpaquePackets;
    CKFFDrawRejectReason m_LastDrawRejectReason;
    CKDWORD m_DrawRejectCounts[CKFF_DRAW_REJECT_COUNT];
    CKDWORD m_BorderPaletteColors[16];
    CKDWORD m_BorderPaletteCount;
    CKDWORD m_BorderPaletteFrameSerial;

    // Internal methods
    void OnFixedFunctionStateChanged(CKDWORD changeMask);
    void MarkStaticUniformsDirty();
    void MarkPacketProgramDirty();
    CKBOOL ValidateDrawState(CKDWORD formatFlags, CKDWORD activeTextureCount);
    CKBOOL ValidateVertexBlendIndices(const VxDrawPrimitiveData *data,
                                      CKDWORD formatFlags);
    CKBOOL RecordDrawReject(CKFFDrawRejectReason reason);
    CKBOOL SubmitPrepared(CKRasterizerEncoder *encoder, const CKFFDrawSubmission &submission);
    void BindTextures(CKRasterizerEncoder *encoder, const CKFFTextureBindingSet *bindingSet);
    CKDWORD SubmitDiscardFlags() const;
    void LogAndResetFrameStats();

};

class CKFFStateGuard {
public:
    explicit CKFFStateGuard(CKFixedFunctionPipeline &pipeline);
    ~CKFFStateGuard();

    CKFFStateGuard(const CKFFStateGuard &) = delete;
    CKFFStateGuard &operator=(const CKFFStateGuard &) = delete;

    void Restore();
    void Dismiss();

private:
    CKFixedFunctionPipeline *m_Pipeline;
    CKDWORD m_RenderStates[CKFF_RS_COUNT];
    CKDWORD m_ColorWriteMask;
    VxMatrix m_World;
    VxMatrix m_View;
    VxMatrix m_Projection;
    CKFFTextureStageSnapshot m_TextureStages[CKFF_MAX_TEXTURE_STAGES];
};

class CKFFRenderStateGuard {
public:
    CKFFRenderStateGuard(CKFixedFunctionPipeline &pipeline, VXRENDERSTATETYPE state, CKBOOL active = TRUE);
    ~CKFFRenderStateGuard();

    CKFFRenderStateGuard(const CKFFRenderStateGuard &) = delete;
    CKFFRenderStateGuard &operator=(const CKFFRenderStateGuard &) = delete;

    void Restore();
    void Dismiss();

private:
    CKFixedFunctionPipeline *m_Pipeline;
    VXRENDERSTATETYPE m_State;
    CKDWORD m_Value;
};

class CKFFOpaquePacketGuard {
public:
    CKFFOpaquePacketGuard(CKFixedFunctionPipeline &pipeline, CKBOOL active = TRUE);
    ~CKFFOpaquePacketGuard();

    CKFFOpaquePacketGuard(const CKFFOpaquePacketGuard &) = delete;
    CKFFOpaquePacketGuard &operator=(const CKFFOpaquePacketGuard &) = delete;

    void Restore();
    void Dismiss();

private:
    CKFixedFunctionPipeline *m_Pipeline;
    CKBOOL m_SavedAllowed;
};

#endif // CKFIXEDFUNCTIONPIPELINE_H
