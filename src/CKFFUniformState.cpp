#include "CKFFUniformState.h"

#include "VxColor.h"

#include <cstring>

float CKFFReadFloatRenderState(const CKDrawStateCache &cache, VXRENDERSTATETYPE state, float fallback) {
    CKDWORD bits = cache.GetRenderState(state);
    if (bits == 0)
        return fallback;

    float value = fallback;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

void CKFFPackColorARGB(CKDWORD color, float outColor[4]) {
    if (!outColor)
        return;

    outColor[0] = (float)ColorGetRed(color) / 255.0f;
    outColor[1] = (float)ColorGetGreen(color) / 255.0f;
    outColor[2] = (float)ColorGetBlue(color) / 255.0f;
    outColor[3] = (float)ColorGetAlpha(color) / 255.0f;
}

CKDWORD CKFFResolveMaterialSource(CKBOOL lighting,
                                  CKBOOL colorVertex,
                                  CKBOOL fromVertex,
                                  CKDWORD dpFlags,
                                  CKDWORD streamFlag,
                                  CKFFMaterialSource vertexSource) {
    (void)lighting;
    if (!colorVertex || !fromVertex || ((dpFlags & streamFlag) == 0))
        return CKFF_MS_MATERIAL;
    return vertexSource;
}

float CKFFEncodeShaderLightType(VXLIGHT_TYPE type) {
    switch (type) {
    case VX_LIGHTDIREC: return 0.0f;
    case VX_LIGHTSPOT:  return 2.0f;
    case VX_LIGHTPOINT:
    default:            return 1.0f;
    }
}
