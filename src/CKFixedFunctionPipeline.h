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
#include "CKFFStageState.h"
#include "CKFFConstants.h"
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

// Dirty flags for uniform upload
#define CKFF_DIRTY_MATRICES   0x01
#define CKFF_DIRTY_LIGHTS     0x02
#define CKFF_DIRTY_MATERIAL   0x04
#define CKFF_DIRTY_FOG        0x08
#define CKFF_DIRTY_TEXFACTOR  0x10
#define CKFF_DIRTY_ALPHATEST  0x20
#define CKFF_DIRTY_ALL        0xFF

struct CKLightData;

struct CKFFFrameStats {
    CKDWORD FrameIndex;
    CKDWORD SoftwareDraws;
    CKDWORD HardwareDraws;
    CKDWORD SubmittedDraws;
    CKDWORD PrepareFailures;
    CKDWORD ProgramMisses;
    CKDWORD UniformSets;
    CKDWORD UniformVec4s;
    CKDWORD UniformHandleSets[64];
    CKDWORD UniformHandleVec4s[64];
    CKDWORD TextureBinds;
    CKDWORD VertexLayoutSets;
    CKDWORD VertexBufferSets;
    CKDWORD IndexBufferSets;
    CKDWORD TransformSets;
    CKDWORD ConsecutiveProgramRepeats;
    CKDWORD ConsecutiveDrawStateRepeats;
    CKDWORD ConsecutiveTextureSetRepeats;
    CKDWORD ConsecutiveVertexBufferRepeats;
    CKDWORD ConsecutiveIndexBufferRepeats;
    CKDWORD ConsecutiveWorldMatrixRepeats;
    CKDWORD QueuedRenderPackets;
    CKDWORD ReplayedRenderPackets;
    CKDWORD RenderPacketFallbacks;
    CKDWORD RenderPacketFlushes;
    CKDWORD RenderPacketUniformOverflows;
    CKDWORD RenderPacketSkippedStates;
    CKDWORD RenderPacketSkippedTextures;
    CKDWORD RenderPacketSkippedUniforms;
    CKDWORD RenderPacketStaticUniformUploads;
    CKDWORD RenderPacketStaticUniformSkips;
    CKDWORD RenderPacketObjectUniformUploads;
    CKDWORD RenderPacketObjectUniformSkips;
    CKDWORD RenderPacketSkippedVertexBuffers;
    CKDWORD RenderPacketSkippedIndexBuffers;
    CKDWORD RenderPacketRuns;
    CKDWORD RenderPacketMaxRunLength;
    CKDWORD RenderPacketStaticPayloadBuilds;
    CKDWORD RenderPacketStaticPayloadReuses;
    CKDWORD RenderPacketStaticPayloadInterns;
    CKDWORD RenderPacketSortSkips;
    CKDWORD RenderPacketAdaptiveSamples;
    CKDWORD RenderPacketAdaptiveBypasses;
    CKDWORD RenderPacketAdaptiveSavedBindEstimate;
    CKDWORD RenderPacketAdaptiveRunBypasses;
    CKDWORD RenderPacketAdaptiveSampleRuns;
    CKDWORD RenderPacketAdaptiveSampleMaxRun;
    CKDWORD RenderPacketAdaptiveSubmitSavedEstimate;
    CKDWORD RenderPacketAdaptiveCooldownBypasses;
    CKDWORD RenderPacketAdaptiveCooldownFrames;
    CKDWORD RenderPacketAdaptiveFrameEndEvaluations;
    CKDWORD RenderPacketAdaptiveFrameEndRunBypasses;
    CKDWORD RenderPacketViewProjectionRebuilds;
    CKDWORD RenderPacketInstancedRuns;
    CKDWORD RenderPacketInstancedPackets;
    CKDWORD RenderPacketInstancedSubmits;
    CKDWORD RenderPacketInstanceBufferBytes;
    CKDWORD RenderPacketInstanceAllocFailures;
    CKDWORD RenderPacketSubmitSavedEstimate;
    CKDWORD RenderPacketInstancingFallbacks;
    double RenderPacketBuildUs;
    double RenderPacketSortUs;
    double RenderPacketReplayUs;
    CKDWORD DrawStateCacheHits;
    CKDWORD DrawStateRebuilds;
    CKDWORD TransientVertexBytes;
    CKDWORD TransientIndexBytes;
    double PrepareUs;
    double StateUs;
    double ProgramUs;
    double UniformUs;
    double TextureUs;
    double TransformUs;
    double DrawStateBuildUs;
    double EncoderStateUs;
    double StencilUs;
    double LayoutUs;
    double BufferBindUs;
    double SubmitUs;
    CKDWORD LastProgram;
    CKDrawState LastDrawState;
    CKDWORD LastActiveTextureCount;
    CKDWORD LastTextureHandles[CKFF_MAX_TEXTURE_STAGES];
    CKDWORD LastVertexBuffer;
    CKDWORD LastIndexBuffer;
    CKDWORD LastVertexLayout;
    VxMatrix LastWorldMatrix;
    CKBOOL HasLastProgram;
    CKBOOL HasLastDrawState;
    CKBOOL HasLastTextureSet;
    CKBOOL HasLastVertexBuffer;
    CKBOOL HasLastIndexBuffer;
    CKBOOL HasLastWorldMatrix;
};

