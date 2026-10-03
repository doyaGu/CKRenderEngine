// Independent scalar clip-plane evaluation after deformation and world transform.
// Geometry/varyings must remain identical when the clip output interface is added.
void TestVertexClipping() {
    Random random(0x751c3021u);
    unsigned saved = 0;
    for (CK_SHADER_FORMAT format : {CKRST_SHADER_FORMAT_SPIRV, CKRST_SHADER_FORMAT_DXIL})
    for (CKFFSamplerLayout layout : kLayouts) for (unsigned affine = 0; affine < 2; ++affine)
    for (unsigned mode = 0; mode < 3; ++mode) {
        CKFFNativeFragmentKey key;
        SetSelectArg1(key.Program, 0, CKRST_TA_TEXTURE);
        key.Switches[0] = CKFF_NATIVE_FRAGMENT_TEXTURE | (affine ? CKFF_NATIVE_FRAGMENT_AFFINE : 0);
        CKJitVertexShader shader, plain;
        const auto compile = [mode](const CKFFNativeFragmentKey &key, CKFFSamplerLayout layout,
                                    CK_SHADER_FORMAT format, CKJitVertexShader &out, bool clipping) {
            return mode == 0 ? CKFFCompileNativePositionTProgram(key, layout, format, out, clipping) :
                   mode == 1 ? CKFFCompileNativeUnlitProgram(key, layout, format, out, clipping) :
                               CKFFCompileNativeLitProgram(key, layout, format, out, clipping);
        };
        TestCheck(compile(key, layout, format, shader, true) && compile(key, layout, format, plain, false),
                  "clipped and ordinary vertex frontends compile");
        TestCheck(shader.ClipDistances.Size() == 8 && plain.ClipDistances.Size() == 0,
                  "native clip interface is eight scalar distances with two padding slots");
        const auto uniform = mode == 0 ? PositionTUniform : UnlitUniform;
        for (unsigned sample = 0; sample < 84; ++sample) {
            VertexInputs input = {};
            for (auto &attribute : input.Attributes) for (float &x : attribute) x = random.Range(-2, 2);
            input.Attributes[0][3] = sample % 4 ? random.Range(0.2f, 2) : 0;
            if (mode) {
                for (unsigned matrix = 0; matrix < 8; ++matrix) for (unsigned row = 0; row < 4; ++row) {
                    float *m = uniform(input, CKRST_BLOCK_MATRICES, matrix * 4 + row);
                    for (unsigned c = 0; c < 4; ++c) m[c] = random.Range(-0.3f, 0.3f) + float(row == c);
                }
                ConfigureTestDeformation(input, sample / 7);
            }
            float *params = uniform(input, CKRST_BLOCK_DRAW_PARAMS, 4);
            params[1] = 7.0f / 1048575; params[3] = float(sample % 5);
            uniform(input, CKRST_BLOCK_DRAW_PARAMS, 6)[0] = mode == 2 ? 0 : -1;
            float *viewport = uniform(input, CKRST_BLOCK_VIEWPORT, 0);
            viewport[0] = 2.0f / 61; viewport[1] = -2.0f / 59;
            viewport[2] = -1; viewport[3] = 1;
            const unsigned count = sample % 7;
            uniform(input, CKRST_BLOCK_CLIP_PARAMS, 0)[0] = float(count);
            float world[4] = {input.Attributes[0][0], input.Attributes[0][1], input.Attributes[0][2], 1};
            if (mode) {
                float normal[3];
                ReferenceVertexDeformation(input, world, normal);
                if (uniform(input, CKRST_BLOCK_DRAW_PARAMS, 19)[1] != 1) {
                    float local[4]; std::memcpy(local, world, sizeof(local));
                    for (unsigned c = 0; c < 4; ++c) {
                        world[c] = 0;
                        for (unsigned r = 0; r < 4; ++r)
                            world[c] += local[r] * uniform(input, CKRST_BLOCK_MATRICES, 4 + r)[c];
                    }
                }
            }
            float expected[8] = {};
            for (unsigned i = 0; i < 6; ++i) {
                float *plane = uniform(input, CKRST_BLOCK_CLIP_PLANES, i);
                for (unsigned c = 0; c < 4; ++c) {
                    plane[c] = i >= count ? std::numeric_limits<float>::quiet_NaN() : random.Range(-1, 1);
                    if (i < count) expected[i] += plane[c] * world[c];
                }
            }
            XArray<Value> values, ordinary;
            Fragment unused = {};
            ExecuteNodes(shader, unused, values, &input);
            ExecuteNodes(plain, unused, ordinary, &input);
            for (unsigned i = 0; i < 8; ++i) {
                const float actual = values[shader.ClipDistances[i].Id].F[0];
                TestCheck(std::fabs(actual - expected[i]) < 0.00002f * (1 + std::fabs(expected[i])),
                          "clip distances match independent world-space reference, with inactive planes zero");
            }
            for (unsigned c = 0; c < 4; ++c)
                TestCheck(values[shader.Position.Id].F[c] == ordinary[plain.Position.Id].F[c],
                          "adding clip outputs preserves position arithmetic");
            for (int i = 0; i < shader.Outputs.Size(); ++i) {
                const auto a = shader.Outputs[i].Value, b = plain.Outputs[i].Value;
                for (unsigned c = 0; c < CKJitComponentCount(shader.Node(a).Type); ++c)
                    TestCheck(values[a.Id].F[c] == ordinary[b.Id].F[c], "clipping preserves every varying");
            }
        }
        XArray<uint32_t> code;
        const CKJitResourceLayout resources = {1, 0, 0};
        const char *names[] = {"clip_positiont", "clip_unlit", "clip_lit"};
        TestCheck(CKJitEmitSpirv(shader, resources, code), "clip frontend emits SPIR-V");
        if (g_ShaderDirectory) Save(names[mode], saved, "spv", code);
#if CKRE_ENABLE_DIRECTX
        TestCheck(CKJitEmitDxbc(shader, resources, code), "clip frontend emits DXBC");
        if (g_ShaderDirectory) Save(names[mode], saved, "dxbc", code);
#endif
        ++saved;
    }
}
