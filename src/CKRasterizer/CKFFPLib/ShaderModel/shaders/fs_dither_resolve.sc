$input v_texcoord0

#include "bgfx_shader.sh"

SAMPLER2D(s_sceneColor, 0);

uniform vec4 u_postParams;

void main()
{
    vec4 color = texture2D(s_sceneColor, v_texcoord0.xy);
    int x = int(gl_FragCoord.x) & 1;
    int y = int(gl_FragCoord.y) & 1;
    int rank = ((x ^ y) * 2) + y;
    float threshold = u_postParams.z > 0.5
        ? (float(rank) + 0.5) * 0.25 : 0.5;
    int format = int(u_postParams.w + 0.5);
    float rbLevels = format == 3 ? 15.0 : 31.0;
    vec3 levels = vec3(rbLevels, format == 1 ? 63.0 : rbLevels,
                       rbLevels);
    color.rgb = floor(clamp(color.rgb, vec3_splat(0.0), vec3_splat(1.0)) * levels +
                      threshold) / levels;
    if (format == 1)
        color.a = 1.0;
    else {
        float alphaLevels = format == 2 ? 1.0 : 15.0;
        color.a = floor(clamp(color.a, 0.0, 1.0) * alphaLevels + 0.5) /
                  alphaLevels;
    }
    gl_FragColor = clamp(color, vec4_splat(0.0), vec4_splat(1.0));
}
