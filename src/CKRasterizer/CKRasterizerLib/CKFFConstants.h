#ifndef CKFFCONSTANTS_H
#define CKFFCONSTANTS_H

#include "VxMath.h"
#include "CKTypes.h"
#include "CKRasterizerBackendEnums.h"

#define CKFF_MAX_LIGHTS         8
#define CKFF_MAX_TEXTURE_STAGES 8
#define CKFF_MAX_TEXTURE_STAGE_STATES (CKRST_TSS_MAXSTATE + 1)
#define CKFF_VERTEX_BLEND_MATRIX_COUNT 4
// Fixed sampler layout (spec 5.3): one 2D sampler per stage plus four cube and
// four volume samplers filled by type ordinal.
#define CKFF_CUBE_SAMPLER_COUNT 4
#define CKFF_VOLUME_SAMPLER_COUNT 4

// ============================================================================
// Light data for shader upload (view-space)
// ============================================================================

struct CKFFLightData {
    float Position[4];    // xyz=position (view space), w=type (0=dir, 1=point, 2=spot)
    float Direction[4];   // xyz=direction (view space), w=range
    float Diffuse[4];     // rgba
    float Specular[4];    // rgba
    float Ambient[4];     // rgba
    float Attenuation[4]; // x=constant, y=linear, z=quadratic, w=falloff
    float SpotParams[4];  // x=cos(theta/2), y=cos(phi/2), z=0, w=0
};

// ============================================================================
// Material data for shader upload
// ============================================================================

struct CKFFMaterialData {
    float Diffuse[4];
    float Ambient[4];
    float Specular[4];
    float Emissive[4];
    float Power;
    float Padding[3];
};

// ============================================================================
// Vertex shader constants - uploaded via SetUniform per draw
// ============================================================================

struct CKFFVertexConstants {
    VxMatrix WorldView;
    VxMatrix ViewProj;
    VxMatrix World;
    VxMatrix NormalMatrix;
    VxMatrix TexMatrix[CKFF_MAX_TEXTURE_STAGES];

    CKFFLightData Lights[CKFF_MAX_LIGHTS];
    float GlobalAmbient[4];
    float LightCount[4]; // x=count, yzw=padding

    CKFFMaterialData Material;

    float FogParams[4];   // x=start, y=end, z=density, w=mode
    float Viewport[4];    // x=scaleX, y=scaleY, z=offsetX, w=offsetY
};

// ============================================================================
// Fragment shader constants - uploaded via SetUniform per draw
// ============================================================================

struct CKFFFragmentConstants {
    float TextureFactor[4]; // RGBA [0..1]
    float FogColor[4];      // RGBA [0..1]
    float AlphaParams[4];   // x=alphaRef [0..1], y=alphaFunc, z=0, w=0
    float BumpEnvMat[4];    // m00, m01, m10, m11
    float BumpEnvLum[4];    // x=scale, y=offset, z=0, w=0
};


#endif // CKFFCONSTANTS_H
