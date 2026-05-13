$input a_position, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, a_color0, a_color1
#ifndef CKFF_VS_CLIP_DISTANCE
#define CKFF_VS_CLIP_DISTANCE 0
#endif
#if CKFF_VS_CLIP_DISTANCE
$output v_color0, v_color1, v_flatColor0, v_flatColor1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_texcoord5, v_texcoord6, v_texcoord7Fog, v_clipPos, v_clipDistance0, v_clipDistance1
#else
$output v_color0, v_color1, v_flatColor0, v_flatColor1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_texcoord5, v_texcoord6, v_texcoord7Fog, v_clipPos
#endif

#include "bgfx_shader.sh"
#include "ff_fog_common.sc"

uniform vec4 u_viewport;
uniform vec4 u_ffDrawParams[12];
uniform vec4 u_stageParams[32];
uniform mat4 u_texMatrix[8];
#if CKFF_VS_CLIP_DISTANCE
uniform vec4 u_clipPlanes[6];
uniform vec4 u_clipParams;
#endif

#if defined(CKFF_FULL_SPECIALIZED)
#ifndef CKFF_VS_FOG_MODE
#define CKFF_VS_FOG_MODE 0
#endif
#ifndef CKFF_VS_TEXCOORD0
#define CKFF_VS_TEXCOORD0 0
#endif
#ifndef CKFF_VS_TEXCOORD1
#define CKFF_VS_TEXCOORD1 1
#endif
#ifndef CKFF_VS_TEXCOORD2
#define CKFF_VS_TEXCOORD2 2
#endif
#ifndef CKFF_VS_TEXCOORD3
#define CKFF_VS_TEXCOORD3 3
#endif
#ifndef CKFF_VS_TEXCOORD4
#define CKFF_VS_TEXCOORD4 4
#endif
#ifndef CKFF_VS_TEXCOORD5
#define CKFF_VS_TEXCOORD5 5
#endif
#ifndef CKFF_VS_TEXCOORD6
#define CKFF_VS_TEXCOORD6 6
#endif
#ifndef CKFF_VS_TEXCOORD7
#define CKFF_VS_TEXCOORD7 7
#endif
#ifndef CKFF_VS_TEXFLAGS0
#define CKFF_VS_TEXFLAGS0 0
#endif
#ifndef CKFF_VS_TEXFLAGS1
#define CKFF_VS_TEXFLAGS1 0
#endif
#ifndef CKFF_VS_TEXFLAGS2
#define CKFF_VS_TEXFLAGS2 0
#endif
#ifndef CKFF_VS_TEXFLAGS3
#define CKFF_VS_TEXFLAGS3 0
#endif
#ifndef CKFF_VS_TEXFLAGS4
#define CKFF_VS_TEXFLAGS4 0
#endif
#ifndef CKFF_VS_TEXFLAGS5
#define CKFF_VS_TEXFLAGS5 0
#endif
#ifndef CKFF_VS_TEXFLAGS6
#define CKFF_VS_TEXFLAGS6 0
#endif
#ifndef CKFF_VS_TEXFLAGS7
#define CKFF_VS_TEXFLAGS7 0
#endif
#ifndef CKFF_VS_ACTIVE_TEXCOORD_COUNT
#define CKFF_VS_ACTIVE_TEXCOORD_COUNT 8
#endif
#endif

bool ckffVsFogEnabled(float runtimeMode)
{
#if defined(CKFF_FULL_SPECIALIZED)
    return CKFF_VS_FOG_MODE != 0;
#else
    return runtimeMode > 0.5;
#endif
}

int ckffVsTexcoordIndex(int stage, int packedIndex)
{
#if defined(CKFF_FULL_SPECIALIZED)
    if (stage == 0) return CKFF_VS_TEXCOORD0;
    if (stage == 1) return CKFF_VS_TEXCOORD1;
    if (stage == 2) return CKFF_VS_TEXCOORD2;
    if (stage == 3) return CKFF_VS_TEXCOORD3;
    if (stage == 4) return CKFF_VS_TEXCOORD4;
    if (stage == 5) return CKFF_VS_TEXCOORD5;
    if (stage == 6) return CKFF_VS_TEXCOORD6;
    return CKFF_VS_TEXCOORD7;
#else
    return packedIndex & 7;
#endif
}

