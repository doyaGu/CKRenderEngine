#ifndef TESTSHADERJITVERTEX_H
#define TESTSHADERJITVERTEX_H

#include "CKJitBuilder.h"

// Shared programs exercise both native emitters and their external validators.
// The SDL_gpu vertex resource ABI uses uniform space 1 and sampler space 0.
static const CKJitResourceLayout kVertexLayout = {1, 0, 0};
static const char *const kVertexCases[] = {"vertex_passthrough", "vertex_transform", "vertex_regions",
                                         "vertex_textures", "vertex_constants", "vertex_mad", "vertex_uint",
    "vertex_clip1", "vertex_clip4", "vertex_clip5", "vertex_clip6", "vertex_clip8"};

inline bool BuildVertexCase(unsigned index, CKJitVertexShader &shader) {
    const uint32_t rows[] = {4, 2};
    CKJitBuilder b(rows, 2);
    const CKJitValue position = b.Input({0, 4, CKJIT_INPUT_ATTRIBUTE});
    b.Input({31, 1, CKJIT_INPUT_ATTRIBUTE}); // declared even when unread
    if (index >= 7 && index <= 11) {
        const unsigned counts[] = {1, 4, 5, 6, 8};
        CKJitValue clips[8];
        // Early computed, aliased, constant and late values must all remain live.
        clips[0] = b.Dot(position, b.Uniform(0));
        for (unsigned i = 1; i < 8; ++i)
            clips[i] = i == 4 ? b.Float(0) : b.Add(clips[0], b.Component(position, i % 4));
        const CKJitVertexOutput outputs[] = {{2, CKJIT_INPUT_SMOOTH, b.Mul(position, b.Uniform(1))}};
        return b.FinishVertex(position, outputs, 1, shader, clips, counts[index - 7]);
    }
    if (index == 6) {
        CKJitValue lanes[4];
        for (unsigned width = 1; width <= 4; ++width) {
            const CKJitValue integer = b.Input({5 + width, uint8_t(width), CKJIT_INPUT_ATTRIBUTE, CKJIT_INPUT_UINT});
            lanes[width - 1] = b.IntToFloat(b.IntAnd(b.Component(integer, width - 1), b.Int(255)));
        }
        return b.FinishVertex(position, {{0, CKJIT_INPUT_FLAT, b.Construct({lanes[0], lanes[1], lanes[2], lanes[3]})}}, shader);
    }
    if (index == 0)
        return b.FinishVertex(position, {}, shader);
    if (index == 4) {
        return b.FinishVertex(b.Float4(0.0f, 0.0f, 0.5f, 1.0f),
                              {{0, CKJIT_INPUT_SMOOTH, b.Float4(0.25f, 0.5f, 0.75f, 1.0f)},
                               {3, CKJIT_INPUT_FLAT, b.Float(0.5f)}}, shader);
    }

    const CKJitValue color = b.Input({2, 4, CKJIT_INPUT_ATTRIBUTE});
    const CKJitValue uv = b.Input({5, 2, CKJIT_INPUT_ATTRIBUTE});
    if (index == 5) {
        const CKJitValue scalar = b.Component(position, 2);
        return b.FinishVertex(b.Mad(position, b.Float(0.5f), color),
            {{0, CKJIT_INPUT_SMOOTH, b.Mad(b.Neg(scalar), b.Component(color, 0), scalar)},
             {1, CKJIT_INPUT_SMOOTH, b.Mad(scalar, scalar, uv)},
             {2, CKJIT_INPUT_SMOOTH, b.Mad(b.Swizzle(position, "zyx"), b.Float(0.75f), scalar)},
             {3, CKJIT_INPUT_SMOOTH, b.Mad(color, position, b.Float(0.25f))}}, shader);
    }
    const CKJitValue transformed = b.Construct({b.Dot(position, b.Uniform(0)), b.Dot(position, b.Uniform(1)),
                                               b.Dot(position, b.Uniform(2)), b.Dot(position, b.Uniform(3))});
    const CKJitValue tint = b.Mul(color, b.Uniform(1, 0));
    if (index == 1) {
        // An early computed output, a view of it, later temporaries, all widths,
        // and out-of-order locations exercise the output liveness union.
        const CKJitValue tex = b.Add(uv, b.Swizzle(b.Uniform(1, 1), "xy"));
        return b.FinishVertex(transformed,
                              {{8, CKJIT_INPUT_FLAT, b.Swizzle(tint, "zyx")},
                               {0, CKJIT_INPUT_SMOOTH, tint}, {4, CKJIT_INPUT_SMOOTH, tex},
                               {12, CKJIT_INPUT_SMOOTH, b.Component(transformed, 3)}}, shader);
    }
    if (index == 2) {
        b.If(b.Less(b.Component(color, 0), b.Float(0.5f)));
        const CKJitValue thenValue = b.Add(tint, b.Float(0.25f));
        b.Else({thenValue});
        const CKJitValue selected = b.EndIf(b.Mul(tint, b.Float(0.5f)));
        CKJitValue carried;
        b.Loop(b.FloatToInt(b.Component(color, 1)), 4, {selected}, &carried);
        const CKJitValue result = b.EndLoop(b.Add(carried, b.Uniform(1, 1)));
        return b.FinishVertex(transformed,
                              {{0, CKJIT_INPUT_SMOOTH, result}, {2, CKJIT_INPUT_FLAT, b.Neg(selected)},
                               {4, CKJIT_INPUT_SMOOTH, b.Swizzle(result, "xy")}}, shader);
    }
    if (index == 3) {
        const CKJitValue level = b.Component(b.Uniform(1, 1), 0);
        const CKJitValue twoD = b.SampleLevel(0, CKJIT_SAMPLER_2D, uv, level);
        const CKJitValue cube = b.SampleLevel(1, CKJIT_SAMPLER_CUBE, b.Swizzle(position, "xyz"), level);
        const CKJitValue volume = b.SampleGrad(2, CKJIT_SAMPLER_3D, b.Swizzle(position, "xyz"),
                                               b.Float3(0.1f, 0.0f, 0.0f), b.Float3(0.0f, 0.1f, 0.0f));
        const CKJitValue shadow = b.SampleCmpLevelZero(3, uv, b.Float(0.5f));
        const CKJitValue texel = b.Load(0, CKJIT_SAMPLER_2D, b.Construct({b.Int(0), b.Int(0), b.Int(0)}));
        const CKJitValue size = b.IntToFloat(b.TextureSize(0, CKJIT_SAMPLER_2D, b.Int(0)));
        const CKJitValue levels = b.IntToFloat(b.TextureLevels(1, CKJIT_SAMPLER_CUBE));
        const CKJitValue sampled = b.Add(b.Add(twoD, cube), b.Add(volume, b.Mul(texel, shadow)));
        return b.FinishVertex(transformed,
                              {{0, CKJIT_INPUT_SMOOTH, sampled}, {4, CKJIT_INPUT_SMOOTH, size},
                               {6, CKJIT_INPUT_FLAT, levels}}, shader);
    }
    return false;
}

#endif
