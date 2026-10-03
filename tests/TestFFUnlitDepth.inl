// unlit 3D must retain the precompiled program's depth when compilation
// completes between draws. Each depth-bias case starts with an empty cache.
void CheckUnlitJitDepth()
{
    const char *jit = GetEnvValue("CKRE_SDL_GPU_FF_JIT");
    const char *vertex = GetEnvValue("CKRE_SDL_GPU_FF_VERTEX_JIT");
    if ((jit && strcmp(jit, "0") == 0) || (vertex && strcmp(vertex, "0") == 0)) return;
    for (bool clipping : {false, true})
    for (unsigned matrixCase : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u}) for (unsigned bias : {0u, 1u, 7u, 16u}) {
        const bool inset = matrixCase % 2 != 0;
        Backend backend;
        TestCheck(OpenBackend(backend, kWidth, kHeight), "open unlit 3D depth context");
        auto *ctx = static_cast<CKSdlGpuRasterizerContext *>(backend.Context);
        if (inset) {
            CKViewportData viewport = {};
            viewport.ViewX = 1; viewport.ViewY = 2;
            viewport.ViewWidth = 61; viewport.ViewHeight = 59; viewport.ViewZMax = 1;
            TestCheck(ctx->SetViewport(&viewport), "unlit 3D non-power-of-two inset viewport");
        }
        auto draw = [&](int writer, int tester, Pixels &pixels, bool writerLit = false, bool testerLit = false) {
            SetDiffuseState(ctx);
            BeginFrame(ctx, CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH);
            ctx->SetRenderState(VXRENDERSTATE_ZENABLE, TRUE);
            ctx->SetRenderState(VXRENDERSTATE_ZBIAS, bias);
            VxPlane plane;
            plane.m_Normal = VxVector(1, 0, 0); plane.m_D = 0.2f;
            TestCheck(ctx->SetUserClipPlane(5, plane), "depth test sets a sparse user plane");
            ctx->SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, clipping ? 32 : 0);
            VxVector positions[3] = {VxVector(-0.91f, -0.85f, 0.23f), VxVector(0.92f, -0.78f, 0.81f),
                                     VxVector(-0.82f, 0.93f, 0.47f)};
            VxMatrix world, projection;
            Vx3DMatrixIdentity(world); Vx3DMatrixIdentity(projection);
            if (matrixCase) {
                world[0][0] = 0.83f; world[1][1] = 0.91f;
                world[0][1] = 0.07f; world[1][0] = -0.04f;
                world[3][0] = 0.023f; world[3][1] = -0.031f;
                world[0][2] = 0.071f; world[1][2] = 0.023f;
                world[3][2] = 0.013f;
            }
            if (matrixCase >= 2) {
                projection[0][3] = 0.11f; projection[1][3] = -0.17f;
                projection[2][3] = 0.31f; projection[3][3] = 1.07f;
                projection[2][2] = 0.73f; projection[3][2] = 0.047f;
            }
            TestCheck(ctx->SetTransformMatrix(VXMATRIX_WORLD, world), "unlit world matrix");
            TestCheck(ctx->SetTransformMatrix(VXMATRIX_PROJECTION, projection), "unlit projective matrix");
            VxVector normals[3] = {VxVector(0, 0, 1), VxVector(0, 0, 1), VxVector(0, 0, 1)};
            VxDrawPrimitiveData data = {};
            data.VertexCount = 3; data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_DIFFUSE;
            data.PositionPtr = positions; data.PositionStride = sizeof(VxVector);
            data.NormalPtr = normals; data.NormalStride = sizeof(VxVector);
            data.ColorPtr = const_cast<CKDWORD *>(kWhite); data.ColorStride = sizeof(CKDWORD);
            VxVector tween[3], tweenNormals[3];
            if (matrixCase >= 4 && matrixCase <= 6) {
                ctx->SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_TWEENING);
                ctx->SetRenderState(VXRENDERSTATE_TWEENFACTOR, FloatBits(0.35f));
                data.Flags |= CKRST_DP_TWEEN;
                for (unsigned i = 0; i < 3; ++i) {
                    tween[i] = positions[i] * 0.63f + VxVector(0.031f, -0.079f, 0.027f);
                    tweenNormals[i] = VxVector(0.2f, 0.1f, 0.9f);
                }
                // Additional matrix cases cover position-only, both streams,
                // and normal-only while switching fallback/JIT depth writers.
                if (matrixCase != 6) { data.TweenPositionPtr = tween; data.TweenPositionStride = sizeof(VxVector); }
                if (matrixCase != 4) { data.TweenNormalPtr = tweenNormals; data.TweenNormalStride = sizeof(VxVector); }
            }
            float blendPositions[3][7] = {};
            if (matrixCase >= 7) {
                const unsigned count = matrixCase - 7;
                const bool indexed = count != 2;
                const unsigned storedWeights = indexed && count == 0 ? 1 : count;
                ctx->SetRenderState(VXRENDERSTATE_VERTEXBLEND, count ? count : VXVBLEND_0WEIGHTS);
                ctx->SetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE, indexed);
                data.Flags |= CKRST_DP_WEIGHT(storedWeights) | (indexed ? CKRST_DP_MATRIXPAL : 0);
                for (unsigned i = 0; i < 3; ++i) {
                    std::memcpy(blendPositions[i], &positions[i], sizeof(VxVector));
                    for (unsigned j = 0; j < count; ++j) blendPositions[i][3 + j] = 0.13f + i * 0.017f + j * 0.09f;
                    const CKDWORD indices = 0x03000102u;
                    if (indexed) std::memcpy(&blendPositions[i][3 + storedWeights], &indices, sizeof(indices));
                }
                data.PositionPtr = blendPositions; data.PositionStride = sizeof(blendPositions[0]);
                for (unsigned slot = 1; slot < 4; ++slot) {
                    VxMatrix matrix = world;
                    matrix[0][0] += 0.03f * slot; matrix[1][2] -= 0.017f * slot;
                    matrix[3][0] += 0.07f * slot; matrix[3][2] += 0.021f * slot;
                    TestCheck(ctx->SetTransformMatrix(VXMATRIX_WORLDMATRIX(slot), matrix), "blend depth palette");
                }
            }
            SetPassThroughStages(ctx, writer);
            ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TFACTOR);
            ctx->SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TFACTOR);
            ctx->SetRenderState(VXRENDERSTATE_TEXTUREFACTOR, kRed[0]);
            ctx->SetRenderState(VXRENDERSTATE_LIGHTING, writerLit);
            ctx->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, TRUE);
            ctx->SetRenderState(VXRENDERSTATE_ZFUNC, VXCMP_LESSEQUAL);
            TestCheck(ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data), "unlit 3D depth writer");
            SetPassThroughStages(ctx, tester);
            ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TFACTOR);
            ctx->SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TFACTOR);
            ctx->SetRenderState(VXRENDERSTATE_TEXTUREFACTOR, kGreen[0]);
            ctx->SetRenderState(VXRENDERSTATE_LIGHTING, testerLit);
            ctx->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, FALSE);
            ctx->SetRenderState(VXRENDERSTATE_ZFUNC, VXCMP_EQUAL);
            TestCheck(ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data), "unlit 3D EQUAL depth reader");
            EndFrame(ctx);
            ReadBackbuffer(ctx, pixels);
        };
        Pixels reference, actual;
        draw(3, 4, reference);
        int green = 0, red = 0;
        for (int y = 0; y < kHeight; ++y) for (int x = 0; x < kWidth; ++x) {
            green += PixelNear(reference, x, y, 0, 255, 0) ? 1 : 0;
            red += PixelNear(reference, x, y, 255, 0, 0) ? 1 : 0;
        }
        TestCheck(green > (clipping ? 120 : matrixCase >= 4 ? 250 : 500) && red == 0, "precompiled unlit 3D depth baseline covers the triangle");
        TestCheck(ctx->FinishBackgroundWorkForTests(30000), "unlit 3D depth shaders and pipelines finish");
        const auto counts = ctx->CountFFJitProgramsForTests();
        TestCheck(counts.UnlitPipelines == 2, "both initial depth pipelines use generated vertex shaders");
        auto beforeMixed = ctx->GetFFJitStats();
        draw(5, 4, actual);
        CheckMatchingImage("unlit 3D JIT reads precompiled depth", actual, reference, 0);
        auto afterMixed = ctx->GetFFJitStats();
        TestCheck(afterMixed.PipelineSelections == beforeMixed.PipelineSelections + 2 &&
                      afterMixed.PipelineReady == beforeMixed.PipelineReady + 1,
                  "mixed depth read uses exactly one ready JIT pipeline and one fallback");
        beforeMixed = afterMixed;
        draw(3, 6, actual);
        CheckMatchingImage("unlit 3D precompiled reads JIT depth", actual, reference, 0);
        afterMixed = ctx->GetFFJitStats();
        TestCheck(afterMixed.PipelineSelections == beforeMixed.PipelineSelections + 2 &&
                      afterMixed.PipelineReady == beforeMixed.PipelineReady + 1,
                  "mixed depth write uses exactly one ready JIT pipeline and one fallback");
        TestCheck(ctx->FinishBackgroundWorkForTests(30000), "all unlit 3D depth jobs finish");
        const auto before = ctx->GetFFJitStats();
        draw(5, 6, actual);
        CheckMatchingImage("unlit 3D warm depth", actual, reference, 0);
        const auto after = ctx->GetFFJitStats();
        TestCheck(after.CompileQueued == before.CompileQueued && after.PipelineQueued == before.PipelineQueued &&
                      after.PipelineReady == before.PipelineReady + 2 && after.VertexCompileFailed == 0,
                  "warm unlit 3D depth draws reuse both generated pipelines");
        // The warm lit binding has its own generated vertex shader. Its
        // geometry must agree with the generated unlit binding as well.
        draw(5, 6, actual, true, true);
        TestCheck(ctx->FinishBackgroundWorkForTests(30000), "lit depth bindings finish");
        const auto beforeLit = ctx->GetFFJitStats();
        draw(5, 6, actual, true, false);
        CheckMatchingImage("generated unlit reads generated lit depth", actual, reference, 0);
        draw(5, 6, actual, false, true);
        CheckMatchingImage("generated lit reads generated unlit depth", actual, reference, 0);
        const auto afterLit = ctx->GetFFJitStats();
        TestCheck(afterLit.PipelineReady == beforeLit.PipelineReady + 4 &&
                      afterLit.CompileQueued == beforeLit.CompileQueued && afterLit.PipelineQueued == beforeLit.PipelineQueued,
                  "mixed lit/unlit depth uses four warm selections without new work");
        TestCheck(clipping ? ctx->GetFFJitStats().ClipReady > 0 : ctx->GetFFJitStats().ClipReady == 0,
                  "strict depth cases execute the expected generated clip interface");
        CloseBackend(backend);
    }
    printf("  unlit 3D mixed precompiled/JIT EQUAL depth passes: zero and nonzero bias\n");
}

