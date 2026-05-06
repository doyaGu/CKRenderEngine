$input a_position, a_texcoord0, a_color0, a_color1
$output v_color0, v_color1, v_texcoord0, v_fogFactor

#include "bgfx_shader.sh"

uniform vec4 u_viewport;

void main()
{
    float rhw = a_position.w == 0.0 ? 1.0 : a_position.w;
    float clipW = 1.0 / rhw;
    float clipX = a_position.x * u_viewport.x + u_viewport.z;
    float clipY = a_position.y * u_viewport.y + u_viewport.w;
    gl_Position = vec4(clipX * clipW, clipY * clipW, a_position.z * clipW, clipW);

    v_color0 = a_color0;
    v_color1 = a_color1;
    v_texcoord0 = a_texcoord0;
    v_fogFactor = 1.0;
}
