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

namespace {

char g_BackendRuntimeFailure[512];

struct PixelResources {
    CKDWORD ColorTexture;
    CKDWORD DepthTexture;
    CKDWORD FrameBuffer;
    CKDWORD ReadbackTexture;

    PixelResources()
        : ColorTexture(0), DepthTexture(0), FrameBuffer(0), ReadbackTexture(0) {}
};

struct ScreenShotResult {
    ScreenShotResult()
        : Calls(0), Width(0), Height(0), Format(UNKNOWN_PF), DataSize(0) {}

    VxMutex Mutex;
    int Calls;
    CKDWORD Width;
    CKDWORD Height;
    VX_PIXELFORMAT Format;
    CKDWORD DataSize;
};

void ScreenShotCallback(void *userData, CKDWORD, CKDWORD width, CKDWORD height,
                        CKDWORD, VX_PIXELFORMAT format, const void *,
                        CKDWORD size, CKBOOL)
{
    ScreenShotResult *result = static_cast<ScreenShotResult *>(userData);
    if (!result)
        return;
    VxMutexLock lock(result->Mutex);
    ++result->Calls;
    result->Width = width;
    result->Height = height;
    result->Format = format;
    result->DataSize = size;
}

int ScreenShotCallCount(ScreenShotResult &result)
{
    VxMutexLock lock(result.Mutex);
    return result.Calls;
}

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

void ValidateEncoderFrameBoundary(CKRasterizerDriver *driver,
                                  CKBgfxRasterizerContext *context)
{
    CKUniformDesc desc;
    desc.Name = (CKSTRING)"u_encoderBoundaryTest";
    desc.Type = CKRST_UNIFORM_VEC4;
    desc.Count = 1;

    CKDWORD uniform = 0;
    TestCheck(context->CreateUniform(&desc, &uniform) == CK_OK && uniform != 0,
              "Encoder boundary test must create a uniform");

    CKRasterizerEncoder *encoder = context->BeginEncoder();
    TestCheck(encoder != NULL,
              "Encoder boundary test must begin an encoder");
    TestCheck(!context->IsIdle(),
              "Context must report an active encoder as non-idle");
    TestCheck(!driver->DestroyContext(context),
              "Driver must reject context destruction while an encoder is active");

    CKDWORD frameNumber = 0;
    TestCheck(context->Frame(CKRST_FRAME_SYNC_IMMEDIATE,
                             CKRST_FRAME_NONE, &frameNumber) ==
                  CKERR_INVALIDOPERATION &&
                  frameNumber == 0,
              "Frame must reject an active encoder without advancing");
    TestCheck(context->DeleteObject(uniform, CKRST_OBJ_UNIFORM) ==
                  CKERR_INVALIDOPERATION &&
                  context->IsObjectAlive(uniform, CKRST_OBJ_UNIFORM),
              "Resource deletion must reject an active encoder without consuming the handle");
    TestCheck(context->Resize(0, 0, 64, 64, 0) == CKERR_INVALIDOPERATION &&
                  context->SetAntialias(4) == CKERR_INVALIDOPERATION,
              "Device reset operations must reject an active encoder");

    TestCheck(context->EndEncoder(encoder) == CK_OK,
              "Encoder boundary test must end the encoder");
    TestCheck(context->IsIdle(),
              "Context must become idle after the encoder ends");
    TestCheck(context->DeleteObject(uniform, CKRST_OBJ_UNIFORM) == CK_OK &&
                  !context->IsObjectAlive(uniform, CKRST_OBJ_UNIFORM),
              "Resource deletion must succeed after all encoders end");

    desc.Name = (CKSTRING)"u_encoderBoundaryReplacement";
    CKDWORD replacement = 0;
    TestCheck(context->CreateUniform(&desc, &replacement) == CK_OK &&
                  replacement != 0 && replacement != uniform,
              "Reused resource slots must receive a new generation handle");
    TestCheck(!context->IsObjectAlive(uniform, CKRST_OBJ_UNIFORM) &&
                  context->DeleteObject(uniform, CKRST_OBJ_UNIFORM) ==
                      CKERR_INVALIDPARAMETER &&
                  context->IsObjectAlive(replacement, CKRST_OBJ_UNIFORM),
              "A stale generation handle must not alias its replacement");
    TestCheck(context->DeleteObject(replacement, CKRST_OBJ_UNIFORM) == CK_OK,
              "Replacement resource must remain independently deletable");
    TestCheck(context->Frame(CKRST_FRAME_SYNC_IMMEDIATE) == CK_OK,
              "Frame must succeed after all encoders end");
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

bool PixelNear(const XArray<CKBYTE> &pixels,
               int x, int y, int r, int g, int b, int tolerance = 24)
{
    const size_t offset = ((size_t)y * 64u + (size_t)x) * 4u;
    if (offset + 3 >= (size_t)pixels.Size())
        return false;
    const int db = abs((int)pixels[offset + 0] - b);
    const int dg = abs((int)pixels[offset + 1] - g);
    const int dr = abs((int)pixels[offset + 2] - r);
    return dr <= tolerance && dg <= tolerance && db <= tolerance;
}

void CreatePixelFrameBuffer(CKBgfxRasterizerContext *context,
                            PixelResources &resources)
{
    CKTextureDesc colorDesc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, colorDesc.Format);
    colorDesc.Format.Width = 64;
    colorDesc.Format.Height = 64;
    colorDesc.MipMapCount = 1;
    colorDesc.Depth = 1;
    colorDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB |
                      CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_RENDERTARGET;
    TestCheck(context->CreateTexture(&colorDesc, NULL, &resources.ColorTexture) == CK_OK,
              "Backend pixel gate must create a color render target");

    CKTextureDesc readbackDesc = colorDesc;
    readbackDesc.MipMapCount = 2;
    readbackDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_BLIT_DST |
                         CKRST_TEXTURE_READBACK;
    TestCheck(context->CreateTexture(&readbackDesc, NULL, &resources.ReadbackTexture) == CK_OK,
              "Backend pixel gate must create a blit readback texture");

    CKDepthTextureDesc depthDesc = {};
    depthDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_DEPTHSTENCIL;
    depthDesc.Width = 64;
    depthDesc.Height = 64;
    depthDesc.MipMapCount = 1;
    depthDesc.DepthFormat = CKRST_DEPTHFMT_D24S8;
    CKERROR depthError = context->CreateDepthTexture(&depthDesc, &resources.DepthTexture);
    if (depthError != CK_OK) {
        depthDesc.DepthFormat = CKRST_DEPTHFMT_D24;
        depthError = context->CreateDepthTexture(&depthDesc, &resources.DepthTexture);
    }
    if (depthError != CK_OK) {
        depthDesc.DepthFormat = CKRST_DEPTHFMT_D16;
        depthError = context->CreateDepthTexture(&depthDesc, &resources.DepthTexture);
    }
    TestCheck(depthError == CK_OK,
              "Backend pixel gate must create a depth render target");

    CKFrameBufferAttachmentDesc colorAttachment;
    colorAttachment.Texture = resources.ColorTexture;
    colorAttachment.Mip = 0;
    colorAttachment.Layer = 0;
    CKFrameBufferDesc frameBufferDesc;
    frameBufferDesc.Color = &colorAttachment;
    frameBufferDesc.ColorCount = 1;
    frameBufferDesc.DepthStencil.Texture = resources.DepthTexture;
    frameBufferDesc.DepthStencil.Mip = 0;
    frameBufferDesc.DepthStencil.Layer = 0;
    TestCheck(context->CreateFrameBuffer(&frameBufferDesc, &resources.FrameBuffer) == CK_OK,
              "Backend pixel gate must create an offscreen framebuffer");
    TestCheck(context->IsFrameBufferValid(
                  frameBufferDesc.ColorCount, frameBufferDesc.Color,
                  &frameBufferDesc.DepthStencil) == TRUE,
              "Framebuffer validation must agree with framebuffer creation");
    TestCheck(context->IsFrameBufferValid(0, NULL, NULL) == FALSE,
              "Framebuffer validation must reject an empty attachment set");
}

void DestroyPixelFrameBuffer(CKBgfxRasterizerContext *context,
                             PixelResources &resources)
{
    context->DeleteObject(resources.FrameBuffer, CKRST_OBJ_FRAMEBUFFER);
    context->DeleteObject(resources.DepthTexture, CKRST_OBJ_TEXTURE);
    context->DeleteObject(resources.ColorTexture, CKRST_OBJ_TEXTURE);
    context->DeleteObject(resources.ReadbackTexture, CKRST_OBJ_TEXTURE);
    resources = PixelResources();
}

void BeginPixelFrame(CKFixedFunctionPipeline &ffp,
                     CKBgfxRasterizerContext *context,
                     const PixelResources &resources)
{
    CKRECT viewport = {0, 0, 64, 64};
    VxMatrix identity;
    Vx3DMatrixIdentity(identity);
    ffp.GetRenderPipeline().BeginFrame(
        viewport, CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH,
        0xFF000000u, 1.0f, identity, identity);
    context->SetViewFrameBuffer(CKRP_VIEW_CLEAR, resources.FrameBuffer);
    context->SetViewFrameBuffer(CKRP_VIEW_OPAQUE3D, resources.FrameBuffer);
    TestCheck(ffp.GetRenderPipeline().GetEncoder() != NULL,
              "Backend pixel gate must acquire a rasterizer encoder");
}

void EndPixelFrameAndRead(CKFixedFunctionPipeline &ffp,
                          CKBgfxRasterizerContext *context,
                          const PixelResources &resources,
                          XArray<CKBYTE> &pixels)
{
    CKRasterizerEncoder *encoder = ffp.GetRenderPipeline().GetEncoder();
    TestCheck(encoder != NULL,
              "Backend pixel gate must retain its encoder through readback blit");
    if (encoder) {
        CKRECT source = {0, 0, 64, 64};
        encoder->Blit(CKRP_VIEW_FOREGROUND2D,
                      resources.ReadbackTexture, 0, 0, 0,
                      resources.ColorTexture, 0, &source);
        CKRECT mipSource = {0, 0, 32, 32};
        encoder->Blit(CKRP_VIEW_FOREGROUND2D,
                      resources.ReadbackTexture, 1, 0, 0,
                      resources.ColorTexture, 0, &mipSource);
    }
    ffp.GetRenderPipeline().EndFrame(CKRST_FRAME_SYNC_IMMEDIATE);

    CKReadbackDesc image;
    TestCheck(context->ReadTexture(resources.ReadbackTexture, 0, &image, NULL) == CK_OK,
              "Backend pixel gate must query the blit destination layout");
    TestCheck(image.Width == 64 && image.Height == 64 && image.RowPitch == 64 * 4,
              "Base mip readback must report a tight 64x64 layout");
    XArray<CKBYTE> basePixels;
    basePixels.Resize((int)image.RequiredSize);
    memset(basePixels.Begin(), 0, image.RequiredSize);
    image.Data = basePixels.Begin();
    image.Capacity = (CKDWORD)basePixels.Size();
    CKDWORD baseAvailableFrame = 0;
    TestCheck(context->ReadTexture(resources.ReadbackTexture, 0, &image,
                                   &baseAvailableFrame) == CK_OK,
              "Backend pixel gate must queue the blit destination readback");

    const size_t mipBytes = 32u * 32u * 4u;
    const size_t guardBytes = 64u;
    XArray<CKBYTE> mipPixels;
    mipPixels.Resize((int)(mipBytes + guardBytes));
    memset(mipPixels.Begin(), 0xCD, mipBytes + guardBytes);
    CKReadbackDesc mipImage;
    TestCheck(context->ReadTexture(resources.ReadbackTexture, 1, &mipImage, NULL) == CK_OK,
              "Backend pixel gate must query the requested mip dimensions");
    TestCheck(mipImage.RequiredSize == mipBytes && mipImage.RowPitch == 32 * 4,
              "Mip readback must report its own tight layout");
    mipImage.Data = &mipPixels[0];
    mipImage.Capacity = (CKDWORD)mipBytes;
    CKDWORD mipAvailableFrame = 0;
    TestCheck(context->ReadTexture(resources.ReadbackTexture, 1, &mipImage,
                                   &mipAvailableFrame) == CK_OK,
              "Backend pixel gate must queue the requested mip readback");

    CKDWORD currentFrame = 0;
    for (int i = 0; i < 120; ++i) {
        TestCheck(context->Frame(CKRST_FRAME_SYNC_IMMEDIATE,
                                 CKRST_FRAME_NONE, &currentFrame) == CK_OK,
                  "Backend pixel gate must advance queued readbacks");
        const bool baseReady =
            (uint32_t)(currentFrame - baseAvailableFrame) < UINT32_C(0x80000000);
        const bool mipReady =
            (uint32_t)(currentFrame - mipAvailableFrame) < UINT32_C(0x80000000);
        if (baseReady && mipReady)
            break;
    }
    TestCheck((uint32_t)(currentFrame - baseAvailableFrame) < UINT32_C(0x80000000) &&
              (uint32_t)(currentFrame - mipAvailableFrame) < UINT32_C(0x80000000),
              "Queued texture readbacks must reach their completion frames");

    pixels.Resize(basePixels.Size());
    memset(pixels.Begin(), 0, (size_t)pixels.Size());
    for (CKDWORD y = 0; y < image.Height; ++y) {
        const CKDWORD sourceY = image.YFlip ? image.Height - 1u - y : y;
        memcpy(&pixels[y * image.RowPitch],
               &basePixels[sourceY * image.RowPitch], image.RowPitch);
    }
    bool guardIntact = true;
    for (size_t i = mipBytes; i < (size_t)mipPixels.Size(); ++i)
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
    PixelResources resources;
    CreatePixelFrameBuffer(context, resources);
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

    XArray<CKBYTE> pixels;

    // D3D8 flat shading uses the first vertex of a triangle as the provoking vertex.
    ffp.SetRenderState(VXRENDERSTATE_ZENABLE, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_FLAT);
    BeginPixelFrame(ffp, context, resources);
    const VxVector flatPositions[3] = {
        VxVector(-0.9f, -0.9f, 0.5f),
        VxVector( 0.9f, -0.9f, 0.5f),
        VxVector( 0.0f,  0.9f, 0.5f)
    };
    const CKDWORD flatColors[3] = {0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu};
    const CKBOOL flatSubmitted = DrawColorTriangle(
        ffp, ffp.GetRenderPipeline().GetEncoder(), flatPositions, flatColors);
    TestCheckf(flatSubmitted,
               "Flat-shaded backend pixel draw must submit: reason=%u encoder=%p",
               (unsigned)ffp.GetLastDrawRejectReason(),
               ffp.GetRenderPipeline().GetEncoder());
    EndPixelFrameAndRead(ffp, context, resources, pixels);
    TestCheckf(PixelNear(pixels, 32, 32, 255, 0, 0),
               "flat-shade center pixel mismatch: BGRA=(%u,%u,%u,%u)",
               pixels[(32 * 64 + 32) * 4 + 0], pixels[(32 * 64 + 32) * 4 + 1],
               pixels[(32 * 64 + 32) * 4 + 2], pixels[(32 * 64 + 32) * 4 + 3]);

    // A farther draw submitted second must fail the D3D-style LESS_EQUAL depth test.
    ffp.SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_GOURAUD);
    ffp.SetRenderState(VXRENDERSTATE_ZENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_ZWRITEENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_ZFUNC, VXCMP_LESSEQUAL);
    BeginPixelFrame(ffp, context, resources);
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
    EndPixelFrameAndRead(ffp, context, resources, pixels);
    TestCheck(PixelNear(pixels, 32, 32, 0, 255, 0),
              "D3D depth-range normalization must preserve the nearer green draw");

