// POSITIONT must retain the precompiled program's depth when compilation
// completes between draws. Each depth-bias case starts with an empty cache.
void CheckPositionTJitDepth()
{
    const char *jit = GetEnvValue("CKRE_SDL_GPU_FF_JIT");
    const char *vertex = GetEnvValue("CKRE_SDL_GPU_FF_VERTEX_JIT");
    if ((jit && strcmp(jit, "0") == 0) || (vertex && strcmp(vertex, "0") == 0)) return;
    for (bool clipping : {false, true})
    for (bool inset : {false, true}) for (unsigned bias : {0u, 1u, 7u, 16u}) {
        Backend backend;
        TestCheck(OpenBackend(backend, kWidth, kHeight), "open POSITIONT depth context");
        auto *ctx = static_cast<CKSdlGpuRasterizerContext *>(backend.Context);
        if (inset) {
            CKViewportData viewport = {};
            viewport.ViewX = 1; viewport.ViewY = 2;
            viewport.ViewWidth = 61; viewport.ViewHeight = 59; viewport.ViewZMax = 1;
            TestCheck(ctx->SetViewport(&viewport), "POSITIONT non-power-of-two inset viewport");
        }
        auto draw = [&](int writer, int tester, Pixels &pixels) {
            SetDiffuseState(ctx);
            BeginFrame(ctx, CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH);
            ctx->SetRenderState(VXRENDERSTATE_ZENABLE, TRUE);
            ctx->SetRenderState(VXRENDERSTATE_ZBIAS, bias);
            VxPlane plane;
            plane.m_Normal = VxVector(1, 0, 0); plane.m_D = -12.0f;
            TestCheck(ctx->SetUserClipPlane(5, plane), "depth test sets a sparse user plane");
            ctx->SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, clipping ? 32 : 0);
            float positions[3][4] = {{2.25f, 3.75f, 0.23f, 0.71f}, {61.1f, 5.25f, 0.81f, 1.37f},
                                     {5.9f, 61.5f, 0.47f, 0.0f}};
            if (inset) {
                positions[0][3] = 1.17f; positions[1][3] = 0.43f; positions[2][3] = 1.83f;
            }
            float uv[3][4] = {};
            SetPassThroughStages(ctx, writer);
            ctx->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, TRUE);
            ctx->SetRenderState(VXRENDERSTATE_ZFUNC, VXCMP_LESSEQUAL);
            TestCheck(DrawTexturedPositionTTriangle(ctx, positions, kRed, uv), "POSITIONT depth writer");
            SetPassThroughStages(ctx, tester);
            ctx->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, FALSE);
            ctx->SetRenderState(VXRENDERSTATE_ZFUNC, VXCMP_EQUAL);
            TestCheck(DrawTexturedPositionTTriangle(ctx, positions, kGreen, uv), "POSITIONT EQUAL depth reader");
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
        TestCheck(green > (clipping ? 600 : 1000) && red == 0, "precompiled POSITIONT depth baseline covers the triangle");
        TestCheck(ctx->FinishBackgroundWorkForTests(30000), "POSITIONT depth shaders and pipelines finish");
        const auto counts = ctx->CountFFJitProgramsForTests();
        TestCheck(counts.PositionTPipelines == 2, "both initial depth pipelines use generated vertex shaders");
        auto beforeMixed = ctx->GetFFJitStats();
        draw(5, 4, actual);
        CheckMatchingImage("POSITIONT JIT reads precompiled depth", actual, reference, 0);
        auto afterMixed = ctx->GetFFJitStats();
        TestCheck(afterMixed.PipelineSelections == beforeMixed.PipelineSelections + 2 &&
                      afterMixed.PipelineReady == beforeMixed.PipelineReady + 1,
                  "mixed depth read uses exactly one ready JIT pipeline and one fallback");
        beforeMixed = afterMixed;
        draw(3, 6, actual);
        CheckMatchingImage("POSITIONT precompiled reads JIT depth", actual, reference, 0);
        afterMixed = ctx->GetFFJitStats();
        TestCheck(afterMixed.PipelineSelections == beforeMixed.PipelineSelections + 2 &&
                      afterMixed.PipelineReady == beforeMixed.PipelineReady + 1,
                  "mixed depth write uses exactly one ready JIT pipeline and one fallback");
        TestCheck(ctx->FinishBackgroundWorkForTests(30000), "all POSITIONT depth jobs finish");
        const auto before = ctx->GetFFJitStats();
        draw(5, 6, actual);
        CheckMatchingImage("POSITIONT warm depth", actual, reference, 0);
        const auto after = ctx->GetFFJitStats();
        TestCheck(after.CompileQueued == before.CompileQueued && after.PipelineQueued == before.PipelineQueued &&
                      after.PipelineReady == before.PipelineReady + 2 && after.VertexCompileFailed == 0,
                  "warm POSITIONT depth draws reuse both generated pipelines");
        TestCheck(clipping ? ctx->GetFFJitStats().ClipReady > 0 : ctx->GetFFJitStats().ClipReady == 0,
                  "strict depth cases execute the expected generated clip interface");
        CloseBackend(backend);
    }
    printf("  POSITIONT mixed precompiled/JIT EQUAL depth passes: zero and nonzero bias\n");
}