#if CKRE_ENABLE_FFP_DIAGNOSTICS
struct CKFFDiagnosticConfig {
    bool StatsEnabled;
    bool UniformHistEnabled;
    int StatsInterval;
};
#endif

struct CKFFUniformSink {
    CKRasterizerEncoder *Encoder;
    CKFFRenderPacketUniformPayload *StaticPayload;
    CKFFRenderPacketUniformPayload *ObjectPayload;
    CKBOOL EmitStatic;
    CKBOOL EmitObject;
    CKBOOL Failed;
};

struct CKFFPacketTextureSetCache {
    CKBOOL Dirty;
    CKDWORD ActiveTextureCount;
    CKDWORD TextureSetHash;
    CKFFRenderPacketTextureBinding Textures[CKFF_MAX_TEXTURE_STAGES];
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

struct CKFFUniformEmissionContext;
struct CKFFPipelineTestAccess;

struct CKFFVertexBufferPacketBuildResult {
    CKBOOL Success;
    CKDWORD RejectReason;
    CKFFProgramContext ProgramContext;
    CKFFTextureBindingSet TextureBindingSet;
    CKRenderPacket Packet;
};

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

    CKBOOL HasOpaqueRenderPackets() const { return m_OpaquePacketQueue.HasPackets(); }
    void FlushOpaqueRenderPackets(CKRasterizerEncoder *encoder = nullptr,
                                  CKBOOL forceDirectReplay = FALSE,
                                  CKBOOL allowAdaptiveLearning = TRUE);
    void SetOpaqueSortingEnabled(CKBOOL enabled) {
        m_OpaqueSortingEnabled = enabled;
        ResetOpaqueRenderPacketFrameState();
    }
    void SetOpaqueInstancingEnabled(CKBOOL enabled) { m_OpaqueInstancingEnabled = enabled; }
    void SetOpaqueRenderPacketsAllowed(CKBOOL allowed) { m_OpaquePacketAllowed = allowed; }
    CKBOOL GetOpaqueRenderPacketsAllowed() const { return m_OpaquePacketAllowed; }
    CKDWORD GetOpaquePacketAdaptiveSamples() const { return m_OpaquePacketQueue.GetAdaptiveSamples(); }
    CKDWORD GetOpaquePacketAdaptiveBypasses() const { return m_OpaquePacketQueue.GetAdaptiveBypasses(); }
    CKDWORD GetOpaquePacketAdaptiveSavedBindEstimate() const { return m_OpaquePacketQueue.GetAdaptiveSavedBindEstimate(); }
    CKDWORD GetOpaquePacketAdaptiveRunBypasses() const { return m_OpaquePacketQueue.GetAdaptiveRunBypasses(); }
    CKDWORD GetOpaquePacketAdaptiveSampleRuns() const { return m_OpaquePacketQueue.GetAdaptiveSampleRuns(); }
    CKDWORD GetOpaquePacketAdaptiveSampleMaxRun() const { return m_OpaquePacketQueue.GetAdaptiveSampleMaxRun(); }
    CKDWORD GetOpaquePacketAdaptiveSubmitSavedEstimate() const { return m_OpaquePacketQueue.GetAdaptiveSubmitSavedEstimate(); }
    CKDWORD GetOpaquePacketAdaptiveCooldownBypasses() const { return m_OpaquePacketQueue.GetAdaptiveCooldownBypasses(); }
    CKDWORD GetOpaquePacketAdaptiveCooldownFrames() const { return m_OpaquePacketQueue.GetAdaptiveCooldownFrames(); }
    CKDWORD GetOpaquePacketAdaptiveFrameEndEvaluations() const { return m_OpaquePacketQueue.GetAdaptiveFrameEndEvaluations(); }
    CKDWORD GetOpaquePacketAdaptiveFrameEndRunBypasses() const { return m_OpaquePacketQueue.GetAdaptiveFrameEndRunBypasses(); }