    // Readback is a public top-first contract, independent of backend framebuffer origin.
    ffp.SetRenderState(VXRENDERSTATE_ZENABLE, FALSE);
    BeginPixelFrame(ffp, context, resources);
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
    EndPixelFrameAndRead(ffp, context, resources, pixels);
    TestCheck(PixelNear(pixels, 32, 16, 255, 0, 0) &&
                  PixelNear(pixels, 32, 48, 0, 0, 255),
              "ReadFrameBuffer must normalize backend output to top-first rows");

    ffp.Shutdown();
    TestCheck(context->RequestScreenShot(resources.FrameBuffer,
                                         ScreenShotCallback) ==
                  CKERR_NOTIMPLEMENTED,
              "Texture-backed framebuffer screenshot must report the bgfx limitation");
    DestroyPixelFrameBuffer(context, resources);
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
    TestCheck(driver->CreateContext() == NULL,
              "CKBgfxRasterizer must reject a second context immediately");

    CKBgfxRasterizerContext *context = static_cast<CKBgfxRasterizerContext *>(baseContext);
    TestCheck(context->Create((WIN_HANDLE)window, 0, 0, 64, 64, 32,
                              FALSE, 0, 24, 8) == CK_OK,
              "bgfx backend context creation must succeed");

    CKRasterizerTargetDesc target;
    TestCheck(context->GetTargetDesc(&target) == CK_OK,
              "backend runtime context must expose a target descriptor");
    CKRasterizerCapsDesc caps;
    TestCheck(context->GetCaps(&caps) == CK_OK &&
                  driver->m_CapsUpToDate &&
                  driver->m_3DCaps.MaxTextureWidth == caps.MaxTextureSize &&
                  driver->m_TextureFormats.Size() > 0,
              "Context creation must refresh legacy driver caps from bgfx");
    if (target.ShaderProfile == CKRST_SHADER_PROFILE_GLSL ||
        target.ShaderProfile == CKRST_SHADER_PROFILE_ESSL) {
        TestCheck(target.HomogeneousDepth && target.OriginBottomLeft,
                  "Desktop OpenGL must advertise homogeneous depth and bottom-left origin");
    } else {
        TestCheck(!target.HomogeneousDepth && !target.OriginBottomLeft,
                  "D3D, Vulkan, and Metal must expose the canonical 0..1/top-left target contract");
    }

