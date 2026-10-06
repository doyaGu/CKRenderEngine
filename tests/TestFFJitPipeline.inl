// Included after the replay and fragment-cache pressure helpers.
void DrawPipelineVariant(CKRasterizerContext *ctx, unsigned id,
                         const VxVector *positions = nullptr, unsigned program = 42)
{
    SetCacheProgram(ctx, program);
    // Every stencil mask names a distinct PSO. Channel masks also make the
    // grid's cells visibly different, without changing the fragment key.
    ctx->SetRenderState(VXRENDERSTATE_STENCILENABLE, TRUE);
    ctx->SetRenderState(VXRENDERSTATE_STENCILFUNC, VXCMP_ALWAYS);
    ctx->SetRenderState(VXRENDERSTATE_STENCILMASK, id);
    ctx->SetRenderState(VXRENDERSTATE_STENCILWRITEMASK, 0);
    ctx->SetRenderState(VXRENDERSTATE_STENCILFAIL, VXSTENCILOP_KEEP);
    ctx->SetRenderState(VXRENDERSTATE_STENCILZFAIL, VXSTENCILOP_KEEP);
    ctx->SetRenderState(VXRENDERSTATE_STENCILPASS, VXSTENCILOP_KEEP);
    ctx->SetRenderState(VXRENDERSTATE_COLORWRITEENABLE, id % 15 + 1);
    const float x = -0.98f + (id % 16) * 0.122f, y = -0.98f + ((id / 16) % 12) * 0.163f;
    const VxVector triangle[] = {VxVector(x, y, 0.5f), VxVector(x + 0.114f, y, 0.5f),
                                 VxVector(x + 0.057f, y + 0.15f, 0.5f)};
    const CKDWORD colors[] = {0xff406080u, 0xff808040u, 0xff604080u};
    TestCheck(DrawColorTriangle(ctx, positions ? positions : triangle, colors), "pipeline pressure draw");
}

void DrawPipelineGrid(CKRasterizerContext *ctx)
{
    for (unsigned id = 0; id < 192; ++id) DrawPipelineVariant(ctx, id);
}