int ckffVsTexTransformFlags(int stage, float runtimeFlags)
{
#if defined(CKFF_FULL_SPECIALIZED)
    if (stage == 0) return CKFF_VS_TEXFLAGS0;
    if (stage == 1) return CKFF_VS_TEXFLAGS1;
    if (stage == 2) return CKFF_VS_TEXFLAGS2;
    if (stage == 3) return CKFF_VS_TEXFLAGS3;
    if (stage == 4) return CKFF_VS_TEXFLAGS4;
    if (stage == 5) return CKFF_VS_TEXFLAGS5;
    if (stage == 6) return CKFF_VS_TEXFLAGS6;
    return CKFF_VS_TEXFLAGS7;
#else
    return int(runtimeFlags);
#endif
}

vec4 selectTexcoord(int index, vec2 tc0, vec2 tc1, vec2 tc2, vec2 tc3, vec2 tc4, vec2 tc5, vec2 tc6, vec2 tc7)
{
    vec2 uv = tc0;
    if (index == 1) uv = tc1;
    else if (index == 2) uv = tc2;
    else if (index == 3) uv = tc3;
    else if (index == 4) uv = tc4;
    else if (index == 5) uv = tc5;
    else if (index == 6) uv = tc6;
    else if (index == 7) uv = tc7;
    return vec4(uv, 0.0, 1.0);
}

vec4 transformTexcoord(int stage, vec4 coord)
{
#if defined(CKFF_FULL_SPECIALIZED)
    int flags = ckffVsTexTransformFlags(stage, 0.0);
#else
    vec4 params = u_stageParams[stage * 4 + 2];
    int flags = ckffVsTexTransformFlags(stage, params.z);
#endif
    if (flags == 0) return coord;

    int count = flags & 0xff;
    bool applyTransform = count > 1 && count <= 4;
    vec4 transformed = applyTransform ? mul(u_texMatrix[stage], coord) : coord;
    if ((flags & 0x100) != 0) {
        float divisor = count == 1 ? transformed.x
                      : count == 2 ? transformed.y
                      : count == 3 ? transformed.z
                      : transformed.w;
        transformed.w = divisor;
    }
    if (count > 0 && count < 4) {
        if (count <= 1) transformed.y = 0.0;
        if (count <= 2) transformed.z = 0.0;
        if (count <= 3 && (flags & 0x100) == 0) transformed.w = 0.0;
    }
    return transformed;
}

