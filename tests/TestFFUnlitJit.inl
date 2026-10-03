float *UnlitUniform(VertexInputs &input, CKFFConstantBlock block, unsigned row = 0) {
    static const CKFFProgramDesc desc = CKFFBuildProgramInterface(0, 0, CKRST_SHADER_FORMAT_SPIRV, FALSE, FALSE);
    for (const auto &binding : desc.Uniforms) {
        if (binding.Stage == CKRST_SHADER_VERTEX && binding.Slot == (CKDWORD)block) {
            TestCheck(row * 16 < binding.Size() && binding.BufferSlot < 3 && binding.Offset / 16 + row < 128,
                      "unlit test uniforms fit the 3D ABI");
            return input.Uniforms[binding.BufferSlot][binding.Offset / 16 + row];
        }
    }
    TestFail("unlit uniform binding exists");
    return nullptr;
}

// Independent scalar deformation: the palette is applied before view matrices.
void ReferenceVertexDeformation(VertexInputs &input, float position[4], float normal[3]) {
    VertexInputs &uniformInput = input;
    const float *state = UnlitUniform(uniformInput, CKRST_BLOCK_DRAW_PARAMS, 19);
    for (unsigned c = 0; c < 3; ++c) { position[c] = input.Attributes[0][c]; normal[c] = input.Attributes[1][c]; }
    position[3] = 1;
    if (state[1] == 2) {
        for (unsigned c = 0; c < 3; ++c) {
            if (int(state[2]) & 1) position[c] = input.Attributes[0][c] * (1 - state[0]) + input.Attributes[2][c] * state[0];
            if (int(state[2]) & 2) normal[c] = input.Attributes[1][c] * (1 - state[0]) + input.Attributes[3][c] * state[0];
        }
    } else if (state[1] == 1) {
        std::memset(position, 0, 4 * sizeof(float)); std::memset(normal, 0, 3 * sizeof(float));
        int32_t indices[4]; std::memcpy(indices, input.Attributes[6], sizeof(indices));
        float remaining = 1;
        for (int slot = 0; slot <= int(state[2]); ++slot) {
            const float weight = slot == int(state[2]) ? remaining : input.Attributes[7][slot];
            if (slot != int(state[2])) remaining -= weight;
            const int index = state[3] ? (indices[slot] < 0 ? 0 : indices[slot] > 3 ? 3 : indices[slot]) : slot;
            for (unsigned c = 0; c < 4; ++c) {
                float p = UnlitUniform(uniformInput, CKRST_BLOCK_VERTEX_BLEND_MATRICES, index * 4 + 3)[c], n = 0;
                for (unsigned r = 0; r < 3; ++r) {
                    const float m = UnlitUniform(uniformInput, CKRST_BLOCK_VERTEX_BLEND_MATRICES, index * 4 + r)[c];
                    p += m * input.Attributes[0][r]; n += m * input.Attributes[1][r];
                }
                position[c] += p * weight;
                if (c < 3) normal[c] += n * weight;
            }
        }
    }
}

void ConfigureTestDeformation(VertexInputs &input, unsigned sample) {
    float *state = UnlitUniform(input, CKRST_BLOCK_DRAW_PARAMS, 19);
    const unsigned mode = sample % 12;
    state[0] = float(int(sample / 4) % 7 - 1) * 0.25f;
    state[1] = mode >= 4 ? 1.0f : mode ? 2.0f : 0.0f;
    state[2] = float(mode >= 4 ? (mode - 4) % 4 : mode);
    state[3] = float(mode >= 8);
    const uint32_t values[] = {0, 1, 2, 3, 4, 255, 0xffffffffu, 0x80000000u};
    uint32_t indices[4];
    for (unsigned slot = 0; slot < 4; ++slot) indices[slot] = values[(sample / 4 + slot * 3) % 8];
    std::memcpy(input.Attributes[6], indices, sizeof(indices));
    for (unsigned matrix = 0; matrix < 4; ++matrix) for (unsigned row = 0; row < 4; ++row) {
        float *m = UnlitUniform(input, CKRST_BLOCK_VERTEX_BLEND_MATRICES, matrix * 4 + row);
        for (unsigned c = 0; c < 4; ++c)
            m[c] = (row == c ? 1.0f : 0.0f) + 0.013f * float(int((sample + matrix * 7 + row * 3 + c) % 17) - 8);
    }
}

