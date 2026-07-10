#include "CKBgfxRasterizer.h"
#include "CKBgfxInternal.h"
#include "CKFFSamplerLayout.h"
#include "CKFFShaderCache.h"
#include "CKFFSpecializedModuleTable.h"
#include "CKFixedFunctionPipeline.h"
#include "CKRenderSettings.h"
#include "TestTriangleMultiset.h"

#include "shaders/generated/CKFFSpecializedModuleTable.generated.h"

#include <SDL3/SDL.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace {

char g_BackendRuntimeFailure[512];

const CKDWORD kPixelColorTexture = 9000;
const CKDWORD kPixelDepthTexture = 9001;
const CKDWORD kPixelFrameBuffer = 9002;
const CKDWORD kPixelReadbackTexture = 9003;

void TestCheckf(bool condition, const char *format, ...)
{
    if (condition)
        return;

    va_list args;
    va_start(args, format);
    vsnprintf(g_BackendRuntimeFailure, sizeof(g_BackendRuntimeFailure), format, args);
    va_end(args);
    TestFail(g_BackendRuntimeFailure);
}

bool EnvFlagEnabled(const char *name)
{
    const char *value = getenv(name);
    return value &&
           (strcmp(value, "1") == 0 ||
            strcmp(value, "true") == 0 ||
            strcmp(value, "TRUE") == 0 ||
            strcmp(value, "on") == 0 ||
            strcmp(value, "ON") == 0);
}

void SetEnvValue(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

const char *GetEnvValue(const char *name)
{
    const char *value = getenv(name);
    return value && value[0] != '\0' ? value : NULL;
}

void SetSelectStage(CKFFShaderKeyFSStage &stage,
                    CKDWORD arg,
                    bool hasTexture,
                    CKDWORD samplerType)
{
    stage = CKFFShaderKeyFSStage();
    stage.ColorOp = CKRST_TOP_SELECTARG1;
    stage.ColorArg1 = arg;
    stage.AlphaOp = CKRST_TOP_SELECTARG1;
    stage.AlphaArg1 = arg;
    stage.HasTexture = hasTexture;
    stage.SamplerType = samplerType;
}

const CKFFSpecializedModuleEntry *FirstGeneratedEntry(CK_SHADER_PROFILE profile)
{
    for (size_t i = 0; i < g_CKFFSpecializedModuleCount; ++i) {
        if (g_CKFFSpecializedModules[i].Profile == profile)
            return &g_CKFFSpecializedModules[i];
    }
    return NULL;
}

CKFFShaderKey MakeRuntimeKeySkeleton(CK_SHADER_PROFILE profile)
{
    const CKFFSpecializedModuleEntry *entry = FirstGeneratedEntry(profile);
    TestCheckf(entry != NULL,
               "backend runtime test requires at least one generated %s full-specialized key",
               CKBgfxShaderProfileName(profile));

    CKFFShaderKey key;
    key.VS = entry->Key.VS;
    key.FS = CKFFShaderKeyFS();
    return key;
}

CKFFShaderKey MakeStageFourFallbackKey(CK_SHADER_PROFILE profile, CKDWORD samplerType)
{
    CKFFShaderKey key = MakeRuntimeKeySkeleton(profile);
    for (CKDWORD stage = 0; stage < 4; ++stage)
        SetSelectStage(key.FS.Stages[stage], CKRST_TA_CURRENT, false, CKFF_SAMPLER_2D);
    SetSelectStage(key.FS.Stages[4], CKRST_TA_TEXTURE, true, samplerType);
    key.FS.LastActiveTextureStage = 4;
    return key;
}

CKFFShaderKey MakeVolumeCubeStaticLayoutKey(CK_SHADER_PROFILE profile)
{
    CKFFShaderKey key = MakeRuntimeKeySkeleton(profile);
    SetSelectStage(key.FS.Stages[0], CKRST_TA_TEXTURE, true, CKFF_SAMPLER_VOLUME);
    SetSelectStage(key.FS.Stages[1], CKRST_TA_TEXTURE, true, CKFF_SAMPLER_CUBE);
    key.FS.LastActiveTextureStage = 1;
    return key;
}

void RunShaderProgramCase(CKBgfxRasterizerContext *context,
                          const CKFFShaderKey &key,
                          bool uberShader,
                          bool expectedFullSpecialized,
                          const char *caseName)
{
    CKRenderSettingsClearOverridesForTests();
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::FFP,
                                        "UberShader",
                                        uberShader ? "1" : "0");

    const CKDWORD fatalBefore = context->GetFatalCountForTests();

    CKFFShaderCache cache;
    cache.Init(context);
    const CKFFProgramBinding binding = cache.GetProgram(key);
    TestCheckf(binding.Program != 0,
               "%s must create a real backend FFP shader program", caseName);
    TestCheckf(binding.FullSpecialized == expectedFullSpecialized,
               "%s selected route mismatch fullSpecialized=%u expected=%u",
               caseName,
               binding.FullSpecialized ? 1u : 0u,
               expectedFullSpecialized ? 1u : 0u);

    TestCheckf(context->Frame(CKRST_FRAME_SYNC_IMMEDIATE) == CK_OK,
               "%s must process bgfx frame after program creation", caseName);
    TestCheckf(context->GetFatalCountForTests() == fatalBefore,
               "%s triggered a bgfx fatal callback during shader creation/link",
               caseName);

    cache.Shutdown();
    context->Frame(CKRST_FRAME_SYNC_IMMEDIATE);
    CKRenderSettingsClearOverridesForTests();
}

