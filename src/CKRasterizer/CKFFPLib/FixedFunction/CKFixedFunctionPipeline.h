#ifndef CKFIXEDFUNCTIONPIPELINE_H
#define CKFIXEDFUNCTIONPIPELINE_H

#include "VxMath.h"
#include "CKRenderEngineTypes.h"
#include "CKRenderEngineEnums.h"
#include "CKRasterizerContextEnums.h"
#include "CKRasterizerContextTypes.h"
#include "CKFFStateDesc.h"
#include "CKFFShaderKey.h"
#include "CKFFDebug.h"
#include "CKFFDrawProbes.h"
#include "CKFFStageState.h"
#include "CKFFConstants.h"
#include "CKFFConstantSet.h"
#include "CKFFDrawTypes.h"
#include "CKRasterizerEnums.h"
#include "CKFFStateStore.h"
#include "CKFFTextureBinder.h"
#include "CKFFUniformEmitter.h"
#include "CKDrawStateCache.h"
#include "CKTransientGeometry.h"

struct CKLightData;

// Reasons a draw returns FALSE. Every fixed-function state the backend cannot
// express is approximated instead (see RecordDrawApproximation); only invalid
// input / state values and backend failures (BACKEND_ERROR: a draw packet
// the backend refused) still reject.
enum CKFFDrawRejectReason {
    CKFF_DRAW_REJECT_NONE = 0,
    CKFF_DRAW_REJECT_INVALID_INPUT,
    CKFF_DRAW_REJECT_PREPARE_FAILED,
    CKFF_DRAW_REJECT_PROGRAM_MISSING,
    CKFF_DRAW_REJECT_TEXTURE_OP,
    CKFF_DRAW_REJECT_STATE_VALUE,
    CKFF_DRAW_REJECT_BACKEND_ERROR,
    CKFF_DRAW_REJECT_COUNT
};

const char *CKFFDrawRejectReasonName(CKFFDrawRejectReason reason);
// Name of an APPROX_* / IGNORE_* diagnostic recorded for a draw.
const char *CKFFDrawApproximationName(CKRST_DIAGNOSTIC code);

class CKFixedFunctionPipeline {
public:
    CKFixedFunctionPipeline();
    ~CKFixedFunctionPipeline();

    // Initializes only the fixed-function translator. Native program and
    // vertex-layout objects belong to the concrete rasterizer context.
    bool Init(uint64_t features, CKDWORD maxTextureBindings,
              CKDWORD shaderTargetFlags);
    CKERROR Shutdown();
    void SetRenderOptions(CKBOOL DisableTextureFiltering, CKBOOL DisableMipmaps,
                          CKBOOL ForceAnisotropicFiltering = FALSE);