void main()
{
    float rhw = a_position.w == 0.0 ? 1.0 : a_position.w;
    float clipW = 1.0 / rhw;
    float clipX = a_position.x * u_viewport.x + u_viewport.z;
    float clipY = a_position.y * u_viewport.y + u_viewport.w;
    gl_Position = vec4(clipX * clipW, clipY * clipW, a_position.z * clipW, clipW);
    v_clipPos = vec4(a_position.xyz, 1.0);
#if CKFF_VS_CLIP_DISTANCE
    int clipCount = int(u_clipParams.x);
    v_clipDistance0.x = clipCount > 0 ? dot(v_clipPos, u_clipPlanes[0]) : 0.0;
    v_clipDistance0.y = clipCount > 1 ? dot(v_clipPos, u_clipPlanes[1]) : 0.0;
    v_clipDistance0.z = clipCount > 2 ? dot(v_clipPos, u_clipPlanes[2]) : 0.0;
    v_clipDistance0.w = clipCount > 3 ? dot(v_clipPos, u_clipPlanes[3]) : 0.0;
    v_clipDistance1.x = clipCount > 4 ? dot(v_clipPos, u_clipPlanes[4]) : 0.0;
    v_clipDistance1.y = clipCount > 5 ? dot(v_clipPos, u_clipPlanes[5]) : 0.0;
    v_clipDistance1.zw = vec2(0.0, 0.0);
#endif

    v_color0 = a_color0;
    v_color1 = a_color1;
    v_flatColor0 = v_color0;
    v_flatColor1 = v_color1;
#if defined(CKFF_FULL_SPECIALIZED)
    int tc0 = ckffVsTexcoordIndex(0, 0);
    int tc1 = ckffVsTexcoordIndex(1, 0);
    int tc2 = ckffVsTexcoordIndex(2, 0);
    int tc3 = ckffVsTexcoordIndex(3, 0);
    int tc4 = ckffVsTexcoordIndex(4, 0);
    int tc5 = ckffVsTexcoordIndex(5, 0);
    int tc6 = ckffVsTexcoordIndex(6, 0);
    int tc7 = ckffVsTexcoordIndex(7, 0);
#else
    int tc0 = ckffVsTexcoordIndex(0, int(u_stageParams[0 * 4 + 2].y));
    int tc1 = ckffVsTexcoordIndex(1, int(u_stageParams[1 * 4 + 2].y));
    int tc2 = ckffVsTexcoordIndex(2, int(u_stageParams[2 * 4 + 2].y));
    int tc3 = ckffVsTexcoordIndex(3, int(u_stageParams[3 * 4 + 2].y));
    int tc4 = ckffVsTexcoordIndex(4, int(u_stageParams[4 * 4 + 2].y));
    int tc5 = ckffVsTexcoordIndex(5, int(u_stageParams[5 * 4 + 2].y));
    int tc6 = ckffVsTexcoordIndex(6, int(u_stageParams[6 * 4 + 2].y));
    int tc7 = ckffVsTexcoordIndex(7, int(u_stageParams[7 * 4 + 2].y));
#endif
    v_texcoord0 = transformTexcoord(0, selectTexcoord(tc0, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7));
#if CKFF_VS_ACTIVE_TEXCOORD_COUNT > 1
    v_texcoord1 = transformTexcoord(1, selectTexcoord(tc1, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7));
#else
    v_texcoord1 = vec4(0.0, 0.0, 0.0, 1.0);
#endif
#if CKFF_VS_ACTIVE_TEXCOORD_COUNT > 2
    v_texcoord2 = transformTexcoord(2, selectTexcoord(tc2, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7));
#else
    v_texcoord2 = vec4(0.0, 0.0, 0.0, 1.0);
#endif
#if CKFF_VS_ACTIVE_TEXCOORD_COUNT > 3
    v_texcoord3 = transformTexcoord(3, selectTexcoord(tc3, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7));
#else
    v_texcoord3 = vec4(0.0, 0.0, 0.0, 1.0);
#endif
#if CKFF_VS_ACTIVE_TEXCOORD_COUNT > 4
    v_texcoord4 = transformTexcoord(4, selectTexcoord(tc4, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7));
#else
    v_texcoord4 = vec4(0.0, 0.0, 0.0, 1.0);
#endif
#if CKFF_VS_ACTIVE_TEXCOORD_COUNT > 5
    v_texcoord5 = transformTexcoord(5, selectTexcoord(tc5, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7));
#else
    v_texcoord5 = vec4(0.0, 0.0, 0.0, 1.0);
#endif
#if CKFF_VS_ACTIVE_TEXCOORD_COUNT > 6
    v_texcoord6 = transformTexcoord(6, selectTexcoord(tc6, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7));
#else
    v_texcoord6 = vec4(0.0, 0.0, 0.0, 1.0);
#endif
#if defined(CKFF_FULL_SPECIALIZED)
    float fogFactor = ckffPositionTFogFactor(CKFF_VS_FOG_MODE != 0, a_color1.a);
#else
    float fogFactor = ckffPositionTFogFactor(ckffVsFogEnabled(u_ffDrawParams[10].w), a_color1.a);
#endif
#if CKFF_VS_ACTIVE_TEXCOORD_COUNT > 7
    v_texcoord7Fog = transformTexcoord(7, selectTexcoord(tc7, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7));
#else
    v_texcoord7Fog = vec4(0.0, 0.0, 0.0, 1.0);
#endif
    v_texcoord7Fog.z = fogFactor;
}

