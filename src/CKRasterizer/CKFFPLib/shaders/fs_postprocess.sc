$input v_texcoord0

#include "bgfx_shader.sh"

SAMPLER2D(s_sceneColor, 0);

uniform vec4 u_postParams;

float luma(vec3 color)
{
    return dot(color, vec3(0.299, 0.587, 0.114));
}

vec4 sampleScene(vec2 uv)
{
    return texture2D(s_sceneColor, uv);
}

vec4 fxaa(vec2 uv)
{
    vec2 texel = u_postParams.xy;
    vec3 rgbNW = sampleScene(uv + vec2(-1.0, -1.0) * texel).rgb;
    vec3 rgbNE = sampleScene(uv + vec2( 1.0, -1.0) * texel).rgb;
    vec3 rgbSW = sampleScene(uv + vec2(-1.0,  1.0) * texel).rgb;
    vec3 rgbSE = sampleScene(uv + vec2( 1.0,  1.0) * texel).rgb;
    vec4 center = sampleScene(uv);
    vec3 rgbM = center.rgb;

    float lumaNW = luma(rgbNW);
    float lumaNE = luma(rgbNE);
    float lumaSW = luma(rgbSW);
    float lumaSE = luma(rgbSE);
    float lumaM = luma(rgbM);
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125, 0.0078125);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-8.0, -8.0), vec2(8.0, 8.0)) * texel;

    vec3 rgbA = 0.5 * (
        sampleScene(uv + dir * (1.0 / 3.0 - 0.5)).rgb +
        sampleScene(uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = 0.5 * rgbA + 0.25 * (
        sampleScene(uv + dir * -0.5).rgb +
        sampleScene(uv + dir * 0.5).rgb);
    float lumaB = luma(rgbB);

    vec3 result = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
    return vec4(result, center.a);
}

vec4 sharpen(vec2 uv, vec4 color)
{
    float amount = u_postParams.w;
    vec2 texel = u_postParams.xy;
    vec3 center = color.rgb;
    vec3 north = sampleScene(uv + vec2( 0.0, -1.0) * texel).rgb;
    vec3 south = sampleScene(uv + vec2( 0.0,  1.0) * texel).rgb;
    vec3 west  = sampleScene(uv + vec2(-1.0,  0.0) * texel).rgb;
    vec3 east  = sampleScene(uv + vec2( 1.0,  0.0) * texel).rgb;
    vec3 blur = (north + south + west + east) * 0.25;

    float lumaCenter = luma(center);
    float lumaNorth = luma(north);
    float lumaSouth = luma(south);
    float lumaWest = luma(west);
    float lumaEast = luma(east);
    float lumaMin = min(lumaCenter, min(min(lumaNorth, lumaSouth), min(lumaWest, lumaEast)));
    float lumaMax = max(lumaCenter, max(max(lumaNorth, lumaSouth), max(lumaWest, lumaEast)));
    float localContrast = lumaMax - lumaMin;

    vec3 detail = center - blur;
    float detailLuma = luma(detail);
    float detailAbs = abs(detailLuma);
    float detailLimit = max(localContrast * 0.35, 0.001);
    float detailLimiter = detailAbs > detailLimit ? detailLimit / detailAbs : 1.0;
    float edgeGate = smoothstep(0.015, 0.12, localContrast);
    float strength = amount * edgeGate * detailLimiter;

    vec3 result = center + detail * strength;
    float resultLuma = luma(result);
    float haloPad = localContrast * 0.25;
    float clampedLuma = clamp(resultLuma, max(0.0, lumaMin - haloPad), min(1.0, lumaMax + haloPad));
    result += vec3(clampedLuma - resultLuma, clampedLuma - resultLuma, clampedLuma - resultLuma);
    result = clamp(result, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0));
    return vec4(result, color.a);
}

void main()
{
    vec2 uv = v_texcoord0.xy;
    vec4 color = u_postParams.z > 0.5 ? fxaa(uv) : sampleScene(uv);
    gl_FragColor = u_postParams.w > 0.001 ? sharpen(uv, color) : color;
}
