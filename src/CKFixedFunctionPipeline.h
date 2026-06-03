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

class CKFixedFunctionPipeline {
public:
    CKFixedFunctionPipeline();
    ~CKFixedFunctionPipeline();

    void Init(CKRasterizerContext *ctx);
    void Shutdown();
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
    void SetVertexBlendMatrix(CKDWORD index, const VxMatrix &matrix);
    void ResetVertexBlendMatrices();
    void SetTexcoordComponentCount(CKDWORD stage, CKDWORD count);
    void ResetTexcoordComponentCounts();
    void BeginDebugFrame();

    // === Drawing ===
    // Draw using VxDrawPrimitiveData (software vertex path)
    void DrawPrimitive(CKRasterizerEncoder *encoder, CKRenderView view,
                       VXPRIMITIVETYPE type, CKWORD *indices, int indexCount,
                       VxDrawPrimitiveData *data);

    // Draw using persistent vertex/index buffer handles
    void DrawVertexBuffer(CKRasterizerEncoder *encoder, CKRenderView view,
                          VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
                          CKDWORD baseVertex, CKDWORD vertexCount,
                          CKDWORD startIndex, CKDWORD indexCount,
                          CKDWORD dpFlags, CKDWORD formatFlags,
                          CKDWORD vertexLayout);

    CKBOOL HasOpaqueRenderPackets() const { return m_OpaquePackets.HasPackets(); }
    void FlushOpaqueRenderPackets(CKRasterizerEncoder *encoder = nullptr,
                                  CKBOOL forceDirectReplay = FALSE,
                                  CKBOOL allowAdaptiveLearning = TRUE);
    void SetOpaqueSortingEnabled(CKBOOL enabled);
    void SetOpaqueInstancingEnabled(CKBOOL enabled) { m_OpaquePackets.SetInstancingEnabled(enabled); }
    void SetOpaqueRenderPacketsAllowed(CKBOOL allowed) { m_OpaquePackets.SetPacketsAllowed(allowed); }
    CKBOOL GetOpaqueRenderPacketsAllowed() const { return m_OpaquePackets.PacketsAllowed(); }
    CKDWORD GetOpaquePacketAdaptiveSamples() const { return m_OpaquePackets.Queue().GetAdaptiveSamples(); }
    CKDWORD GetOpaquePacketAdaptiveBypasses() const { return m_OpaquePackets.Queue().GetAdaptiveBypasses(); }
    CKDWORD GetOpaquePacketAdaptiveSavedBindEstimate() const { return m_OpaquePackets.Queue().GetAdaptiveSavedBindEstimate(); }
    CKDWORD GetOpaquePacketAdaptiveRunBypasses() const { return m_OpaquePackets.Queue().GetAdaptiveRunBypasses(); }
    CKDWORD GetOpaquePacketAdaptiveSampleRuns() const { return m_OpaquePackets.Queue().GetAdaptiveSampleRuns(); }
    CKDWORD GetOpaquePacketAdaptiveSampleMaxRun() const { return m_OpaquePackets.Queue().GetAdaptiveSampleMaxRun(); }
    CKDWORD GetOpaquePacketAdaptiveSubmitSavedEstimate() const { return m_OpaquePackets.Queue().GetAdaptiveSubmitSavedEstimate(); }
    CKDWORD GetOpaquePacketAdaptiveCooldownBypasses() const { return m_OpaquePackets.Queue().GetAdaptiveCooldownBypasses(); }
    CKDWORD GetOpaquePacketAdaptiveCooldownFrames() const { return m_OpaquePackets.Queue().GetAdaptiveCooldownFrames(); }
    CKDWORD GetOpaquePacketAdaptiveFrameEndEvaluations() const { return m_OpaquePackets.Queue().GetAdaptiveFrameEndEvaluations(); }
    CKDWORD GetOpaquePacketAdaptiveFrameEndRunBypasses() const { return m_OpaquePackets.Queue().GetAdaptiveFrameEndRunBypasses(); }

    // === Subsystem access ===
    CKDrawStateCache &GetDrawStateCache() { return m_DrawStateCache; }
    CKVertexLayoutCache &GetVertexLayoutCache() { return m_VertexLayoutCache; }
    CKTransientGeometry &GetTransientGeometry() { return m_TransientGeometry; }
    CKFFShaderCache &GetShaderCache() { return m_ShaderCache; }
    CKRenderPipeline &GetRenderPipeline() { return m_RenderPipeline; }
    CKFrustumCuller &GetFrustumCuller() { return m_FrustumCuller; }

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

private:
#if CKRE_ENABLE_TEST_ACCESS
    friend struct CKFFPipelineTestAccess;
#endif

    enum CKFFStateChange { CKFF_CHANGE_STATIC_UNIFORM = 0x1, CKFF_CHANGE_PROGRAM = 0x2 };

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