    const CKFFSpecializedModuleEntry *fullEntry = FirstGeneratedEntry(target.ShaderProfile);
    TestCheckf(fullEntry != NULL,
               "backend runtime test requires a generated %s specialized module",
               CKBgfxShaderProfileName(target.ShaderProfile));
    printf("  backend: requested=%s profile=%s\n",
           requestedBackend,
           CKBgfxShaderProfileName(target.ShaderProfile));

    ValidateEncoderFrameBoundary(driver, context);

    RunShaderProgramCase(context, fullEntry->Key, false, true,
                         "full-specialized backend route");
    RunShaderProgramCase(context, MakeStageFourFallbackKey(target.ShaderProfile, CKFF_SAMPLER_2D),
                         false, false,
                         "stage 4 uber fallback backend route");
    RunShaderProgramCase(context, MakeStageFourFallbackKey(target.ShaderProfile, CKFF_SAMPLER_VOLUME),
                         false, false,
                         "stage 4 volume fallback backend route");
    RunShaderProgramCase(context, MakeVolumeCubeStaticLayoutKey(target.ShaderProfile),
                         false, false,
                         "volume+cube static sampler backend route");
    RunShaderProgramCase(context, fullEntry->Key, true, false,
                         "forced uber backend route");

    BackendRuntimeMatchesFFPPixelSemantics(context);

