#ifndef CKFFSTAGESTATE_H
#define CKFFSTAGESTATE_H

#include "VxMath.h"
#include "CKRenderEngineEnums.h"
#include "CKRasterizerEnums.h"
#include "CKRasterizerTypes.h"
#include "CKFFConstants.h"

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

struct CKFFVertexBlendState {
    CKDWORD Mode;
    CKDWORD Count;
    CKBOOL Indexed;
    CKBOOL Supported;
};

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

CKDWORD CKFFBaseTextureArg(CKDWORD arg);
CKFFTextureStageOps CKFFLegacyTextureBlendToStageOps(CKDWORD blend);
CKDWORD CKFFLegacyTextureBlendToColorOp(CKDWORD blend);
CKDWORD CKFFLegacyTextureBlendToAlphaOp(CKDWORD blend);
CKBOOL CKFFStageBlendToTextureOps(CKDWORD stageBlend,
                                  CKDWORD &colorOp, CKDWORD &colorArg1, CKDWORD &colorArg2,
                                  CKDWORD &alphaOp, CKDWORD &alphaArg1, CKDWORD &alphaArg2);

CKDWORD CKFFResolveStageColorOp(const CKDWORD *stage, bool stageActive, bool hasTexture);
CKDWORD CKFFResolveStageAlphaOp(const CKDWORD *stage, bool stageActive, bool hasTexture);
CKDWORD CKFFResolveStageColorArg0(const CKDWORD *stage);
CKDWORD CKFFResolveStageColorArg1(const CKDWORD *stage, bool hasTexture);
CKDWORD CKFFResolveStageColorArg2(const CKDWORD *stage);
CKDWORD CKFFResolveStageAlphaArg0(const CKDWORD *stage);
CKDWORD CKFFResolveStageAlphaArg1(const CKDWORD *stage, bool hasTexture);
CKDWORD CKFFResolveStageAlphaArg2(const CKDWORD *stage);
CKDWORD CKFFResolveStageResultArg(const CKDWORD *stage);

CK_ADDRESS_MODE CKFFTranslateAddressMode(CKDWORD mode);
void CKFFPackBumpEnvUniform(const CKDWORD *stageState, float outBumpEnv[2][4]);
Vx2DVector CKFFEvaluateBumpEnvTexcoord(const Vx2DVector &baseUv,
                                       const VxColor &bumpValue,
                                       const float bumpEnv[2][4]);
float CKFFEvaluateBumpEnvLuminance(const VxColor &bumpValue,
                                   const float bumpEnv[2][4]);
CKFFVertexBlendState CKFFResolveVertexBlendState(CKDWORD vertexBlend,
                                                 CKBOOL indexed,
                                                 CKDWORD formatFlags);
int CKFFActiveTextureCountFromDPFlags(CKDWORD dpFlags);
int CKFFResolveActiveTextureCount(CKDWORD dpFlags,
                                  const CKDWORD textureHandles[CKFF_MAX_TEXTURE_STAGES],
                                  const CKDWORD stageStates[CKFF_MAX_TEXTURE_STAGES][CKFF_MAX_TEXTURE_STAGE_STATES]);
CKSamplerDesc CKFFBuildSamplerDesc(const CKDWORD *stageState);
CKDWORD CKFFPackTexcoordIndex(CKDWORD index, CKDWORD generation);
CKDWORD CKFFTexcoordIndex(CKDWORD packed);
CKDWORD CKFFTexcoordGeneration(CKDWORD packed);
CKFFCoverage CKFFClassifyTextureOpCoverage(CKDWORD op);
CKFFCoverage CKFFClassifyShaderSemanticCoverage(CKFFShaderSemantic semantic);

#endif // CKFFSTAGESTATE_H
