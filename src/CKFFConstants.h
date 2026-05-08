#ifndef CKFFCONSTANTS_H
#define CKFFCONSTANTS_H

#include "VxMath.h"
#include "CKTypes.h"
#include "CKRasterizerEnums.h"

#define CKFF_MAX_LIGHTS         8
#define CKFF_MAX_TEXTURE_STAGES 8
#define CKFF_MAX_TEXTURE_STAGE_STATES (CKRST_TSS_MAXSTATE + 1)

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

// ============================================================================
// Uniform handle table - created once at init
// ============================================================================

struct CKFFUniformHandles {
    // User uniforms (uploaded via SetUniform per draw)
    // Transform uniforms are NOT here: bgfx handles u_model, u_modelView,
    // u_modelViewProj, u_viewProj automatically via SetViewTransform/SetTransform.
    CKDWORD u_lights;       // vec4 array: 8 lights x 7 vec4 = 56 elements
    CKDWORD u_ckModelViewProj;
    CKDWORD u_ckModel;
    CKDWORD u_ckModelView;
    CKDWORD u_ckNormalMatrix;
    CKDWORD u_texMatrix;   // mat4 array: one texture matrix per stage
    CKDWORD u_lightParams;  // vec4: x=count, yzw=globalAmbient RGB
    CKDWORD u_material;     // vec4 array: 5 elements (diff, amb, spec, emis, power)
    CKDWORD u_ffParams;     // vec4: material source selectors diff/amb/spec/emis
    CKDWORD u_lightModelParams; // vec4: x=localViewer, yzw=0
    CKDWORD u_fogParams;    // vec4: start, end, density, mode
    CKDWORD u_fogColor;     // vec4: RGBA
    CKDWORD u_texFactor;    // vec4: RGBA
    CKDWORD u_alphaParams;  // vec4: ref, func, specularEnable, 0
    CKDWORD u_bumpEnv;      // vec4 array: 2 elements (matrix, luminance)
    CKDWORD u_viewport;     // vec4: scaleX, scaleY, offsetX, offsetY
    CKDWORD u_stageParams;  // vec4 array: per-stage color/alpha ops and args
    CKDWORD u_ffSpec;       // vec4 array: DXVK-style FFP specialization dwords mirror
    CKDWORD u_clipPlanes;   // vec4 array: compact enabled user clip planes
    CKDWORD u_clipParams;   // vec4: x=enabled clip plane count
    CKDWORD s_texture[CKFF_MAX_TEXTURE_STAGES]; // samplers

    CKFFUniformHandles() { memset(this, 0, sizeof(*this)); }
};

#endif // CKFFCONSTANTS_H