    // Internal methods
    void BuildCurrentPreparedState(CKFFPreparedState *prepared, CKDWORD dpFlags, CKDWORD activeTextureCount,
                                   CKDWORD formatFlags = 0,
                                   const CKBYTE *texcoordComponentCounts = nullptr);
    void OnFixedFunctionStateChanged(CKDWORD changeMask);
    void MarkStaticUniformsDirty();
    void MarkPacketProgramDirty();
    void BuildCurrentTextureBindingSet(CKFFTextureBindingSet *bindingSet, CKDWORD activeTextureCount);
    CKBOOL BuildStaticUniformPayload(CKFFRenderPacketUniformPayload *payload,
                                     const CKFFProgramContext *programContext,
                                     CKDWORD activeTextureCount);
    CKBOOL BuildPacketObjectUniforms(CKRenderPacketObjectUniforms *uniforms,
                                     const CKFFProgramContext *programContext);
    CKDWORD GetPacketObjectUniformRejectReason(const CKFFProgramContext *programContext) const;
    void UpdateViewProjectionCache();
    void BindTextures(CKRasterizerEncoder *encoder, const CKFFTextureBindingSet *bindingSet);
    CKDWORD SubmitDiscardFlags() const;
    void LogAndResetFrameStats();
    float ComputeDepthKey() const;
    CKBOOL ResolveVertexBufferPacketProgram(CKDWORD dpFlags,
                                            CKDWORD formatFlags,
                                            CKFFPreparedState *preparedState,
                                            CKFFProgramContext *programContext);
    void CaptureVertexBufferPacketIdentity(CKRenderPacket *packet,
                                           const CKFFProgramContext *programContext,
                                           CKRenderView view,
                                           VXPRIMITIVETYPE type,
                                           CKDWORD vb,
                                           CKDWORD ib,
                                           CKDWORD baseVertex,
                                           CKDWORD vertexCount,
                                           CKDWORD startIndex,
                                           CKDWORD indexCount,
                                           CKDWORD vertexLayout);
    void CaptureVertexBufferPacketTextures(CKRenderPacket *packet,
                                           const CKFFTextureBindingSet *bindingSet);
    CKBOOL CaptureVertexBufferPacketObjectUniforms(CKRenderPacket *packet,
                                                   const CKFFProgramContext *programContext);
    void CaptureVertexBufferPacketInstancing(CKRenderPacket *packet,
                                             const CKFFProgramContext *programContext);
    CKBOOL CaptureVertexBufferPacketStaticUniforms(CKRenderPacket *packet,
                                                   const CKFFProgramContext *programContext,
                                                   CKBOOL collectStats);
    CKDWORD GetOpaqueVertexBufferPacketRejectReason(CKRenderView view, VXPRIMITIVETYPE type,
                                                    CKDWORD vb, CKDWORD ib,
                                                    CKDWORD vertexLayout) const;
    void BuildVertexBufferPacket(CKFFVertexBufferPacketBuildResult *result,
                                   CKRasterizerEncoder *encoder,
                                   CKRenderView view,
                                   VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
                                   CKDWORD baseVertex, CKDWORD vertexCount,
                                   CKDWORD startIndex, CKDWORD indexCount,
                                   CKDWORD dpFlags, CKDWORD formatFlags,
                                   CKDWORD vertexLayout);
    void SubmitVertexBufferPacketImmediate(CKRasterizerEncoder *encoder, CKRenderView view,
                                           VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
                                           CKDWORD baseVertex, CKDWORD vertexCount,
                                           CKDWORD startIndex, CKDWORD indexCount,
                                           CKDWORD dpFlags, CKDWORD formatFlags,
                                           CKDWORD vertexLayout);
    CKDWORD GetVertexBufferPacketInstancingRejectReason(const CKFFProgramContext *programContext) const;
    void SortOpaqueRenderPackets(XArray<CKDWORD> &indices);
    void InitRenderPacketReplayContext(CKFFRenderPacketReplayContext *context,
                                       CKRasterizerEncoder *encoder);
    void ClearOpaqueRenderPackets();
    void ResetOpaqueRenderPacketFrameState();
    CKDWORD InternStaticUniformPayload(const CKFFRenderPacketUniformPayload &payload);
    void BuildRenderPacketSortKey(CKRenderPacket *packet) const;
    void InitVertexBufferPacketForCapture(CKRenderPacket *packet) const;
    void TrackOpaqueRenderPacket(const CKRenderPacket &packet);
    void TrackOpaqueRenderPacketReject(CKDWORD rejectReason);
    void UpdateOpaqueRenderPacketAdaptiveStats();
    CKBOOL CheckOpaqueRenderPacketAdaptiveBypass(CKRasterizerEncoder *encoder);

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