void TestUnlitEligibility() {
    CKFFConstantSet constants;
    TestCheck(!CKFFNativeUnlitDraw(constants), "missing uniforms keep the fallback");
    float draw[20][4] = {};
    float palette[4][16] = {};
    constants.Set(CKRST_BLOCK_VERTEX_BLEND_MATRICES, palette, sizeof(palette));
    draw[6][0] = -1;
    constants.Set(CKRST_BLOCK_DRAW_PARAMS, draw, sizeof(draw) - 1);
    TestCheck(!CKFFNativeUnlitDraw(constants), "truncated uniforms keep the fallback");
    for (float lights : {-1.0f, 0.0f, 1.0f, 8.0f, std::numeric_limits<float>::quiet_NaN()})
    for (float blend : {0.0f, 1.0f, 2.0f, 3.0f, std::numeric_limits<float>::quiet_NaN()})
    for (float mask : {0.0f, 1.0f, 2.0f, 3.0f, 1.5f, 4.0f, std::numeric_limits<float>::quiet_NaN()})
    for (float factor : {-0.25f, 0.0f, 0.5f, 1.0f, 1.25f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    for (float indexed : {0.0f, 1.0f}) {
        draw[6][0] = lights; draw[19][1] = blend;
        draw[19][0] = factor; draw[19][2] = mask; draw[19][3] = indexed;
        constants.Set(CKRST_BLOCK_DRAW_PARAMS, draw, sizeof(draw));
        const bool supported = blend == 0 || (blend == 1 && (mask == 0 || mask == 1 || mask == 2 || mask == 3)) || (blend == 2 && indexed == 0 && std::isfinite(factor) &&
            (mask == 1 || mask == 2 || mask == 3));
        TestCheck(CKFFNativeUnlitDraw(constants) == (lights == -1 && supported),
                  "canonical unlit and tween draws use the generated 3D shader");
    }
    draw[6][0] = -1; draw[19][1] = 1; draw[19][2] = 3; draw[19][3] = 0;
    CKFFConstantSet truncated;
    truncated.Set(CKRST_BLOCK_DRAW_PARAMS, draw, sizeof(draw));
    TestCheck(!CKFFNativeUnlitDraw(truncated), "missing matrix palette keeps fallback");
    truncated.Set(CKRST_BLOCK_VERTEX_BLEND_MATRICES, palette, sizeof(palette) - 1);
    TestCheck(!CKFFNativeUnlitDraw(truncated), "truncated matrix palette keeps fallback");

}

void TestUnlit() {
    Random random(0x19832213u);
    unsigned saved = 0;
    for (CK_SHADER_FORMAT format : {CKRST_SHADER_FORMAT_SPIRV, CKRST_SHADER_FORMAT_DXIL})
    for (CKFFSamplerLayout layout : kLayouts) for (unsigned mask : {0u, 1u, 129u, 255u}) for (unsigned affine = 0; affine < 2; ++affine) {
        CKFFNativeFragmentKey key;
        key.Program.Set(CKFF_FRAGMENT_PROGRAM_LAST_ACTIVE_TEXTURE_STAGE, 7);
        for (unsigned stage = 0; stage < 8; ++stage)
            SetSelectArg1(key.Program, stage, mask & (1u << stage) ? CKRST_TA_TEXTURE : CKRST_TA_DIFFUSE);
        key.Switches[0] = (mask << 8) | (affine ? CKFF_NATIVE_FRAGMENT_AFFINE : 0);
        CKJitVertexShader shader;
        TestCheck(CKFFCompileNativeUnlitProgram(key, layout, format, shader) && CKJitVerify(shader), "unlit frontend verifies");
        TestCheck(shader.Outputs.Size() == 14 && shader.UniformBufferCount == 3 && shader.SamplerCount == 0,
                  "unlit interface keeps native 3D resources and varyings");
        bool texcoords = false, normals = false;
        for (const auto &node : shader.Nodes) {
            if (node.Op != CKJIT_OP_INPUT) continue;
            const auto &input = shader.Inputs[node.Imm[0]];
            texcoords |= input.Location >= 8;
            normals |= input.Location == 1;
            if (input.Location == 6) TestCheck(input.Scalar == CKJIT_INPUT_UINT, "palette indices retain integer input type");
        }
        TestCheck(texcoords == (mask != 0) && normals == (mask != 0), "unused texgen dependencies disappear");
        for (unsigned sample = 0; sample < 128; ++sample) {
            VertexInputs input = {};
            for (auto &attribute : input.Attributes) for (float &x : attribute) x = random.Range(-1, 1);
            for (unsigned matrix = 0; matrix < 8; ++matrix) for (unsigned row = 0; row < 4; ++row) {
                float *m = UnlitUniform(input, CKRST_BLOCK_MATRICES, matrix * 4 + row);
                float *t = UnlitUniform(input, CKRST_BLOCK_TEX_MATRICES, matrix * 4 + row);
                for (unsigned c = 0; c < 4; ++c) {
                    m[c] = random.Range(-0.25f, 0.25f) + (row == c ? 1.0f : 0.0f);
                    t[c] = random.Range(-0.5f, 0.5f) + (row == c ? 1.0f : 0.0f);
                }
            }
            float *viewport = UnlitUniform(input, CKRST_BLOCK_VIEWPORT);
            viewport[0] = 2.0f / 61; viewport[1] = -2.0f / 59;
            float *params = UnlitUniform(input, CKRST_BLOCK_DRAW_PARAMS, 4);
            params[1] = sample % 3 ? 7.0f / 1048575.0f : 0;
            params[3] = float(sample % 5);
            float *light = UnlitUniform(input, CKRST_BLOCK_DRAW_PARAMS, 7);
            light[1] = float(sample % 2); light[2] = float(sample % 3 == 0);
            float *fog = UnlitUniform(input, CKRST_BLOCK_DRAW_PARAMS, 10);
            fog[0] = 0.2f; fog[1] = sample % 7 ? 1.5f : 0.2f; fog[2] = 0.7f; fog[3] = float(sample % 4);
            const unsigned flags[] = {0, 1, 2, 3, 4, 256, 257, 258, 259, 260, 255, 511, 0x1000};
            for (unsigned stage = 0; stage < 8; ++stage) {
                float *state = UnlitUniform(input, CKRST_BLOCK_STAGE_PARAMS, stage * 2);
                state[0] = float(random.Below(8) | (((sample + stage) % 6) << 16));
                state[1] = float(flags[(sample + stage) % (sizeof(flags) / sizeof(flags[0]))]);
            }
            ConfigureTestDeformation(input, sample);
            const float *deformation = UnlitUniform(input, CKRST_BLOCK_DRAW_PARAMS, 19);
            float localPosition[4], localNormal[3];
            ReferenceVertexDeformation(input, localPosition, localNormal);
            // Scalar reference follows vs_ff_3d.sc, using ordinary arithmetic
            // rather than the builder's matrix and fog expression helpers.
            const auto transform = [&](CKFFConstantBlock block, unsigned matrix, const float *v, float w, float *out) {
                for (unsigned c = 0; c < 4; ++c) {
                    out[c] = UnlitUniform(input, block, matrix * 4 + 3)[c] * w;
                    for (unsigned r = 0; r < 3; ++r) out[c] += UnlitUniform(input, block, matrix * 4 + r)[c] * v[r];
                }
            };
            const auto length = [](const float *v) { return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); };
            float clip[4], view[4], normal[4];
            transform(CKRST_BLOCK_MATRICES, 0, localPosition, localPosition[3], clip);
            transform(CKRST_BLOCK_MATRICES, 2, localPosition, localPosition[3], view);
            transform(CKRST_BLOCK_MATRICES, 3, localNormal, 0, normal);
            if (light[1] > 0.5f) {
                const float n = length(normal);
                for (unsigned c = 0; c < 3; ++c) normal[c] /= n;
            }
            float expected[14][4] = {};
            for (unsigned i = 0; i < 4; ++i) std::memcpy(expected[i], input.Attributes[4 + i % 2], sizeof(expected[i]));
            const float *expansion = input.Attributes[(params[3] == 2 || params[3] == 4) ? 7 : 2];
            expected[12][0] = clip[3]; expected[12][1] = params[3] > 2.5f ? expansion[2] : input.Attributes[deformation[1] == 2 ? 7 : 2][0];
            expected[12][2] = std::fabs(view[2]); expected[12][3] = 1;
            if (params[3] > 2.5f) for (unsigned c = 0; c < 2; ++c) expected[13][c] = expansion[c] * clip[3];
            for (unsigned stage = 0; stage < 8; ++stage) if (mask & (1u << stage)) {
                float *coord = expected[4 + stage];
                const float *state = UnlitUniform(input, CKRST_BLOCK_STAGE_PARAMS, stage * 2);
                const int packed = int(state[0]), generation = packed / 65536;
                coord[0] = input.Attributes[8 + (packed & 7)][0];
                coord[1] = input.Attributes[8 + (packed & 7)][1]; coord[3] = 1;
                if (generation == 1 || generation == 2) std::memcpy(coord, generation == 1 ? normal : view, 3 * sizeof(float));
                if (generation == 3 || generation == 4) {
                    float eye[3], reflection[3], dot = 0;
                    const float n = length(view);
                    for (unsigned c = 0; c < 3; ++c) { eye[c] = view[c] / n; dot += eye[c] * normal[c]; }
                    for (unsigned c = 0; c < 3; ++c) reflection[c] = eye[c] - 2 * dot * normal[c];
                    std::memcpy(coord, reflection, sizeof(reflection));
                    if (generation == 4) {
                        reflection[2] += 1;
                        const float m = std::fmax(length(reflection) * 2.0f, 0.0001f);
                        coord[0] = coord[0] / m + 0.5f; coord[1] = coord[1] / m + 0.5f; coord[2] = 0;
                    }
                }
                const int bits = int(state[1]), count = bits & 255;
                if (count >= 1 && count <= 4) {
                    float transformed[4]; transform(CKRST_BLOCK_TEX_MATRICES, stage, coord, coord[3], transformed);
                    std::memcpy(coord, transformed, sizeof(transformed));
                }
                if (bits & 256) coord[3] = coord[count >= 1 && count <= 3 ? count - 1 : 3];
                if (count > 0 && count < 4) {
                    if (count <= 1) coord[1] = 0;
                    if (count <= 2) coord[2] = 0;
                    if (!(bits & 256)) coord[3] = 0;
                }
                if (affine) for (unsigned c = 0; c < 4; ++c) coord[c] *= clip[3];
            }
            const float depth = light[2] > 0.5f ? length(view) : std::fabs(view[2]);
            const float e = fog[2] * depth;
            float fogFactor = 1;
            if (fog[3] == 1) fogFactor = std::exp(-e);
            if (fog[3] == 2) fogFactor = std::exp(-e * e);
            if (fog[3] == 3) fogFactor = (fog[1] - depth) / std::fmax(fog[1] - fog[0], 0.0001f);
            expected[11][2] = std::fmin(std::fmax(fogFactor, 0.0f), 1.0f);
            clip[2] -= params[1] * clip[3];
            if (params[3] > 0.5f) for (unsigned c = 0; c < 2; ++c) clip[c] += expansion[c] * viewport[c] * clip[3];
            XArray<Value> values;
            Fragment unused = {};
            ExecuteNodes(shader, unused, values, &input);
            const auto matches = [](const float *a, const float *e, unsigned width) {
                for (unsigned c = 0; c < width; ++c)
                    TestCheck(std::fabs(a[c] - e[c]) <= 0.00003f * std::fmax(1.0f, std::fabs(e[c])),
                              "unlit IR agrees with the scalar vertex reference");
            };
            matches(values[shader.Position.Id].F, clip, 4);
            for (const auto &output : shader.Outputs)
                matches(values[output.Value.Id].F, expected[output.Location], CKJitComponentCount(shader.Node(output.Value).Type));
        }
        XArray<uint32_t> code;
        const CKJitResourceLayout resources = {1, 0, 0};
        TestCheck(CKJitEmitSpirv(shader, resources, code), "unlit emits SPIR-V");
        if (g_ShaderDirectory) Save("unlit", saved, "spv", code);
#if CKRE_ENABLE_DIRECTX
        TestCheck(CKJitEmitDxbc(shader, resources, code), "unlit emits DXBC");
        if (g_ShaderDirectory) Save("unlit", saved, "dxbc", code);
#endif
        ++saved;
    }
    CKJitVertexShader shader;
    CKFFNativeFragmentKey key;
    TestCheck(!CKFFCompileNativeUnlitProgram(key, kLayouts[0], CKRST_SHADER_FORMAT_UNKNOWN, shader),
              "unlit frontend requires a known fallback format");
}
