#include "CKFFNativePositionTJit.h"
#include "CKFFShaderInterface.h"
#include "CKJitBuilder.h"

namespace {
struct Row {
    uint32_t Buffer = 0, Index = 0;
};
struct Uniforms {
    uint32_t Count = 0, Rows[CKJIT_MAX_UNIFORM_BUFFERS] = {};
    Row Draw, Viewport, Stages, Textures, ClipPlanes, ClipParams;
};

bool ResolveUniforms(Uniforms &out) {
    const CKFFProgramDesc desc = CKFFBuildProgramInterface(0, 0, CKRST_SHADER_FORMAT_SPIRV, FALSE, TRUE);
    for (const auto &buffer : desc.UniformBuffers) {
        if (buffer.Stage != CKRST_SHADER_VERTEX) continue;
        if (buffer.Slot >= CKJIT_MAX_UNIFORM_BUFFERS || buffer.Size % 16) return false;
        out.Rows[buffer.Slot] = buffer.Size / 16;
        if (buffer.Slot >= out.Count) out.Count = buffer.Slot + 1;
    }
    struct Block { CKFFConstantBlock Id; uint32_t Count; Row *Target; };
    const Block blocks[] = {{CKRST_BLOCK_DRAW_PARAMS, CKFF_DRAW_PARAM_VEC4_COUNT, &out.Draw},
                            {CKRST_BLOCK_VIEWPORT, 1, &out.Viewport},
                            {CKRST_BLOCK_TEX_MATRICES, 4 * CKFF_MAX_TEXTURE_STAGES, &out.Textures},
                            {CKRST_BLOCK_STAGE_PARAMS, CKFF_STAGE_PARAM_VEC4_COUNT, &out.Stages},
                            {CKRST_BLOCK_CLIP_PLANES, CKFF_CLIP_PLANE_COUNT, &out.ClipPlanes},
                            {CKRST_BLOCK_CLIP_PARAMS, 1, &out.ClipParams}};
    for (const auto &block : blocks) {
        bool found = false;
        for (const auto &binding : desc.Uniforms) {
            if (binding.Stage != CKRST_SHADER_VERTEX || binding.Slot != (CKDWORD)block.Id) continue;
            if (binding.Offset % 16 || binding.Size() < block.Count * 16 || binding.BufferSlot >= out.Count ||
                binding.Offset / 16 + block.Count > out.Rows[binding.BufferSlot]) return false;
            *block.Target = {binding.BufferSlot, binding.Offset / 16};
            found = true;
            break;
        }
        if (!found) return false;
    }
    return true;
}
}

