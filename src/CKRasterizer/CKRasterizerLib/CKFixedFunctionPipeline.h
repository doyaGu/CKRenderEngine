#ifndef CKFIXEDFUNCTIONPIPELINE_H
#define CKFIXEDFUNCTIONPIPELINE_H

#include "VxMath.h"
#include "CKRenderEngineTypes.h"
#include "CKRenderEngineEnums.h"
#include "CKRasterizerDeviceEnums.h"
#include "CKRasterizerDeviceTypes.h"
#include "CKRasterizerBackend.h"
#include "CKFFStateDesc.h"
#include "CKFFShaderKey.h"
#include "CKFFDebug.h"
#include "CKFFDrawProbes.h"
#include "CKFFStageState.h"
#include "CKFFConstants.h"
#include "CKFFDrawTypes.h"
#include "CKRasterizerEnums.h"
#include "CKFFDrawPreparer.h"
#include "CKFFStateStore.h"
#include "CKFFTextureBinder.h"
#include "CKFFUniformEmitter.h"
#include "CKFFShaderCache.h"
#include "CKDrawStateCache.h"
#include "CKVertexLayoutCache.h"
#include "CKTransientGeometry.h"

#ifndef CKRE_ENABLE_TEST_ACCESS
#define CKRE_ENABLE_TEST_ACCESS 0
#endif

struct CKLightData;

struct CKFFPipelineTestAccess;

// Reasons a draw returns FALSE. Every fixed-function state the backend cannot
// express is approximated instead (see RecordDrawApproximation); only invalid
// input / state values and backend failures (ENCODER_ERROR: a PushConstants /
// Draw call the backend refused) still reject.
enum CKFFDrawRejectReason {
    CKFF_DRAW_REJECT_NONE = 0,
    CKFF_DRAW_REJECT_INVALID_INPUT,
    CKFF_DRAW_REJECT_PREPARE_FAILED,
    CKFF_DRAW_REJECT_PROGRAM_MISSING,
    CKFF_DRAW_REJECT_TEXTURE_OP,
    CKFF_DRAW_REJECT_STATE_VALUE,
    CKFF_DRAW_REJECT_ENCODER_ERROR,
    CKFF_DRAW_REJECT_COUNT
};

const char *CKFFDrawRejectReasonName(CKFFDrawRejectReason reason);
// Name of an APPROX_* / IGNORE_* contract diagnostic recorded for a draw.
const char *CKFFDrawApproximationName(CKRST_DIAGNOSTIC code);

class CKFixedFunctionPipeline {
public:
    CKFixedFunctionPipeline();
    ~CKFixedFunctionPipeline();

