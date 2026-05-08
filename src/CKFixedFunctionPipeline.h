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

struct CKFFTextureStageOps {
    CKDWORD ColorOp;
    CKDWORD ColorArg0;
    CKDWORD ColorArg1;
    CKDWORD ColorArg2;
    CKDWORD AlphaOp;
    CKDWORD AlphaArg0;
    CKDWORD AlphaArg1;
    CKDWORD AlphaArg2;
    CKDWORD ResultArg;
};

struct CKFFTextureStageSnapshot {
    CKDWORD Texture;
    CKDWORD States[CKFF_MAX_TEXTURE_STAGE_STATES];
    VxMatrix TextureMatrix;
};

CKFFTextureStageOps CKFFLegacyTextureBlendToStageOps(CKDWORD blend);
VxColor CKFFEvaluateTextureOp(CKDWORD op,
                              const VxColor &arg0,
                              const VxColor &arg1,
                              const VxColor &arg2,
                              const VxColor &current,
                              const VxColor &diffuse,
                              const VxColor &textureColor,
                              const VxColor &textureFactor = VxColor(0.0f, 0.0f, 0.0f, 0.0f));
CKDWORD CKFFLegacyTextureBlendToColorOp(CKDWORD blend);
CKDWORD CKFFLegacyTextureBlendToAlphaOp(CKDWORD blend);
CKBOOL CKFFStageBlendToTextureOps(CKDWORD stageBlend,
                                  CKDWORD &colorOp, CKDWORD &colorArg1, CKDWORD &colorArg2,
                                  CKDWORD &alphaOp, CKDWORD &alphaArg1, CKDWORD &alphaArg2);
void CKFFPackBumpEnvUniform(const CKDWORD *stageState, float outBumpEnv[2][4]);
int CKFFActiveTextureCountFromDPFlags(CKDWORD dpFlags);
CKDWORD CKFFPackTexcoordIndex(CKDWORD index, CKDWORD generation);
CKDWORD CKFFTexcoordIndex(CKDWORD packed);
CKDWORD CKFFTexcoordGeneration(CKDWORD packed);

enum CKFFTexcoordGenerationMode {
    CKFF_TEXGEN_NONE = 0,
    CKFF_TEXGEN_CAMERASPACENORMAL = 1,
    CKFF_TEXGEN_CAMERASPACEPOSITION = 2,
    CKFF_TEXGEN_CAMERASPACEREFLECTION = 3,
    CKFF_TEXGEN_SPHEREMAP = 4
};

enum CKFFCoverage {
    CKFF_COVERAGE_EXACT = 0,
    CKFF_COVERAGE_APPROXIMATE = 1,
    CKFF_COVERAGE_FALLBACK = 2,
    CKFF_COVERAGE_UNTESTED = 3
};

enum CKFFShaderSemantic {
    CKFF_SHADER_SEMANTIC_ARG_DIFFUSE = 0,
    CKFF_SHADER_SEMANTIC_ARG_CURRENT,
    CKFF_SHADER_SEMANTIC_ARG_TEXTURE,
    CKFF_SHADER_SEMANTIC_ARG_TFACTOR,
    CKFF_SHADER_SEMANTIC_ARG_SPECULAR,
    CKFF_SHADER_SEMANTIC_ARG_TEMP,
    CKFF_SHADER_SEMANTIC_ARG_COMPLEMENT,
    CKFF_SHADER_SEMANTIC_ARG_ALPHAREPLICATE,
    CKFF_SHADER_SEMANTIC_RESULT_CURRENT,
    CKFF_SHADER_SEMANTIC_RESULT_TEMP,
    CKFF_SHADER_SEMANTIC_BUMPENVMAP,
    CKFF_SHADER_SEMANTIC_BUMPENVMAPLUMINANCE,
    CKFF_SHADER_SEMANTIC_PROJECTED_SAMPLING,
    CKFF_SHADER_SEMANTIC_TEXGEN_CAMERASPACENORMAL,
    CKFF_SHADER_SEMANTIC_TEXGEN_CAMERASPACEPOSITION,
    CKFF_SHADER_SEMANTIC_TEXGEN_CAMERASPACEREFLECTION,
    CKFF_SHADER_SEMANTIC_TEXGEN_SPHEREMAP,
    CKFF_SHADER_SEMANTIC_COUNT
};

CKFFCoverage CKFFClassifyTextureOpCoverage(CKDWORD op);
CKFFCoverage CKFFClassifyShaderSemanticCoverage(CKFFShaderSemantic semantic);

class CKFixedFunctionPipeline {
public:
    CKFixedFunctionPipeline();
    ~CKFixedFunctionPipeline();

    void Init(CKRasterizerContext *ctx);
    void Shutdown();

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
    CKDWORD GetTexture(int stage) const;
    void SetViewport(const CKViewportData &viewport);
    void SetUserClipPlane(int index, const VxPlane &plane);
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

private:
    CKRasterizerContext *m_Context;

    // Subsystems
    CKFFShaderCache m_ShaderCache;
    CKDrawStateCache m_DrawStateCache;
    CKVertexLayoutCache m_VertexLayoutCache;
    CKTransientGeometry m_TransientGeometry;
    CKRenderPipeline m_RenderPipeline;
    CKFrustumCuller m_FrustumCuller;
    CKFFDebugState m_DebugState;
    CKFFSpecializationInfo m_CurrentSpecializationInfo;

    // Current transform state
    VxMatrix m_World;
    VxMatrix m_View;
    VxMatrix m_Projection;
    VxMatrix m_TexMatrix[CKFF_MAX_TEXTURE_STAGES];

    // Current material
    CKFFMaterialData m_Material;

    // Current lights
    CKFFLightData m_Lights[CKFF_MAX_LIGHTS];
    CKBOOL m_LightEnabled[CKFF_MAX_LIGHTS];
    int m_ActiveLightCount;

    // Current textures
    CKDWORD m_TextureHandles[CKFF_MAX_TEXTURE_STAGES];
    int m_CurrentActiveTextureCount;
    bool m_CurrentLightingEnabled;
    float m_MaterialSource[4];

    // Texture stage state
    CKDWORD m_StageStates[CKFF_MAX_TEXTURE_STAGES][CKFF_MAX_TEXTURE_STAGE_STATES];
    float m_Viewport[4];
    VxPlane m_UserClipPlanes[6];

    // Dirty tracking
    CKDWORD m_DirtyFlags;

    // Internal methods
    CKFFStateDesc BuildCurrentStateDesc(CKDWORD dpFlags, CKDWORD formatFlags = 0);
    CKFFShaderKey BuildCurrentShaderKey(const CKFFStateDesc &stateDesc) const;
    void SetCurrentShaderKey(const CKFFShaderKey &shaderKey);
    void UploadUniforms(CKRasterizerEncoder *encoder);
    void BindTextures(CKRasterizerEncoder *encoder);
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