CKBOOL DrawColorTriangle(CKFixedFunctionPipeline &ffp,
                         CKRasterizerEncoder *encoder,
                         const VxVector positions[3],
                         const CKDWORD colors[3])
{
    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TR_VC;
    data.PositionPtr = const_cast<VxVector *>(positions);
    data.PositionStride = sizeof(VxVector);
    data.ColorPtr = const_cast<CKDWORD *>(colors);
    data.ColorStride = sizeof(CKDWORD);
    return ffp.DrawPrimitive(encoder, CKRP_VIEW_OPAQUE3D,
                             VX_TRIANGLELIST, NULL, 0, &data);
}

bool PixelNear(const std::vector<CKBYTE> &pixels,
               int x, int y, int r, int g, int b, int tolerance = 24)
{
    const size_t offset = ((size_t)y * 64u + (size_t)x) * 4u;
    if (offset + 3 >= pixels.size())
        return false;
    const int db = abs((int)pixels[offset + 0] - b);
    const int dg = abs((int)pixels[offset + 1] - g);
    const int dr = abs((int)pixels[offset + 2] - r);
    return dr <= tolerance && dg <= tolerance && db <= tolerance;
}

void CreatePixelFrameBuffer(CKBgfxRasterizerContext *context)
{
    CKTextureDesc colorDesc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, colorDesc.Format);
    colorDesc.Format.Width = 64;
    colorDesc.Format.Height = 64;
    colorDesc.MipMapCount = 1;
    colorDesc.Depth = 1;
    colorDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB |
                      CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_RENDERTARGET;
    TestCheck(context->CreateTexture(kPixelColorTexture, &colorDesc, NULL) == CK_OK,
              "Backend pixel gate must create a color render target");

    CKTextureDesc readbackDesc = colorDesc;
    readbackDesc.MipMapCount = 2;
    readbackDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_BLIT_DST |
                         CKRST_TEXTURE_READBACK;
    TestCheck(context->CreateTexture(kPixelReadbackTexture, &readbackDesc, NULL) == CK_OK,
              "Backend pixel gate must create a blit readback texture");

    CKDepthTextureDesc depthDesc = {};
    depthDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_DEPTHSTENCIL;
    depthDesc.Width = 64;
    depthDesc.Height = 64;
    depthDesc.MipMapCount = 1;
    depthDesc.DepthFormat = CKRST_DEPTHFMT_D24S8;
    CKERROR depthError = context->CreateDepthTexture(kPixelDepthTexture, &depthDesc);
    if (depthError != CK_OK) {
        depthDesc.DepthFormat = CKRST_DEPTHFMT_D24;
        depthError = context->CreateDepthTexture(kPixelDepthTexture, &depthDesc);
    }
    if (depthError != CK_OK) {
        depthDesc.DepthFormat = CKRST_DEPTHFMT_D16;
        depthError = context->CreateDepthTexture(kPixelDepthTexture, &depthDesc);
    }
    TestCheck(depthError == CK_OK,
              "Backend pixel gate must create a depth render target");

    CKFrameBufferAttachmentDesc colorAttachment;
    colorAttachment.Texture = kPixelColorTexture;
    colorAttachment.Mip = 0;
    colorAttachment.Layer = 0;
    CKFrameBufferDesc frameBufferDesc;
    frameBufferDesc.Color = &colorAttachment;
    frameBufferDesc.ColorCount = 1;
    frameBufferDesc.DepthStencil.Texture = kPixelDepthTexture;
    frameBufferDesc.DepthStencil.Mip = 0;
    frameBufferDesc.DepthStencil.Layer = 0;
    TestCheck(context->CreateFrameBuffer(kPixelFrameBuffer, &frameBufferDesc) == CK_OK,
              "Backend pixel gate must create an offscreen framebuffer");
}