    bool Init(CKRasterizerBackend *backend);
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
    // Back to "not set": value 0 and the explicit bit cleared, so the state
    // is derived from TEXTUREMAPBLEND / the bound texture at draw time.
    void ClearTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type);
    CKDWORD GetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type) const;
    // TRUE when the state was set explicitly since the last stage reset.
    CKBOOL IsTextureStageStateSet(int stage, CKRST_TEXTURESTAGESTATETYPE type) const {
        if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES || (int)type < 0 || (int)type >= CKFF_MAX_TEXTURE_STAGE_STATES)
            return FALSE;
        return (m_State.StageStateSetMasks[stage] & (1ull << (CKDWORD)type)) != 0 ? TRUE : FALSE;
    }
    void SetTransform(VXMATRIX_TYPE type, const VxMatrix &matrix);
    void ResetMaterial();
    void SetMaterial(const CKMaterialData *mat);
    void SetLight(int index, const CKLightData *light);
    void EnableLight(int index, CKBOOL enable);
    void SetTexture(int stage, CKDWORD textureHandle);
    void SetTexture(int stage, CKDWORD textureHandle, CKDWORD textureFlags);
    CKDWORD GetTexture(int stage) const;
    void SetViewport(const CKViewportData &viewport);
    // Extents of the current target (spec 4.4): the logical size the engine's
    // viewport and PositionT coordinates refer to (window pixels or texture
    // size) and the physical size of the texture actually rendered into
    // (window x RenderScale for the scene target). 0 = unknown, no mapping.
    void SetTargetExtents(CKDWORD logicalWidth, CKDWORD logicalHeight, CKDWORD physicalWidth, CKDWORD physicalHeight);
    const float *GetViewportRemap() const { return m_State.ViewportRemap; }
    CKBOOL GetViewportScissor(CKRECT *rect) const;
    // Render-target binding (spec 5.9 RTT origin): on bottom-left-origin
    // backends draws into a texture flip the projection and the winding so
    // the texture memory ends up in the D3D (top-down) layout.
    void SetRenderTargetActive(CKBOOL active);
    CKBOOL IsRenderTargetActive() const { return m_State.RenderTargetActive; }
    CKBOOL RenderTargetOriginFlip() const;
    void UpdateViewportMapping();
    // The frame renders into a multisampled scene target (spec 4.4).
    void SetMultisampledTarget(CKBOOL multisampled) { m_DrawStateCache.SetMultisampledTarget(multisampled); }
    CKBOOL IsMultisampledTarget() const { return m_DrawStateCache.GetMultisampledTarget(); }
    void SetUserClipPlane(int index, const VxPlane &plane);
    void SetAlphaTestPrecision(CKDWORD precision);
    CKDWORD GetAlphaTestPrecision() const;
    CKBOOL SetVertexBlendMatrix(CKDWORD index, const VxMatrix &matrix);
    void ResetVertexBlendMatrices();
    void SetTexcoordComponentCount(CKDWORD stage, CKDWORD count);
    void ResetTexcoordComponentCounts();
    void BeginDebugFrame();
    CKBOOL HadRejectedDrawsThisFrame() const { return m_FrameDrawRejected; }
    // Frame counter published by the frame flow (the translated context);
    // per-frame state such as the border colour palette resets on it.
    void SetFrameNumber(CKDWORD frameNumber) { m_FrameNumber = frameNumber; }
    CKDWORD GetFrameNumber() const { return m_FrameNumber; }

    // === Drawing ===
    // Draws go to the backend's current pass (the frame flow opened it).
    // Draw using VxDrawPrimitiveData (software vertex path)
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int indexCount,
                         VxDrawPrimitiveData *data);

    // Draw using persistent vertex/index buffer handles
    CKBOOL DrawVertexBuffer(VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
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
    // Approximations applied to the last draw (bit = CKRST_DIAGNOSTIC code) and
    // the number of draws that used each approximation since Init.
    uint64_t GetLastDrawApproximationMask() const { return m_LastDrawApproximationMask; }
    CKDWORD GetApproximatedDrawCount(CKRST_DIAGNOSTIC code) const {
        return (CKDWORD)code < CKRST_DIAG_COUNT ? m_DrawApproximationCounts[code] : 0;
    }

    // === Subsystem access ===
    CKVertexLayoutCache &GetVertexLayoutCache() { return m_VertexLayoutCache; }
    CKFFShaderCache &GetShaderCache() { return m_ShaderCache; }
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

private:
#if CKRE_ENABLE_TEST_ACCESS
    friend struct CKFFPipelineTestAccess;
#endif

    // Static uniforms are uploaded with every draw, so only program-affecting
    // changes have to invalidate anything.
    enum CKFFStateChange { CKFF_CHANGE_STATIC_UNIFORM = 0x1, CKFF_CHANGE_PROGRAM = 0x2 };
    enum CKFFSubmitSource { CKFF_SUBMIT_PRIMITIVE, CKFF_SUBMIT_VERTEX_BUFFER };

    struct CKFFDrawSubmission {
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

    CKRasterizerBackend *m_Backend;
    // Subsystems
    CKFFShaderCache m_ShaderCache;
    CKDrawStateCache m_DrawStateCache;
    CKVertexLayoutCache m_VertexLayoutCache;
    CKTransientGeometry m_TransientGeometry;
    CKDWORD m_FrameNumber;
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    CKFFDebugState m_DebugState;
#endif
    CKFFStateStore m_State;

#if CKRE_ENABLE_FFP_DIAGNOSTICS
    CKFFDrawProbes m_Probes;
#endif
    CKFFDrawPreparer m_DrawPreparer;
    CKFFTextureBinder m_TextureBinder;
    CKFFUniformEmitter m_UniformEmitter;
    CKFFDrawRejectReason m_LastDrawRejectReason;
    CKBOOL m_FrameDrawRejected;
    CKDWORD m_DrawRejectCounts[CKFF_DRAW_REJECT_COUNT];
    uint64_t m_LastDrawApproximationMask;
    CKDWORD m_DrawApproximationCounts[CKRST_DIAG_COUNT];
    CKDWORD m_BorderPaletteColors[16];
    CKDWORD m_BorderPaletteCount;
    CKDWORD m_BorderPaletteFrameSerial;

    // Internal methods
    void OnFixedFunctionStateChanged(CKDWORD changeMask);
    void MarkPreparedProgramDirty();
    CKBOOL ValidateDrawState(CKDWORD formatFlags, CKDWORD activeTextureCount);
    CKBOOL ValidateVertexBlendIndices(const VxDrawPrimitiveData *data,
                                      CKDWORD formatFlags);
    CKBOOL RecordDrawReject(CKFFDrawRejectReason reason);
    void RecordDrawApproximation(CKRST_DIAGNOSTIC code);
    void BeginDrawDiagnostics() { m_LastDrawApproximationMask = 0; }
    CKBOOL ResolveStencilWrite(CKBOOL *forceKeepOps, CKDWORD *effectiveWriteMask) const;
    CKDWORD NearestBorderPaletteSlot(CKDWORD argb) const;
    CKBOOL SubmitPrepared(const CKFFDrawSubmission &submission);
    void BindTextures(CKDWORD program, const CKFFTextureBindingSet *bindingSet);
    void LogAndResetFrameStats();

    CKBOOL BuildCurrentTextureBindingSet(CKFFTextureBindingSet *bindingSet,
                                         CKDWORD activeTextureCount,
                                         const CKFFShaderKey &shaderKey);
    float ComputeDepthKey() const;
    CKBOOL SubmitVertexBufferImmediate(const CKFFProgramPreparation &preparation,
                                       VXPRIMITIVETYPE type,
                                       CKDWORD vb,
                                       CKDWORD ib,
                                       CKDWORD baseVertex,
                                       CKDWORD vertexCount,
                                       CKDWORD startIndex,
                                       CKDWORD indexCount,
                                       CKDWORD dpFlags,
                                       CKDWORD formatFlags,
                                       CKDWORD vertexLayout);

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

#endif // CKFIXEDFUNCTIONPIPELINE_H
