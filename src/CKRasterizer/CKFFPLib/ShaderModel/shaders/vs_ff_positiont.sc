$input a_position, a_tangent, a_bitangent, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, a_color0, a_color1
#ifndef CKFF_VS_CLIP_DISTANCE
#define CKFF_VS_CLIP_DISTANCE 0
#endif
$output v_color0, v_color1, v_flatColor0, v_flatColor1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_texcoord5, v_texcoord6, v_texcoord7Fog, v_fogPos, v_clipDistance0, v_clipDistance1

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
uniform vec4 u_stageParams[16];
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
    int flags = int(u_stageParams[stage * 2].y);
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
    // D3D8 POSITIONT coordinates name integer pixel centers; modern backends
    // rasterize at half-integer centers. Apply the compatibility offset here.
    float clipX = (a_position.x + 0.5) * u_viewport.x + u_viewport.z;
    float clipY = (a_position.y + 0.5) * u_viewport.y + u_viewport.w;
    gl_Position = vec4(clipX * clipW, clipY * clipW, a_position.z * clipW, clipW);
    vec4 worldClipPos = vec4(a_position.xyz, 1.0);
    v_fogPos = gl_Position;
    v_fogPos.x = clipW;
#if CKFF_VS_CLIP_DISTANCE
    int clipCount = int(u_clipParams.x);
    v_clipDistance0.x = clipCount > 0 ? dot(worldClipPos, u_clipPlanes[0]) : 0.0;
    v_clipDistance0.y = clipCount > 1 ? dot(worldClipPos, u_clipPlanes[1]) : 0.0;
    v_clipDistance0.z = clipCount > 2 ? dot(worldClipPos, u_clipPlanes[2]) : 0.0;
    v_clipDistance0.w = clipCount > 3 ? dot(worldClipPos, u_clipPlanes[3]) : 0.0;
    v_clipDistance1.x = clipCount > 4 ? dot(worldClipPos, u_clipPlanes[4]) : 0.0;
    v_clipDistance1.y = clipCount > 5 ? dot(worldClipPos, u_clipPlanes[5]) : 0.0;
    v_clipDistance1.zw = vec2(0.0, 0.0);
#elif !CKFF_NATIVE_SDL_GPU
    v_clipDistance0 = vec4_splat(0.0);
    v_clipDistance1 = vec4_splat(0.0);
#endif

    v_color0 = a_color0;
    v_color1 = a_color1;
    v_flatColor0 = v_color0;
    v_flatColor1 = v_color1;
    int tc0 = int(u_stageParams[0 * 2].x) & 7;
    int tc1 = int(u_stageParams[1 * 2].x) & 7;
    int tc2 = int(u_stageParams[2 * 2].x) & 7;
    int tc3 = int(u_stageParams[3 * 2].x) & 7;
    int tc4 = int(u_stageParams[4 * 2].x) & 7;
    int tc5 = int(u_stageParams[5 * 2].x) & 7;
    int tc6 = int(u_stageParams[6 * 2].x) & 7;
    int tc7 = int(u_stageParams[7 * 2].x) & 7;
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
    if (u_ffDrawParams[4].z > 0.5) {
        v_texcoord0 *= clipW;
        v_texcoord1 *= clipW;
        v_texcoord2 *= clipW;
        v_texcoord3 *= clipW;
        v_texcoord4 *= clipW;
        v_texcoord5 *= clipW;
        v_texcoord6 *= clipW;
        v_texcoord7Fog.xyw *= clipW;
    }
    // ZBIAS approximation: pull the depth towards the viewer by the resolved offset.
    gl_Position.z -= u_ffDrawParams[4].y * gl_Position.w;
    if (u_ffDrawParams[4].w > 0.5)
        gl_Position.xy += vec2(a_tangent.x, a_bitangent.x) * u_viewport.xy * gl_Position.w;
    ckffApplyBackendClipSpace(gl_Position);
}