    // === State tracking ===
    void SetRenderState(VXRENDERSTATETYPE state, CKDWORD value);
    CKDWORD GetRenderState(VXRENDERSTATETYPE state) const;
    CKBOOL NeedsVertexBufferWrap(CKDWORD texcoordCount) const;
    CKBOOL NeedsVertexBufferPointExpansion(CKDWORD dpFlags) const;
    CKBOOL NeedsVertexBufferPointFillExpansion(VXPRIMITIVETYPE type,
                                               CKDWORD dpFlags) const;
    CKBOOL NeedsVertexBufferLinePattern(VXPRIMITIVETYPE type) const;
    CKBOOL NeedsVertexBufferEdgeAntialias(VXPRIMITIVETYPE type) const;
    CKBOOL NeedsVertexBufferBlendValidation(CKDWORD formatFlags) const;
    CKDWORD QueryRenderState(VXRENDERSTATETYPE state) const { return m_State.DrawState.QueryRenderState(state); }
    void InitDefaultStates();
    void SetColorWriteMask(CKBOOL r, CKBOOL g, CKBOOL b, CKBOOL a);
    CKDWORD GetColorWriteMask() const;
    void SetColorWriteMask(CKDWORD mask);
    void ResetTextureStage(int stage);
    // Semantic material reset, directly mutating the FFP state once per range.
    void ResetTextureStages(int firstStage, int stageCount);
    void DisableTextureStagesFrom(int firstStage);
    void SaveTextureStage(int stage, CKFFTextureStageSnapshot &snapshot) const;
    void RestoreTextureStage(int stage, const CKFFTextureStageSnapshot &snapshot);
    void SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type, CKDWORD value);
    // Back to "not set": value 0 and the explicit bit cleared, so the state
    // is derived from TEXTUREMAPBLEND / the bound texture at draw time.
    void ClearTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type);
    CKDWORD GetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type) const;
    CKDWORD QueryTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type) const;
    // TRUE when the state was set explicitly since the last stage reset.
    CKBOOL IsTextureStageStateSet(int stage, CKRST_TEXTURESTAGESTATETYPE type) const {
        if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES || (int)type < 0 || (int)type >= CKFF_MAX_TEXTURE_STAGE_STATES)
            return FALSE;
        return (m_State.StageStateSetMasks[stage] & (1ull << (CKDWORD)type)) != 0 ? TRUE : FALSE;
    }
    void SetTransform(VXMATRIX_TYPE type, const VxMatrix &matrix);
    CKBOOL GetTransform(VXMATRIX_TYPE type, VxMatrix &matrix) const;
    void ResetMaterial();
    void ApplyMaterial(const CKMaterialRenderState &state);
    void SetMaterial(const CKMaterialData *mat);
    const CKMaterialData &GetMaterial() const { return m_State.Material; }
    const CKLightData &GetLight(int index) const { return m_State.Lights[index]; }
    CKBOOL IsLightEnabled(int index) const { return m_State.LightEnabled[index]; }
    void SetLight(int index, const CKLightData *light);
    void EnableLight(int index, CKBOOL enable);
    void SetTexture(int stage, CKDWORD textureHandle);
    void SetTexture(int stage, CKDWORD textureHandle, CKDWORD textureFlags);
    CKDWORD GetTexture(int stage) const;
    void SetViewport(const CKViewportData &viewport);
    const CKViewportData &GetViewport() const { return m_State.ViewportData; }
    // Extents of the current target: the logical size the engine's
    // viewport and PositionT coordinates refer to (window pixels or texture
    // size) and the physical size of the texture actually rendered into
    // (window x RenderScale for the scene target). 0 = unknown, no mapping.
    void SetTargetExtents(CKDWORD logicalWidth, CKDWORD logicalHeight, CKDWORD physicalWidth, CKDWORD physicalHeight);
    const float *GetViewportRemap() const { return m_State.ViewportRemap; }
    CKBOOL GetViewportScissor(CKRECT *rect) const;
    // Render-target binding: on bottom-left-origin
    // backends draws into a texture flip the projection and the winding so
    // the texture memory ends up in the D3D (top-down) layout.
    void SetRenderTargetActive(CKBOOL active);
    CKBOOL IsRenderTargetActive() const { return m_State.RenderTargetActive; }
    CKBOOL RenderTargetOriginFlip() const;
    void UpdateViewportMapping();
    // The frame renders into a multisampled scene target.
    void SetMultisampledTarget(CKBOOL multisampled) { m_State.DrawState.SetMultisampledTarget(multisampled); }
    CKBOOL IsMultisampledTarget() const { return m_State.DrawState.GetMultisampledTarget(); }
    void SetUserClipPlane(int index, const VxPlane &plane);
    const VxPlane &GetUserClipPlane(int index) const { return m_State.UserClipPlanes[index]; }
    void SetDrawMarker(const char *marker) { m_DrawMarker = marker; }
    void SetAlphaTestPrecision(CKDWORD precision);
    CKDWORD GetAlphaTestPrecision() const;
    void SetColorTargetFormat(CKFFColorTargetFormat format);
    CKFFColorTargetFormat GetColorTargetFormat() const {
        return m_State.ColorTargetFormat;
    }
    void SetDepthBiasFormat(CK_DEPTH_FORMAT format);
    CK_DEPTH_FORMAT GetDepthBiasFormat() const { return m_State.DepthBiasFormat; }
    CKBOOL SetVertexBlendMatrix(CKDWORD index, const VxMatrix &matrix);
    void ResetVertexBlendMatrices();
    void SetTexcoordComponentCount(CKDWORD stage, CKDWORD count);
    void ResetTexcoordComponentCounts();
    void BeginDebugFrame();
    CKBOOL HadRejectedDrawsThisFrame() const { return m_FrameDrawRejected; }
    // Frame counter published by the concrete Context frame flow;
    // per-frame state such as the border colour palette resets on it.
    void SetFrameNumber(CKDWORD frameNumber) { m_FrameNumber = frameNumber; }
    CKDWORD GetFrameNumber() const { return m_FrameNumber; }

    // === Drawing ===
    // Prepare one complete draw. The concrete rasterizer consumes GetDraw()
    // immediately; CKFFPLib never queues or owns device commands.
    CKBOOL PreparePrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int indexCount,
                            VxDrawPrimitiveData *data);
    CKBOOL PrepareVertexBuffer(VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
                               CKDWORD baseVertex, CKDWORD vertexCount,
                               CKDWORD startIndex, CKDWORD indexCount,
                               CKDWORD dpFlags, CKDWORD formatFlags,
                               CKDWORD vertexLayout,
                               const CKWORD *indices = NULL);
    const CKFFDraw &GetDraw() const { return m_Draw; }
    CKBOOL FinishDraw(CKERROR error, uint64_t approximationMask);

    CKFFDrawRejectReason GetLastDrawRejectReason() const { return m_LastDrawRejectReason; }
    CKDWORD GetRejectedDrawCount(CKFFDrawRejectReason reason) const {
        return reason > CKFF_DRAW_REJECT_NONE && reason < CKFF_DRAW_REJECT_COUNT
            ? m_DrawRejectCounts[reason]
            : 0;
    }
    // Approximations applied to the last draw (bit = CKRST_DIAGNOSTIC code) and
    // the number of draws that used each approximation since Init.
    uint64_t GetLastDrawApproximationMask() const { return m_LastDrawApproximationMask; }
    CKDWORD GetApproximatedDrawCount(CKRST_DIAGNOSTIC code) const {
        return (CKDWORD)code < CKRST_DIAG_COUNT ? m_DrawApproximationCounts[code] : 0;
    }

    // === Matrix access ===
    const VxMatrix &GetWorldMatrix() const { return m_State.World; }
    const VxMatrix &GetViewMatrix() const { return m_State.View; }
    const VxMatrix &GetProjectionMatrix() const { return m_State.Projection; }
    CKSamplerDesc BuildSamplerDesc(int stage) const;
    const CKFFFrameStats &GetFrameStats() const { return m_Probes.Stats; }
    CKFFStateStore CaptureState() const { return m_State; }
    void RestoreState(const CKFFStateStore &state);
    uint64_t GetConstantRevision(CKDWORD block) const;
    uint64_t GetStaticUniformRevision() const { return m_StaticUniformRevision; }
    CKBOOL IsVertexBufferProgramCacheValid() const { return m_VertexBufferProgramCacheValid; }
    CKBOOL IsDrawValidationCacheValid() const { return m_DrawValidationCacheValid; }