void DestroyPixelFrameBuffer(CKBgfxRasterizerContext *context)
{
    context->DeleteObject(kPixelFrameBuffer, CKRST_OBJ_FRAMEBUFFER);
    context->DeleteObject(kPixelDepthTexture, CKRST_OBJ_TEXTURE);
    context->DeleteObject(kPixelColorTexture, CKRST_OBJ_TEXTURE);
    context->DeleteObject(kPixelReadbackTexture, CKRST_OBJ_TEXTURE);
}

void BeginPixelFrame(CKFixedFunctionPipeline &ffp,
                     CKBgfxRasterizerContext *context)
{
    CKRECT viewport = {0, 0, 64, 64};
    VxMatrix identity;
    Vx3DMatrixIdentity(identity);
    ffp.GetRenderPipeline().BeginFrame(
        viewport, CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH,
        0xFF000000u, 1.0f, identity, identity);
    context->SetViewFrameBuffer(CKRP_VIEW_CLEAR, kPixelFrameBuffer);
    context->SetViewFrameBuffer(CKRP_VIEW_OPAQUE3D, kPixelFrameBuffer);
    TestCheck(ffp.GetRenderPipeline().GetEncoder() != NULL,
              "Backend pixel gate must acquire a rasterizer encoder");
}

void EndPixelFrameAndRead(CKFixedFunctionPipeline &ffp,
                          CKBgfxRasterizerContext *context,
                          std::vector<CKBYTE> &pixels)
{
    CKRasterizerEncoder *encoder = ffp.GetRenderPipeline().GetEncoder();
    TestCheck(encoder != NULL,
              "Backend pixel gate must retain its encoder through readback blit");
    if (encoder) {
        CKRECT source = {0, 0, 64, 64};
        encoder->Blit(CKRP_VIEW_FOREGROUND2D,
                      kPixelReadbackTexture, 0, 0, 0,
                      kPixelColorTexture, 0, &source);
        CKRECT mipSource = {0, 0, 32, 32};
        encoder->Blit(CKRP_VIEW_FOREGROUND2D,
                      kPixelReadbackTexture, 1, 0, 0,
                      kPixelColorTexture, 0, &mipSource);
    }
    ffp.GetRenderPipeline().EndFrame(CKRST_FRAME_SYNC_IMMEDIATE);

    const int readbackPitch = 64 * 4 + 16;
    std::vector<CKBYTE> paddedPixels(readbackPitch * 64, 0);
    VxImageDescEx image;
    VxPixelFormat2ImageDesc(_32_ARGB8888, image);
    image.Width = 64;
    image.Height = 64;
    image.BytesPerLine = readbackPitch;
    image.Image = &paddedPixels[0];
    TestCheck(context->ReadTexture(kPixelReadbackTexture, 0, &image) == CK_OK,
              "Backend pixel gate must read the blit destination texture");
    pixels.assign(64 * 64 * 4, 0);
    for (int y = 0; y < 64; ++y) {
        memcpy(&pixels[y * 64 * 4], &paddedPixels[y * readbackPitch], 64 * 4);
    }

    const size_t mipBytes = 32u * 32u * 4u;
    const size_t guardBytes = 64u;
    std::vector<CKBYTE> mipPixels(mipBytes + guardBytes, 0xCD);
    VxImageDescEx mipImage;
    VxPixelFormat2ImageDesc(_32_ARGB8888, mipImage);
    mipImage.Width = 32;
    mipImage.Height = 32;
    mipImage.BytesPerLine = 32 * 4;
    mipImage.Image = &mipPixels[0];
    TestCheck(context->ReadTexture(kPixelReadbackTexture, 1, &mipImage) == CK_OK,
              "Backend pixel gate must read the requested mip dimensions");
    bool guardIntact = true;
    for (size_t i = mipBytes; i < mipPixels.size(); ++i)
        guardIntact = guardIntact && mipPixels[i] == 0xCD;
    TestCheck(guardIntact,
              "Mip readback orientation normalization must not use base-level dimensions");
}

