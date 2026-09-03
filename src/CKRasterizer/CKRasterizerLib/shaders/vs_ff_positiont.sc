$input a_position, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, a_color0, a_color1
#ifndef CKFF_VS_CLIP_DISTANCE
#define CKFF_VS_CLIP_DISTANCE 0
#endif
#if CKFF_VS_CLIP_DISTANCE
$output v_color0, v_color1, v_flatColor0, v_flatColor1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_texcoord5, v_texcoord6, v_texcoord7Fog, v_fogPos, v_clipDistance0, v_clipDistance1
#else
$output v_color0, v_color1, v_flatColor0, v_flatColor1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_texcoord5, v_texcoord6, v_texcoord7Fog, v_fogPos
#endif

#include "bgfx_shader.sh"
#include "ff_fog_common.sc"

#ifndef CKFF_NDC_MINUS_ONE_TO_ONE
#define CKFF_NDC_MINUS_ONE_TO_ONE 0
#endif

void ckffApplyBackendClipSpace(inout vec4 position)
{
#if CKFF_NDC_MINUS_ONE_TO_ONE
    position.z = position.z * 2.0 - position.w;
#endif
}

uniform vec4 u_viewport;
uniform vec4 u_ffDrawParams[20];
uniform vec4 u_stageParams[32];
uniform mat4 u_texMatrix[8];
#if CKFF_VS_CLIP_DISTANCE
uniform vec4 u_clipPlanes[6];
uniform vec4 u_clipParams;
#endif

vec4 selectTexcoord(int index, vec4 tc0, vec4 tc1, vec4 tc2, vec4 tc3, vec4 tc4, vec4 tc5, vec4 tc6, vec4 tc7)
{
    if (index == 1) return tc1;
    if (index == 2) return tc2;
    if (index == 3) return tc3;
    if (index == 4) return tc4;
    if (index == 5) return tc5;
    if (index == 6) return tc6;
    if (index == 7) return tc7;
    return tc0;
}

vec4 transformTexcoord(int stage, vec4 coord)
{
    vec4 params = u_stageParams[stage * 4 + 2];
    int flags = int(params.z);
    if (flags == 0) return coord;

    int count = flags & 0xff;
    vec4 transformed = coord;
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
    vec4 worldClipPos = vec4(a_position.xyz, 1.0);
    v_fogPos = gl_Position;
#if CKFF_VS_CLIP_DISTANCE
    int clipCount = int(u_clipParams.x);
    v_clipDistance0.x = clipCount > 0 ? dot(worldClipPos, u_clipPlanes[0]) : 0.0;
    v_clipDistance0.y = clipCount > 1 ? dot(worldClipPos, u_clipPlanes[1]) : 0.0;
    v_clipDistance0.z = clipCount > 2 ? dot(worldClipPos, u_clipPlanes[2]) : 0.0;
    v_clipDistance0.w = clipCount > 3 ? dot(worldClipPos, u_clipPlanes[3]) : 0.0;
    v_clipDistance1.x = clipCount > 4 ? dot(worldClipPos, u_clipPlanes[4]) : 0.0;
    v_clipDistance1.y = clipCount > 5 ? dot(worldClipPos, u_clipPlanes[5]) : 0.0;
    v_clipDistance1.zw = vec2(0.0, 0.0);
#endif

    v_color0 = a_color0;
    v_color1 = a_color1;
    v_flatColor0 = v_color0;
    v_flatColor1 = v_color1;
    int tc0 = int(u_stageParams[0 * 4 + 2].y) & 7;
    int tc1 = int(u_stageParams[1 * 4 + 2].y) & 7;
    int tc2 = int(u_stageParams[2 * 4 + 2].y) & 7;
    int tc3 = int(u_stageParams[3 * 4 + 2].y) & 7;
    int tc4 = int(u_stageParams[4 * 4 + 2].y) & 7;
    int tc5 = int(u_stageParams[5 * 4 + 2].y) & 7;
    int tc6 = int(u_stageParams[6 * 4 + 2].y) & 7;
    int tc7 = int(u_stageParams[7 * 4 + 2].y) & 7;
    v_texcoord0 = transformTexcoord(0, selectTexcoord(tc0, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7));
    v_texcoord1 = transformTexcoord(1, selectTexcoord(tc1, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7));
    v_texcoord2 = transformTexcoord(2, selectTexcoord(tc2, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7));
    v_texcoord3 = transformTexcoord(3, selectTexcoord(tc3, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7));
    v_texcoord4 = transformTexcoord(4, selectTexcoord(tc4, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7));
    v_texcoord5 = transformTexcoord(5, selectTexcoord(tc5, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7));
    v_texcoord6 = transformTexcoord(6, selectTexcoord(tc6, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7));
    float fogFactor = ckffPositionTFogFactor(u_ffDrawParams[10].w > 0.5, a_color1.a);
    v_texcoord7Fog = transformTexcoord(7, selectTexcoord(tc7, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7));
    v_texcoord7Fog.z = fogFactor;
    ckffApplyBackendClipSpace(gl_Position);
}
