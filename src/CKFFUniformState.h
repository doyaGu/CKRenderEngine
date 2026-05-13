#ifndef CKFFUNIFORMSTATE_H
#define CKFFUNIFORMSTATE_H

#include "CKDrawStateCache.h"
#include "CKFFConstants.h"
#include "CKFFSpecializationInfo.h"
#include "CKFFStateDesc.h"
#include "CKRenderEngineEnums.h"
#include "CKTypes.h"
#include "VxMath.h"

struct CKFFStageParamsUniform {
    float Values[CKFF_MAX_TEXTURE_STAGES * 4][4];
};

struct CKFFSpecUniform {
    float Values[CKFFSpecializationInfo::MaxSpecDwords][4];
};

struct CKFFClipPlaneUniform {
    float Planes[6][4];
    float Params[4];
};

float CKFFReadFloatRenderState(const CKDrawStateCache &cache, VXRENDERSTATETYPE state, float fallback);
CKBYTE CKFFAlphaRefByte(CKDWORD alphaRef);
float CKFFNormalizeAlphaRef(CKDWORD alphaRef);
float CKFFPackAlphaFuncPrecision(CKDWORD func, CKDWORD precision);
CKDWORD CKFFAlphaTestPrecisionForFormat(const VxImageDescEx &desc);
void CKFFPackColorARGB(CKDWORD color, float outColor[4]);
CKDWORD CKFFResolveMaterialSource(CKBOOL lighting,
                                  CKBOOL colorVertex,
                                  CKBOOL fromVertex,
                                  CKDWORD dpFlags,
                                  CKDWORD streamFlag,
                                  CKFFMaterialSource vertexSource);
float CKFFEncodeShaderLightType(VXLIGHT_TYPE type);
void CKFFPackStageParams(const CKDWORD stageStates[CKFF_MAX_TEXTURE_STAGES][CKFF_MAX_TEXTURE_STAGE_STATES],
                         const CKDWORD textureHandles[CKFF_MAX_TEXTURE_STAGES],
                         int activeTextureCount,
                         CKFFStageParamsUniform &outParams);
void CKFFPackSpecializationDwords(const CKFFSpecializationInfo &info, CKFFSpecUniform &outSpec);
int CKFFPackClipPlaneUniforms(const VxPlane planes[6], CKDWORD clipMask, CKFFClipPlaneUniform &outClip);
int CKFFPackViewLights(const CKFFLightData lights[CKFF_MAX_LIGHTS],
                       const CKBOOL lightEnabled[CKFF_MAX_LIGHTS],
                       int activeLightCount,
                       CKBOOL lightingEnabled,
                       const VxMatrix &view,
                       CKFFLightData outViewLights[CKFF_MAX_LIGHTS]);

#endif // CKFFUNIFORMSTATE_H