void BackendRuntimeMatchesFFPPixelSemantics(CKBgfxRasterizerContext *context)
{
    CKRenderSettingsClearOverridesForTests();
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::FFP,
                                        "UberShader", "1");

    CKFixedFunctionPipeline ffp;
    ffp.Init(context);
    CreatePixelFrameBuffer(context);
    ffp.GetRenderPipeline().SetExternalRenderTarget(TRUE);
    ffp.SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_COLORVERTEX, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_DIFFUSEFROMVERTEX, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
    ffp.SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, FALSE);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_DIFFUSE);
    ffp.DisableTextureStagesFrom(1);

    std::vector<CKBYTE> pixels;

    // D3D8 flat shading uses the first vertex of a triangle as the provoking vertex.
    ffp.SetRenderState(VXRENDERSTATE_ZENABLE, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_FLAT);
    BeginPixelFrame(ffp, context);
    const VxVector flatPositions[3] = {
        VxVector(-0.9f, -0.9f, 0.5f),
        VxVector( 0.9f, -0.9f, 0.5f),
        VxVector( 0.0f,  0.9f, 0.5f)
    };
    const CKDWORD flatColors[3] = {0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu};
    TestCheck(DrawColorTriangle(ffp, ffp.GetRenderPipeline().GetEncoder(),
                                flatPositions, flatColors),
              "Flat-shaded backend pixel draw must submit");
    EndPixelFrameAndRead(ffp, context, pixels);
    TestCheckf(PixelNear(pixels, 32, 32, 255, 0, 0),
               "flat-shade center pixel mismatch: BGRA=(%u,%u,%u,%u)",
               pixels[(32 * 64 + 32) * 4 + 0], pixels[(32 * 64 + 32) * 4 + 1],
               pixels[(32 * 64 + 32) * 4 + 2], pixels[(32 * 64 + 32) * 4 + 3]);

    // A farther draw submitted second must fail the D3D-style LESS_EQUAL depth test.
    ffp.SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_GOURAUD);
    ffp.SetRenderState(VXRENDERSTATE_ZENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_ZWRITEENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_ZFUNC, VXCMP_LESSEQUAL);
    BeginPixelFrame(ffp, context);
    const VxVector nearPositions[3] = {
        VxVector(-0.9f, -0.9f, 0.25f),
        VxVector( 0.9f, -0.9f, 0.25f),
        VxVector( 0.0f,  0.9f, 0.25f)
    };
    const VxVector farPositions[3] = {
        VxVector(-0.9f, -0.9f, 0.75f),
        VxVector( 0.9f, -0.9f, 0.75f),
        VxVector( 0.0f,  0.9f, 0.75f)
    };
    const CKDWORD green[3] = {0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u};
    const CKDWORD red[3] = {0xFFFF0000u, 0xFFFF0000u, 0xFFFF0000u};
    TestCheck(DrawColorTriangle(ffp, ffp.GetRenderPipeline().GetEncoder(),
                                nearPositions, green) &&
                  DrawColorTriangle(ffp, ffp.GetRenderPipeline().GetEncoder(),
                                    farPositions, red),
              "Depth-order backend pixel draws must submit");
    EndPixelFrameAndRead(ffp, context, pixels);
    TestCheck(PixelNear(pixels, 32, 32, 0, 255, 0),
              "D3D depth-range normalization must preserve the nearer green draw");

    // Readback is a public top-first contract, independent of backend framebuffer origin.
    ffp.SetRenderState(VXRENDERSTATE_ZENABLE, FALSE);
    BeginPixelFrame(ffp, context);
    const VxVector upperPositions[3] = {
        VxVector(-1.0f, 0.0f, 0.5f),
        VxVector( 1.0f, 0.0f, 0.5f),
        VxVector( 0.0f, 1.0f, 0.5f)
    };
    const VxVector lowerPositions[3] = {
        VxVector(-1.0f,  0.0f, 0.5f),
        VxVector( 0.0f, -1.0f, 0.5f),
        VxVector( 1.0f,  0.0f, 0.5f)
    };
    const CKDWORD blue[3] = {0xFF0000FFu, 0xFF0000FFu, 0xFF0000FFu};
    TestCheck(DrawColorTriangle(ffp, ffp.GetRenderPipeline().GetEncoder(),
                                upperPositions, red) &&
                  DrawColorTriangle(ffp, ffp.GetRenderPipeline().GetEncoder(),
                                    lowerPositions, blue),
              "Readback-orientation backend pixel draws must submit");
    EndPixelFrameAndRead(ffp, context, pixels);
    TestCheck(PixelNear(pixels, 32, 16, 255, 0, 0) &&
                  PixelNear(pixels, 32, 48, 0, 0, 255),
              "ReadFrameBuffer must normalize backend output to top-first rows");

    ffp.Shutdown();
    DestroyPixelFrameBuffer(context);
    context->Frame(CKRST_FRAME_SYNC_IMMEDIATE);
    CKRenderSettingsClearOverridesForTests();
    printf("  coverage: backendPixelCases=3 tolerance=24\n");
}