bool CKFFCompileNativePositionTProgram(const CKFFNativeFragmentKey &input, CKFFSamplerLayout layout,
                                       CK_SHADER_FORMAT referenceFormat, CKJitVertexShader &out, bool clipping, bool depthPad) {
    if ((CKDWORD)layout >= CKFF_SAMPLER_LAYOUT_COUNT ||
        (referenceFormat != CKRST_SHADER_FORMAT_DXIL && referenceFormat != CKRST_SHADER_FORMAT_SPIRV)) return false;
    const bool dxil = referenceFormat == CKRST_SHADER_FORMAT_DXIL;
    Uniforms rows;
    if (!ResolveUniforms(rows)) return false;
    CKFFNativeFragmentKey key = input;
    CKFFCanonicalizeNativeFragmentKey(key, layout);
    CKJitBuilder b(rows.Rows, rows.Count);
    const auto uniform = [&](Row row, uint32_t offset = 0) { return b.Uniform(row.Buffer, row.Index + offset); };
    const auto component = [&](CKJitValue v, uint32_t i) { return b.Component(v, i); };
    const CKJitValue position = b.Input({0, 4, CKJIT_INPUT_ATTRIBUTE});
    const CKJitValue tangent = b.Input({2, 3, CKJIT_INPUT_ATTRIBUTE});
    const CKJitValue diffuse = b.Input({4, 4, CKJIT_INPUT_ATTRIBUTE});
    const CKJitValue specular = b.Input({5, 4, CKJIT_INPUT_ATTRIBUTE});
    const CKJitValue weight = b.Input({7, 3, CKJIT_INPUT_ATTRIBUTE});
    const CKJitValue viewport = uniform(rows.Viewport);
    const CKJitValue params = uniform(rows.Draw, CKFF_DRAW_PARAM_MATERIAL_POWER);
    const CKJitValue one = b.Float(1.0f), zero = b.Float(0.0f);
    CKJitValue distances[8];
    if (clipping) {
        const CKJitValue world = b.Construct({b.Swizzle(position, "xyz"), one});
        const CKJitValue count = b.FloatToInt(component(uniform(rows.ClipParams), 0));
        for (unsigned i = 0; i < CKFF_CLIP_PLANE_COUNT; ++i)
            distances[i] = b.Select(b.IntGreater(count, b.Int(i)), b.Dot(world, uniform(rows.ClipPlanes, i)), zero);
        distances[6] = distances[7] = zero;
    }
    const CKJitValue rhw = component(position, 3);
    const CKJitValue clipW = b.Div(one, b.Select(b.Equal(rhw, zero), one, rhw));
    const CKJitValue screen = b.Add(b.Swizzle(position, "xy"), b.Float(0.5f));
    const CKJitValue scale = b.Swizzle(viewport, "xy"), origin = b.Swizzle(viewport, "zw");
    const CKJitValue ndcXY = dxil ? b.Mad(screen, scale, origin) : b.Add(b.Mul(screen, scale), origin);
    const CKJitValue clipZ = b.Mul(component(position, 2), clipW);
    const CKJitValue expansion = component(params, 3);
    const CKJitValue edge = b.Greater(expansion, b.Float(2.5f));
    const CKJitValue phase = b.Select(edge, component(tangent, 2), component(tangent, 0));
    const CKJitValue lineOffset = b.Select(edge, b.Mul(b.Swizzle(tangent, "xy"), clipW), b.Float2(0, 0));
    const CKJitValue fogPosition = b.Construct({clipW, phase, clipZ, clipW});
    const CKJitValue fog = b.Select(b.Greater(component(uniform(rows.Draw, CKFF_DRAW_PARAM_FOG), 3), b.Float(0.5f)),
                                   component(specular, 3), one);
    XArray<CKJitVertexOutput> outputs;
    outputs.PushBack({0, CKJIT_INPUT_SMOOTH, diffuse});
    outputs.PushBack({1, CKJIT_INPUT_SMOOTH, specular});
    outputs.PushBack({2, CKJIT_INPUT_FLAT, diffuse});
    outputs.PushBack({3, CKJIT_INPUT_FLAT, specular});
    for (uint32_t stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        CKJitValue coord = b.Float4(0, 0, 0, 0);
        if (key.Switches[0] & (CKFF_NATIVE_FRAGMENT_TEXTURE << stage)) {
            const CKJitValue state = uniform(rows.Stages, CKFFStageParamIndex(stage, CKFF_STAGE_PARAM_COORD));
            const CKJitValue index = b.IntAnd(b.FloatToInt(component(state, 0)), b.Int(7));
            coord = b.Input({8, 4, CKJIT_INPUT_ATTRIBUTE});
            for (uint32_t i = 1; i < 8; ++i)
                coord = b.Select(b.IntEqual(index, b.Int(i)), b.Input({8 + i, 4, CKJIT_INPUT_ATTRIBUTE}), coord);
            const CKJitValue flags = b.FloatToInt(component(state, 1));
            if (depthPad) {
                // The draw-local matrix remaps only XY into a padded depth
                // texture. Preserve the comparison reference and input W.
                CKJitValue padded = b.Mul(b.Swizzle(uniform(rows.Textures, stage * 4), "xy"), component(coord, 0));
                for (unsigned column = 1; dxil && column < 4; ++column) {
                    const CKJitValue matrix = b.Swizzle(uniform(rows.Textures, stage * 4 + column), "xy");
                    padded = b.Mad(matrix, column == 3 ? one : component(coord, column), padded);
                }
                if (!dxil) {
                    // The SPIR-V reference multiplies by the matrix, as 3D transforms do.
                    const CKJitValue vector = b.Construct({b.Swizzle(coord, "xyz"), one});
                    CKJitValue dots[2];
                    for (unsigned row = 0; row < 2; ++row) {
                        CKJitValue parts[4];
                        for (unsigned column = 0; column < 4; ++column)
                            parts[column] = component(uniform(rows.Textures, stage * 4 + column), row);
                        dots[row] = b.Dot(b.Construct({parts[0], parts[1], parts[2], parts[3]}), vector);
                    }
                    padded = b.Construct({dots[0], dots[1]});
                }
                const CKJitValue enabled = b.IntNotEqual(b.IntAnd(flags, b.Int(0x1000)), b.Int(0));
                coord = b.Construct({b.Select(enabled, padded, b.Swizzle(coord, "xy")), b.Swizzle(coord, "zw")});
            }
            const CKJitValue count = b.IntAnd(flags, b.Int(255));
            const CKJitValue projected = b.IntNotEqual(b.IntAnd(flags, b.Int(256)), b.Int(0));
            CKJitValue divisor = component(coord, 3);
            for (int i = 3; i >= 1; --i)
                divisor = b.Select(b.IntEqual(count, b.Int(i)), component(coord, i - 1), divisor);
            const CKJitValue trim = b.And(b.IntGreater(count, b.Int(0)), b.IntLess(count, b.Int(4)));
            const CKJitValue y = b.Select(b.And(trim, b.IntLessEqual(count, b.Int(1))), zero, component(coord, 1));
            const CKJitValue z = b.Select(b.And(trim, b.IntLessEqual(count, b.Int(2))), zero, component(coord, 2));
            const CKJitValue w = b.Select(projected, divisor, b.Select(trim, zero, component(coord, 3)));
            coord = b.Construct({component(coord, 0), y, z, w});
            if (key.Switches[0] & CKFF_NATIVE_FRAGMENT_AFFINE)
                coord = b.Mul(coord, clipW);
        }
        if (stage == 7)
            coord = b.Construct({b.Swizzle(coord, "xy"), fog, component(coord, 3)});
        outputs.PushBack({4 + stage, CKJIT_INPUT_SMOOTH, coord});
    }
    outputs.PushBack({12, CKJIT_INPUT_SMOOTH, fogPosition});
    outputs.PushBack({13, CKJIT_INPUT_SMOOTH, lineOffset});
    const CKJitValue useWeight = b.And(b.Greater(expansion, b.Float(1.5f)), b.Less(expansion, b.Float(2.5f)));
    const CKJitValue offset = b.Select(useWeight, b.Swizzle(weight, "xy"), b.Swizzle(tangent, "xy"));
    // The reference biases and expands before the W multiply, as DXIL keeps
    // it; SPIR-V leaves the driver to contract the offset's multiply-add.
    const CKJitValue expandedNdc = dxil ? b.Mad(offset, scale, ndcXY) : b.Add(b.Mul(offset, scale), ndcXY);
    const CKJitValue expandedXY = b.Mul(b.Select(b.Greater(expansion, b.Float(0.5f)), expandedNdc, ndcXY), clipW);
    const CKJitValue biasedZ = b.Mul(b.Sub(component(position, 2), component(params, 1)), clipW);
    return b.FinishVertex(b.Construct({expandedXY, biasedZ, clipW}), outputs.Begin(), outputs.Size(), out, distances, clipping ? 8 : 0);
}