    // === Subsystem access ===
    CKDrawStateCache &GetDrawStateCache() { return m_DrawStateCache; }
    CKVertexLayoutCache &GetVertexLayoutCache() { return m_VertexLayoutCache; }
    CKTransientGeometry &GetTransientGeometry() { return m_TransientGeometry; }
    CKFFShaderCache &GetShaderCache() { return m_ShaderCache; }
    CKRenderPipeline &GetRenderPipeline() { return m_RenderPipeline; }
    CKFrustumCuller &GetFrustumCuller() { return m_FrustumCuller; }

    // === Matrix access ===
    const VxMatrix &GetWorldMatrix() const { return m_World; }
    const VxMatrix &GetViewMatrix() const { return m_View; }
    const VxMatrix &GetProjectionMatrix() const { return m_Projection; }
    CKSamplerDesc BuildSamplerDesc(int stage) const;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const CKFFFrameStats &GetFrameStats() const { return m_FrameStats; }
#else
    const CKFFFrameStats &GetFrameStats() const;
#endif

private:
#if CKRE_ENABLE_TEST_ACCESS
    friend struct CKFFPipelineTestAccess;
#endif

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
    CKFFShaderKey m_CurrentShaderKey;
    CKFFProgramBinding m_CurrentProgramBinding;
    CKFFSpecializationInfo m_CurrentSpecializationInfo;

    // Current transform state
    VxMatrix m_World;
    VxMatrix m_View;
    VxMatrix m_Projection;
    VxMatrix m_ViewProjection;
    CKDWORD m_ViewProjectionHash;
    VxMatrix m_TexMatrix[CKFF_MAX_TEXTURE_STAGES];
    VxMatrix m_VertexBlendMatrices[CKFF_VERTEX_BLEND_MATRIX_COUNT];
    CKBOOL m_VertexBlendMatrixSet[CKFF_VERTEX_BLEND_MATRIX_COUNT];
    CKBOOL m_ViewProjectionDirty;

    // Current material
    CKFFMaterialData m_Material;

    // Current lights
    CKFFLightData m_Lights[CKFF_MAX_LIGHTS];
    CKBOOL m_LightEnabled[CKFF_MAX_LIGHTS];
    int m_ActiveLightCount;

    // Current textures
    CKDWORD m_TextureHandles[CKFF_MAX_TEXTURE_STAGES];
    CKDWORD m_TextureFlags[CKFF_MAX_TEXTURE_STAGES];
    CKFFPacketTextureSetCache m_PacketTextureSetCache;
    CKBOOL m_PacketProgramCacheValid;
    CKDWORD m_PacketProgramCacheDPFlags;
    CKDWORD m_PacketProgramCacheFormatFlags;
    int m_PacketProgramCacheActiveTextureCount;
    CKFFPreparedState m_PacketProgramCachePreparedState;
    CKFFProgramContext m_PacketProgramCacheContext;
    int m_CurrentActiveTextureCount;
    CKDWORD m_AlphaTestPrecision;

