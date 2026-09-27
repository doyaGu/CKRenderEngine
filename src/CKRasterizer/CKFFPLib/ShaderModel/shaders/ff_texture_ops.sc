// Fixed-function texture arguments, combiners and stage blending.

vec4 getSampleCoord(vec4 coord, int transformFlags)
{
    if ((transformFlags & 0x100) != 0) {
        coord /= abs(coord.w) < 0.0001 ? (coord.w < 0.0 ? -0.0001 : 0.0001) : coord.w;
    }
    return coord;
}

float computePixelFogFactor(float depth, int mode, float vertexFogFactor)
{
    if (mode == 0) return vertexFogFactor;
    return ckffFogFactor(depth, mode, u_ffDrawParams[10]);
}

vec4 applyArgModifiers(vec4 value, int arg)
{
    if ((arg & 0x10) != 0) {
        value.rgb = 1.0 - value.rgb;
        value.a = 1.0 - value.a;
    }
    if ((arg & 0x20) != 0) {
        value = value.aaaa;
    }
    return value;
}

vec4 getArg(int arg, vec4 textureColor, vec4 current, vec4 diffuse, vec4 specular,
            vec4 temp, vec4 stageConstant, bool premodulateCurrent)
{
    int baseArg = arg & ~(0x10 | 0x20);
    vec4 value = current;
    if (baseArg == 0) value = diffuse;
    else if (baseArg == 1) value = premodulateCurrent ? current * textureColor : current;
    else if (baseArg == 2) value = textureColor;
    else if (baseArg == 3) value = u_ffDrawParams[9];
    else if (baseArg == 4) value = specular;
    else if (baseArg == 5) value = temp;
    else if (baseArg == 6) value = stageConstant;
    return applyArgModifiers(value, arg);
}

vec4 applyOp(int op, vec4 a, vec4 b, vec4 c, vec4 dst, vec4 current, vec4 diffuse, vec4 textureColor)
{
    if (op == 1) return dst;
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
    if (op == 14) return mix(b, a, u_ffDrawParams[9].a);
    if (op == 15) return clamp(a + b * (1.0 - textureColor.a), 0.0, 1.0);
    if (op == 16) return mix(b, a, current.a);
    if (op == 17) return a;
    if (op == 18) return clamp(a + vec4_splat(a.a) * b, 0.0, 1.0);
    if (op == 19) return clamp(a * b + vec4_splat(a.a), 0.0, 1.0);
    if (op == 20) return clamp(a + (1.0 - a.a) * b, 0.0, 1.0);
    if (op == 21) return clamp((vec4_splat(1.0) - a) * b + vec4_splat(a.a), 0.0, 1.0);
    if (op == 22 || op == 23) return dst;
    if (op == 24) {
        float v = clamp(dot(a.rgb - 0.5, b.rgb - 0.5) * 4.0, 0.0, 1.0);
        return vec4_splat(v);
    }
    if (op == 25) return clamp(a * b + c, 0.0, 1.0);
    if (op == 26) return clamp(c * a + (vec4_splat(1.0) - c) * b, 0.0, 1.0);
    return current;
}

vec4 ckffBlendFactor(int factor, vec4 source, vec4 destination)
{
    if (factor == 1) return vec4_splat(0.0);
    if (factor == 2) return vec4_splat(1.0);
    if (factor == 3) return source;
    if (factor == 4) return vec4_splat(1.0) - source;
    if (factor == 5 || factor == 12) return source.aaaa;
    if (factor == 6 || factor == 13) return vec4_splat(1.0) - source.aaaa;
    if (factor == 7) return destination.aaaa;
    if (factor == 8) return vec4_splat(1.0) - destination.aaaa;
    if (factor == 9) return destination;
    if (factor == 10) return vec4_splat(1.0) - destination;
    if (factor == 11) {
        float saturated = min(source.a, 1.0 - destination.a);
        return vec4(saturated, saturated, saturated, 1.0);
    }
    return vec4_splat(0.0);
}

vec4 ckffStageBlend(vec4 source, vec4 destination, int packedFactors)
{
    int src = (packedFactors >> 4) & 15;
    int dst = packedFactors & 15;
    if (src == 12) { src = 5; dst = 6; }
    else if (src == 13) { src = 6; dst = 5; }
    return clamp(source * ckffBlendFactor(src, source, destination) +
                 destination * ckffBlendFactor(dst, source, destination), 0.0, 1.0);
}

bool alphaPass(float alpha, int func)
{
    float ref = u_ffDrawParams[8].x;
    int alphaPrecision = func / 16;
    func = func - alphaPrecision * 16;
    float alphaTestValue = alpha;
    if (alphaPrecision != 15) {
        alphaPrecision = min(alphaPrecision, 8);
        float precisionScale = exp2(float(8 + alphaPrecision));
        float factor = precisionScale - 1.0;
        float refScale = exp2(float(alphaPrecision));
        float refWrap = exp2(float(8 - alphaPrecision));
        alphaTestValue = round(alpha * factor);
        ref = floor(ref) * refScale + floor(floor(ref) / refWrap);
    } else {
        ref = ref / 255.0;
    }
    if (func == 0 || func == 8) return true;
    if (func == 1) return false;
    if (func == 2) return alphaTestValue < ref;
    if (func == 3) return alphaTestValue == ref;
    if (func == 4) return alphaTestValue <= ref;
    if (func == 5) return alphaTestValue > ref;
    if (func == 6) return alphaTestValue != ref;
    if (func == 7) return alphaTestValue >= ref;
    return true;
}

vec2 ckffDecodeBump(vec2 bump, bool unormEncoded)
{
    return unormEncoded
        ? clamp((bump * 255.0 - 128.0) / 127.0, vec2(-1.0, -1.0), vec2(1.0, 1.0))
        : bump;
}
