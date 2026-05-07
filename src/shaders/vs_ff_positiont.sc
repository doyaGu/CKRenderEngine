$input a_position, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7, a_color0, a_color1
$output v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_texcoord5, v_texcoord6, v_texcoord7Fog, v_clipPos

#include "bgfx_shader.sh"

uniform vec4 u_viewport;
uniform vec4 u_fogParams;
uniform vec4 u_stageParams[32];

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
    return vec4(uv, 0.0, 0.0);
}

void main()
{
    float rhw = a_position.w == 0.0 ? 1.0 : a_position.w;
    float clipW = 1.0 / rhw;
    float clipX = a_position.x * u_viewport.x + u_viewport.z;
    float clipY = a_position.y * u_viewport.y + u_viewport.w;
    gl_Position = vec4(clipX * clipW, clipY * clipW, a_position.z * clipW, clipW);
    v_clipPos = vec4(a_position.xyz, 1.0);

    v_color0 = a_color0;
    v_color1 = a_color1;
    int tc0 = int(u_stageParams[0 * 4 + 2].y) & 7;
    int tc1 = int(u_stageParams[1 * 4 + 2].y) & 7;
    int tc2 = int(u_stageParams[2 * 4 + 2].y) & 7;
    int tc3 = int(u_stageParams[3 * 4 + 2].y) & 7;
    int tc4 = int(u_stageParams[4 * 4 + 2].y) & 7;
    int tc5 = int(u_stageParams[5 * 4 + 2].y) & 7;
    int tc6 = int(u_stageParams[6 * 4 + 2].y) & 7;
    int tc7 = int(u_stageParams[7 * 4 + 2].y) & 7;
    v_texcoord0 = selectTexcoord(tc0, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7);
    v_texcoord1 = selectTexcoord(tc1, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7);
    v_texcoord2 = selectTexcoord(tc2, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7);
    v_texcoord3 = selectTexcoord(tc3, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7);
    v_texcoord4 = selectTexcoord(tc4, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7);
    v_texcoord5 = selectTexcoord(tc5, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7);
    v_texcoord6 = selectTexcoord(tc6, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7);
    float fogFactor = u_fogParams.w > 0.5 ? a_color1.a : 1.0;
    v_texcoord7Fog = selectTexcoord(tc7, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6, a_texcoord7);
    v_texcoord7Fog.z = fogFactor;
}