    // Texture stage state
    CKDWORD m_StageStates[CKFF_MAX_TEXTURE_STAGES][CKFF_MAX_TEXTURE_STAGE_STATES];
    float m_Viewport[4];
    VxPlane m_UserClipPlanes[6];
    CKBYTE m_TexcoordComponentCounts[CKFF_MAX_TEXTURE_STAGES];

    // Dirty tracking
    CKDWORD m_DirtyFlags;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    CKFFFrameStats m_FrameStats;
    CKFFDiagnosticConfig m_DiagnosticConfig;
#endif

    // Internal methods
    void BuildCurrentPreparedState(CKFFPreparedState *prepared, CKDWORD dpFlags, CKDWORD formatFlags = 0,
                                   const CKBYTE *texcoordComponentCounts = nullptr);
    void SetCurrentProgramBinding(const CKFFShaderKey &shaderKey, const CKFFProgramBinding &binding);
    void MarkStaticUniformsDirty();
    void MarkPacketTextureSetDirty();
    void MarkPacketProgramDirty();
    void BuildCurrentTextureBindingSet(CKFFTextureBindingSet *bindingSet);
    void UpdatePacketTextureSetCache();
    void UploadUniforms(CKRasterizerEncoder *encoder);
    void UploadUniform(CKRasterizerEncoder *encoder, CKDWORD uniform, const void *data, CKDWORD count);
    CKBOOL EmitUniform(CKFFUniformSink *sink, CKDWORD uniform, const void *data,
                       CKDWORD count, CKDWORD vec4Count, CKBOOL objectUniform);
    void CKFFEmitObjectMatrixUniforms(const CKFFUniformEmissionContext *context);
    void CKFFEmitTextureMatrixUniforms(const CKFFUniformEmissionContext *context);
    void CKFFEmitStageAndSpecUniforms(const CKFFUniformEmissionContext *context);
    void CKFFEmitClipPlaneUniforms(const CKFFUniformEmissionContext *context);
    void EmitUniformPayloads(CKFFUniformSink *sink,
                             const CKFFProgramContext *programContext);
    CKBOOL BuildUniformPayloads(CKFFRenderPacketUniformPayload *staticPayload,
                                CKFFRenderPacketUniformPayload *objectPayload,
                                const CKFFProgramContext *programContext);
    CKBOOL BuildStaticUniformPayload(CKFFRenderPacketUniformPayload *payload,
                                     const CKFFProgramContext *programContext);
    CKBOOL BuildObjectUniformPayload(CKFFRenderPacketUniformPayload *payload,
                                     const CKFFProgramContext *programContext);
    CKBOOL BuildPacketObjectUniforms(CKRenderPacketObjectUniforms *uniforms,
                                     const CKFFProgramContext *programContext);
    CKDWORD GetPacketObjectUniformRejectReason() const;
    void UpdateViewProjectionCache();
    CKDWORD CurrentTextureMatrixUploadCount() const;
    bool ProgramUsesBumpEnv(const CKFFProgramContext *programContext) const;
    bool ProgramUsesTexFactor(const CKFFProgramContext *programContext) const;
    bool ProgramUsesStageConstant(const CKFFProgramContext *programContext) const;
    bool ProgramUsesMaterialUniform(const CKFFProgramContext *programContext) const;
    bool ProgramUsesViewSpaceUniforms(const CKFFProgramContext *programContext) const;
    CKDWORD BuildDrawParams(float (*drawParams)[4],
                            const CKFFLightData *viewLights,
                            int packedLightCount,
                            const CKFFProgramContext *programContext,
                            const CKFFShaderKey &shaderKey,
                            CKBOOL positionT,
                            CKBOOL fullSpecialized,
                            CKBOOL shaderUsesLighting,
                            CKBOOL fogEnabled,
                            CKDWORD vertexFogMode,
                            CKDWORD pixelFogMode) const;
    void BindTextures(CKRasterizerEncoder *encoder);
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
    CKDWORD GetVertexBufferPacketInstancingRejectReason() const;
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

    CKFFRenderPacketQueue m_OpaquePacketQueue;
    CKBOOL m_OpaqueInstancingEnabled;
    CKDWORD m_InstanceLayout;
    CKBOOL m_OpaqueSortingEnabled;
    CKBOOL m_OpaquePacketAllowed;
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