    TestCheck(SDL_SetWindowSize(window, 48, 96),
              "Backend runtime window must accept a portrait resize");
    TestCheck(SDL_SyncWindow(window),
              "Backend runtime window portrait resize must complete");
    TestCheck(context->Resize(0, 0, 48, 96, 0) == CK_OK,
              "Backend runtime context must resize to a portrait target");
    ScreenShotResult completedScreenShot;
    TestCheck(context->RequestScreenShot(0, ScreenShotCallback,
                                         &completedScreenShot) == CK_OK,
              "Backbuffer screenshot request must be accepted");
    for (int i = 0; i < 16 && ScreenShotCallCount(completedScreenShot) == 0; ++i)
        TestCheck(context->Frame(CKRST_FRAME_SYNC_IMMEDIATE) == CK_OK,
                  "Backbuffer screenshot frame advance must succeed");
    {
        VxMutexLock lock(completedScreenShot.Mutex);
        TestCheckf(completedScreenShot.Calls == 1 &&
                       completedScreenShot.Width == 48 &&
                       completedScreenShot.Height == 96 &&
                       completedScreenShot.Format != UNKNOWN_PF &&
                       completedScreenShot.DataSize != 0,
                   "Backbuffer screenshot callback invalid: calls=%d size=%ux%u format=%u bytes=%u",
                   completedScreenShot.Calls,
                   (unsigned)completedScreenShot.Width,
                   (unsigned)completedScreenShot.Height,
                   (unsigned)completedScreenShot.Format,
                   (unsigned)completedScreenShot.DataSize);
    }