void CheckFFJitPipelinePressure()
{
    const char *jit = GetEnvValue("CKRE_SDL_GPU_FF_JIT");
    if (jit && strcmp(jit, "0") == 0) return;
    const bool restoreJit = jit != nullptr;
    const std::string previousJit = jit ? jit : "";
    const char *cache = SDL_getenv("CKRE_SDL_GPU_FF_JIT_CACHE");
    const bool restoreCache = cache != nullptr;
    const std::string previousCache = cache ? cache : "";
    char directory[1024];
    snprintf(directory, sizeof(directory), "%sffjit-pipeline-test", SDL_GetBasePath());
    RemoveManifestDirectory(directory);
    SDL_setenv_unsafe("CKRE_SDL_GPU_FF_JIT_CACHE", directory, 1);

    const CKDWORD clear = CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH | CKRST_CTXCLEAR_STENCIL;
    Backend backend;
    Pixels expected, actual;
    SDL_setenv_unsafe("CKRE_SDL_GPU_FF_JIT", "0", 1);
    TestCheck(OpenBackend(backend, kWidth, kHeight), "open pipeline reference context");
    RenderAndRead(backend.Context, clear, NULL, [&]() { DrawPipelineGrid(backend.Context); }, expected);
    CloseBackend(backend);
    if (restoreJit) SDL_setenv_unsafe("CKRE_SDL_GPU_FF_JIT", previousJit.c_str(), 1);
    else SDL_unsetenv_unsafe("CKRE_SDL_GPU_FF_JIT");

    TestCheck(OpenBackend(backend, kWidth, kHeight), "open pipeline pressure context");
    auto *ctx = static_cast<CKSdlGpuRasterizerContext *>(backend.Context);
    // Compile the one fragment shader using a PSO outside the grid. The burst
    // then tests pipeline admission independently of shader compilation.
    RenderAndRead(ctx, clear, NULL, [&]() { DrawPipelineVariant(ctx, 255); }, actual);
    TestCheck(ctx->FinishBackgroundWorkForTests(30000), "pressure shader becomes ready");
    const auto before = ctx->GetFFJitStats();
    RenderAndRead(ctx, clear, NULL, [&]() {
        DrawPipelineGrid(ctx);
        DrawPipelineVariant(ctx, 0);   // Already queued: must not duplicate work.
        DrawPipelineVariant(ctx, 191); // Deferred: must not become failed/pending forever.
    }, actual);
    CheckMatchingImage("pipeline-budget-fallback", actual, expected);
    const auto burst = ctx->GetFFJitStats();
    TestCheck(burst.CompileQueued == before.CompileQueued &&
                  burst.PipelineQueued == before.PipelineQueued + 128 && burst.PipelinePendingPeak == 128 &&
                  burst.PipelineQueueDeferred == before.PipelineQueueDeferred + 65 &&
                  burst.PipelineDeferred == before.PipelineDeferred + 65 &&
                  burst.PipelineFailed == 0 && burst.ShaderPending == before.ShaderPending,
              "192 PSOs respect the 128-job budget, deduplicate and report deferred draws");
    TestCheck(ctx->FinishBackgroundWorkForTests(30000), "pipeline burst drains");
    const auto drained = ctx->GetFFJitStats();
    RenderAndRead(ctx, clear, NULL, [&]() { DrawPipelineGrid(ctx); }, actual);
    CheckMatchingImage("pipeline-budget-retry", actual, expected);
    const auto retried = ctx->GetFFJitStats();
    TestCheck(retried.PipelineQueued == drained.PipelineQueued + 64 &&
                  retried.PipelineDeferred == drained.PipelineDeferred &&
                  retried.SynchronousRequests == drained.SynchronousRequests,
              "all deferred PSOs retry without rebuilding fallback pipelines");
    TestCheck(ctx->FinishBackgroundWorkForTests(30000), "retried pipelines finish");
    TestCheck(ctx->CountFFJitProgramsForTests().Ready == 1 &&
                  ctx->CountFFJitProgramsForTests().Pipelines == 193,
              "one fragment key owns the seed PSO and all 192 grid PSOs");
    const auto ready = ctx->GetFFJitStats();
    RenderAndRead(ctx, clear, NULL, [&]() { DrawPipelineGrid(ctx); }, actual);
    CheckMatchingImage("pipeline-budget-warm", actual, expected);
    const auto warm = ctx->GetFFJitStats();
    TestCheck(warm.PipelineReady == ready.PipelineReady + 192 &&
                  warm.CompileQueued == ready.CompileQueued && warm.PipelineQueued == ready.PipelineQueued &&
                  warm.SynchronousRequests == ready.SynchronousRequests,
              "every grid draw selects a ready pipeline after retry");
    CheckFFJitStatistics(warm);
    CloseBackend(backend);

    // This manifest contains more than 64 precompiled PSOs. Even before any
    // completion is collected, startup must leave room for a new draw's PSO.
    TestCheck(OpenBackend(backend, kWidth, kHeight), "reopen pipeline pressure context");
    ctx = static_cast<CKSdlGpuRasterizerContext *>(backend.Context);
    const auto loaded = ctx->GetFFJitStats();
    TestCheck(loaded.PipelineQueued == 64 && loaded.PipelinePendingPeak == 64 &&
                  loaded.PipelinePrewarmDeferred != 0,
              "idle prewarming stops at half the outstanding pipeline budget");
    RenderAndRead(ctx, clear, NULL, [&]() { DrawPipelineVariant(ctx, 254); }, actual);
    const auto active = ctx->GetFFJitStats();
    TestCheck(active.PipelineQueued >= 65 && active.PipelinePendingPeak >= 65 &&
                  active.PipelineDeferred == 0,
              "new draw work can queue while the prewarm budget is full");
    TestCheck(ctx->FinishBackgroundWorkForTests(30000), "startup pipeline work drains");
    // Deferred prewarms queue again as collected jobs free the idle budget,
    // so the first grid draws find every manifest PSO ready.
    const auto startup = ctx->GetFFJitStats();
    TestCheck(startup.PipelineQueued > active.PipelineQueued && startup.PipelineBuildFailed == 0,
              "deferred prewarms queue as completions free the idle budget");
    RenderAndRead(ctx, clear, NULL, [&]() { DrawPipelineGrid(ctx); }, actual);
    CheckMatchingImage("pipeline-prewarm-complete", actual, expected);
    const auto first = ctx->GetFFJitStats();
    TestCheck(first.PipelineReady == startup.PipelineReady + 192 && first.PipelineQueued == startup.PipelineQueued &&
                  first.SynchronousRequests == startup.SynchronousRequests,
              "every deferred manifest PSO is prewarmed before its first draw");
    // Draws still recover any PSO a prewarm missed.
    for (int pass = 0; pass < 2; ++pass) {
        RenderAndRead(ctx, clear, NULL, [&]() { DrawPipelineGrid(ctx); }, actual);
        CheckMatchingImage("pipeline-prewarm-retry", actual, expected);
        TestCheck(ctx->FinishBackgroundWorkForTests(30000), "prewarm retry batch drains");
    }
    const auto recovered = ctx->GetFFJitStats();
    RenderAndRead(ctx, clear, NULL, [&]() { DrawPipelineGrid(ctx); }, actual);
    CheckMatchingImage("pipeline-prewarm-warm", actual, expected);
    const auto final = ctx->GetFFJitStats();
    TestCheck(final.PipelineReady == recovered.PipelineReady + 192 &&
                  final.CompileQueued == recovered.CompileQueued && final.PipelineQueued == recovered.PipelineQueued &&
                  final.SynchronousRequests == recovered.SynchronousRequests && final.PipelineBuildFailed == 0,
              "prewarm-deferred variants converge to warm pipelines without failure");
    CheckFFJitStatistics(final);
    CloseBackend(backend);
    RemoveManifestDirectory(directory);
    if (restoreCache) SDL_setenv_unsafe("CKRE_SDL_GPU_FF_JIT_CACHE", previousCache.c_str(), 1);
    else SDL_unsetenv_unsafe("CKRE_SDL_GPU_FF_JIT_CACHE");
    printf("  FF JIT pipelines: 128 outstanding, 64 prewarm, fallback parity and deferred retry passed\n");
}