// All draws share one fragment key. Tween factors reuse lit/unlit bindings;
// matrix blending shares the same generated binding as ordinary geometry.
void CheckUnlitJitTransitions()
{
    const char *jit = GetEnvValue("CKRE_SDL_GPU_FF_JIT");
    const char *vertex = GetEnvValue("CKRE_SDL_GPU_FF_VERTEX_JIT");
    if ((jit && strcmp(jit, "0") == 0) || (vertex && strcmp(vertex, "0") == 0)) return;
    const bool hadSetting = jit != nullptr;
    const std::string setting = jit ? jit : "";
    auto replay = [](CKSdlGpuRasterizerContext *ctx, std::vector<Pixels> &images) {
        images.clear();
        for (unsigned mode : {0u, 1u, 0u, 2u, 3u, 4u, 0u}) {
            SetDiffuseState(ctx);
            ctx->SetRenderState(VXRENDERSTATE_LIGHTING, mode == 1 || mode == 3);
            ctx->SetRenderState(VXRENDERSTATE_AMBIENT, 0xff204080u);
            ctx->SetRenderState(VXRENDERSTATE_VERTEXBLEND, (mode == 2 || mode == 3) ? VXVBLEND_TWEENING : mode == 4 ? VXVBLEND_0WEIGHTS : VXVBLEND_DISABLE);
            ctx->SetRenderState(VXRENDERSTATE_TWEENFACTOR, FloatBits(1.0f));
            VxVector normals[3] = {VxVector(0, 0, 1), VxVector(0, 0, 1), VxVector(0, 0, 1)};
            VxVector tween[3];
            for (unsigned i = 0; i < 3; ++i) tween[i] = kCenterTriangle[i] * 0.6f;
            VxDrawPrimitiveData data = {};
            data.VertexCount = 3;
            data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_DIFFUSE | ((mode == 2 || mode == 3) ? CKRST_DP_TWEEN : 0);
            data.PositionPtr = const_cast<VxVector *>(kCenterTriangle); data.PositionStride = sizeof(VxVector);
            data.NormalPtr = normals; data.NormalStride = sizeof(VxVector);
            data.ColorPtr = const_cast<CKDWORD *>(kWhite); data.ColorStride = sizeof(CKDWORD);
            if (mode == 2) { data.TweenPositionPtr = tween; data.TweenPositionStride = sizeof(VxVector); }
            if (mode == 3) { data.TweenNormalPtr = normals; data.TweenNormalStride = sizeof(VxVector); }
            BeginFrame(ctx, CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH);
            TestCheck(ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data), "unlit/lighting/tween transition draw");
            EndFrame(ctx);
            Pixels image;
            ReadBackbuffer(ctx, image);
            images.push_back(image);
        }
    };
    SetEnvValue("CKRE_SDL_GPU_FF_JIT", "0");
    Backend reference;
    TestCheck(OpenBackend(reference, kWidth, kHeight), "open unlit transition reference");
    std::vector<Pixels> expected, actual;
    replay(static_cast<CKSdlGpuRasterizerContext *>(reference.Context), expected);
    CloseBackend(reference);
    if (hadSetting) SetEnvValue("CKRE_SDL_GPU_FF_JIT", setting.c_str());
    else SDL_unsetenv_unsafe("CKRE_SDL_GPU_FF_JIT");
    TestCheck(!PixelNear(expected[1], kWidth / 2, kHeight / 2, 255, 255, 255), "lighting reference changes the color");
    TestCheck(!PixelNear(expected[3], kWidth / 2, kHeight * 4 / 5, 255, 255, 255), "tween reference moves the triangle");
    Backend backend;
    TestCheck(OpenBackend(backend, kWidth, kHeight), "open unlit transition context");
    auto *ctx = static_cast<CKSdlGpuRasterizerContext *>(backend.Context);
    replay(ctx, actual);
    TestCheck(ctx->FinishBackgroundWorkForTests(30000), "transition bindings finish");
    const auto before = ctx->GetFFJitStats();
    const auto counts = ctx->CountFFJitProgramsForTests();
    TestCheck(counts.Ready == 1 && counts.Programs == 2 && counts.UnlitPrograms == 1 && counts.UnlitPipelines == 2 &&
                  counts.LitPrograms == 1 && counts.LitPipelines == 2,
              "one fragment key has shared ordinary/tween lit and unlit bindings and matrix blending");
    replay(ctx, actual);
    for (unsigned i = 0; i < actual.size(); ++i)
        CheckMatchingImage("unlit/lighting/tween A-B-A", actual[i], expected[i], 1);
    const auto after = ctx->GetFFJitStats();
    TestCheck(before.CompileQueued == after.CompileQueued && before.PipelineQueued == after.PipelineQueued &&
                  after.PipelineReady == before.PipelineReady + 7 && after.VertexCompileFailed == 0 &&
                  after.LitReady == before.LitReady + 2 && after.UnlitReady == before.UnlitReady + 5 &&
                  after.TweenReady == before.TweenReady + 2 && after.MatrixBlendReady == before.MatrixBlendReady + 1,
              "all transition draws reuse their ready pipelines");
    CloseBackend(backend);
}
