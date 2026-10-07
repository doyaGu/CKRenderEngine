// Uses the shared IR interpreter, with a direct scalar reference for the
// POSITIONT shader. Uniforms are packed through the real program interface.
float *PositionTUniform(VertexInputs &input, CKFFConstantBlock block, unsigned row = 0) {
    static const CKFFProgramDesc desc = CKFFBuildProgramInterface(0, 0, FALSE, TRUE);
    for (const auto &binding : desc.Uniforms) {
        if (binding.Stage == CKRST_SHADER_VERTEX && binding.Slot == (CKDWORD)block) {
            TestCheck(row * 16 < binding.Size() && binding.BufferSlot < 2 && binding.Offset / 16 + row < 128,
                      "test uniforms fit the POSITIONT ABI");
            return input.Uniforms[binding.BufferSlot][binding.Offset / 16 + row];
        }
    }
    TestFail("POSITIONT uniform binding exists");
    return nullptr;
}

void TestPositionT() {
    Random random(0x287a3991u);
    unsigned saved = 0;
    for (bool depthPad : {false, true})
    for (CK_SHADER_FORMAT format : {CKRST_SHADER_FORMAT_SPIRV, CKRST_SHADER_FORMAT_DXIL})
    for (CKFFSamplerLayout layout : kLayouts) for (unsigned mask : {0u, 1u, 129u, 255u}) for (unsigned affine = 0; affine < 2; ++affine) {
        CKFFNativeFragmentKey key;
        key.Program.Set(CKFF_FRAGMENT_PROGRAM_LAST_ACTIVE_TEXTURE_STAGE, 7);
        for (unsigned stage = 0; stage < 8; ++stage)
            SetSelectArg1(key.Program, stage, mask & (1u << stage) ? CKRST_TA_TEXTURE : CKRST_TA_DIFFUSE);
        key.Switches[0] = (mask << 8) | (affine ? CKFF_NATIVE_FRAGMENT_AFFINE : 0);
        CKFFCanonicalizeNativeFragmentKey(key, layout);
        CKJitVertexShader shader;
        TestCheck(CKFFCompileNativePositionTProgram(key, layout, format, shader, depthPad && affine, depthPad) && CKJitVerify(shader), "POSITIONT frontend verifies");
        TestCheck(shader.Outputs.Size() == 14 && shader.UniformBufferCount == 2 && shader.SamplerCount == 0,
                  "POSITIONT interface keeps all varyings and has no sampled resources");
        bool texcoords = false;
        for (const auto &input : shader.Inputs) texcoords |= input.Location >= 8;
        TestCheck(texcoords == (mask != 0), "unused texture coordinate calculations disappear");
        for (unsigned sample = 0; sample < 64; ++sample) {
            VertexInputs input = {};
            for (auto &attribute : input.Attributes) for (float &x : attribute) x = random.Range(-1.0f, 1.0f);
            float *p = input.Attributes[0];
            p[0] = random.Range(-10.0f, 70.0f);
            p[1] = random.Range(-10.0f, 70.0f);
            p[2] = random.Range(0.0f, 1.0f);
            p[3] = sample % 4 == 0 ? 0.0f : random.Range(0.2f, 2.0f);
            float *viewport = PositionTUniform(input, CKRST_BLOCK_VIEWPORT);
            viewport[0] = 0.03125f; viewport[1] = -0.03125f;
            viewport[2] = -1.03f; viewport[3] = 0.97f;
            float *params = PositionTUniform(input, CKRST_BLOCK_DRAW_PARAMS, 4);
            params[1] = sample % 3 ? 7.0f / 1048575.0f : 0.0f;
            params[2] = affine ? 1.0f : 0.0f;
            params[3] = float(sample % 5);
            float *fog = PositionTUniform(input, CKRST_BLOCK_DRAW_PARAMS, 10);
            fog[3] = sample % 2 ? 3.0f : 0.0f;
            const unsigned flags[] = {0, 1, 2, 3, 4, 256, 257, 258, 259, 260, 255, 511, 0x1000, 0x1001, 0x1002, 0x1003, 0x1004, 0x1101, 0x1102, 0x1103, 0x1104};
            for (unsigned stage = 0; stage < 8; ++stage) {
                float *state = PositionTUniform(input, CKRST_BLOCK_STAGE_PARAMS, stage * 2);
                state[0] = float(random.Below(8) | (random.Below(5) << 16));
                state[1] = float(flags[(sample + stage) % (sizeof(flags) / sizeof(flags[0]))]);
                state[2] = mask & (1u << stage) ? 1.0f : 0.0f;
                for (unsigned column = 0; column < 4; ++column)
                    for (unsigned c = 0; c < 4; ++c)
                        PositionTUniform(input, CKRST_BLOCK_TEX_MATRICES, stage * 4 + column)[c] = random.Range(-1.5f, 1.5f);
            }
            float expected[14][4] = {};
            for (unsigned i = 0; i < 4; ++i)
                std::memcpy(expected[i], input.Attributes[4 + (i % 2)], sizeof(expected[i]));
            const float w = 1.0f / (p[3] == 0.0f ? 1.0f : p[3]);
            float clip[4] = {((p[0] + 0.5f) * viewport[0] + viewport[2]) * w,
                             ((p[1] + 0.5f) * viewport[1] + viewport[3]) * w, p[2] * w, w};
            expected[12][0] = w;
            expected[12][1] = input.Attributes[2][params[3] > 2.5f ? 2 : 0];
            expected[12][2] = clip[2]; expected[12][3] = w;
            if (params[3] > 2.5f) for (unsigned c = 0; c < 2; ++c) expected[13][c] = input.Attributes[2][c] * w;
            for (unsigned stage = 0; stage < 8; ++stage) {
                float *coord = expected[4 + stage];
                if (mask & (1u << stage)) {
                    const float *state = PositionTUniform(input, CKRST_BLOCK_STAGE_PARAMS, stage * 2);
                    std::memcpy(coord, input.Attributes[8 + (int(state[0]) & 7)], sizeof(expected[0]));
                    const int bits = int(state[1]), count = bits & 255;
                    if (depthPad && (bits & 0x1000)) {
                        float padded[2] = {};
                        for (unsigned c = 0; c < 2; ++c)
                            for (unsigned column = 0; column < 4; ++column)
                                padded[c] += PositionTUniform(input, CKRST_BLOCK_TEX_MATRICES, stage * 4 + column)[c] *
                                             (column == 3 ? 1.0f : coord[column]);
                        coord[0] = padded[0]; coord[1] = padded[1];
                    }
                    if (bits & 256) coord[3] = coord[count >= 1 && count <= 3 ? count - 1 : 3];
                    if (count > 0 && count < 4) {
                        if (count <= 1) coord[1] = 0;
                        if (count <= 2) coord[2] = 0;
                        if (!(bits & 256)) coord[3] = 0;
                    }
                    if (affine) for (unsigned c = 0; c < 4; ++c) coord[c] *= w;
                }
            }
            expected[11][2] = fog[3] > 0.5f ? input.Attributes[5][3] : 1.0f;
            clip[2] -= params[1] * w;
            if (params[3] > 0.5f) {
                const float *offset = input.Attributes[params[3] > 1.5f && params[3] < 2.5f ? 7 : 2];
                for (unsigned c = 0; c < 2; ++c) clip[c] += offset[c] * viewport[c] * w;
            }
            XArray<Value> values;
            Fragment unused = {};
            ExecuteNodes(shader, unused, values, &input);
            const auto matches = [](const float *a, const float *e, unsigned width) {
                for (unsigned c = 0; c < width; ++c)
                    TestCheck(std::fabs(a[c] - e[c]) <= 0.00001f * std::fmax(1.0f, std::fabs(e[c])),
                              "POSITIONT IR matches the precompiled shader's calculations");
            };
            matches(values[shader.Position.Id].F, clip, 4);
            for (const auto &output : shader.Outputs)
                matches(values[output.Value.Id].F, expected[output.Location], CKJitComponentCount(shader.Node(output.Value).Type));
        }
        XArray<uint32_t> code;
        const CKJitResourceLayout resources = {1, 0, 0};
        TestCheck(CKJitEmitSpirv(shader, resources, code), "POSITIONT emits SPIR-V");
        if (g_ShaderDirectory) Save("positiont", saved, "spv", code);
#if CKRE_ENABLE_DIRECTX
        TestCheck(CKJitEmitDxbc(shader, resources, code), "POSITIONT emits DXBC");
        if (g_ShaderDirectory) Save("positiont", saved, "dxbc", code);
#endif
        ++saved;
    }
    CKJitVertexShader shader;
    CKFFNativeFragmentKey key;
    TestCheck(!CKFFCompileNativePositionTProgram(key, kLayouts[0], CKRST_SHADER_FORMAT_UNKNOWN, shader),
              "POSITIONT requires the precompiled fallback's format");
}