private:
    enum CKFFStateChange {
        CKFF_CHANGE_OBJECT_UNIFORM = 0x1,
        CKFF_CHANGE_STATIC_UNIFORM = 0x2,
        CKFF_CHANGE_PROGRAM = 0x4,
        CKFF_CHANGE_DRAW_VALIDATION = 0x8
    };
    struct CKFFDrawSubmission {
        VXPRIMITIVETYPE DrawStateType;
        CKBOOL ForceSolidFill;
        CKBOOL PolygonDepthBias;
        CKBOOL PatternedLines;
        CKBOOL EdgeAntialias;
        const CKFFProgramContext *ProgramContext;
        const CKFFTextureBindingSet *Textures;
        CKDWORD VertexBuffer;
        CKDWORD IndexBuffer;
        const CKBYTE *Indices;
        CKBOOL Index32;
        CKDWORD BaseVertex;
        CKDWORD VertexCount;
        CKDWORD StartIndex;
        CKDWORD IndexCount;
        CKDWORD VertexLayout;
        CKDWORD VertexFormat;
        CKFFDrawSource Source;
    };

    uint64_t m_Features;
    CKDWORD m_MaxTextureBindings;
    // Subsystems
    CKTransientGeometry m_TransientGeometry;
    CKDWORD m_FrameNumber;
    CKFFDebugState m_DebugState;
    CKFFStateStore m_State;
    CKFFConstantSet m_Constants;
    CKFFDraw m_Draw;
    XArray<CKWORD> m_ImmediateIndices;
    const char *m_DrawMarker = nullptr;
    CKDWORD m_ShaderTargetFlags;

    CKFFDrawProbes m_Probes;
    CKFFTextureBinder m_TextureBinder;
    CKFFUniformEmitter m_UniformEmitter;
    uint64_t m_StaticUniformRevision;
    CKBOOL m_DrawValidationCacheValid;
    VXPRIMITIVETYPE m_DrawValidationCacheTopology;
    CKDWORD m_DrawValidationCacheFormatFlags;
    CKDWORD m_DrawValidationCacheActiveTextureCount;
    uint64_t m_DrawValidationCacheApproximationMask;
    CKBOOL m_VertexBufferProgramCacheValid;
    CKDWORD m_VertexBufferProgramCacheDPFlags;
    CKDWORD m_VertexBufferProgramCacheFormatFlags;
    CKDWORD m_VertexBufferProgramCacheActiveTextureCount;
    CKFFProgramPreparation m_VertexBufferProgramCache;
    CKBOOL m_SoftwareProgramCacheValid;
    CKDWORD m_SoftwareProgramCacheDPFlags;
    CKDWORD m_SoftwareProgramCacheFormatFlags;
    CKDWORD m_SoftwareProgramCacheActiveTextureCount;
    CKBOOL m_SoftwareProgramCachePointSprite;
    CKBYTE m_SoftwareProgramCacheTexcoordComponentCounts[CKFF_MAX_TEXTURE_STAGES];
    CKFFProgramPreparation m_SoftwareProgramCache;
    CKFFDrawRejectReason m_LastDrawRejectReason;
    CKBOOL m_FrameDrawRejected;
    CKDWORD m_DrawRejectCounts[CKFF_DRAW_REJECT_COUNT];
    uint64_t m_LastDrawApproximationMask;
    CKDWORD m_DrawApproximationCounts[CKRST_DIAG_COUNT];

    // Internal methods
    void OnFixedFunctionStateChanged(CKDWORD changeMask);
    void MarkPreparedProgramDirty();
    CKFFProgramPrepareStatus PrepareProgram(CKFFProgramPreparation *preparation,
                                            CKDWORD dpFlags,
                                            CKDWORD activeTextureCount,
                                            CKDWORD formatFlags = 0,
                                            const CKBYTE *texcoordComponentCounts = nullptr,
                                            CKBOOL pointSprite = FALSE);
    CKFFProgramPrepareStatus PrepareVertexBufferProgram(const CKFFProgramPreparation **preparation,
                                                        CKDWORD dpFlags,
                                                        CKDWORD formatFlags);
    CKFFProgramPrepareStatus PrepareSoftwareProgram(const CKFFProgramPreparation **preparation,
                                                    CKDWORD dpFlags,
                                                    CKDWORD activeTextureCount,
                                                    CKDWORD formatFlags,
                                                    const CKBYTE *texcoordComponentCounts,
                                                    CKBOOL pointSprite);
    CKBOOL ValidateDrawState(VXPRIMITIVETYPE topology, CKDWORD formatFlags,
                             CKDWORD activeTextureCount);
    CKBOOL ValidateVertexBlendWeights(CKDWORD dpFlags, CKDWORD formatFlags);
    CKBOOL ValidateVertexBlendIndices(const VxDrawPrimitiveData *data,
                                      CKDWORD formatFlags,
                                      const CKWORD *drawIndices,
                                      int drawIndexCount);
    CKBOOL RecordDrawReject(CKFFDrawRejectReason reason);
    void RecordDrawApproximation(CKRST_DIAGNOSTIC code);
    void BeginDrawDiagnostics() { m_LastDrawApproximationMask = 0; }
    void ResolveStencilWrite(CKDWORD *effectiveWriteMask) const;
    CKBOOL PrepareDraw(const CKFFDrawSubmission &submission);
    void LogAndResetFrameStats();

    CKBOOL BuildCurrentTextureBindingSet(const CKFFTextureBindingSet **bindingSet,
                                         CKDWORD activeTextureCount,
                                         const CKFFShaderKey &shaderKey,
                                         const CKFFSamplerLayoutPlan &layoutPlan);
    float ComputeDepthKey() const;
    CKBOOL PrepareVertexBufferImmediate(const CKFFProgramPreparation &preparation,
                                        VXPRIMITIVETYPE type,
                                        CKDWORD vb,
                                        CKDWORD ib,
                                        CKDWORD baseVertex,
                                        CKDWORD vertexCount,
                                        CKDWORD startIndex,
                                        CKDWORD indexCount,
                                        CKDWORD dpFlags,
                                        CKDWORD formatFlags,
                                        CKDWORD vertexLayout,
                                        const CKWORD *indices);

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
    CKFFStateStore m_Snapshot;
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

#endif // CKFIXEDFUNCTIONPIPELINE_H
