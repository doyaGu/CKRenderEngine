#include "CKFFNative3dJit.h"
#include "CKFFShaderInterface.h"
#include "CKJitBuilder.h"

#include <cstring>
#include <cmath>

namespace {
struct Row { uint32_t Buffer = 0, Index = 0; };
struct Uniforms {
    uint32_t Count = 0, Rows[CKJIT_MAX_UNIFORM_BUFFERS] = {};
    Row Matrices, Palette, Draw, Textures, Viewport, Stages, Lights, ClipPlanes, ClipParams;
};

bool ResolveUniforms(Uniforms &out) {
    const CKFFProgramDesc desc = CKFFBuildProgramInterface(0, 0, CKRST_SHADER_FORMAT_SPIRV, FALSE, FALSE);
    for (const auto &buffer : desc.UniformBuffers) {
        if (buffer.Stage != CKRST_SHADER_VERTEX) continue;
        if (buffer.Slot >= CKJIT_MAX_UNIFORM_BUFFERS || buffer.Size % 16) return false;
        out.Rows[buffer.Slot] = buffer.Size / 16;
        if (buffer.Slot >= out.Count) out.Count = buffer.Slot + 1;
    }
    struct Block { CKFFConstantBlock Id; uint32_t Rows; Row *Target; };
    const Block blocks[] = {{CKRST_BLOCK_MATRICES, 4 * CKFF_MATRIX_VEC4_COUNT, &out.Matrices},
                            {CKRST_BLOCK_VERTEX_BLEND_MATRICES, 4 * CKFF_VERTEX_BLEND_MATRIX_COUNT, &out.Palette},
                            {CKRST_BLOCK_DRAW_PARAMS, CKFF_DRAW_PARAM_VEC4_COUNT, &out.Draw},
                            {CKRST_BLOCK_TEX_MATRICES, 4 * CKFF_MAX_TEXTURE_STAGES, &out.Textures},
                            {CKRST_BLOCK_VIEWPORT, 1, &out.Viewport},
                            {CKRST_BLOCK_STAGE_PARAMS, CKFF_STAGE_PARAM_VEC4_COUNT, &out.Stages},
                            {CKRST_BLOCK_LIGHTS, CKFF_MAX_LIGHTS * 7, &out.Lights},
                            {CKRST_BLOCK_CLIP_PLANES, CKFF_CLIP_PLANE_COUNT, &out.ClipPlanes},
                            {CKRST_BLOCK_CLIP_PARAMS, 1, &out.ClipParams}};
    for (const auto &block : blocks) {
        bool found = false;
        for (const auto &binding : desc.Uniforms) {
            if (binding.Stage != CKRST_SHADER_VERTEX || binding.Slot != (CKDWORD)block.Id) continue;
            if (binding.Offset % 16 || binding.Size() < block.Rows * 16 || binding.BufferSlot >= out.Count ||
                binding.Offset / 16 + block.Rows > out.Rows[binding.BufferSlot]) return false;
            *block.Target = {binding.BufferSlot, binding.Offset / 16};
            found = true;
            break;
        }
        if (!found) return false;
    }
    return true;
}

bool SupportedDeformation(const CKFFConstantSet &constants) {
    const CKBYTE *draw = constants[CKRST_BLOCK_DRAW_PARAMS].Bytes.Begin();
    float tween[4];
    std::memcpy(tween, draw + CKFF_DRAW_PARAM_TWEEN * 16, sizeof(tween));
    if (tween[1] == 0.0f) return true;
    if (tween[1] == 1.0f)
        return constants[CKRST_BLOCK_VERTEX_BLEND_MATRICES].Bytes.Size() >= CKFF_VERTEX_BLEND_MATRIX_COUNT * 64 &&
               (tween[2] == 0.0f || tween[2] == 1.0f || tween[2] == 2.0f || tween[2] == 3.0f) &&
               (tween[3] == 0.0f || tween[3] == 1.0f);
    // The resolver supplies a position/normal stream mask and no indices.
    // Finite factors are deliberately not clamped: extrapolation is legal.
    return tween[1] == 2.0f && std::isfinite(tween[0]) && tween[3] == 0.0f &&
           (tween[2] == 1.0f || tween[2] == 2.0f || tween[2] == 3.0f);
}
}