    ScreenShotResult cancelledScreenShot;
    TestCheck(context->RequestScreenShot(0, ScreenShotCallback,
                                         &cancelledScreenShot) == CK_OK &&
                  context->CancelScreenShots(&cancelledScreenShot) == CK_OK &&
                  ScreenShotCallCount(cancelledScreenShot) == 1 &&
                  context->CancelScreenShots(&cancelledScreenShot) ==
                      CKERR_NOTFOUND,
              "Cancelling a screenshot must complete its callback exactly once");
    TestCheck(context->Frame(CKRST_FRAME_SYNC_IMMEDIATE) == CK_OK &&
                  ScreenShotCallCount(cancelledScreenShot) == 1,
              "A cancelled screenshot must not re-enter its callback on a later frame");

    context->InjectFatalForTests();
    TestCheck(context->GetDeviceStatus() == CKERR_INVALIDRENDERCONTEXT &&
                  context->BeginEncoder() == NULL &&
                  context->Frame(CKRST_FRAME_SYNC_IMMEDIATE) ==
                      CKERR_INVALIDRENDERCONTEXT,
              "A latched bgfx fatal must block encoder and frame submission");
    TestCheck(context->BeginShutdown() == CK_OK &&
                  context->BeginEncoder() == NULL,
              "An idle bgfx context must enter shutdown and reject new encoders");

    TestCheck(driver->DestroyContext(context),
              "A fatal but idle bgfx context must remain safely destructible");
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
