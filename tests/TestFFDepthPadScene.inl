// A real depth prepass and POSITIONT comparison panels share a frame. Native
// depth resources are registered here because CK2 exposes only color RTTs.
void CheckDepthPadScene()
{
    constexpr int width = 640, height = 480, size = 160, frames = 6;
    const char *names[] = {"CKRE_SDL_GPU_FF_JIT", "CKRE_SDL_GPU_FF_VERTEX_JIT", "CKRE_SDL_GPU_FF_JIT_CACHE"};
    std::string previous[3];
    bool existed[3];
    for (unsigned i = 0; i < 3; ++i) {
        const char *value = SDL_getenv(names[i]);
        existed[i] = value != nullptr;
        previous[i] = value ? value : "";
    }
    const std::string directory = std::string(SDL_GetBasePath()) + "ffjit-depth-pad-test";
    RemoveManifestDirectory(directory.c_str());
    Pixels reference[frames];
    for (unsigned mode = 0; mode < 4; ++mode) {
        SDL_setenv_unsafe(names[0], mode ? "1" : "0", 1);
        SDL_setenv_unsafe(names[1], mode >= 2 ? "1" : "0", 1);
        SDL_setenv_unsafe(names[2], mode >= 2 ? directory.c_str() : "0", 1);
        Backend backend;
        TestCheck(OpenBackend(backend, width, height), "open depth-pad scene");
        auto *ctx = static_cast<CKSdlGpuRasterizerContext *>(backend.Context);
        CKTextureDesc color;
        VxPixelFormat2ImageDesc(_32_ARGB8888, color.Format);
        color.Format.Width = color.Format.Height = size;
        color.Format.BytesPerLine = size * 4;
        color.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_RENDERTARGET;
        color.MipMapCount = 1;
        CKDWORD colorTexture = 0, depthTexture = 0, target = 0;
        TestCheck(ctx->CreateTexture(&color, &colorTexture), "scene color RTT");
        CKDepthTextureDesc depth;
        depth.Width = depth.Height = size;
        depth.Format = CKRST_DEPTHFMT_D32F;
        TestCheck(ctx->CreateDepthTexture(&depth, &depthTexture) == CK_OK, "scene sampleable depth");
        color.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_DEPTHSTENCIL;
        TestCheck(ctx->RegisterTextureForTests(depthTexture, color), "scene depth registration");
        CKRenderTargetDesc attachment;
        attachment.ColorTexture = colorTexture;
        attachment.DepthTexture = depthTexture;
        TestCheck(ctx->CreateRenderTarget(&attachment, &target) == CK_OK, "scene depth and color attachments");
        const auto render = [&](unsigned frame, Pixels &image) {
            SetDiffuseState(ctx);
            ctx->SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, 0);
            BeginFrame(ctx, CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH);
            CKRenderPassDesc pass;
            pass.RenderTarget = target;
            pass.Rect.right = pass.Rect.bottom = size;
            pass.ClearFlags = CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH;
            pass.ClearColor = 0xff1b2940u;
            pass.ClearZ = 1.0f;
            TestCheck(ctx->BeginPass(&pass) == CK_OK, "3D scene depth prepass");
            ctx->GetFFPipelineForTests()->SetTargetExtents(size, size, size, size);
            CKViewportData viewport = {};
            viewport.ViewWidth = viewport.ViewHeight = size;
            viewport.ViewZMax = 1;
            TestCheck(ctx->SetViewport(&viewport), "depth prepass viewport");
            VxMatrix projection;
            projection.Perspective(1.1f, 1.0f, 0.5f, 12.0f);
            ctx->SetTransformMatrix(VXMATRIX_PROJECTION, projection);
            ctx->SetRenderState(VXRENDERSTATE_ZENABLE, TRUE);
            ctx->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, TRUE);
            ctx->SetRenderState(VXRENDERSTATE_ZFUNC, VXCMP_LESSEQUAL);
            const VxVector corners[8] = {
                {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},
                {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1}};
            const unsigned indices[36] = {0,1,2,0,2,3,4,6,5,4,7,6,0,4,5,0,5,1,
                                          3,2,6,3,6,7,0,3,7,0,7,4,1,5,6,1,6,2};
            // Three overlapping cubes, perspective projection, per-face colors,
            // and a rotating world transform populate a nontrivial depth map.
            for (unsigned object = 0; object < 3; ++object) {
                const float angle = 0.31f * frame + 0.7f * object;
                VxMatrix world;
                Vx3DMatrixIdentity(world);
                const float scale = object == 0 ? 0.7f : 0.48f;
                world[0][0] = world[2][2] = std::cos(angle) * scale;
                world[0][2] = std::sin(angle) * scale;
                world[2][0] = -world[0][2];
                world[1][1] = scale;
                world[3][0] = (float(object) - 1.0f) * 0.85f;
                world[3][1] = object == 1 ? 0.55f : -0.4f;
                world[3][2] = 3.6f + 0.6f * object;
                ctx->SetTransformMatrix(VXMATRIX_WORLD, world);
                for (unsigned face = 0; face < 12; ++face) {
                    VxVector positions[3];
                    for (unsigned i = 0; i < 3; ++i) positions[i] = corners[indices[face * 3 + i]];
                    const CKDWORD palette[] = {0xffe59940u, 0xff55cba8u, 0xff739fefu};
                    const CKDWORD shade = (face / 2) % 2 ? 0x00202020u : 0;
                    const CKDWORD colors[3] = {palette[object] - shade, palette[object] - shade, palette[object] - shade};
                    TestCheck(DrawColorTriangle(ctx, positions, colors), "3D cube depth and color");
                }
            }
            // Clear returns to the normal backbuffer after the native prepass.
            ctx->GetFFPipelineForTests()->SetTargetExtents(width, height, width, height);
            viewport.ViewWidth = width; viewport.ViewHeight = height;
            TestCheck(ctx->SetViewport(&viewport), "scene display viewport");
            TestCheck(ctx->Clear(CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH, 0xff101820u, 1, 0, 0, nullptr),
                      "scene display clear");
            SetDiffuseState(ctx);
            for (unsigned panel = 0; panel < 6; ++panel) {
                const bool compare = panel != 0;
                const unsigned axes = panel % 4; // includes an unpadded comparison next to padded draws
                const bool clipping = compare && (frame & 1);
                VxPlane plane; plane.m_Normal = VxVector(1, 0, 0);
                const float left = 12.0f + float(panel % 3) * 212.0f;
                const float top = 14.0f + float(panel / 3) * 232.0f;
                plane.m_D = -(left + 18.0f + 3.0f * frame);
                TestCheck(ctx->SetUserClipPlane(5, plane), "panel screen clip plane");
                ctx->SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, clipping ? 32 : 0);
                ctx->SetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE, panel != 5);
                TestCheck(ctx->SetTexture(compare ? depthTexture : colorTexture, 0), "panel RTT binding");
                ctx->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
                ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
                ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_LINEAR);
                ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_LINEAR);
                ctx->SetTextureStageState(0, CKRST_TSS_ADDRESSU, axes & 1 ? VXTEXTURE_ADDRESSBORDER : VXTEXTURE_ADDRESSCLAMP);
                ctx->SetTextureStageState(0, CKRST_TSS_ADDRESSV, axes & 2 ? VXTEXTURE_ADDRESSBORDER : VXTEXTURE_ADDRESSCLAMP);
                ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, frame % 3 == 0 ? 0xffff0000u : 0);
                ctx->SetTextureStageState(0, CKRST_TSS_COMPAREFUNC, compare ? CKRST_COMPARE_LEQUAL : CKRST_COMPARE_NONE);
                ctx->SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS,
                    panel == 3 ? CKRST_TTF_COUNT4 | CKRST_TTF_PROJECTED : CKRST_TTF_COUNT3);
                ctx->GetFFPipelineForTests()->SetTexcoordComponentCount(0, 4);
                // Public POSITIONT texture matrices must remain ignored; the
                // backend replaces this with the draw-local padding transform.
                VxMatrix ignored; Vx3DMatrixIdentity(ignored);
                ignored[0][0] = 3; ignored[3][0] = -2; ignored[3][2] = 4;
                ctx->SetTransformMatrix(VXMATRIX_TEXTURE0, ignored);
                const float xy[4][2] = {{left,top},{left+192,top},{left+192,top+204},{left,top+204}};
                const float uv[4][2] = {{-0.12f,-0.12f},{1.12f,-0.12f},{1.12f,1.12f},{-0.12f,1.12f}};
                const unsigned triangles[6] = {0,1,2,0,2,3};
                for (unsigned tri = 0; tri < 2; ++tri) {
                    float positions[3][4], coords[3][4];
                    for (unsigned i = 0; i < 3; ++i) {
                        const unsigned corner = triangles[tri * 3 + i];
                        positions[i][0] = xy[corner][0]; positions[i][1] = xy[corner][1];
                        positions[i][2] = 0.5f; positions[i][3] = panel == 5 ? 0.7f + 0.1f * corner : 1;
                        coords[i][0] = uv[corner][0]; coords[i][1] = uv[corner][1];
                        coords[i][2] = 0.88f + 0.01f * float((panel + frame) % 5);
                        coords[i][3] = 1;
                    }
                    TestCheck(DrawTexturedPositionTTriangle(ctx, positions, kWhite, coords), "depth comparison panel");
                }
            }
            ctx->SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, 0);
            EndFrame(ctx);
            ReadBackbuffer(ctx, image);
        };
        if (mode == 3) {
            TestCheck(ctx->FinishBackgroundWorkForTests(30000), "depth-pad manifest prewarm finishes");
            TestCheck(ctx->GetFFJitStats().CompileCompleted > 0, "depth-pad manifest was loaded");
        }
        const auto prewarmed = ctx->GetFFJitStats();
        for (unsigned frame = 0; frame < frames; ++frame) {
            Pixels image;
            render(frame, image);
            if (!mode) {
                reference[frame] = image;
                unsigned colored = 0, rejected = 0, accepted = 0;
                for (int y = 14; y < 218; ++y) for (int x = 12; x < 204; ++x) {
                    CKBYTE pixel[4]; GetPixel(image, x, y, pixel);
                    if (pixel[0] > 65 || pixel[1] > 65 || pixel[2] > 65) ++colored;
                }
                for (int y = 14; y < 218; ++y) for (int x = 245; x < 396; ++x) {
                    rejected += PixelNear(image, x, y, 0, 0, 0);
                    accepted += PixelNear(image, x, y, 255, 255, 255);
                }
                TestCheck(colored > 4000 && rejected > 1500 && accepted > 1500,
                          "scene has visible 3D geometry and both accepted/rejected depth regions");
            }
            else {
                char name[96];
                snprintf(name, sizeof(name), "depth-pad-mode%u-frame%u-cold", mode, frame);
                CheckMatchingImage(name, image, reference[frame]);
                TestCheck(ctx->FinishBackgroundWorkForTests(30000), "depth-pad scene jobs finish");
                const auto before = ctx->GetFFJitStats();
                render(frame, image);
                snprintf(name, sizeof(name), "depth-pad-mode%u-frame%u-warm", mode, frame);
                CheckMatchingImage(name, image, reference[frame]);
                const auto after = ctx->GetFFJitStats();
                TestCheck(after.CompileQueued == before.CompileQueued && after.PipelineQueued == before.PipelineQueued,
                          "warm depth-pad scene reuses shaders and pipelines");
                TestCheck(mode >= 2 ? after.DepthPadReady == before.DepthPadReady + 8 : after.DepthPadReady == 0,
                          "depth-pad scene executes generated vertices only when enabled");
            }
        }
        const auto stats = ctx->GetFFJitStats();
        TestCheck(!stats.CompileFailed && !stats.VertexCompileFailed && !stats.PipelineBuildFailed,
                  "depth-pad scene has no shader or pipeline failures");
        if (mode >= 2) TestCheck(stats.ClipReady > 0 && stats.UnlitReady > 0, "scene executes clipped panels and 3D JIT");
        if (mode == 3) TestCheck(stats.CompileQueued == prewarmed.CompileQueued && stats.PipelineQueued == prewarmed.PipelineQueued,
                                "prewarmed depth-pad scene draws queue no compilation");
        TestCheck(ctx->SetTexture(0, 0), "unbind scene depth");
        TestCheck(ctx->DestroyObject(target, CKRST_OBJ_RENDERTARGET) == CK_OK, "destroy scene RTT");
        TestCheck(ctx->DeleteObject(depthTexture, CKRST_OBJ_TEXTURE), "destroy scene depth");
        TestCheck(ctx->DeleteObject(colorTexture, CKRST_OBJ_TEXTURE), "destroy scene color");
        CloseBackend(backend);
    }
    RemoveManifestDirectory(directory.c_str());
    for (unsigned i = 0; i < 3; ++i) {
        if (existed[i]) SDL_setenv_unsafe(names[i], previous[i].c_str(), 1);
        else SDL_unsetenv_unsafe(names[i]);
    }
    printf("  depth-pad scene: six animated frames, off/fragment/on/prewarmed, full-image comparisons passed\n");
}
