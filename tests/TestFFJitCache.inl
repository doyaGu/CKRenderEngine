// Included by the SDL GPU pixel suite after the replay helpers.
void CheckFFJitUsage()
{
    CKSdlGpuFFJitUsage usage;
    usage.Touch(1);
    TestCheck(usage.Score(1) == 1 && usage.Score(256) == 1 && usage.Score(257) == 0,
              "one-off frequency expires after 256 other requests");
    for (uint64_t i = 2; i < 1000; ++i) usage.Touch(i);
    TestCheck(usage.Hits == 255 && usage.Score(1255) == 127 && usage.Score(3047) == 0,
              "hot frequency saturates and old working sets eventually expire");
    usage.Touch(4000);
    TestCheck(usage.Hits == 1 && usage.Last == 4000, "an expired key starts cold again");
}

void SetCacheProgram(CKRasterizerContext *ctx, unsigned id)
{
    const CKDWORD ops[] = {CKRST_TOP_ADD, CKRST_TOP_SUBTRACT, CKRST_TOP_MODULATE,
        CKRST_TOP_MODULATE2X, CKRST_TOP_MODULATE4X, CKRST_TOP_ADDSIGNED, CKRST_TOP_ADDSIGNED2X, CKRST_TOP_LERP};
    SetDiffuseState(ctx);
    ctx->SetRenderState(VXRENDERSTATE_TEXTUREFACTOR, 0xff906050u);
    for (int stage = 0; stage < 4; ++stage) {
        ctx->SetTextureStageState(stage, CKRST_TSS_OP, ops[(id >> (stage * 3)) & 7]);
        ctx->SetTextureStageState(stage, CKRST_TSS_ARG1, stage ? CKRST_TA_CURRENT : CKRST_TA_DIFFUSE);
        ctx->SetTextureStageState(stage, CKRST_TSS_ARG2, CKRST_TA_CONSTANT);
        ctx->SetTextureStageState(stage, CKRST_TSS_COLORARG0, CKRST_TA_TFACTOR);
        ctx->SetTextureStageState(stage, CKRST_TSS_CONSTANT, 0xff204030u);
        ctx->SetTextureStageState(stage, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
        ctx->SetTextureStageState(stage, CKRST_TSS_AARG1, CKRST_TA_CURRENT);
    }
}

void DrawCacheProgram(CKRasterizerContext *ctx, unsigned id, const VxVector *positions = kCenterTriangle)
{
    SetCacheProgram(ctx, id);
    const CKDWORD colors[3] = {0xff406080u, 0xff808040u, 0xff604080u};
    TestCheck(DrawColorTriangle(ctx, positions, colors), "cache stress draw");
}

void DrawCacheGrid(CKRasterizerContext *ctx)
{
    for (unsigned id = 0; id < 256; ++id) {
        const float x = -0.98f + (id % 16) * 0.108f, y = -0.98f + (id / 16) * 0.122f;
        const VxVector triangle[] = {VxVector(x, y, 0.5f), VxVector(x + 0.1f, y, 0.5f),
                                     VxVector(x + 0.05f, y + 0.114f, 0.5f)};
        DrawCacheProgram(ctx, id, triangle);
    }
}

void CheckFFJitCachePressure()
{
    const char *setting = GetEnvValue("CKRE_SDL_GPU_FF_JIT");
    if (setting && strcmp(setting, "0") == 0) return;
    char directory[1024];
    snprintf(directory, sizeof(directory), "%sffjit-capacity-test", SDL_GetBasePath());
    RemoveManifestDirectory(directory);
    const char *cache = SDL_getenv("CKRE_SDL_GPU_FF_JIT_CACHE");
    const bool restore = cache != nullptr;
    const std::string previous = cache ? cache : "";
    SDL_setenv_unsafe("CKRE_SDL_GPU_FF_JIT_CACHE", directory, 1);
    Backend backend;
    TestCheck(OpenBackend(backend, kWidth, kHeight), "open cache stress context");
    auto *ctx = static_cast<CKSdlGpuRasterizerContext *>(backend.Context);

    // No frame boundary collects completions inside this burst. Even a worker
    // that finishes instantly cannot exceed the outstanding compilation budget.
    BeginFrame(ctx, CKRST_CTXCLEAR_COLOR);
    for (unsigned id = 0; id < 96; ++id) DrawCacheProgram(ctx, id);
    const auto burst = ctx->GetFFJitStats();
    TestCheck(burst.CompilePendingPeak == 64 && burst.QueueDeferred != 0,
              "a burst is limited to 64 outstanding compilations");
    EndFrame(ctx);
    TestCheck(ctx->FinishBackgroundWorkForTests(30000), "burst work finishes");

    // Revisit deferred keys and fill the resident cache in bounded batches.
    for (unsigned first = 0; first < 256; first += 16) {
        BeginFrame(ctx, CKRST_CTXCLEAR_COLOR);
        for (unsigned id = first; id < first + 16; ++id) DrawCacheProgram(ctx, id);
        EndFrame(ctx);
        TestCheck(ctx->FinishBackgroundWorkForTests(30000), "cache fill work finishes");
    }
    auto counts = ctx->CountFFJitProgramsForTests();
    TestCheck(counts.Ready == 256 && counts.Queued == 0 && counts.Rejected == 0,
              "all 256 distinct keys, including queue-deferred ones, become ready");

    // A frequently used resident must survive cold-key replacement.
    BeginFrame(ctx, CKRST_CTXCLEAR_COLOR);
    for (int i = 0; i < 128; ++i) DrawCacheProgram(ctx, 0);
    EndFrame(ctx);
    Pixels hotResident;
    ReadBackbuffer(ctx, hotResident);

    const VxVector lateTriangle[] = {VxVector(0.8f, -0.5f, 0.5f), VxVector(0.99f, -0.5f, 0.5f),
                                     VxVector(0.9f, 0.5f, 0.5f)};
    const unsigned late = 300;
    const auto full = ctx->GetFFJitStats();
    BeginFrame(ctx, CKRST_CTXCLEAR_COLOR);
    DrawCacheGrid(ctx);
    DrawCacheProgram(ctx, late, lateTriangle);
    EndFrame(ctx);
    Pixels expected;
    ReadBackbuffer(ctx, expected);
    TestCheck(ctx->GetFFJitStats().CompileQueued == full.CompileQueued && ctx->GetFFJitStats().Capacity > full.Capacity,
              "a full cache does not compile a one-off key");

    // Every old program is retained by an unsubmitted draw when a late hot key
    // evicts one. The old draw still renders its own cell with its old pipeline.
    BeginFrame(ctx, CKRST_CTXCLEAR_COLOR);
    DrawCacheGrid(ctx);
    for (int i = 0; i < 4; ++i) DrawCacheProgram(ctx, late, lateTriangle);
    TestCheck(ctx->GetFFJitStats().Evictions == 1 && ctx->GetFFJitStats().CompileQueued == full.CompileQueued + 1,
              "a late repeated key replaces one cold resident before submission");
    EndFrame(ctx);
    Pixels actual;
    ReadBackbuffer(ctx, actual);
    CheckMatchingImage("cache-retained-draws", actual, expected);
    TestCheck(ctx->FinishBackgroundWorkForTests(30000), "replacement work finishes");

    // The frequently used original program must remain resident.
    const auto ready = ctx->GetFFJitStats();
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() { DrawCacheProgram(ctx, 0); }, actual);
    CheckMatchingImage("cache-hot-resident", actual, hotResident);
    TestCheck(ctx->GetFFJitStats().CompileQueued == ready.CompileQueued,
              "the frequently used original program remains resident");

    // Revisit every old draw key, including the evicted one. Its stale alias
    // must not select the late program now occupying that slot.
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        DrawCacheGrid(ctx);
        DrawCacheProgram(ctx, late, lateTriangle);
    }, actual);
    CheckMatchingImage("cache-revisited-keys", actual, expected);
    TestCheck(ctx->GetFFJitStats().CompileQueued == ready.CompileQueued,
              "revisiting the evicted key once keeps its fallback");

    // More one-off keys than the metadata budget must recycle candidate
    // history without admitting shaders or forgetting resident programs.
    const auto beforeFlood = ctx->GetFFJitStats();
    BeginFrame(ctx, CKRST_CTXCLEAR_COLOR);
    for (unsigned id = 512; id < 1792; ++id) DrawCacheProgram(ctx, id);
    DrawCacheProgram(ctx, 512); // Its earlier candidate record has expired.
    EndFrame(ctx);
    const auto afterFlood = ctx->GetFFJitStats();
    TestCheck(ctx->CountFFJitProgramsForTests().Candidates == 1024 &&
                  afterFlood.Evictions == beforeFlood.Evictions &&
                  afterFlood.CompileQueued == beforeFlood.CompileQueued &&
                  afterFlood.Capacity == beforeFlood.Capacity + 1281,
              "candidate FIFO wraps at 1024 and expired one-offs remain cold");
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() { DrawCacheProgram(ctx, 512); }, actual);
    TestCheck(ctx->GetFFJitStats().Evictions == afterFlood.Evictions + 1 &&
                  ctx->GetFFJitStats().CompileQueued == afterFlood.CompileQueued + 1,
              "a candidate can become hot and enter after its metadata was recycled");
    TestCheck(ctx->FinishBackgroundWorkForTests(30000), "admission after metadata wrap finishes");

    const auto beforeHotLoop = ctx->GetFFJitStats();
    BeginFrame(ctx, CKRST_CTXCLEAR_COLOR);
    for (int i = 0; i < 255; ++i) DrawCacheProgram(ctx, late, lateTriangle);
    FFReplayImage hottest;
    const CKFFDraw &draw = ctx->GetFFPipelineForTests()->GetDraw();
    hottest.Key = CKFFNativeFragmentDrawKey(draw.ProgramContext->FragmentProgram, *draw.Constants, false, 0);
    CKFFCanonicalizeNativeFragmentKey(hottest.Key, hottest.Layout);
    EndFrame(ctx);
    const auto warm = ctx->GetFFJitStats();
    TestCheck(warm.PipelineReady == beforeHotLoop.PipelineReady + 255 && warm.CompileQueued == beforeHotLoop.CompileQueued &&
                  warm.PipelineQueued == beforeHotLoop.PipelineQueued && warm.SynchronousRequests == beforeHotLoop.SynchronousRequests,
              "the late hot program draws through its cached JIT pipeline");
    CheckFFJitStatistics(warm);
    counts = ctx->CountFFJitProgramsForTests();
    TestCheck(counts.Ready == 256 && counts.Candidates <= 1024 && counts.DrawKeys <= 1024 &&
                  warm.CompilePendingPeak <= 64 && warm.CompileFailed == 0 && warm.PipelineBuildFailed == 0,
              "resident keys, admission metadata and compile work stay bounded");
    CloseBackend(backend);

    // The manifest must put the frequently used late program before older
    // programs, and its limited startup batch must actually prewarm that key.
    int fileCount = 0;
    char **files = SDL_GlobDirectory(directory, "ffjit-*.bin", 0, &fileCount);
    TestCheck(fileCount == 1 && files, "capacity test saves one manifest");
    unsigned long long identity = 0;
    TestCheck(sscanf(files[0], "ffjit-%llx.bin", &identity) == 1, "manifest names its identity");
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s", directory, files[0]);
    SDL_free(files);
    CKSdlGpuFFJitManifest manifest;
    TestCheck(CKSdlGpuLoadFFJitManifest(path, identity, manifest) && manifest.Programs.Size() == 256,
              "all resident keys are saved");
    TestCheck(memcmp(manifest.Programs[0].Lanes, hottest.Key.Program.Lanes(), sizeof(manifest.Programs[0].Lanes)) == 0 &&
                  memcmp(manifest.Programs[0].Switches, hottest.Key.Switches, sizeof(hottest.Key.Switches)) == 0,
              "manifest priority follows frequency rather than first encounter");
    TestCheck(OpenBackend(backend, kWidth, kHeight), "reopen cache stress context");
    ctx = static_cast<CKSdlGpuRasterizerContext *>(backend.Context);
    TestCheck(ctx->FinishBackgroundWorkForTests(30000), "bounded prewarming finishes");
    TestCheck(ctx->CountFFJitProgramsForTests().Ready == 32, "prewarming reserves half the compile budget for new work");
    const auto prewarmed = ctx->GetFFJitStats();
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() { DrawCacheProgram(ctx, late, lateTriangle); }, actual);
    const auto resumed = ctx->GetFFJitStats();
    TestCheck(resumed.PipelineReady == prewarmed.PipelineReady + 1 && resumed.CompileQueued == prewarmed.CompileQueued &&
                  resumed.PipelineQueued == prewarmed.PipelineQueued && resumed.SynchronousRequests == prewarmed.SynchronousRequests,
              "the late hot key is fully prewarmed after restart");
    CloseBackend(backend);
    RemoveManifestDirectory(directory);
    if (restore) SDL_setenv_unsafe("CKRE_SDL_GPU_FF_JIT_CACHE", previous.c_str(), 1);
    else SDL_unsetenv_unsafe("CKRE_SDL_GPU_FF_JIT_CACHE");
    printf("  FF JIT cache: 256 residents, bounded queue, late hot admission, retained draws and frequency-ranked prewarm passed\n");
}
