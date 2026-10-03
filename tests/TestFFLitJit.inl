void TestLitEligibility() {
    CKFFConstantSet constants;
    TestCheck(!CKFFNativeLitDraw(constants), "missing lighting constants keep fallback");
    float draw[20][4] = {};
    float palette[4][16] = {};
    constants.Set(CKRST_BLOCK_VERTEX_BLEND_MATRICES, palette, sizeof(palette));
    constants.Set(CKRST_BLOCK_DRAW_PARAMS, draw, sizeof(draw) - 1);
    TestCheck(!CKFFNativeLitDraw(constants), "truncated lighting constants keep fallback");
    for (float count : {-2.0f, -1.0f, 0.0f, 1.0f, 1.5f, 8.0f, 9.0f,
                        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    for (float blend : {0.0f, 1.0f, 2.0f, 3.0f, std::numeric_limits<float>::quiet_NaN()}) {
        draw[6][0] = count; draw[19][1] = blend; draw[19][2] = 3;
        constants.Set(CKRST_BLOCK_DRAW_PARAMS, draw, sizeof(draw));
        const bool expected = (blend == 0 || blend == 1 || blend == 2) && (count == 0 || count == 1 || count == 8);
        TestCheck(CKFFNativeLitDraw(constants) == expected, "only canonical lit and tween draws are eligible");
    }
}

void TestLit() {
    Random random(0x92a1369u);
    unsigned saved = 0;
    for (CK_SHADER_FORMAT format : {CKRST_SHADER_FORMAT_SPIRV, CKRST_SHADER_FORMAT_DXIL})
    for (CKFFSamplerLayout layout : kLayouts) for (unsigned textured : {0u, 1u}) {
        CKFFNativeFragmentKey key;
        SetSelectArg1(key.Program, 0, textured ? CKRST_TA_TEXTURE : CKRST_TA_DIFFUSE);
        key.Switches[0] = textured ? CKFF_NATIVE_FRAGMENT_TEXTURE : 0;
        CKJitVertexShader shader, unlit;
        TestCheck(CKFFCompileNativeLitProgram(key, layout, format, shader) && CKJitVerify(shader), "lit frontend verifies");
        TestCheck(CKFFCompileNativeUnlitProgram(key, layout, format, unlit), "geometry reference compiles");
        TestCheck(shader.Outputs.Size() == 14 && shader.UniformBufferCount == 3 && shader.SamplerCount == 0,
                  "lit shader preserves the native 3D interface");
        for (unsigned sample = 0; sample < 192; ++sample) {
            VertexInputs input = {};
            for (unsigned matrix = 0; matrix < 8; ++matrix) for (unsigned c = 0; c < 4; ++c) {
                UnlitUniform(input, CKRST_BLOCK_MATRICES, matrix * 4 + c)[c] = 1;
                UnlitUniform(input, CKRST_BLOCK_TEX_MATRICES, matrix * 4 + c)[c] = 1;
            }
            for (auto &attribute : input.Attributes) for (float &x : attribute) x = random.Range(0.1f, 0.9f);
            input.Attributes[0][2] = random.Range(0.5f, 4.0f);
            for (unsigned row = 0; row < 4; ++row) for (unsigned c = 0; c < 4; ++c)
                UnlitUniform(input, CKRST_BLOCK_DRAW_PARAMS, row)[c] = random.Range(0, 0.7f);
            float *power = UnlitUniform(input, CKRST_BLOCK_DRAW_PARAMS, 4);
            power[0] = sample % 5 ? random.Range(0.5f, 32) : 0;
            float *sources = UnlitUniform(input, CKRST_BLOCK_DRAW_PARAMS, 5);
            for (unsigned c = 0; c < 4; ++c) sources[c] = float((sample + c) % 4);
            float *lighting = UnlitUniform(input, CKRST_BLOCK_DRAW_PARAMS, 6);
            lighting[0] = float(sample % 9);
            for (unsigned c = 1; c < 4; ++c) lighting[c] = random.Range(0, 0.3f);
            float *flags = UnlitUniform(input, CKRST_BLOCK_DRAW_PARAMS, 7);
            flags[0] = float(sample % 2); flags[1] = float((sample / 2) % 2); flags[3] = float((sample / 4) % 2);
            UnlitUniform(input, CKRST_BLOCK_DRAW_PARAMS, 8)[2] = float((sample / 8) % 2);
            for (unsigned i = 0; i < 8; ++i) {
                float light[7][4] = {};
                for (unsigned c = 0; c < 3; ++c) {
                    light[0][c] = random.Range(-2, 5);
                    light[1][c] = random.Range(-1, 1);
                    light[2][c] = random.Range(0, 0.4f);
                    light[3][c] = random.Range(0, 0.4f);
                    light[4][c] = random.Range(0, 0.1f);
                }
                light[0][3] = float((sample + i) % 3);
                light[1][3] = sample % 3 ? 3.5f : 0; // ranged and unlimited lights
                light[5][0] = 0.8f; light[5][1] = 0.1f; light[5][2] = 0.03f;
                light[5][3] = sample % 7 ? 2.0f : 0;
                light[6][0] = 0.8f; light[6][1] = 0.3f;
                for (unsigned row = 0; row < 7; ++row) {
                    std::memcpy(UnlitUniform(input, CKRST_BLOCK_LIGHTS, i * 7 + row), light[row], 16);
                    if (i == 0) {
                        // Distinct inline data detects accidentally reading the light buffer.
                        light[row][0] *= 0.7f;
                        std::memcpy(UnlitUniform(input, CKRST_BLOCK_DRAW_PARAMS, 12 + row), light[row], 16);
                    }
                }
            }
            ConfigureTestDeformation(input, sample);
            float localPosition[4], localNormal[3];
            ReferenceVertexDeformation(input, localPosition, localNormal);
            const auto length = [](const float *v) { return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); };
            const auto dot = [](const float *a, const float *b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; };
            float normal[3];
            for (unsigned c = 0; c < 3; ++c)
                normal[c] = localNormal[c] / (flags[1] ? length(localNormal) : 1.0f);
            const float *material[4];
            for (unsigned c = 0; c < 4; ++c) {
                const int source = int(sources[c] + 0.5f);
                material[c] = source == 1 ? input.Attributes[4] : source == 2 ? input.Attributes[5] :
                    UnlitUniform(input, CKRST_BLOCK_DRAW_PARAMS, c);
            }
            float amb[3] = {}, diff[3] = {}, spec[3] = {};
            for (unsigned i = 0; i < unsigned(lighting[0]); ++i) {
                float light[7][4];
                for (unsigned row = 0; row < 7; ++row)
                    std::memcpy(light[row], i == 0 && flags[3] ? UnlitUniform(input, CKRST_BLOCK_DRAW_PARAMS, 12 + row) :
                        UnlitUniform(input, CKRST_BLOCK_LIGHTS, i * 7 + row), 16);
                float toLight[3], attenuation = 1;
                if (light[0][3] < 0.5f) {
                    for (unsigned c = 0; c < 3; ++c) toLight[c] = -light[1][c] / length(light[1]);
                } else {
                    for (unsigned c = 0; c < 3; ++c) toLight[c] = light[0][c] - localPosition[c];
                    const float distance = length(toLight);
                    for (float &x : toLight) x /= std::fmax(distance, 0.0001f);
                    attenuation = 1 / std::fmax(light[5][0] + light[5][1] * distance + light[5][2] * distance * distance, 0.0001f);
                    if (light[1][3] > 0 && distance > light[1][3]) attenuation = 0;
                    if (light[0][3] > 1.5f) {
                        const float rho = -dot(toLight, light[1]) / length(light[1]);
                        float spot = std::pow(std::fmin(1.0f, std::fmax(0.0f,
                            (rho - light[6][1]) / std::fmax(light[6][0] - light[6][1], 0.0001f))), light[5][3]);
                        if (rho <= light[6][1]) spot = 0;
                        if (rho > light[6][0]) spot = 1;
                        attenuation *= spot;
                    }
                }
                const float ndotl = std::fmax(dot(normal, toLight) / length(toLight), 0.0f);
                for (unsigned c = 0; c < 3; ++c) {
                    amb[c] += light[4][c] * attenuation;
                    diff[c] += light[2][c] * ndotl * attenuation;
                }
                if (ndotl > 0 && power[0] > 0) {
                    float halfVector[3];
                    for (unsigned c = 0; c < 3; ++c) halfVector[c] = toLight[c] -
                        (flags[0] ? localPosition[c] / length(localPosition) : float(c == 2));
                    const float ndoth = std::fmax(dot(normal, halfVector) / length(halfVector), 0.0f);
                    for (unsigned c = 0; c < 3; ++c) spec[c] += light[3][c] * std::pow(ndoth, power[0]) * attenuation;
                }
            }
            float expected[2][4];
            for (unsigned c = 0; c < 3; ++c) {
                expected[0][c] = std::fmin(1.0f, std::fmax(0.0f, material[3][c] + material[1][c] * lighting[c + 1] +
                    material[1][c] * amb[c] + material[0][c] * diff[c]));
                expected[1][c] = UnlitUniform(input, CKRST_BLOCK_DRAW_PARAMS, 8)[2] ?
                    std::fmin(1.0f, std::fmax(0.0f, material[2][c] * spec[c])) : input.Attributes[5][c];
            }
            expected[0][3] = material[0][3]; expected[1][3] = input.Attributes[5][3];
            XArray<Value> values, geometry;
            Fragment unused = {};
            ExecuteNodes(shader, unused, values, &input);
            ExecuteNodes(unlit, unused, geometry, &input);
            for (const auto &output : shader.Outputs) {
                if (output.Location >= 4) continue;
                for (unsigned c = 0; c < 4; ++c)
                    TestCheck(std::fabs(values[output.Value.Id].F[c] - expected[output.Location % 2][c]) < 0.0001f,
                              "lit smooth/flat colors match independent scalar lighting");
            }
            for (unsigned c = 0; c < 4; ++c)
                TestCheck(values[shader.Position.Id].F[c] == geometry[unlit.Position.Id].F[c], "lighting preserves geometry arithmetic");
        }
        XArray<uint32_t> code;
        const CKJitResourceLayout resources = {1, 0, 0};
        TestCheck(CKJitEmitSpirv(shader, resources, code), "lit emits SPIR-V");
        if (g_ShaderDirectory) Save("lit", saved, "spv", code);
#if CKRE_ENABLE_DIRECTX
        TestCheck(CKJitEmitDxbc(shader, resources, code), "lit emits DXBC");
        if (g_ShaderDirectory) Save("lit", saved, "dxbc", code);
#endif
        ++saved;
    }
}