void BackendRuntimeCreatesRepresentativeFFPPrograms()
{
    const char *requestedBackend = GetEnvValue("CKRE_RUNTIME_BACKEND");
    if (!requestedBackend)
        requestedBackend = GetEnvValue("CKRE_BGFX_RUNTIME_BACKEND");
    if (!requestedBackend)
        requestedBackend = GetEnvValue("CKBGFX_RENDERER_BACKEND");
    if (!requestedBackend)
        requestedBackend = "opengl";
    SetEnvValue("CKBGFX_RENDERER_BACKEND", requestedBackend);

    TestCheckf(SDL_Init(SDL_INIT_VIDEO),
               "SDL video init failed: %s", SDL_GetError());

    SDL_Window *window = SDL_CreateWindow("ffp-backend-runtime",
                                          64, 64,
                                          SDL_WINDOW_HIDDEN);
    TestCheckf(window != NULL,
               "SDL hidden window creation failed: %s", SDL_GetError());

    CKBgfxRasterizer rasterizer;
    TestCheck(rasterizer.Start((WIN_HANDLE)window) == TRUE,
              "CKBgfxRasterizer must start for backend runtime test");
    TestCheck(rasterizer.GetDriverCount() > 0,
              "CKBgfxRasterizer must expose a driver");

    CKRasterizerDriver *driver = rasterizer.GetDriver(0);
    TestCheck(driver != NULL,
              "CKBgfxRasterizer driver must exist");

    CKRasterizerContext *baseContext = driver->CreateContext();
    TestCheck(baseContext != NULL,
              "CKBgfxRasterizer driver must create a context");

    CKBgfxRasterizerContext *context = static_cast<CKBgfxRasterizerContext *>(baseContext);
    TestCheck(context->Create((WIN_HANDLE)window, 0, 0, 64, 64, 32,
                              FALSE, 0, 24, 8) == TRUE,
              "bgfx backend context creation must succeed");

    CKShaderTargetDesc target;
    TestCheck(driver->GetShaderTarget(&target) == CK_OK,
              "backend runtime driver must expose a shader target");
    if (target.Profile == CKRST_SHADER_PROFILE_GLSL) {
        TestCheck((target.Flags & CKRST_SHADER_TARGET_NDC_MINUS_ONE_TO_ONE) != 0 &&
                      (target.Flags & CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT) != 0,
                  "Desktop OpenGL must advertise homogeneous depth and bottom-left origin");
    } else {
        TestCheck((target.Flags & (CKRST_SHADER_TARGET_NDC_MINUS_ONE_TO_ONE |
                                  CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT)) == 0,
                  "D3D, Vulkan, and Metal must expose the canonical 0..1/top-left target contract");
    }

    const CKFFSpecializedModuleEntry *fullEntry = FirstGeneratedEntry(target.Profile);
    TestCheckf(fullEntry != NULL,
               "backend runtime test requires a generated %s specialized module",
               CKBgfxShaderProfileName(target.Profile));
    printf("  backend: requested=%s profile=%s\n",
           requestedBackend,
           CKBgfxShaderProfileName(target.Profile));

    RunShaderProgramCase(context, fullEntry->Key, false, true,
                         "full-specialized backend route");
    RunShaderProgramCase(context, MakeStageFourFallbackKey(target.Profile, CKFF_SAMPLER_2D),
                         false, false,
                         "stage 4 uber fallback backend route");
    RunShaderProgramCase(context, MakeStageFourFallbackKey(target.Profile, CKFF_SAMPLER_VOLUME),
                         false, false,
                         "stage 4 volume fallback backend route");
    RunShaderProgramCase(context, MakeVolumeCubeStaticLayoutKey(target.Profile),
                         false, false,
                         "volume+cube static sampler backend route");
    RunShaderProgramCase(context, fullEntry->Key, true, false,
                         "forced uber backend route");

    BackendRuntimeMatchesFFPPixelSemantics(context);

    driver->DestroyContext(context);
    rasterizer.Close();
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    printf("  coverage: backendRuntimeProgramCases=5\n");
}

} // namespace

int main()
{
    if (!EnvFlagEnabled("CKRE_RUN_OPENGL_RUNTIME_TESTS") &&
        !EnvFlagEnabled("CKRE_RUN_BGFX_BACKEND_RUNTIME_TESTS")) {
        printf("SKIPPED: set CKRE_RUN_OPENGL_RUNTIME_TESTS=1 or CKRE_RUN_BGFX_BACKEND_RUNTIME_TESTS=1 to run the bgfx backend runtime gate.\n");
        return 0;
    }

    TestFramework tests;
    tests.Run("bgfx backend runtime creates representative FFP programs",
              &BackendRuntimeCreatesRepresentativeFFPPrograms);
    return tests.ExitCode();
}
