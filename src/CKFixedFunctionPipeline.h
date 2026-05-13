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
#include "CKFFShaderCache.h"
#include "CKDrawStateCache.h"
#include "CKVertexLayoutCache.h"
#include "CKTransientGeometry.h"
#include "CKRenderPipeline.h"
#include "CKFrustumCuller.h"

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

class CKFixedFunctionPipeline {
public:
    CKFixedFunctionPipeline();
    ~CKFixedFunctionPipeline();

    void Init(CKRasterizerContext *ctx);
    void Shutdown();
    void SetRenderOptions(CKBOOL DisableTextureFiltering, CKBOOL DisableMipmaps);

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
    CKRasterizerContext *m_Context;
    CKBOOL m_DisableTextureFiltering;
    CKBOOL m_DisableMipmaps;

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
    VxMatrix m_TexMatrix[CKFF_MAX_TEXTURE_STAGES];
    VxMatrix m_VertexBlendMatrices[CKFF_VERTEX_BLEND_MATRIX_COUNT];
    CKBOOL m_VertexBlendMatrixSet[CKFF_VERTEX_BLEND_MATRIX_COUNT];

    // Current material
    CKFFMaterialData m_Material;

    // Current lights
    CKFFLightData m_Lights[CKFF_MAX_LIGHTS];
    CKBOOL m_LightEnabled[CKFF_MAX_LIGHTS];
    int m_ActiveLightCount;

    // Current textures
    CKDWORD m_TextureHandles[CKFF_MAX_TEXTURE_STAGES];
    CKDWORD m_TextureFlags[CKFF_MAX_TEXTURE_STAGES];
    int m_CurrentActiveTextureCount;
    bool m_CurrentLightingEnabled;
    CKDWORD m_AlphaTestPrecision;
    float m_MaterialSource[4];

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
    CKFFStateDesc BuildCurrentStateDesc(CKDWORD dpFlags, CKDWORD formatFlags = 0,
                                        const CKBYTE *texcoordComponentCounts = nullptr);
    CKFFShaderKey BuildCurrentShaderKey(const CKFFStateDesc &stateDesc) const;
    void SetCurrentProgramBinding(const CKFFShaderKey &shaderKey, const CKFFProgramBinding &binding);
    void UploadUniforms(CKRasterizerEncoder *encoder);
    void UploadUniform(CKRasterizerEncoder *encoder, CKDWORD uniform, const void *data, CKDWORD count);
    CKDWORD CurrentTextureMatrixUploadCount() const;
    bool CurrentShaderUsesBumpEnv() const;
    bool CurrentShaderUsesTexFactor() const;
    bool CurrentShaderUsesStageConstant() const;
    bool CurrentShaderUsesMaterialUniform() const;
    bool CurrentShaderUsesViewSpaceUniforms() const;
    void BindTextures(CKRasterizerEncoder *encoder);
    CKDWORD SubmitDiscardFlags() const;
    void LogAndResetFrameStats();
    float ComputeDepthKey() const;
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
