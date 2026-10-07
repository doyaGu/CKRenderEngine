

void main()
{
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    v_texcoord0 = vec4(a_texcoord0.xy, 0.0, 1.0);
}
