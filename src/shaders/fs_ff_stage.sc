$input v_color0, v_color1, v_texcoord0, v_fogFactor

#include "bgfx_shader.sh"

uniform vec4 u_fogColor;
uniform vec4 u_alphaParams;
uniform vec4 u_texFactor;
uniform vec4 u_stageParams[6];

SAMPLER2D(s_texture0, 0);
SAMPLER2D(s_texture1, 1);
SAMPLER2D(s_texture2, 2);

vec4 getTextureColor(int stage, vec2 uv, bool hasTexture)
{
    if (!hasTexture) return vec4(1.0, 1.0, 1.0, 1.0);
    if (stage == 0) return texture2D(s_texture0, uv);
    if (stage == 1) return texture2D(s_texture1, uv);
    return texture2D(s_texture2, uv);
}

vec4 applyArgModifiers(vec4 value, int arg)
{
    if ((arg & 0x20) != 0) {
        value.rgb = value.aaa;
    }
    if ((arg & 0x10) != 0) {
        value.rgb = 1.0 - value.rgb;
        value.a = 1.0 - value.a;
    }
    return value;
}

vec4 getArg(int arg, vec4 textureColor, vec4 current, vec4 diffuse, vec4 specular)
{
    int baseArg = arg & ~(0x10 | 0x20);
    vec4 value = current;
    if (baseArg == 0) value = diffuse;
    else if (baseArg == 1) value = current;
    else if (baseArg == 2) value = textureColor;
    else if (baseArg == 3) value = u_texFactor;
    else if (baseArg == 4) value = specular;
    return applyArgModifiers(value, arg);
}

vec4 applyOp(int op, vec4 a, vec4 b, vec4 current, vec4 diffuse, vec4 textureColor)
{
    if (op == 1) return current;
    if (op == 2) return a;
    if (op == 3) return b;
    if (op == 4) return a * b;
    if (op == 5) return clamp(a * b * 2.0, 0.0, 1.0);
    if (op == 6) return clamp(a * b * 4.0, 0.0, 1.0);
    if (op == 7) return clamp(a + b, 0.0, 1.0);
    if (op == 8) return clamp(a + b - 0.5, 0.0, 1.0);
    if (op == 9) return clamp((a + b - 0.5) * 2.0, 0.0, 1.0);
    if (op == 10) return clamp(a - b, 0.0, 1.0);
    if (op == 11) return clamp(a + b - a * b, 0.0, 1.0);
    if (op == 12) return mix(b, a, diffuse.a);
    if (op == 13) return mix(b, a, textureColor.a);
    if (op == 14) return mix(b, a, u_texFactor.a);
    if (op == 15) return clamp(a + b * (1.0 - textureColor.a), 0.0, 1.0);
    if (op == 16) return mix(b, a, current.a);
    if (op == 18) return clamp(a + a.a * b, 0.0, 1.0);
    if (op == 19) return clamp(a * b + a.a, 0.0, 1.0);
    if (op == 20) return clamp(a + (1.0 - a.a) * b, 0.0, 1.0);
    if (op == 21) return clamp((1.0 - a) * b + a.a, 0.0, 1.0);
    if (op == 24) {
        float v = dot(a.rgb * 2.0 - 1.0, b.rgb * 2.0 - 1.0);
        return vec4(v, v, v, current.a);
    }
    return a * b;
}

bool alphaPass(float alpha)
{
    float ref = u_alphaParams.x;
    int func = int(u_alphaParams.y);
    if (func == 0 || func == 8) return true;
    if (func == 1) return false;
    if (func == 2) return alpha < ref;
    if (func == 3) return abs(alpha - ref) < 0.0001;
    if (func == 4) return alpha <= ref;
    if (func == 5) return alpha > ref;
    if (func == 6) return abs(alpha - ref) >= 0.0001;
    if (func == 7) return alpha >= ref;
    return true;
}

void main()
{
    vec4 current = v_color0;

    for (int stage = 0; stage < 3; ++stage) {
        vec4 colorParams = u_stageParams[stage * 2 + 0];
        vec4 alphaParams = u_stageParams[stage * 2 + 1];
        int colorOp = int(colorParams.x);
        int alphaOp = int(alphaParams.x);
        bool hasTexture = colorParams.w > 0.5;

        if (colorOp == 1) break;

        vec4 texColor = getTextureColor(stage, v_texcoord0, hasTexture);
        vec4 colorA = getArg(int(colorParams.y), texColor, current, v_color0, v_color1);
        vec4 colorB = getArg(int(colorParams.z), texColor, current, v_color0, v_color1);
        vec4 alphaA = getArg(int(alphaParams.y), texColor, current, v_color0, v_color1);
        vec4 alphaB = getArg(int(alphaParams.z), texColor, current, v_color0, v_color1);

        vec4 colorResult = applyOp(colorOp, colorA, colorB, current, v_color0, texColor);
        vec4 alphaResult = applyOp(alphaOp, alphaA, alphaB, current, v_color0, texColor);
        current.rgb = colorResult.rgb;
        current.a = alphaResult.a;
    }

    if (u_alphaParams.z > 0.5) {
        current.rgb += v_color1.rgb;
    }
    if (!alphaPass(current.a)) discard;
    current.rgb = mix(u_fogColor.rgb, current.rgb, v_fogFactor);
    gl_FragColor = clamp(current, 0.0, 1.0);
}
