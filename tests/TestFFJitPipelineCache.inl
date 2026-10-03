// Included after the pipeline queue pressure helpers.
CKDWORD CreatePipelineCacheTarget(CKRasterizerContext *ctx)
{
    CKTextureDesc desc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
    desc.Format.Width = kWidth;
    desc.Format.Height = kHeight;
    desc.Format.BytesPerLine = kWidth * 4;
    desc.MipMapCount = 1;
    desc.Flags = CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_RENDERTARGET;
    CKDWORD texture = 0;
    TestCheck(ctx->CreateTexture(&desc, &texture) && texture, "create pipeline cache target");
    TestCheck(ctx->SetTargetTexture(texture, 0, 0, CKRST_CUBEFACE_XPOS), "bind pipeline cache target");
    return texture;
}

void CheckPipelineCacheBounds(CKSdlGpuRasterizerContext *ctx)
{
    const auto counts = ctx->CountPipelineCacheForTests();
    TestCheck(counts.Slots <= 1024 && counts.Entries <= counts.Slots && counts.Ready <= counts.Entries &&
                  counts.Pending <= 128 && counts.Failed == 0,
              "global pipeline entries and pending work remain bounded");
    CheckFFJitStatistics(ctx->GetFFJitStats());
}

void CheckFFJitPipelineCache()
{
    const char *jit = GetEnvValue("CKRE_SDL_GPU_FF_JIT");
    const bool enabled = !jit || strcmp(jit, "0") != 0;
    const bool restoreJit = jit != nullptr;
    const std::string previousJit = jit ? jit : "";
    const char *cache = SDL_getenv("CKRE_SDL_GPU_FF_JIT_CACHE");
    const bool restoreCache = cache != nullptr;
    const std::string previousCache = cache ? cache : "";
    SDL_setenv_unsafe("CKRE_SDL_GPU_FF_JIT_CACHE", "0", 1);

    const CKDWORD clear = CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH | CKRST_CTXCLEAR_STENCIL;
    const VxVector invisible[] = {VxVector(3, 3, 0.5f), VxVector(4, 3, 0.5f), VxVector(3.5f, 4, 0.5f)};
    Backend backend;
    Pixels expected, hotExpected, actual;
    SDL_setenv_unsafe("CKRE_SDL_GPU_FF_JIT", "0", 1);
    TestCheck(OpenBackend(backend, kWidth, kHeight), "open resident pipeline reference context");
    CreatePipelineCacheTarget(backend.Context);
    RenderAndRead(backend.Context, clear, NULL, [&]() { DrawPipelineVariant(backend.Context, 14); }, expected);
    RenderAndRead(backend.Context, clear, NULL, [&]() { DrawPipelineVariant(backend.Context, 1199); }, hotExpected);
    CloseBackend(backend);
    if (restoreJit) SDL_setenv_unsafe("CKRE_SDL_GPU_FF_JIT", previousJit.c_str(), 1);
    else SDL_unsetenv_unsafe("CKRE_SDL_GPU_FF_JIT");

    TestCheck(OpenBackend(backend, kWidth, kHeight), "open resident pipeline pressure context");
    auto *ctx = static_cast<CKSdlGpuRasterizerContext *>(backend.Context);
    const CKDWORD target = CreatePipelineCacheTarget(ctx);
    // Use two canonical programs sharing a fallback, so the limit cannot be
    // satisfied merely by bounding each program's own table independently.
    RenderAndRead(ctx, clear, NULL, [&]() {
        DrawPipelineVariant(ctx, 14, invisible, 43);
        DrawPipelineVariant(ctx, 14);
    }, actual);
    TestCheck(ctx->FinishBackgroundWorkForTests(30000), "resident test shaders become ready");

    BeginFrame(ctx, clear);
    DrawPipelineVariant(ctx, 14);
    TestCheck(ctx->FlushPendingCommandsForTests() == CK_OK, "encode the first resident PSO without submitting");
    const auto firstPipeline = ctx->GetLastPipelineForTests();
    TestCheck(!firstPipeline.expired(), "the first encoded PSO is alive");
    for (unsigned id = 100; id < 1200; ++id)
        DrawPipelineVariant(ctx, id, invisible, 42 + (id & 1));
    TestCheck(ctx->FlushPendingCommandsForTests() == CK_OK, "encode PSO churn in the same command buffer");
    const auto full = ctx->CountPipelineCacheForTests();
    TestCheck(full.Slots == 1024 && full.Entries == 1024 && ctx->GetFFJitStats().PipelineEvictions > 0,
              "more than 1024 variants trigger global replacement");
    TestCheck(!firstPipeline.expired(), "an evicted encoded PSO survives until its submission completes");
    CheckPipelineCacheBounds(ctx);
    EndFrame(ctx);
    ReadBackbuffer(ctx, actual);
    CheckMatchingImage("pipeline-cache-retained", actual, expected);
    ctx->CollectForTests();
    TestCheck(firstPipeline.expired(), "the evicted PSO is released after its submission fence completes");
    TestCheck(ctx->FinishBackgroundWorkForTests(30000), "pending entries survive resident eviction and complete");
    CheckPipelineCacheBounds(ctx);
    TestCheck(ctx->CountPipelineCacheForTests().Pending == 0 && ctx->GetFFJitStats().PipelineBuildFailed == 0,
              "completed jobs leave no orphan pending entries after eviction");

    // Make one late PSO hot, then keep using it between batches of new keys.
    RenderAndRead(ctx, clear, NULL, [&]() { DrawPipelineVariant(ctx, 1199); }, actual);
    TestCheck(ctx->FinishBackgroundWorkForTests(30000), "late hot pipeline finishes");
    RenderAndRead(ctx, clear, NULL, [&]() { DrawPipelineVariant(ctx, 1199); }, actual);
    CheckMatchingImage("pipeline-cache-hot", actual, hotExpected);
    for (unsigned first = 1200; first < 1520; first += 32) {
        const auto before = ctx->GetFFJitStats();
        RenderAndRead(ctx, clear, NULL, [&]() {
            for (unsigned id = first; id < first + 32; ++id)
                DrawPipelineVariant(ctx, id, invisible, 42 + (id & 1));
            DrawPipelineVariant(ctx, 1199);
        }, actual);
        const auto after = ctx->GetFFJitStats();
        TestCheck(after.SynchronousRequests == before.SynchronousRequests + 32 &&
                      after.PipelineQueued == before.PipelineQueued + (enabled ? 32 : 0) &&
                      after.PipelineReady == before.PipelineReady + (enabled ? 1 : 0),
                  "new variants replace cold entries without rebuilding the repeatedly used PSO");
        CheckMatchingImage("pipeline-cache-hot-retained", actual, hotExpected);
        TestCheck(ctx->FinishBackgroundWorkForTests(30000), "resident churn batch completes");
        CheckPipelineCacheBounds(ctx);
    }

    // The early PSO was evicted. Its next draw rebuilds the fallback and queues
    // its specialized replacement; another warm draw must create nothing.
    const auto beforeRetry = ctx->GetFFJitStats();
    RenderAndRead(ctx, clear, NULL, [&]() { DrawPipelineVariant(ctx, 14); }, actual);
    CheckMatchingImage("pipeline-cache-recreated", actual, expected);
    const auto retried = ctx->GetFFJitStats();
    TestCheck(retried.SynchronousRequests == beforeRetry.SynchronousRequests + 1 &&
                  retried.PipelineQueued == beforeRetry.PipelineQueued + (enabled ? 1 : 0),
              "an evicted PSO can be rebuilt and used again");
    TestCheck(ctx->FinishBackgroundWorkForTests(30000), "recreated PSO finishes");
    const auto ready = ctx->GetFFJitStats();
    RenderAndRead(ctx, clear, NULL, [&]() { DrawPipelineVariant(ctx, 14); }, actual);
    CheckMatchingImage("pipeline-cache-recreated-warm", actual, expected);
    const auto warm = ctx->GetFFJitStats();
    TestCheck(warm.PipelineQueued == ready.PipelineQueued && warm.SynchronousRequests == ready.SynchronousRequests,
              "recreated PSO is cached for subsequent draws");
    CheckPipelineCacheBounds(ctx);
    TestCheck(ctx->SetTargetTexture(0, 0, 0, CKRST_CUBEFACE_XPOS), "release pipeline cache target");
    TestCheck(ctx->DeleteObject(target, CKRST_OBJ_TEXTURE), "delete pipeline cache target");
    CloseBackend(backend);
    if (restoreCache) SDL_setenv_unsafe("CKRE_SDL_GPU_FF_JIT_CACHE", previousCache.c_str(), 1);
    else SDL_unsetenv_unsafe("CKRE_SDL_GPU_FF_JIT_CACHE");
    printf("  PSO cache: 1024 global slots, fenced eviction, hot retention and recreation passed (JIT %s)\n",
           enabled ? "on" : "off");
}
