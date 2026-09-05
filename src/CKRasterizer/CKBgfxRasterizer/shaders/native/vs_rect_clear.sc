$input a_position
#include "bgfx_shader.sh"
uniform vec4 u_ckRectClearDepth;
void main()
{
    gl_Position = vec4(a_position.xy, u_ckRectClearDepth.x, 1.0);
}
