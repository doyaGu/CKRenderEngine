#ifndef CKFFUNIFORMSTATE_H
#define CKFFUNIFORMSTATE_H

#include "CKDrawStateCache.h"
#include "CKFFStateDesc.h"
#include "CKRenderEngineEnums.h"
#include "CKTypes.h"

float CKFFReadFloatRenderState(const CKDrawStateCache &cache, VXRENDERSTATETYPE state, float fallback);
void CKFFPackColorARGB(CKDWORD color, float outColor[4]);
CKDWORD CKFFResolveMaterialSource(CKBOOL lighting,
                                  CKBOOL colorVertex,
                                  CKBOOL fromVertex,
                                  CKDWORD dpFlags,
                                  CKDWORD streamFlag,
                                  CKFFMaterialSource vertexSource);
float CKFFEncodeShaderLightType(VXLIGHT_TYPE type);

#endif // CKFFUNIFORMSTATE_H