bool CKFFNativeUnlitDraw(const CKFFConstantSet &constants) {
    const auto &bytes = constants[CKRST_BLOCK_DRAW_PARAMS].Bytes;
    if (bytes.Size() < CKFF_DRAW_PARAM_VEC4_COUNT * 16) return false;
    float lightCount;
    std::memcpy(&lightCount, bytes.Begin() + CKFF_DRAW_PARAM_LIGHTING * 16, sizeof(float));
    return lightCount == -1.0f && SupportedDeformation(constants);
}

bool CKFFNativeLitDraw(const CKFFConstantSet &constants) {
    const auto &bytes = constants[CKRST_BLOCK_DRAW_PARAMS].Bytes;
    if (bytes.Size() < CKFF_DRAW_PARAM_VEC4_COUNT * 16) return false;
    float lightCount;
    std::memcpy(&lightCount, bytes.Begin() + CKFF_DRAW_PARAM_LIGHTING * 16, sizeof(float));
    return SupportedDeformation(constants) && std::isfinite(lightCount) && lightCount >= 0 &&
           lightCount <= CKFF_MAX_LIGHTS && std::floor(lightCount) == lightCount;
}

static bool CompileNative3dProgram(const CKFFNativeFragmentKey &input, CKFFSamplerLayout layout,
                                  CK_SHADER_FORMAT referenceFormat, bool lighting, CKJitVertexShader &out, bool clipping) {
    if ((CKDWORD)layout >= CKFF_SAMPLER_LAYOUT_COUNT ||
        (referenceFormat != CKRST_SHADER_FORMAT_DXIL && referenceFormat != CKRST_SHADER_FORMAT_SPIRV)) return false;
    Uniforms rows;
    if (!ResolveUniforms(rows)) return false;
    CKFFNativeFragmentKey key = input;
    CKFFCanonicalizeNativeFragmentKey(key, layout);
    CKJitBuilder b(rows.Rows, rows.Count);
    const auto uniform = [&](Row row, uint32_t offset = 0) { return b.Uniform(row.Buffer, row.Index + offset); };
    const auto c = [&](CKJitValue value, uint32_t index) { return b.Component(value, index); };
    // Native matrices are four packed columns. DXIL uses explicit MADs;
    // SPIR-V matrix operations retain contraction freedom instead of forcing
    // Fma. Cube reflection magnifies this rounding difference. Keep the final
    // translation addition shared by lit and unlit geometry.
    const auto transformColumns = [&](const CKJitValue *columns, CKJitValue value, bool translate, bool homogeneous) {
        CKJitValue result = b.Mul(columns[0], c(value, 0));
        for (unsigned axis = 1; axis < 3; ++axis) {
            const CKJitValue column = columns[axis], component = c(value, axis);
            result = referenceFormat == CKRST_SHADER_FORMAT_DXIL ? b.Mad(column, component, result)
                : b.Add(b.Mul(column, component), result);
        }
        if (!translate) return result;
        if (!homogeneous) return b.Add(result, columns[3]);
        return referenceFormat == CKRST_SHADER_FORMAT_DXIL ? b.Mad(columns[3], c(value, 3), result)
            : b.Add(b.Mul(columns[3], c(value, 3)), result);
    };
    const auto transform = [&](Row row, unsigned index, CKJitValue value, bool translate, bool homogeneous = false) {
        CKJitValue columns[4];
        for (unsigned axis = 0; axis < 4; ++axis) columns[axis] = uniform(row, index * 4 + axis);
        return transformColumns(columns, value, translate, homogeneous);
    };
    const auto normalize = [&](CKJitValue value) { return b.Div(value, b.Length(value)); };
    const CKJitValue zero = b.Float(0), one = b.Float(1), half = b.Float(0.5f);
    CKJitValue position = b.Input({0, 3, CKJIT_INPUT_ATTRIBUTE});
    CKJitValue normal = b.Input({1, 3, CKJIT_INPUT_ATTRIBUTE});
    const CKJitValue tangent = b.Input({2, 3, CKJIT_INPUT_ATTRIBUTE});
    CKJitValue diffuse = b.Input({4, 4, CKJIT_INPUT_ATTRIBUTE});
    CKJitValue specular = b.Input({5, 4, CKJIT_INPUT_ATTRIBUTE});
    const CKJitValue weight = b.Input({7, 3, CKJIT_INPUT_ATTRIBUTE});
    const CKJitValue tween = uniform(rows.Draw, CKFF_DRAW_PARAM_TWEEN);
    const CKJitValue tweening = b.Equal(c(tween, 1), b.Float(2));
    const CKJitValue streams = b.FloatToInt(c(tween, 2));
    const auto interpolate = [&](CKJitValue from, CKJitValue to) {
        return referenceFormat == CKRST_SHADER_FORMAT_DXIL
            ? b.Mad(b.Sub(to, from), c(tween, 0), from) : b.Lerp(from, to, c(tween, 0));
    };
    position = b.Select(b.And(tweening, b.IntNotEqual(b.IntAnd(streams, b.Int(1)), b.Int(0))),
        interpolate(position, tangent), position);
    normal = b.Select(b.And(tweening, b.IntNotEqual(b.IntAnd(streams, b.Int(2)), b.Int(0))),
        interpolate(normal, b.Input({3, 3, CKJIT_INPUT_ATTRIBUTE})), normal);
    b.If(b.Equal(c(tween, 1), one));
    const CKJitValue indices = b.Input({6, 4, CKJIT_INPUT_ATTRIBUTE, CKJIT_INPUT_UINT});
    CKJitValue remaining = one, blendedPosition = b.Float4(0, 0, 0, 0), blendedNormal = b.Float3(0, 0, 0);
    for (unsigned slot = 0; slot < CKFF_VERTEX_BLEND_MATRIX_COUNT; ++slot) {
        b.If(b.IntGreaterEqual(streams, b.Int(slot)));
        const CKJitValue explicitWeight = b.IntGreater(streams, b.Int(slot));
        const CKJitValue influence = b.Select(explicitWeight, slot < 3 ? c(weight, slot) : zero, remaining);
        const CKJitValue nextRemaining = b.Select(explicitWeight, b.Sub(remaining, influence), remaining);
        const CKJitValue index = b.Select(b.Greater(c(tween, 3), half), c(indices, slot), b.Int(slot));
        CKJitValue columns[4];
        for (unsigned axis = 0; axis < 4; ++axis) {
            columns[axis] = uniform(rows.Palette, 3 * 4 + axis);
            for (int matrix = 2; matrix >= 0; --matrix)
                columns[axis] = b.Select(matrix ? b.IntEqual(index, b.Int(matrix)) : b.IntLessEqual(index, b.Int(0)),
                    uniform(rows.Palette, matrix * 4 + axis), columns[axis]);
        }
        const CKJitValue worldPosition = transformColumns(columns, position, true, false);
        const CKJitValue worldNormal = b.Swizzle(transformColumns(columns, normal, false, false), "xyz");
        const auto accumulate = [&](CKJitValue value, CKJitValue sum) {
            return referenceFormat == CKRST_SHADER_FORMAT_DXIL ? b.Mad(value, influence, sum) : b.Add(b.Mul(value, influence), sum);
        };
        b.Else({nextRemaining, accumulate(worldPosition, blendedPosition), accumulate(worldNormal, blendedNormal)});
        CKJitValue joined[3];
        b.EndIf({remaining, blendedPosition, blendedNormal}, joined);
        remaining = joined[0]; blendedPosition = joined[1]; blendedNormal = joined[2];
    }
    const CKJitValue blendedClip = transform(rows.Matrices, CKFF_MATRIX_MVP_OR_VIEWPROJ, blendedPosition, true, true);
    const CKJitValue blendedView = b.Swizzle(transform(rows.Matrices, CKFF_MATRIX_MODELVIEW, blendedPosition, true, true), "xyz");
    const CKJitValue blendedViewNormal = b.Swizzle(transform(rows.Matrices, CKFF_MATRIX_NORMAL, blendedNormal, false), "xyz");
    b.Else({blendedClip, blendedView, blendedViewNormal, blendedPosition});
    CKJitValue geometry[4];
    b.EndIf({transform(rows.Matrices, CKFF_MATRIX_MVP_OR_VIEWPROJ, position, true),
        b.Swizzle(transform(rows.Matrices, CKFF_MATRIX_MODELVIEW, position, true), "xyz"),
        b.Swizzle(transform(rows.Matrices, CKFF_MATRIX_NORMAL, normal, false), "xyz"),
        transform(rows.Matrices, CKFF_MATRIX_WORLD, position, true)}, geometry);
    const CKJitValue clip = geometry[0], view = geometry[1], transformedNormal = geometry[2];
    CKJitValue distances[8];
    if (clipping) {
        const CKJitValue count = b.FloatToInt(c(uniform(rows.ClipParams), 0));
        for (unsigned i = 0; i < CKFF_CLIP_PLANE_COUNT; ++i)
            distances[i] = b.Select(b.IntGreater(count, b.Int(i)), b.Dot(geometry[3], uniform(rows.ClipPlanes, i)), zero);
        distances[6] = distances[7] = zero;
    }
    const CKJitValue lightFlags = uniform(rows.Draw, CKFF_DRAW_PARAM_LIGHT_FLAGS);
    const CKJitValue viewNormal = b.Select(b.Greater(c(lightFlags, 1), half), normalize(transformedNormal), transformedNormal);
    const CKJitValue params = uniform(rows.Draw, CKFF_DRAW_PARAM_MATERIAL_POWER);
    if (lighting) {
        const CKJitValue sources = uniform(rows.Draw, CKFF_DRAW_PARAM_MATERIAL_SOURCES);
        const auto material = [&](unsigned channel) {
            const CKJitValue source = b.FloatToInt(b.Add(c(sources, channel), half));
            return b.Select(b.IntEqual(source, b.Int(1)), diffuse,
                b.Select(b.IntEqual(source, b.Int(2)), specular, uniform(rows.Draw, channel)));
        };
        const CKJitValue matDiffuse = material(0), matAmbient = material(1);
        const CKJitValue matSpecular = material(2), matEmissive = material(3);
        const CKJitValue power = c(params, 0);
        const CKJitValue lightState = uniform(rows.Draw, CKFF_DRAW_PARAM_LIGHTING);
        CKJitValue accum[] = {b.Float4(0, 0, 0, 0), b.Float4(0, 0, 0, 0), b.Float3(0, 0, 0)};
        // Constant row addresses keep the native uniform ABI. Inactive lights
        // must not evaluate normalization/power expressions (including NaNs).
        for (unsigned i = 0; i < CKFF_MAX_LIGHTS; ++i) {
            b.If(b.Greater(c(lightState, 0), b.Float(float(i))));
            const auto light = [&](unsigned field) {
                CKJitValue value = uniform(rows.Lights, i * 7 + field);
                return i == 0 ? b.Select(b.Greater(c(lightFlags, 3), half),
                    uniform(rows.Draw, CKFF_DRAW_PARAM_INLINE_LIGHT_BASE + field), value) : value;
            };
            const CKJitValue pos = light(0), dir = light(1), color = light(2), spec = light(3);
            const CKJitValue ambient = light(4), attenuation = light(5), spot = light(6);
            b.If(b.Less(c(pos, 3), half));
            const CKJitValue directional = b.Neg(normalize(b.Swizzle(dir, "xyz")));
            b.Else({directional, one});
            const CKJitValue delta = b.Sub(b.Swizzle(pos, "xyz"), view);
            const CKJitValue distance = b.Length(delta);
            const CKJitValue local = b.Div(delta, b.Max(distance, b.Float(0.0001f)));
            CKJitValue atten = b.Div(one, b.Max(b.Add(b.Add(c(attenuation, 0), b.Mul(c(attenuation, 1), distance)),
                b.Mul(b.Mul(c(attenuation, 2), distance), distance)), b.Float(0.0001f)));
            atten = b.Select(b.And(b.Greater(c(dir, 3), zero), b.Greater(distance, c(dir, 3))), zero, atten);
            b.If(b.Greater(c(pos, 3), b.Float(1.5f)));
            const CKJitValue rho = b.Dot(b.Neg(local), normalize(b.Swizzle(dir, "xyz")));
            const CKJitValue cone = b.Saturate(b.Div(b.Sub(rho, c(spot, 1)),
                b.Max(b.Sub(c(spot, 0), c(spot, 1)), b.Float(0.0001f))));
            CKJitValue spotAtten = b.Exp2(b.Mul(b.Log2(cone), c(attenuation, 3)));
            spotAtten = b.Select(b.LessEqual(rho, c(spot, 1)), zero, spotAtten);
            spotAtten = b.Select(b.Greater(rho, c(spot, 0)), one, spotAtten);
            b.Else({b.Mul(atten, spotAtten)});
            atten = b.EndIf(atten);
            CKJitValue resolved[2];
            b.EndIf({local, atten}, resolved);
            const CKJitValue toLight = resolved[0];
            atten = resolved[1];
            const CKJitValue nDotL = b.Max(b.Dot(viewNormal, normalize(toLight)), zero);
            const CKJitValue nextAmbient = b.Add(accum[0], b.Mul(ambient, atten));
            const CKJitValue nextDiffuse = b.Add(accum[1], b.Mul(b.Mul(color, nDotL), atten));
            b.If(b.And(b.Greater(nDotL, zero), b.Greater(power, zero)));
            const CKJitValue eye = b.Select(b.Greater(c(lightFlags, 0), half), normalize(view), b.Float3(0, 0, 1));
            const CKJitValue halfVector = normalize(b.Sub(toLight, eye));
            const CKJitValue nDotH = b.Max(b.Dot(viewNormal, halfVector), zero);
            const CKJitValue highlight = b.Exp2(b.Mul(b.Log2(nDotH), power));
            b.Else({b.Add(accum[2], b.Mul(b.Mul(b.Swizzle(spec, "xyz"), highlight), atten))});
            const CKJitValue nextSpecular = b.EndIf(accum[2]);
            b.Else({nextAmbient, nextDiffuse, nextSpecular});
            b.EndIf({accum[0], accum[1], accum[2]}, accum);
        }
        CKJitValue lit = b.Add(matEmissive, b.Mul(matAmbient, b.Construct({b.Swizzle(lightState, "yzw"), one})));
        lit = b.Add(lit, b.Mul(matAmbient, accum[0]));
        lit = b.Add(lit, b.Mul(matDiffuse, accum[1]));
        diffuse = b.Construct({b.Saturate(b.Swizzle(lit, "xyz")), c(matDiffuse, 3)});
        const CKJitValue litSpecular = b.Construct({b.Saturate(b.Mul(b.Swizzle(matSpecular, "xyz"), accum[2])), c(specular, 3)});
        specular = b.Select(b.Greater(c(uniform(rows.Draw, CKFF_DRAW_PARAM_ALPHA), 2), half), litSpecular, specular);
    }
    const CKJitValue expansion = c(params, 3), clipW = c(clip, 3);
    const CKJitValue useWeight = b.Or(b.And(b.Greater(expansion, b.Float(1.5f)), b.Less(expansion, b.Float(2.5f))),
                                     b.Greater(expansion, b.Float(3.5f)));
    const CKJitValue expansionData = b.Select(useWeight, weight, tangent);
    const CKJitValue edge = b.Greater(expansion, b.Float(2.5f));
    const CKJitValue linePhase = b.Select(tweening, c(weight, 0), c(tangent, 0));
    const CKJitValue fogPosition = b.Construct({clipW, b.Select(edge, c(expansionData, 2), linePhase),
                                               b.Abs(c(view, 2)), one});
    const CKJitValue lineOffset = b.Select(edge, b.Mul(b.Swizzle(expansionData, "xy"), clipW), b.Float2(0, 0));
    const CKJitValue fogParams = uniform(rows.Draw, CKFF_DRAW_PARAM_FOG);
    const CKJitValue depth = b.Select(b.Greater(c(lightFlags, 2), half), b.Length(view), b.Abs(c(view, 2)));
    const CKJitValue fogMode = b.FloatToInt(b.Add(c(fogParams, 3), half));
    const CKJitValue densityDepth = b.Mul(c(fogParams, 2), depth);
    const CKJitValue diff = b.Sub(c(fogParams, 1), c(fogParams, 0));
    const CKJitValue denominator = b.Select(b.Less(b.Abs(diff), b.Float(0.0001f)),
        b.Select(b.Less(diff, zero), b.Float(-0.0001f), b.Float(0.0001f)), diff);
    CKJitValue fog = b.Saturate(b.Div(b.Sub(c(fogParams, 1), depth), denominator));
    fog = b.Select(b.IntEqual(fogMode, b.Int(2)), b.Saturate(b.Exp(b.Neg(b.Mul(densityDepth, densityDepth)))), fog);
    fog = b.Select(b.IntEqual(fogMode, b.Int(1)), b.Saturate(b.Exp(b.Neg(densityDepth))), fog);
    fog = b.Select(b.IntEqual(fogMode, b.Int(0)), one, fog);
    XArray<CKJitVertexOutput> outputs;
    outputs.PushBack({0, CKJIT_INPUT_SMOOTH, diffuse});
    outputs.PushBack({1, CKJIT_INPUT_SMOOTH, specular});
    outputs.PushBack({2, CKJIT_INPUT_FLAT, diffuse});
    outputs.PushBack({3, CKJIT_INPUT_FLAT, specular});
    for (unsigned stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        CKJitValue coord = b.Float4(0, 0, 0, 0);
        if (key.Switches[0] & (CKFF_NATIVE_FRAGMENT_TEXTURE << stage)) {
            const CKJitValue state = uniform(rows.Stages, CKFFStageParamIndex(stage, CKFF_STAGE_PARAM_COORD));
            const CKJitValue packed = b.FloatToInt(c(state, 0));
            const CKJitValue index = b.IntAnd(packed, b.Int(7));
            CKJitValue declared = b.Input({8, 2, CKJIT_INPUT_ATTRIBUTE});
            for (unsigned i = 1; i < 8; ++i)
                declared = b.Select(b.IntEqual(index, b.Int(i)), b.Input({8 + i, 2, CKJIT_INPUT_ATTRIBUTE}), declared);
            coord = b.Construct({declared, zero, one});
            // The packed state is a nonnegative, exactly representable integer.
            const CKJitValue generation = b.IntShiftRight(packed, b.Int(16));
            const CKJitValue eye = normalize(view);
            const CKJitValue reflection = b.Sub(eye, b.Mul(b.Mul(b.Dot(viewNormal, eye), b.Float(2)), viewNormal));
            const CKJitValue m = b.Mul(b.Length(b.Add(reflection, b.Float3(0, 0, 1))), b.Float(2));
            const CKJitValue sphere = b.Construct({b.Add(b.Div(b.Swizzle(reflection, "xy"), b.Max(m, b.Float(0.0001f))), half), zero, one});
            coord = b.Select(b.IntEqual(generation, b.Int(4)), sphere, coord);
            coord = b.Select(b.IntEqual(generation, b.Int(3)), b.Construct({reflection, one}), coord);
            coord = b.Select(b.IntEqual(generation, b.Int(2)), b.Construct({view, one}), coord);
            coord = b.Select(b.IntEqual(generation, b.Int(1)), b.Construct({viewNormal, one}), coord);
            const CKJitValue flags = b.FloatToInt(c(state, 1));
            const CKJitValue count = b.IntAnd(flags, b.Int(255));
            const CKJitValue transformed = transform(rows.Textures, stage, coord, true);
            coord = b.Select(b.And(b.IntGreaterEqual(count, b.Int(1)), b.IntLessEqual(count, b.Int(4))), transformed, coord);
            const CKJitValue projected = b.IntNotEqual(b.IntAnd(flags, b.Int(256)), b.Int(0));
            CKJitValue divisor = c(coord, 3);
            for (int i = 3; i >= 1; --i)
                divisor = b.Select(b.IntEqual(count, b.Int(i)), c(coord, i - 1), divisor);
            const CKJitValue trim = b.And(b.IntGreater(count, b.Int(0)), b.IntLess(count, b.Int(4)));
            coord = b.Construct({c(coord, 0),
                b.Select(b.And(trim, b.IntLessEqual(count, b.Int(1))), zero, c(coord, 1)),
                b.Select(b.And(trim, b.IntLessEqual(count, b.Int(2))), zero, c(coord, 2)),
                b.Select(projected, divisor, b.Select(trim, zero, c(coord, 3)))});
            if (key.Switches[0] & CKFF_NATIVE_FRAGMENT_AFFINE) coord = b.Mul(coord, clipW);
        }
        if (stage == 7) coord = b.Construct({b.Swizzle(coord, "xy"), fog, c(coord, 3)});
        outputs.PushBack({4 + stage, CKJIT_INPUT_SMOOTH, coord});
    }
    outputs.PushBack({12, CKJIT_INPUT_SMOOTH, fogPosition});
    outputs.PushBack({13, CKJIT_INPUT_SMOOTH, lineOffset});
    const CKJitValue offset = b.Mul(b.Swizzle(expansionData, "xy"), b.Swizzle(uniform(rows.Viewport), "xy"));
    const CKJitValue xy = b.Select(b.Greater(expansion, half), b.Mad(offset, clipW, b.Swizzle(clip, "xy")), b.Swizzle(clip, "xy"));
    return b.FinishVertex(b.Construct({xy, b.Mad(b.Neg(c(params, 1)), clipW, c(clip, 2)), clipW}),
                           outputs.Begin(), outputs.Size(), out, distances, clipping ? 8 : 0);
}

bool CKFFCompileNativeUnlitProgram(const CKFFNativeFragmentKey &key, CKFFSamplerLayout layout,
                                  CK_SHADER_FORMAT referenceFormat, CKJitVertexShader &out, bool clipping) {
    return CompileNative3dProgram(key, layout, referenceFormat, false, out, clipping);
}

bool CKFFCompileNativeLitProgram(const CKFFNativeFragmentKey &key, CKFFSamplerLayout layout,
                                CK_SHADER_FORMAT referenceFormat, CKJitVertexShader &out, bool clipping) {
    return CompileNative3dProgram(key, layout, referenceFormat, true, out, clipping);
}
