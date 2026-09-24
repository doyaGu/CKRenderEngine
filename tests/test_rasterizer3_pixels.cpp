// rasterizer3_pixel_tests: fixed-function semantics that only a real backend
// can prove. Most cases drive the private CKRasterizer interface in a
// visible SDL window; explicit white-box cases verify backend resource and
// presentation invariants. Pixels come back through CopyToMemoryBuffer, so
// the readback path is part of the gate.
//
// Gated by CKRE_RUN_BGFX_BACKEND_RUNTIME_TESTS=1 (or the older
// CKRE_RUN_OPENGL_RUNTIME_TESTS=1); the backend comes from
// CKBGFX_RENDERER_BACKEND (default opengl).

#include "CKRasterizer.h"
#ifdef CKRE_PIXEL_SDL_GPU
#include "CKSdlGpuRasterizerContext.h"
#else
#include "CKBgfxRasterizerContext.h"
#endif
#include "TestTriangleMultiset.h"

#include <SDL3/SDL.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// Static plugin entry; both executables run the same public rasterizer cases.
#ifdef CKRE_PIXEL_SDL_GPU
extern void CKSdlGpuRasterizerGetInfo(CKRasterizerInfo *info);
#else
extern void CKBgfxRasterizerGetInfo(CKRasterizerInfo *info);

#endif

namespace {

const int kWidth = 64;
const int kHeight = 64;
const int kTolerance = 24;

char g_Failure[512];

void TestCheckf(bool condition, const char *format, ...)
{
    if (condition)
        return;
    va_list args;
    va_start(args, format);
    vsnprintf(g_Failure, sizeof(g_Failure), format, args);
    va_end(args);
    TestFail(g_Failure);
}

CKRenderStats ReadStats(CKRasterizerContext *context)
{
    CKRenderStats stats = {};
    context->GetStats(stats);
    return stats;
}

bool EnvFlagEnabled(const char *name)
{
    const char *value = getenv(name);
    return value && (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "TRUE") == 0 ||
                     strcmp(value, "on") == 0 || strcmp(value, "ON") == 0);
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

CKDWORD FloatBits(float value)
{
    CKDWORD bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// ---------------------------------------------------------------------------
// Backend
// ---------------------------------------------------------------------------

struct Backend {
    SDL_Window *Window;
    CKRasterizerInfo Info;
    CKRasterizer *Rasterizer;
    CKRasterizerDriver *Driver;
    CKRasterizerContext *Context;

    Backend() : Window(NULL), Rasterizer(NULL), Driver(NULL), Context(NULL) {}
};

CKBOOL OpenBackend(Backend &b, int width, int height)
{
    b.Window = SDL_CreateWindow("rasterizer3-pixels", 640, 480, SDL_WINDOW_RESIZABLE);
    TestCheckf(b.Window != NULL, "SDL visible window creation failed: %s", SDL_GetError());
    if (!b.Window)
        return FALSE;
    SDL_ShowWindow(b.Window);
    SDL_RaiseWindow(b.Window);
#ifdef CKRE_PIXEL_SDL_GPU
    CKSdlGpuRasterizerGetInfo(&b.Info);
#else
    CKBgfxRasterizerGetInfo(&b.Info);
#endif
    TestCheck(b.Info.InterfaceRevision == CKRST_INTERFACE_REVISION,
              "plugin reports the current interface revision");
    TestCheck(b.Info.StartFct != NULL && b.Info.CloseFct != NULL, "plugin entry points");
    b.Rasterizer = b.Info.StartFct((WIN_HANDLE)b.Window);
    TestCheck(b.Rasterizer != NULL, "rasterizer must start");
    if (!b.Rasterizer)
        return FALSE;
    TestCheck(b.Rasterizer->GetDriverCount() > 0, "rasterizer exposes a driver");
    b.Driver = b.Rasterizer->GetDriver(0);
    TestCheck(b.Driver != NULL, "driver 0");
    if (!b.Driver)
        return FALSE;
    b.Context = b.Driver->CreateContext();
    TestCheck(b.Context != NULL, "driver creates a context");
    if (!b.Context)
        return FALSE;
    TestCheck(b.Context->Create((WIN_HANDLE)b.Window, 0, 0, width, height, 32, FALSE, 0, 24, 8),
              "context creation on the real backend");
    return TRUE;
}

void CloseBackend(Backend &b)
{
    if (b.Context) {
        TestCheck(b.Context->BeginShutdown(), "BeginShutdown");
        TestCheck(b.Driver->DestroyContext(b.Context), "DestroyContext");
        b.Context = NULL;
    }
    if (b.Rasterizer) {
        b.Info.CloseFct(b.Rasterizer);
        b.Rasterizer = NULL;
    }
    if (b.Window) {
        SDL_DestroyWindow(b.Window);
        b.Window = NULL;
    }
}

// ---------------------------------------------------------------------------
// Pixels
// ---------------------------------------------------------------------------

struct Pixels {
    XArray<CKBYTE> Data;
    int Width;
    int Height;
    Pixels() : Width(0), Height(0) {}
};

CKBOOL ReadBackbuffer(CKRasterizerContext *ctx, Pixels &out)
{
    VxImageDescEx image;
    memset(&image, 0, sizeof(image));
    const int required = ctx->CopyToMemoryBuffer(NULL, VXBUFFER_BACKBUFFER, image);
    TestCheckf(required > 0 && image.BitsPerPixel == 32, "CopyToMemoryBuffer size query failed: %d bytes", required);
    if (required <= 0)
        return FALSE;
    out.Data.Resize(required);
    memset(out.Data.Begin(), 0, required);
    image.Image = out.Data.Begin();
    const int copied = ctx->CopyToMemoryBuffer(NULL, VXBUFFER_BACKBUFFER, image);
    TestCheckf(copied == required, "CopyToMemoryBuffer copied %d of %d bytes", copied, required);
    out.Width = image.Width;
    out.Height = image.Height;
    return copied == required;
}

void GetPixel(const Pixels &pixels, int x, int y, CKBYTE bgra[4])
{
    memset(bgra, 0, 4);
    if (x < 0 || y < 0 || x >= pixels.Width || y >= pixels.Height)
        return;
    const int offset = (y * pixels.Width + x) * 4;
    if (offset + 3 < pixels.Data.Size())
        memcpy(bgra, &pixels.Data[offset], 4);
}

bool PixelNear(const Pixels &pixels, int x, int y, int r, int g, int b)
{
    CKBYTE bgra[4];
    GetPixel(pixels, x, y, bgra);
    return abs((int)bgra[2] - r) <= kTolerance && abs((int)bgra[1] - g) <= kTolerance &&
           abs((int)bgra[0] - b) <= kTolerance;
}

void ExpectCenter(const Pixels &pixels, int r, int g, int b, const char *what)
{
    CKBYTE bgra[4];
    GetPixel(pixels, kWidth / 2, kHeight / 2, bgra);
    TestCheckf(PixelNear(pixels, kWidth / 2, kHeight / 2, r, g, b),
               "%s: expected RGB(%d,%d,%d) got BGRA=(%u,%u,%u,%u)", what, r, g, b,
               (unsigned)bgra[0], (unsigned)bgra[1], (unsigned)bgra[2], (unsigned)bgra[3]);
}

// ---------------------------------------------------------------------------
// State and frame helpers (public interface only)
// ---------------------------------------------------------------------------

void ResetStage(CKRasterizerContext *ctx, int stage)
{
    ctx->SetTexture(0, stage);
    for (CKDWORD tss = CKRST_TSS_OP; tss < (CKDWORD)CKRST_TSS_MAXSTATE; ++tss)
        ctx->SetTextureStageState(stage, (CKRST_TEXTURESTAGESTATETYPE)tss,
                                  tss == (CKDWORD)CKRST_TSS_TEXCOORDINDEX ? (CKDWORD)stage : 0);
    VxMatrix identity;
    Vx3DMatrixIdentity(identity);
    ctx->SetTransformMatrix((VXMATRIX_TYPE)(VXMATRIX_TEXTURE0 + stage), identity);
}

void ResetStagesFrom(CKRasterizerContext *ctx, int first)
{
    for (int stage = first; stage < CKRST_MAX_TEXTURE_STAGES; ++stage)
        ResetStage(ctx, stage);
}

// Vertex colours straight to the framebuffer, no lighting, no depth.
void SetDiffuseState(CKRasterizerContext *ctx)
{
    ctx->SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    ctx->SetRenderState(VXRENDERSTATE_COLORVERTEX, TRUE);
    ctx->SetRenderState(VXRENDERSTATE_DIFFUSEFROMVERTEX, TRUE);
    ctx->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
    ctx->SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, FALSE);
    ctx->SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, FALSE);
    ctx->SetRenderState(VXRENDERSTATE_FOGENABLE, FALSE);
    ctx->SetRenderState(VXRENDERSTATE_ZENABLE, FALSE);
    ctx->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, FALSE);
    ctx->SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_GOURAUD);
    ctx->SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_DISABLE);
    ctx->SetRenderState(VXRENDERSTATE_TWEENFACTOR, FloatBits(0.0f));
    ResetStagesFrom(ctx, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ctx->SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_DIFFUSE);
    VxMatrix identity;
    Vx3DMatrixIdentity(identity);
    ctx->SetTransformMatrix(VXMATRIX_WORLD, identity);
    ctx->SetTransformMatrix(VXMATRIX_VIEW, identity);
    ctx->SetTransformMatrix(VXMATRIX_PROJECTION, identity);
}

void BeginFrame(CKRasterizerContext *ctx, CKDWORD clearFlags, const VxMatrix *projection = NULL)
{
    VxMatrix identity;
    Vx3DMatrixIdentity(identity);
    ctx->SetTransformMatrix(VXMATRIX_VIEW, identity);
    ctx->SetTransformMatrix(VXMATRIX_PROJECTION, projection ? *projection : identity);
    TestCheck(ctx->Clear(clearFlags, 0xFF000000u, 1.0f, 0, 0, NULL), "Clear");
    TestCheck(ctx->BeginScene(), "BeginScene");
}

void EndFrame(CKRasterizerContext *ctx)
{
    TestCheck(ctx->EndScene(), "EndScene");
    TestCheck(ctx->BackToFront(FALSE), "BackToFront");
}

// Renders one frame and reads it back (spec 5.8: the readback is the frame
// just presented, no swap-chain repeats).
template <class Draw>
void RenderAndRead(CKRasterizerContext *ctx, CKDWORD clearFlags, const VxMatrix *projection, Draw draw,
                   Pixels &pixels)
{
    BeginFrame(ctx, clearFlags, projection);
    draw();
    EndFrame(ctx);
    ReadBackbuffer(ctx, pixels);
}

CKBOOL DrawColorTriangle(CKRasterizerContext *ctx, const VxVector positions[3], const CKDWORD colors[3])
{
    VxDrawPrimitiveData data;
    memset(&data, 0, sizeof(data));
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TR_VC;
    data.PositionPtr = const_cast<VxVector *>(positions);
    data.PositionStride = sizeof(VxVector);
    data.ColorPtr = const_cast<CKDWORD *>(colors);
    data.ColorStride = sizeof(CKDWORD);
    return ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data);
}

CKBOOL DrawTexturedTriangle(CKRasterizerContext *ctx, const VxVector positions[3], const CKDWORD colors[3],
                            float texcoords[3][4])
{
    VxDrawPrimitiveData data;
    memset(&data, 0, sizeof(data));
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TR_CL_VCT;
    data.PositionPtr = const_cast<VxVector *>(positions);
    data.PositionStride = sizeof(VxVector);
    data.ColorPtr = const_cast<CKDWORD *>(colors);
    data.ColorStride = sizeof(CKDWORD);
    data.TexCoordPtr = texcoords;
    data.TexCoordStride = sizeof(texcoords[0]);
    return ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data);
}

CKBOOL DrawTexturedPositionTTriangle(CKRasterizerContext *ctx, float positions[3][4],
                                     const CKDWORD colors[3], float texcoords[3][4])
{
    VxDrawPrimitiveData data;
    memset(&data, 0, sizeof(data));
    data.VertexCount = 3;
    data.Flags = CKRST_DP_CL_VCT;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(positions[0]);
    data.ColorPtr = const_cast<CKDWORD *>(colors);
    data.ColorStride = sizeof(CKDWORD);
    data.TexCoordPtr = texcoords;
    data.TexCoordStride = sizeof(texcoords[0]);
    return ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data);
}

CKBOOL DrawTweenTriangle(CKRasterizerContext *ctx, const VxVector positions[3], const VxVector tween[3],
                         const CKDWORD colors[3])
{
    VxDrawPrimitiveData data;
    memset(&data, 0, sizeof(data));
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TR_VC | CKRST_DP_TWEEN;
    data.PositionPtr = const_cast<VxVector *>(positions);
    data.PositionStride = sizeof(VxVector);
    data.TweenPositionPtr = const_cast<VxVector *>(tween);
    data.TweenPositionStride = sizeof(VxVector);
    data.ColorPtr = const_cast<CKDWORD *>(colors);
    data.ColorStride = sizeof(CKDWORD);
    return ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data);
}

const VxVector kCenterTriangle[3] = {VxVector(-0.9f, -0.9f, 0.5f), VxVector(0.9f, -0.9f, 0.5f), VxVector(0.0f, 0.9f, 0.5f)};
const CKDWORD kWhite[3] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
const CKDWORD kRed[3] = {0xFFFF0000u, 0xFFFF0000u, 0xFFFF0000u};
const CKDWORD kGreen[3] = {0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u};
const CKDWORD kBlue[3] = {0xFF0000FFu, 0xFF0000FFu, 0xFF0000FFu};

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

struct Textures {
    CKDWORD Transform;      // 2x1: red | green
    CKDWORD BumpLuminance;  // 4x4 X8L8V8U8 with two uploaded mips
    CKDWORD MirrorVolume;   // 2x2x2: red | green in each slice
    Textures() : Transform(0), BumpLuminance(0), MirrorVolume(0) {}
};

void CreateTextures(CKRasterizerContext *ctx, Textures &t)
{
    CKTextureDesc transformDesc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, transformDesc.Format);
    transformDesc.Format.Width = 2;
    transformDesc.Format.Height = 1;
    transformDesc.Format.BytesPerLine = 2 * 4;
    transformDesc.MipMapCount = 1;
    transformDesc.Flags = CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
    TestCheck(ctx->CreateTexture(&transformDesc, &t.Transform) && t.Transform != 0, "transform texture");
    CKDWORD transformPixels[2] = {0xFFFF0000u, 0xFF00FF00u};
    VxImageDescEx transformImage = transformDesc.Format;
    transformImage.Image = (XBYTE *)transformPixels;
    TestCheck(ctx->LoadTexture(t.Transform, transformImage, 0, CKRST_CUBEFACE_XPOS, NULL), "transform texture upload");

    CKTextureDesc volumeDesc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, volumeDesc.Format);
    volumeDesc.Format.Width = volumeDesc.Format.Height = 2;
    volumeDesc.Format.BytesPerLine = 2 * 4;
    volumeDesc.Depth = 2;
    volumeDesc.MipMapCount = 2;
    volumeDesc.Flags = CKRST_TEXTURE_RGB | CKRST_TEXTURE_VOLUMEMAP;
    TestCheck(ctx->CreateTexture(&volumeDesc, &t.MirrorVolume) && t.MirrorVolume != 0,
              "mirror volume texture");
    CKDWORD volumePixels[4] = {0xFFFF0000u, 0xFF00FF00u, 0xFFFF0000u, 0xFF00FF00u};
    VxImageDescEx volumeImage = volumeDesc.Format;
    volumeImage.Image = (XBYTE *)volumePixels;
    for (unsigned slice = 0; slice < 2; ++slice)
        TestCheck(ctx->LoadTexture(t.MirrorVolume, volumeImage, 0, (CKRST_CUBEFACE)slice, NULL),
                  "mirror volume slice upload");
    CKDWORD volumeMipPixel = 0xFF0000FFu;
    VxImageDescEx volumeMipImage = volumeImage;
    volumeMipImage.Width = volumeMipImage.Height = 1;
    volumeMipImage.BytesPerLine = 4;
    volumeMipImage.Image = (XBYTE *)&volumeMipPixel;
    TestCheck(ctx->LoadTexture(t.MirrorVolume, volumeMipImage, 1, CKRST_CUBEFACE_XPOS, NULL),
              "mirror volume mip upload");

    CKTextureDesc bumpDesc;
    VxPixelFormat2ImageDesc(_32_X8L8V8U8, bumpDesc.Format);
    bumpDesc.Format.Width = 4;
    bumpDesc.Format.Height = 4;
    bumpDesc.Format.BytesPerLine = 4 * 4;
    bumpDesc.MipMapCount = 3;
    bumpDesc.Flags = CKRST_TEXTURE_BUMPDUDV | CKRST_TEXTURE_BUMPLUMINANCE;
    TestCheck(ctx->CreateTexture(&bumpDesc, &t.BumpLuminance) && t.BumpLuminance != 0, "luminance bump texture");
    CKDWORD bumpPixels[16];
    for (int i = 0; i < 16; ++i)
        bumpPixels[i] = 0x00800000u; // L = 0.5, dU = dV = 0
    VxImageDescEx bumpImage = bumpDesc.Format;
    bumpImage.Image = (XBYTE *)bumpPixels;
    TestCheck(ctx->LoadTexture(t.BumpLuminance, bumpImage, 0, CKRST_CUBEFACE_XPOS, NULL), "bump mip 0 upload");
    VxImageDescEx bumpMip = bumpImage;
    bumpMip.Width = 2;
    bumpMip.Height = 2;
    bumpMip.BytesPerLine = 2 * 4;
    TestCheck(ctx->LoadTexture(t.BumpLuminance, bumpMip, 1, CKRST_CUBEFACE_XPOS, NULL), "bump mip 1 upload");
}

void DestroyTextures(CKRasterizerContext *ctx, Textures &t)
{
    if (t.Transform)
        ctx->DeleteObject(t.Transform, CKRST_OBJ_TEXTURE);
    if (t.BumpLuminance)
        ctx->DeleteObject(t.BumpLuminance, CKRST_OBJ_TEXTURE);
    if (t.MirrorVolume)
        ctx->DeleteObject(t.MirrorVolume, CKRST_OBJ_TEXTURE);
    t = Textures();
}

// ---------------------------------------------------------------------------
// Pixel cases (centre samples recorded for the shader-mode parity check)
// ---------------------------------------------------------------------------

enum SampleId {
    SAMPLE_FLAT = 0,
    SAMPLE_SATURATION,
    SAMPLE_TEMP_ALPHA,
    SAMPLE_TEMP_FINAL,
    SAMPLE_DEPTH_ORDER,
    SAMPLE_TEXTURE_MATRIX,
    SAMPLE_STAGEBLEND,
    SAMPLE_MIRROR_ONCE,
    SAMPLE_VOLUME_MIRROR_ONCE,
    SAMPLE_VOLUME_MIRROR_BORDER,
    SAMPLE_VOLUME_MIRROR_MIP,
    SAMPLE_BUMP_LUMINANCE,
    SAMPLE_TWEEN,
    SAMPLE_PIXEL_FOG,
    SAMPLE_CONSTANT,
    SAMPLE_RTT,
    SAMPLE_COUNT
};

struct Samples {
    CKBYTE Center[SAMPLE_COUNT][4];
    Samples() { memset(Center, 0, sizeof(Center)); }
};

void Record(Samples &s, SampleId id, const Pixels &pixels)
{
    GetPixel(pixels, kWidth / 2, kHeight / 2, s.Center[id]);
}

void RunPixelCases(CKRasterizerContext *ctx, const char *mode, Samples &samples)
{
    Textures textures;
    CreateTextures(ctx, textures);
    Pixels pixels;
    char what[128];

    // D3D8 flat shading uses the first vertex of a triangle as the provoking vertex.
    SetDiffuseState(ctx);
    ctx->SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_FLAT);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH, NULL, [&]() {
        const CKDWORD flatColors[3] = {0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu};
        TestCheck(DrawColorTriangle(ctx, kCenterTriangle, flatColors), "flat-shaded draw");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] flat shading provoking vertex", mode);
    ExpectCenter(pixels, 255, 0, 0, what);
    Record(samples, SAMPLE_FLAT, pixels);

    // Fixed-function texture stages saturate each result before it becomes CURRENT.
    SetDiffuseState(ctx);
    ctx->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE4X);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_CONSTANT);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_CONSTANT);
    ctx->SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_CONSTANT);
    ctx->SetTextureStageState(0, CKRST_TSS_CONSTANT, 0xFFBFBFBFu);
    ctx->SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_MODULATE);
    ctx->SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ctx->SetTextureStageState(1, CKRST_TSS_ARG2, CKRST_TA_CONSTANT);
    ctx->SetTextureStageState(1, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(1, CKRST_TSS_AARG1, CKRST_TA_CURRENT);
    ctx->SetTextureStageState(1, CKRST_TSS_CONSTANT, 0xFF404040u);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kWhite), "saturation draw");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] texture stage saturation", mode);
    ExpectCenter(pixels, 64, 64, 64, what);
    Record(samples, SAMPLE_SATURATION, pixels);

    // A disabled alpha operation preserves the selected TEMP destination alpha.
    SetDiffuseState(ctx);
    ctx->SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, TRUE);
    ctx->SetRenderState(VXRENDERSTATE_ALPHAFUNC, VXCMP_GREATER);
    ctx->SetRenderState(VXRENDERSTATE_ALPHAREF, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_CONSTANT);
    ctx->SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_DISABLE);
    ctx->SetTextureStageState(0, CKRST_TSS_RESULTARG0, CKRST_TA_TEMP);
    ctx->SetTextureStageState(0, CKRST_TSS_CONSTANT, 0xFFFF0000u);
    ctx->SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_TEMP);
    ctx->SetTextureStageState(1, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(1, CKRST_TSS_AARG1, CKRST_TA_TEMP);
    ctx->SetTextureStageState(1, CKRST_TSS_RESULTARG0, CKRST_TA_CURRENT);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kWhite), "TEMP alpha draw");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] disabled TEMP alpha preserves zero and fails the alpha test", mode);
    ExpectCenter(pixels, 0, 0, 0, what);
    Record(samples, SAMPLE_TEMP_ALPHA, pixels);

    // The framebuffer always consumes CURRENT, even when the final stage writes TEMP.
    SetDiffuseState(ctx);
    ctx->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ctx->SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_DIFFUSE);
    ctx->SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_CONSTANT);
    ctx->SetTextureStageState(1, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(1, CKRST_TSS_AARG1, CKRST_TA_CONSTANT);
    ctx->SetTextureStageState(1, CKRST_TSS_RESULTARG0, CKRST_TA_TEMP);
    ctx->SetTextureStageState(1, CKRST_TSS_CONSTANT, 0xFF0000FFu);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kGreen), "final TEMP draw");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] final TEMP write leaves CURRENT unchanged", mode);
    ExpectCenter(pixels, 0, 255, 0, what);
    Record(samples, SAMPLE_TEMP_FINAL, pixels);

    // A farther draw submitted second must fail the D3D-style LESS_EQUAL depth test.
    SetDiffuseState(ctx);
    ctx->SetRenderState(VXRENDERSTATE_ZENABLE, TRUE);
    ctx->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, TRUE);
    ctx->SetRenderState(VXRENDERSTATE_ZFUNC, VXCMP_LESSEQUAL);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH, NULL, [&]() {
        const VxVector nearTriangle[3] = {VxVector(-0.9f, -0.9f, 0.25f), VxVector(0.9f, -0.9f, 0.25f), VxVector(0.0f, 0.9f, 0.25f)};
        const VxVector farTriangle[3] = {VxVector(-0.9f, -0.9f, 0.75f), VxVector(0.9f, -0.9f, 0.75f), VxVector(0.0f, 0.9f, 0.75f)};
        TestCheck(DrawColorTriangle(ctx, nearTriangle, kGreen) && DrawColorTriangle(ctx, farTriangle, kRed), "depth-order draws");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] LESS_EQUAL depth test keeps the nearer draw", mode);
    ExpectCenter(pixels, 0, 255, 0, what);
    Record(samples, SAMPLE_DEPTH_ORDER, pixels);

    // Readback is top-first, independent of the backend framebuffer origin.
    SetDiffuseState(ctx);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        const VxVector upper[3] = {VxVector(-1.0f, 0.0f, 0.5f), VxVector(1.0f, 0.0f, 0.5f), VxVector(0.0f, 1.0f, 0.5f)};
        const VxVector lower[3] = {VxVector(-1.0f, 0.0f, 0.5f), VxVector(0.0f, -1.0f, 0.5f), VxVector(1.0f, 0.0f, 0.5f)};
        TestCheck(DrawColorTriangle(ctx, upper, kRed) && DrawColorTriangle(ctx, lower, kBlue), "orientation draws");
    }, pixels);
    TestCheckf(PixelNear(pixels, kWidth / 2, kHeight / 4, 255, 0, 0) &&
                   PixelNear(pixels, kWidth / 2, kHeight * 3 / 4, 0, 0, 255),
               "[%s] CopyToMemoryBuffer must return top-first rows", mode);

    // A float2 coordinate is extended with w = 1 before the texture matrix.
    SetDiffuseState(ctx);
    ctx->SetTexture(textures.Transform, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ctx->SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
    ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    ctx->SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSCLAMP);
    ctx->SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT2);
    VxMatrix textureMatrix;
    Vx3DMatrixIdentity(textureMatrix);
    textureMatrix[3][0] = 0.5f;
    ctx->SetTransformMatrix(VXMATRIX_TEXTURE0, textureMatrix);
    float translated[3][4] = {{0.25f, 0.5f, 0.0f, 0.0f}, {0.25f, 0.5f, 0.0f, 0.0f}, {0.25f, 0.5f, 0.0f, 0.0f}};
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, translated), "texture-matrix draw");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] texture matrix translation selects the green texel", mode);
    ExpectCenter(pixels, 0, 255, 0, what);
    Record(samples, SAMPLE_TEXTURE_MATRIX, pixels);

    // Luminance bump mapping modulates the next stage by L.
    SetDiffuseState(ctx);
    ctx->SetTexture(textures.BumpLuminance, 0);
    ctx->SetTexture(textures.Transform, 1);
    ctx->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_BUMPENVMAPLUMINANCE);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ctx->SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_CURRENT);
    ctx->SetTextureStageState(0, CKRST_TSS_BUMPENVLSCALE, FloatBits(1.0f));
    ctx->SetTextureStageState(0, CKRST_TSS_BUMPENVLOFFSET, FloatBits(0.0f));
    ctx->SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ctx->SetTextureStageState(1, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(1, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);
    ctx->SetTextureStageState(1, CKRST_TSS_TEXCOORDINDEX, 0);
    for (int stage = 0; stage < 2; ++stage) {
        ctx->SetTextureStageState(stage, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
        ctx->SetTextureStageState(stage, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
        ctx->SetTextureStageState(stage, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSCLAMP);
    }
    float bumpTexcoords[3][4] = {{0.25f, 0.5f, 0.0f, 1.0f}, {0.25f, 0.5f, 0.0f, 1.0f}, {0.25f, 0.5f, 0.0f, 1.0f}};
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, bumpTexcoords), "luminance bump draw");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] luminance bump modulates the environment texel", mode);
    ExpectCenter(pixels, 128, 0, 0, what);
    Record(samples, SAMPLE_BUMP_LUMINANCE, pixels);

    // STAGEBLEND(SRCCOLOR, DESTALPHA) has no equivalent public texture op.
    // The red texel contributes red^2 and the half-alpha blue vertex adds
    // half blue; MODULATE would instead produce black.
    SetDiffuseState(ctx);
    ctx->SetTexture(textures.Transform, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_STAGEBLEND,
                              STAGEBLEND(VXBLEND_SRCCOLOR, VXBLEND_DESTALPHA));
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
    ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    const CKDWORD halfBlue[3] = {0x800000FFu, 0x800000FFu, 0x800000FFu};
    float redTexcoords[3][4] = {
        {0.25f, 0.5f, 0.0f, 0.0f},
        {0.25f, 0.5f, 0.0f, 0.0f},
        {0.25f, 0.5f, 0.0f, 0.0f}
    };
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, halfBlue, redTexcoords),
                  "stage blend draw");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] arbitrary STAGEBLEND factors", mode);
    ExpectCenter(pixels, 255, 0, 128, what);
    Record(samples, SAMPLE_STAGEBLEND, pixels);

    // One corner has W=8. At (20, 40) affine U is 0.25 (red), while the
    // perspective-correct U is about 0.60 (green).
    SetDiffuseState(ctx);
    ctx->SetTexture(textures.Transform, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
    ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    float positionT[3][4] = {
        {8.0f, 8.0f, 0.5f, 1.0f},
        {56.0f, 8.0f, 0.5f, 1.0f},
        {8.0f, 56.0f, 0.5f, 0.125f}
    };
    float varyingTexcoords[3][4] = {
        {0.0f, 0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f}
    };
    ctx->SetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE, TRUE);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawTexturedPositionTTriangle(ctx, positionT, kWhite, varyingTexcoords),
                  "perspective texture draw");
    }, pixels);
    TestCheck(PixelNear(pixels, 20, 40, 0, 255, 0),
              "perspective texture interpolation selects the green texel");
    ctx->SetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE, FALSE);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawTexturedPositionTTriangle(ctx, positionT, kWhite, varyingTexcoords),
                  "affine texture draw");
    }, pixels);
    TestCheck(PixelNear(pixels, 20, 40, 255, 0, 0),
              "affine texture interpolation selects the red texel");

    SetDiffuseState(ctx);
    ctx->SetTexture(textures.Transform, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
    ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    ctx->SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSMIRRORONCE);
    float mirroredTexcoords[3][4] = {
        {-0.75f, 0.5f, 0.0f, 0.0f},
        {-0.75f, 0.5f, 0.0f, 0.0f},
        {-0.75f, 0.5f, 0.0f, 0.0f}
    };
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, mirroredTexcoords),
                  "MIRRORONCE draw");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] MIRRORONCE maps negative U to the reflected texel", mode);
    ExpectCenter(pixels, 0, 255, 0, what);
    Record(samples, SAMPLE_MIRROR_ONCE, pixels);

    SetDiffuseState(ctx);
    ctx->SetTexture(textures.MirrorVolume, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
    ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    ctx->SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSMIRRORONCE);
    ctx->SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT3);
    VxMatrix volumeTransform;
    Vx3DMatrixIdentity(volumeTransform);
    volumeTransform[3][2] = 0.25f;
    ctx->SetTransformMatrix(VXMATRIX_TEXTURE0, volumeTransform);
    float mirroredVolumeCoords[3][4] = {
        {-0.75f, 0.25f, 0.0f, 0.0f},
        {-0.75f, 0.25f, 0.0f, 0.0f},
        {-0.75f, 0.25f, 0.0f, 0.0f}
    };
    float directVolumeCoords[3][4] = {
        {0.75f, 0.25f, 0.0f, 0.0f},
        {0.75f, 0.25f, 0.0f, 0.0f},
        {0.75f, 0.25f, 0.0f, 0.0f}
    };
    ctx->SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSCLAMP);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, directVolumeCoords),
                  "volume direct sample draw");
    }, pixels);
    ExpectCenter(pixels, 0, 255, 0, "volume direct sample");
    ctx->SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSMIRRORONCE);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, mirroredVolumeCoords),
                  "volume MIRRORONCE draw");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] volume MIRRORONCE maps negative U to the reflected texel", mode);
    ExpectCenter(pixels, 0, 255, 0, what);
    Record(samples, SAMPLE_VOLUME_MIRROR_ONCE, pixels);

    ctx->SetTextureStageState(0, CKRST_TSS_ADDRESSV, VXTEXTURE_ADDRESSBORDER);
    ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0xFF0000FFu);
    float borderVolumeCoords[3][4] = {
        {-0.75f, -0.25f, 0.0f, 0.0f},
        {-0.75f, -0.25f, 0.0f, 0.0f},
        {-0.75f, -0.25f, 0.0f, 0.0f}
    };
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, borderVolumeCoords),
                  "volume MIRRORONCE and BORDER draw");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] volume MIRRORONCE keeps the other axis BORDER color", mode);
    ExpectCenter(pixels, 0, 0, 255, what);
    Record(samples, SAMPLE_VOLUME_MIRROR_BORDER, pixels);
    ctx->SetTextureStageState(0, CKRST_TSS_ADDRESSV, VXTEXTURE_ADDRESSMIRRORONCE);

    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_MIPNEAREST);
    float mipVolumeCoords[3][4] = {
        {-100.0f, 0.25f, 0.0f, 0.0f},
        {-200.0f, 0.25f, 0.0f, 0.0f},
        {-100.0f, 0.25f, 0.0f, 0.0f}
    };
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, mipVolumeCoords),
                  "volume MIRRORONCE mip draw");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] volume MIRRORONCE keeps the source mip footprint", mode);
    ExpectCenter(pixels, 0, 0, 255, what);
    Record(samples, SAMPLE_VOLUME_MIRROR_MIP, pixels);

    // Neither tween endpoint covers the centre; the half-way tween does.
    SetDiffuseState(ctx);
    ctx->SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_TWEENING);
    ctx->SetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE, TRUE);
    ctx->SetRenderState(VXRENDERSTATE_TWEENFACTOR, FloatBits(0.5f));
    const VxVector tweenFrom[3] = {VxVector(-1.9f, -0.9f, 0.5f), VxVector(-0.1f, -0.9f, 0.5f), VxVector(-1.0f, 0.9f, 0.5f)};
    const VxVector tweenTo[3] = {VxVector(0.1f, -0.9f, 0.5f), VxVector(1.9f, -0.9f, 0.5f), VxVector(1.0f, 0.9f, 0.5f)};
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawTweenTriangle(ctx, tweenFrom, tweenTo, kRed), "vertex tween draw");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] half-way vertex tween covers the centre", mode);
    ExpectCenter(pixels, 255, 0, 0, what);
    Record(samples, SAMPLE_TWEEN, pixels);
    ctx->SetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE, FALSE);

    // A normal-only tween must keep the original position when the factor is 1.
    ctx->SetRenderState(VXRENDERSTATE_TWEENFACTOR, FloatBits(1.0f));
    VxVector tweenNormals[3] = {VxVector(0, 0, 1), VxVector(0, 0, 1), VxVector(0, 0, 1)};
    VxDrawPrimitiveData normalTween = {};
    normalTween.VertexCount = 3;
    normalTween.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_DIFFUSE | CKRST_DP_TWEEN;
    normalTween.PositionPtr = const_cast<VxVector *>(kCenterTriangle);
    normalTween.PositionStride = sizeof(VxVector);
    normalTween.NormalPtr = tweenNormals;
    normalTween.NormalStride = sizeof(VxVector);
    normalTween.TweenNormalPtr = tweenNormals;
    normalTween.TweenNormalStride = sizeof(VxVector);
    normalTween.ColorPtr = const_cast<CKDWORD *>(kRed);
    normalTween.ColorStride = sizeof(CKDWORD);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &normalTween),
                  "normal-only tween draw");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] normal-only tween preserves position", mode);
    ExpectCenter(pixels, 255, 0, 0, what);

    SetDiffuseState(ctx);
    const CKDWORD blendFormat = CKRST_DP_TRANSFORM | CKRST_DP_WEIGHTS1 |
                                CKRST_DP_MATRIXPAL | CKRST_DP_DIFFUSE;
    CKRSTVertexLayout blendLayout;
    const CKDWORD blendStride = CKRSTGetVertexLayout(blendFormat, NULL, &blendLayout);
    std::vector<CKBYTE> blendVertices(blendStride * 3, 0);
    for (CKDWORD i = 0; i < 3; ++i) {
        const float weight = 0.5f;
        const CKDWORD matrixIndices = 0x00000100u;
        CKBYTE *vertex = blendVertices.data() + i * blendStride;
        memcpy(vertex + blendLayout.PositionOffset, &kCenterTriangle[i], sizeof(VxVector));
        memcpy(vertex + blendLayout.WeightOffset, &weight, sizeof(weight));
        memcpy(vertex + blendLayout.BlendIndexOffset, &matrixIndices, sizeof(matrixIndices));
        memcpy(vertex + blendLayout.DiffuseOffset, &kRed[i], sizeof(CKDWORD));
    }
    CKVertexBufferDesc blendVBDesc;
    blendVBDesc.m_VertexFormat = blendFormat;
    blendVBDesc.m_MaxVertexCount = 3;
    blendVBDesc.m_Flags = CKRST_VB_WRITEONLY;
    CKDWORD blendVB = 0;
    TestCheck(ctx->CreateVertexBuffer(&blendVBDesc, blendVertices.data(), &blendVB),
              "create indexed-blend write-only VB");
    CKIndexBufferDesc blendIBDesc;
    blendIBDesc.m_MaxIndexCount = 3;
    blendIBDesc.m_Flags = CKRST_VB_WRITEONLY;
    CKWORD blendIndices[3] = {0, 1, 2};
    CKDWORD blendIB = 0;
    TestCheck(ctx->CreateIndexBuffer(&blendIBDesc, blendIndices, &blendIB),
              "create indexed-blend write-only IB");
    ctx->SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_1WEIGHTS);
    ctx->SetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE, TRUE);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitiveVB(VX_TRIANGLELIST, blendVB, 0, 3, NULL, 0),
                  "indexed-blend VB draw");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] indexed-blend VB uses validated palette", mode);
    ExpectCenter(pixels, 255, 0, 0, what);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitiveVBIB(VX_TRIANGLELIST, blendVB, blendIB, 0, 3, 0, 3),
                  "indexed-blend VBIB draw");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] indexed-blend VBIB uses validated palette", mode);
    ExpectCenter(pixels, 255, 0, 0, what);
    TestCheck(ReadStats(ctx).Diagnostics[CKRST_DIAG_APPROX_VERTEX_BLEND_PALETTE] == 0,
              "validated indexed-blend buffers need no palette approximation");
    TestCheck(ctx->DeleteObject(blendIB, CKRST_OBJ_INDEXBUFFER),
              "delete indexed-blend IB");
    TestCheck(ctx->DeleteObject(blendVB, CKRST_OBJ_VERTEXBUFFER),
              "delete indexed-blend VB");

    // Table fog consumes eye-space depth; projection-space z/w would leave this nearly white.
    SetDiffuseState(ctx);
    VxMatrix fogProjection;
    Vx3DMatrixIdentity(fogProjection);
    fogProjection[2][2] = 0.01f;
    ctx->SetRenderState(VXRENDERSTATE_FOGENABLE, TRUE);
    ctx->SetRenderState(VXRENDERSTATE_FOGVERTEXMODE, VXFOG_NONE);
    ctx->SetRenderState(VXRENDERSTATE_FOGPIXELMODE, VXFOG_LINEAR);
    ctx->SetRenderState(VXRENDERSTATE_FOGSTART, FloatBits(0.0f));
    ctx->SetRenderState(VXRENDERSTATE_FOGEND, FloatBits(20.0f));
    ctx->SetRenderState(VXRENDERSTATE_FOGCOLOR, 0xFF000000u);
    const VxVector fogTriangle[3] = {VxVector(-0.9f, -0.9f, 10.0f), VxVector(0.9f, -0.9f, 10.0f), VxVector(0.0f, 0.9f, 10.0f)};
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, &fogProjection, [&]() {
        TestCheck(DrawColorTriangle(ctx, fogTriangle, kWhite), "pixel fog draw");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] pixel fog uses eye-space depth", mode);
    ExpectCenter(pixels, 128, 128, 128, what);
    Record(samples, SAMPLE_PIXEL_FOG, pixels);

    // An untextured stage reading CONSTANT stays active.
    SetDiffuseState(ctx);
    ctx->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_CONSTANT);
    ctx->SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_CONSTANT);
    ctx->SetTextureStageState(0, CKRST_TSS_CONSTANT, 0x80402010u);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kWhite), "untextured constant draw");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] untextured constant stage", mode);
    ExpectCenter(pixels, 64, 32, 16, what);
    Record(samples, SAMPLE_CONSTANT, pixels);

    // Render to texture, then sample the texture on the backbuffer.
    SetDiffuseState(ctx);
    CKTextureDesc targetDesc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, targetDesc.Format);
    targetDesc.Format.Width = kWidth;
    targetDesc.Format.Height = kHeight;
    targetDesc.Format.BytesPerLine = kWidth * 4;
    targetDesc.MipMapCount = 1;
    targetDesc.Flags = CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_RENDERTARGET;
    CKDWORD target = 0;
    TestCheck(ctx->CreateTexture(&targetDesc, &target) && target != 0, "render target texture");
    TestCheck(ctx->SetTargetTexture(target, 0, 0, CKRST_CUBEFACE_XPOS), "SetTargetTexture");
    const VxVector fullQuad[3] = {VxVector(-3.0f, -3.0f, 0.5f), VxVector(3.0f, -3.0f, 0.5f), VxVector(0.0f, 3.0f, 0.5f)};
    BeginFrame(ctx, CKRST_CTXCLEAR_COLOR);
    TestCheck(DrawColorTriangle(ctx, fullQuad, kBlue), "draw into the render target");
    EndFrame(ctx);
    TestCheck(ctx->SetTargetTexture(0, 0, 0, CKRST_CUBEFACE_XPOS), "back to the backbuffer");
    ctx->SetTexture(target, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ctx->SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
    ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    ctx->SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSCLAMP);
    float centerTexcoords[3][4] = {{0.5f, 0.5f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.0f, 0.0f}};
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, centerTexcoords), "draw sampling the render target");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] render-to-texture round trip", mode);
    ExpectCenter(pixels, 0, 0, 255, what);
    Record(samples, SAMPLE_RTT, pixels);
    ctx->SetTexture(0, 0);
    TestCheck(ctx->DeleteObject(target, CKRST_OBJ_TEXTURE), "delete render target");

    SetDiffuseState(ctx);
    DestroyTextures(ctx, textures);
}

// Ordered updates and copies must preserve the values sampled by earlier draws.
void CheckOrderedTextureUpdates(Backend &b)
{
    auto *ctx = b.Context;
    SetDiffuseState(ctx);
    Textures textures;
    CreateTextures(ctx, textures);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ctx->SetTexture(textures.Transform, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
    ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    const VxVector covering[3] = {VxVector(-1,-1,0.5f), VxVector(3,-1,0.5f), VxVector(-1,3,0.5f)};
    float uv[3][4] = {{0.25f,0.5f,0,0}, {0.25f,0.5f,0,0}, {0.25f,0.5f,0,0}};
    CKViewportData viewport = {};
    viewport.ViewWidth = 20; viewport.ViewHeight = 64; viewport.ViewZMax = 1;
    Pixels pixels;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->SetViewport(&viewport), "first update viewport");
        TestCheck(DrawTexturedTriangle(ctx, covering, kWhite, uv), "sample before update");
        CKDWORD blue = 0xff0000ff;
        VxImageDescEx patch;
        VxPixelFormat2ImageDesc(_32_ARGB8888, patch);
        patch.Width = patch.Height = 1; patch.BytesPerLine = 4;
        patch.Image = reinterpret_cast<CKBYTE *>(&blue);
        CKRECT region = {0, 0, 1, 1};
        TestCheck(ctx->LoadTexture(textures.Transform, patch, 0, CKRST_CUBEFACE_XPOS, &region), "patch first texel after draw");
        viewport.ViewX = 20;
        TestCheck(ctx->SetViewport(&viewport), "second update viewport");
        TestCheck(DrawTexturedTriangle(ctx, covering, kWhite, uv), "sample updated texel");
        viewport.ViewX = 40;
        TestCheck(ctx->SetViewport(&viewport), "third update viewport");
        for (auto &coord : uv) coord[0] = 0.75f;
        TestCheck(DrawTexturedTriangle(ctx, covering, kWhite, uv), "sample preserved texel");
        viewport.ViewX = 0; viewport.ViewWidth = 64;
        TestCheck(ctx->SetViewport(&viewport), "restore update viewport");
    }, pixels);
    for (int x : {10,30,50}) {
        CKBYTE bgra[4]; GetPixel(pixels, x, 32, bgra);
        printf("  update region x=%d RGB=%u,%u,%u\n", x, unsigned(bgra[2]), unsigned(bgra[1]), unsigned(bgra[0]));
    }
    TestCheck(PixelNear(pixels, 10, 32, 255, 0, 0), "draw before update retains red");
    TestCheck(PixelNear(pixels, 30, 32, 0, 0, 255), "draw after update sees blue");
    TestCheck(PixelNear(pixels, 50, 32, 0, 255, 0), "partial texture update preserves green texel");
    DestroyTextures(ctx, textures);
    SetDiffuseState(ctx);
    printf("  ordered texture patch: old red / new blue / preserved green\n");
}

void CheckPaddedTextureUpload(Backend &b)
{
    auto *ctx = b.Context;
    SetDiffuseState(ctx);
    CKTextureDesc desc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
    desc.Format.Width = desc.Format.Height = 2;
    desc.Format.BytesPerLine = 16; // A valid row pitch, even when >= packed image size.
    desc.MipMapCount = 1;
    desc.Flags = CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
    CKDWORD texture = 0;
    TestCheck(ctx->CreateTexture(&desc, &texture), "create padded source texture");
    CKDWORD source[] = {0xffff0000, 0xff00ff00, 0xffffff00, 0xffffff00,
                        0xff0000ff, 0xffffffff, 0xffffff00, 0xffffff00};
    VxImageDescEx image = desc.Format;
    image.Image = reinterpret_cast<CKBYTE *>(source);
    TestCheck(ctx->LoadTexture(texture, image, 0, CKRST_CUBEFACE_XPOS, nullptr), "upload padded rows");
    ctx->SetTexture(texture, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
    ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    float uv[3][4] = {{0.25f,0.75f,0,0}, {0.25f,0.75f,0,0}, {0.25f,0.75f,0,0}};
    Pixels pixels;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, uv), "sample second padded row");
    }, pixels);
    ExpectCenter(pixels, 0, 0, 255, "uncompressed BytesPerLine must remain a row pitch");
    ctx->SetTexture(0, 0);
    TestCheck(ctx->DeleteObject(texture, CKRST_OBJ_TEXTURE), "delete padded texture");
    SetDiffuseState(ctx);
    printf("  padded image rows preserve the declared pitch: passed\n");
}

void CheckLineTopologyWithPointFill(Backend &b)
{
    CKRasterizerContext *ctx = b.Context;
    SetDiffuseState(ctx);
    ctx->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_POINT);
    const VxVector positions[2] = {
        VxVector(-0.8f, 0.0f, 0.5f), VxVector(0.8f, 0.0f, 0.5f)};
    const CKDWORD colors[2] = {0xff00ff00u, 0xff00ff00u};
    VxDrawPrimitiveData data = {};
    data.VertexCount = 2;
    data.Flags = CKRST_DP_TR_VC;
    data.PositionPtr = const_cast<VxVector *>(positions);
    data.PositionStride = sizeof(VxVector);
    data.ColorPtr = const_cast<CKDWORD *>(colors);
    data.ColorStride = sizeof(CKDWORD);
    Pixels pixels;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitive(VX_LINELIST, NULL, 0, &data),
                  "line with point polygon fill submits");
    }, pixels);
    int greenPixels = 0;
    for (int y = 30; y <= 34; ++y)
        for (int x = 8; x <= 56; ++x)
            if (PixelNear(pixels, x, y, 0, 255, 0))
                ++greenPixels;
    TestCheckf(greenPixels > 20,
               "point polygon fill must preserve the line segment (%d green pixels)",
               greenPixels);
    ctx->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);
}

void CheckVertexBufferPointFilledStripsAndFans(Backend &b)
{
    CKRasterizerContext *ctx = b.Context;
    SetDiffuseState(ctx);
    const CKDWORD format = CKRST_DP_TR_VC;
    CKRSTVertexLayout layout;
    const CKDWORD stride = CKRSTGetVertexLayout(format, NULL, &layout);
    const VxVector positions[4] = {
        VxVector(-0.75f, -0.75f, 0.5f), VxVector(0.75f, -0.75f, 0.5f),
        VxVector(-0.75f, 0.75f, 0.5f), VxVector(0.75f, 0.75f, 0.5f)};
    const CKDWORD colors[4] = {0xff400000u, 0xff400000u,
                               0xff400000u, 0xff400000u};
    XArray<CKBYTE> vertices;
    vertices.Resize(stride * 4);
    memset(vertices.Begin(), 0, vertices.Size());
    for (int i = 0; i < 4; ++i) {
        CKBYTE *vertex = vertices.Begin() + i * stride;
        memcpy(vertex + layout.PositionOffset, &positions[i], sizeof(VxVector));
        memcpy(vertex + layout.DiffuseOffset, &colors[i], sizeof(CKDWORD));
    }
    CKVertexBufferDesc vbDesc;
    vbDesc.m_VertexFormat = format;
    vbDesc.m_MaxVertexCount = 4;
    CKDWORD vb = 0;
    TestCheck(ctx->CreateVertexBuffer(&vbDesc, vertices.Begin(), &vb),
              "create point-filled strip/fan VB");
    const CKWORD indices[4] = {0, 1, 2, 3};
    CKIndexBufferDesc ibDesc;
    ibDesc.m_MaxIndexCount = 4;
    CKDWORD ib = 0;
    TestCheck(ctx->CreateIndexBuffer(&ibDesc, indices, &ib),
              "create point-filled strip/fan IB");

    VxDrawPrimitiveData data = {};
    data.VertexCount = 4;
    data.Flags = format;
    data.PositionPtr = const_cast<VxVector *>(positions);
    data.PositionStride = sizeof(VxVector);
    data.ColorPtr = const_cast<CKDWORD *>(colors);
    data.ColorStride = sizeof(CKDWORD);
    ctx->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_POINT);
    ctx->SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, TRUE);
    ctx->SetRenderState(VXRENDERSTATE_SRCBLEND, VXBLEND_ONE);
    ctx->SetRenderState(VXRENDERSTATE_DESTBLEND, VXBLEND_ONE);

    for (VXPRIMITIVETYPE type : {VX_TRIANGLESTRIP, VX_TRIANGLEFAN}) {
        Pixels reference, candidate;
        RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
            TestCheck(ctx->DrawPrimitive(type, NULL, 0, &data),
                      "draw point-filled strip/fan reference");
        }, reference);
        int redPixels = 0;
        for (int i = 2; i < reference.Data.Size(); i += 4)
            redPixels += reference.Data[i] != 0;
        TestCheck(redPixels >= 4, "point-filled strip/fan reference covers vertices");

        auto matchesReference = [&]() {
            return candidate.Width == reference.Width &&
                   candidate.Height == reference.Height &&
                   candidate.Data.Size() == reference.Data.Size() &&
                   memcmp(candidate.Data.Begin(), reference.Data.Begin(),
                          reference.Data.Size()) == 0;
        };
        RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
            TestCheck(ctx->DrawPrimitiveVB(type, vb, 0, 4, NULL, 0),
                      "draw point-filled strip/fan VB");
        }, candidate);
        TestCheck(matchesReference(), "point-filled strip/fan VB matches transient draw");
        RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
            TestCheck(ctx->DrawPrimitiveVB(type, vb, 0, 4,
                                           const_cast<CKWORD *>(indices), 4),
                      "draw indexed point-filled strip/fan VB");
        }, candidate);
        TestCheck(matchesReference(), "indexed point-filled strip/fan VB matches transient draw");
        RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
            TestCheck(ctx->DrawPrimitiveVBIB(type, vb, ib, 0, 4, 0, 4),
                      "draw point-filled strip/fan VBIB");
        }, candidate);
        TestCheck(matchesReference(), "point-filled strip/fan VBIB matches transient draw");
    }

    ctx->SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, FALSE);
    ctx->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);
    TestCheck(ctx->DeleteObject(ib, CKRST_OBJ_INDEXBUFFER), "delete point-fill IB");
    TestCheck(ctx->DeleteObject(vb, CKRST_OBJ_VERTEXBUFFER), "delete point-fill VB");
    printf("  point-filled VB strips and fans preserve per-triangle vertices: passed\n");
}

void CheckPointFillTriangleCulling(Backend &b)
{
    CKRasterizerContext *ctx = b.Context;
    SetDiffuseState(ctx);
    const CKDWORD format = CKRST_DP_TR_VC;
    const VxVector positions[3] = {
        VxVector(-0.5f, -0.5f, 0.5f), VxVector(0.5f, -0.5f, 0.5f),
        VxVector(-0.5f, 0.5f, 0.5f)};
    const CKDWORD colors[3] = {0xffff0000u, 0xffff0000u, 0xffff0000u};
    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = format;
    data.PositionPtr = const_cast<VxVector *>(positions);
    data.PositionStride = sizeof(VxVector);
    data.ColorPtr = const_cast<CKDWORD *>(colors);
    data.ColorStride = sizeof(CKDWORD);

    CKRSTVertexLayout layout;
    const CKDWORD stride = CKRSTGetVertexLayout(format, NULL, &layout);
    XArray<CKBYTE> vertices;
    vertices.Resize(stride * 3);
    memset(vertices.Begin(), 0, vertices.Size());
    for (int i = 0; i < 3; ++i) {
        CKBYTE *vertex = vertices.Begin() + i * stride;
        memcpy(vertex + layout.PositionOffset, &positions[i], sizeof(VxVector));
        memcpy(vertex + layout.DiffuseOffset, &colors[i], sizeof(CKDWORD));
    }
    CKVertexBufferDesc vbDesc;
    vbDesc.m_VertexFormat = format;
    vbDesc.m_MaxVertexCount = 3;
    CKDWORD vb = 0;
    TestCheck(ctx->CreateVertexBuffer(&vbDesc, vertices.Begin(), &vb),
              "create point-fill culling VB");
    const CKWORD triangleIndices[3] = {0, 1, 2};
    CKIndexBufferDesc ibDesc;
    ibDesc.m_MaxIndexCount = 3;
    CKDWORD ib = 0;
    TestCheck(ctx->CreateIndexBuffer(&ibDesc, triangleIndices, &ib),
              "create point-fill culling IB");

    auto redCount = [](const Pixels &pixels) {
        int count = 0;
        for (int y = 0; y < pixels.Height; ++y) {
            for (int x = 0; x < pixels.Width; ++x) {
                CKBYTE bgra[4];
                GetPixel(pixels, x, y, bgra);
                if (bgra[2] > 180 && bgra[1] < 50 && bgra[0] < 50)
                    ++count;
            }
        }
        return count;
    };
    auto render = [&](CKDWORD fill, CKDWORD cull, CKBOOL inverse,
                      int source, Pixels &pixels) {
        ctx->SetRenderState(VXRENDERSTATE_FILLMODE, fill);
        ctx->SetRenderState(VXRENDERSTATE_CULLMODE, cull);
        ctx->SetRenderState(VXRENDERSTATE_INVERSEWINDING, inverse);
        RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
            if (source == 1)
                TestCheck(ctx->DrawPrimitiveVB(VX_TRIANGLELIST, vb, 0, 3, NULL, 0),
                          "draw point-fill culling VB");
            else if (source == 2)
                TestCheck(ctx->DrawPrimitiveVBIB(VX_TRIANGLELIST, vb, ib, 0, 3, 0, 3),
                          "draw point-fill culling VBIB");
            else
                TestCheckf(ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data),
                           "draw point-fill culling primitive fill=%u cull=%u",
                           (unsigned)fill, (unsigned)cull);
        }, pixels);
    };
    Pixels none;
    render(VXFILL_POINT, VXCULL_NONE, FALSE, 0, none);
    TestCheck(redCount(none) >= 3, "unculled point fill draws all triangle vertices");
    int visibleFaces = 0;
    for (CKDWORD cull : {VXCULL_CW, VXCULL_CCW}) {
        Pixels solid, point, inverted, vbPixels, ibPixels;
        render(VXFILL_SOLID, cull, FALSE, 0, solid);
        render(VXFILL_POINT, cull, FALSE, 0, point);
        const bool visible = redCount(solid) > 0;
        visibleFaces += visible ? 1 : 0;
        TestCheck((redCount(point) > 0) == visible,
                  "point fill culls the same face as solid triangles");
        render(VXFILL_POINT, cull, TRUE, 0, inverted);
        TestCheck((redCount(inverted) > 0) != visible,
                  "inverse winding reverses point-fill face culling");
        render(VXFILL_POINT, cull, FALSE, 1, vbPixels);
        render(VXFILL_POINT, cull, FALSE, 2, ibPixels);
        TestCheck(vbPixels.Data.Size() == point.Data.Size() &&
                  memcmp(vbPixels.Data.Begin(), point.Data.Begin(), point.Data.Size()) == 0,
                  "point-filled VB triangles match transient face culling");
        TestCheck(ibPixels.Data.Size() == point.Data.Size() &&
                  memcmp(ibPixels.Data.Begin(), point.Data.Begin(), point.Data.Size()) == 0,
                  "point-filled VBIB triangles match transient face culling");
    }
    TestCheck(visibleFaces == 1, "opposite cull modes select opposite triangle faces");

    ctx->SetRenderState(VXRENDERSTATE_POINTSIZE, FloatBits(20.0f));
    for (CKDWORD cull : {VXCULL_CW, VXCULL_CCW}) {
        Pixels solid, point;
        render(VXFILL_SOLID, cull, FALSE, 0, solid);
        render(VXFILL_POINT, cull, FALSE, 0, point);
        TestCheck((redCount(point) > 0) == (redCount(solid) > 0),
                  "expanded point quads preserve source triangle face culling");
    }
    ctx->SetRenderState(VXRENDERSTATE_POINTSIZE, FloatBits(1.0f));

    float screenPositions[3][4] = {
        {16.0f, 48.0f, 0.5f, 1.0f}, {48.0f, 48.0f, 0.5f, 1.0f},
        {16.0f, 16.0f, 0.5f, 1.0f}};
    VxDrawPrimitiveData screenData = {};
    screenData.VertexCount = 3;
    screenData.Flags = CKRST_DP_CL_VCT;
    screenData.PositionPtr = screenPositions;
    screenData.PositionStride = sizeof(screenPositions[0]);
    screenData.ColorPtr = const_cast<CKDWORD *>(colors);
    screenData.ColorStride = sizeof(CKDWORD);
    for (CKDWORD cull : {VXCULL_CW, VXCULL_CCW}) {
        Pixels solid, point;
        for (int fill = 0; fill < 2; ++fill) {
            ctx->SetRenderState(VXRENDERSTATE_FILLMODE,
                                fill ? VXFILL_POINT : VXFILL_SOLID);
            ctx->SetRenderState(VXRENDERSTATE_CULLMODE, cull);
            ctx->SetRenderState(VXRENDERSTATE_INVERSEWINDING, FALSE);
            RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
                TestCheck(ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &screenData),
                          "draw POSITIONT point-fill culling primitive");
            }, fill ? point : solid);
        }
        TestCheck((redCount(point) > 0) == (redCount(solid) > 0),
                  "POSITIONT point fill culls the same face as solid triangles");
    }

    VxMatrix mirror;
    Vx3DMatrixIdentity(mirror);
    mirror[0][0] = -1.0f;
    TestCheck(ctx->SetTransformMatrix(VXMATRIX_WORLD, mirror), "set matrix-blend world 0");
    TestCheck(ctx->SetTransformMatrix(VXMATRIX_WORLDMATRIX(1), mirror),
              "set matrix-blend world 1");
    struct BlendPosition {
        VxVector Position;
        float Weight;
        CKDWORD Indices;
    } blendPositions[3];
    for (int i = 0; i < 3; ++i) {
        blendPositions[i].Position = positions[i];
        blendPositions[i].Weight = 0.5f;
        blendPositions[i].Indices = 0x00000100u;
    }
    VxDrawPrimitiveData blendData = {};
    blendData.VertexCount = 3;
    blendData.Flags = CKRST_DP_TR_VC | CKRST_DP_WEIGHTS1 | CKRST_DP_MATRIXPAL;
    blendData.PositionPtr = blendPositions;
    blendData.PositionStride = sizeof(BlendPosition);
    blendData.ColorPtr = const_cast<CKDWORD *>(colors);
    blendData.ColorStride = sizeof(CKDWORD);
    ctx->SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_1WEIGHTS);
    ctx->SetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE, TRUE);
    Pixels blendUnculled;
    ctx->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);
    ctx->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &blendData),
                  "draw unculled matrix-blended triangle");
    }, blendUnculled);
    TestCheck(redCount(blendUnculled) > 0 &&
                  PixelNear(blendUnculled, 44, 20, 255, 0, 0),
              "indexed matrix blend uses palette slot one");
    ctx->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_POINT);
    for (CKDWORD cull : {VXCULL_CW, VXCULL_CCW}) {
        Pixels solid, point;
        for (int fill = 0; fill < 2; ++fill) {
            ctx->SetRenderState(VXRENDERSTATE_FILLMODE,
                                fill ? VXFILL_POINT : VXFILL_SOLID);
            ctx->SetRenderState(VXRENDERSTATE_CULLMODE, cull);
            RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
                TestCheck(ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &blendData),
                          "draw matrix-blended point-fill culling primitive");
            }, fill ? point : solid);
        }
        TestCheckf((redCount(point) > 0) == (redCount(solid) > 0),
                   "matrix-blended point fill culls transformed face cull=%u solid=%d point=%d",
                   (unsigned)cull, redCount(solid), redCount(point));
    }
    Vx3DMatrixIdentity(mirror);
    ctx->SetTransformMatrix(VXMATRIX_WORLD, mirror);
    ctx->SetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE, FALSE);

    VxVector tweenPositions[3] = {
        VxVector(-positions[0].x, positions[0].y, positions[0].z),
        VxVector(-positions[1].x, positions[1].y, positions[1].z),
        VxVector(-positions[2].x, positions[2].y, positions[2].z)};
    ctx->SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_TWEENING);
    ctx->SetRenderState(VXRENDERSTATE_TWEENFACTOR, FloatBits(1.0f));
    for (CKDWORD cull : {VXCULL_CW, VXCULL_CCW}) {
        Pixels solid, point;
        for (int fill = 0; fill < 2; ++fill) {
            ctx->SetRenderState(VXRENDERSTATE_FILLMODE,
                                fill ? VXFILL_POINT : VXFILL_SOLID);
            ctx->SetRenderState(VXRENDERSTATE_CULLMODE, cull);
            RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
                TestCheck(DrawTweenTriangle(ctx, positions, tweenPositions, colors),
                          "draw tweened point-fill culling primitive");
            }, fill ? point : solid);
        }
        TestCheck((redCount(point) > 0) == (redCount(solid) > 0),
                  "tweened point fill culls the interpolated face");
    }

    ctx->SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_DISABLE);
    ctx->SetRenderState(VXRENDERSTATE_TWEENFACTOR, FloatBits(0.0f));
    ctx->SetRenderState(VXRENDERSTATE_INVERSEWINDING, FALSE);
    ctx->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
    ctx->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);
    TestCheck(ctx->DeleteObject(ib, CKRST_OBJ_INDEXBUFFER), "delete point-fill culling IB");
    TestCheck(ctx->DeleteObject(vb, CKRST_OBJ_VERTEXBUFFER), "delete point-fill culling VB");
    printf("  point-filled triangle face culling matches solid triangles and VB paths: passed\n");
}

void CheckPointFilledTriangleSizes(Backend &b)
{
    CKRasterizerContext *ctx = b.Context;
    SetDiffuseState(ctx);
    struct SizedPosition {
        VxVector Position;
        float Size;
    } positions[3] = {
        {VxVector(-0.5f, -0.5f, 0.5f), 20.0f},
        {VxVector( 0.5f, -0.5f, 0.5f),  1.0f},
        {VxVector(-0.5f,  0.5f, 0.5f),  1.0f}
    };
    const CKDWORD colors[3] = {0xffff0000u, 0xffff0000u, 0xffff0000u};
    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TR_VC;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(SizedPosition);
    data.ColorPtr = const_cast<CKDWORD *>(colors);
    data.ColorStride = sizeof(CKDWORD);
    ctx->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_POINT);
    ctx->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);

    Pixels pixels;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data),
                  "draw one-pixel filled triangle");
    }, pixels);
    TestCheck(PixelNear(pixels, 22, 48, 0, 0, 0),
              "default point-filled triangles stay one pixel wide");

    const CKDWORD before = ReadStats(ctx).Diagnostics[CKRST_DIAG_APPROX_FILLMODE_POINT];
    ctx->SetRenderState(VXRENDERSTATE_POINTSIZE, FloatBits(20.0f));
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data),
                  "draw 20-pixel filled triangle");
    }, pixels);
    TestCheck(PixelNear(pixels, 22, 48, 255, 0, 0) &&
              PixelNear(pixels, 28, 48, 0, 0, 0),
              "point-filled triangles honor the constant point size");
    TestCheck(ReadStats(ctx).Diagnostics[CKRST_DIAG_APPROX_FILLMODE_POINT] == before,
              "constant-size point fill has no approximation");

    ctx->SetRenderState(VXRENDERSTATE_POINTSIZE, FloatBits(1.0f));
    data.Flags |= CKRST_DP_PSIZE;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data),
                  "draw per-vertex-sized filled triangle");
    }, pixels);
    TestCheck(PixelNear(pixels, 22, 48, 255, 0, 0) &&
              PixelNear(pixels, 42, 48, 0, 0, 0),
              "point-filled triangles honor per-vertex PSIZE");

    const CKDWORD format = CKRST_DP_TR_VC | CKRST_DP_PSIZE;
    CKRSTVertexLayout layout;
    const CKDWORD stride = CKRSTGetVertexLayout(format, NULL, &layout);
    XArray<CKBYTE> vertices;
    vertices.Resize(stride * 3);
    memset(vertices.Begin(), 0, vertices.Size());
    for (int i = 0; i < 3; ++i) {
        CKBYTE *vertex = vertices.Begin() + i * stride;
        memcpy(vertex + layout.PositionOffset, &positions[i].Position, sizeof(VxVector));
        memcpy(vertex + layout.PointSizeOffset, &positions[i].Size, sizeof(float));
        memcpy(vertex + layout.DiffuseOffset, &colors[i], sizeof(CKDWORD));
    }
    CKVertexBufferDesc vbDesc;
    vbDesc.m_VertexFormat = format;
    vbDesc.m_MaxVertexCount = 3;
    CKDWORD vb = 0;
    TestCheck(ctx->CreateVertexBuffer(&vbDesc, vertices.Begin(), &vb),
              "create point-sized triangle VB");
    Pixels vbPixels;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitiveVB(VX_TRIANGLELIST, vb, 0, 3, NULL, 0),
                  "draw point-sized triangle VB");
    }, vbPixels);
    TestCheck(vbPixels.Data.Size() == pixels.Data.Size() &&
              memcmp(vbPixels.Data.Begin(), pixels.Data.Begin(), pixels.Data.Size()) == 0,
              "point-sized triangle VB matches transient geometry");
    TestCheck(ReadStats(ctx).Diagnostics[CKRST_DIAG_APPROX_FILLMODE_POINT] == before,
              "PSIZE point fill has no approximation on either draw path");

    const CKWORD triangleIndices[3] = {0, 1, 2};
    CKIndexBufferDesc ibDesc;
    ibDesc.m_MaxIndexCount = 3;
    CKDWORD ib = 0;
    TestCheck(ctx->CreateIndexBuffer(&ibDesc, triangleIndices, &ib),
              "create point-sized triangle IB");
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitiveVBIB(VX_TRIANGLELIST, vb, ib, 0, 3, 0, 3),
                  "draw point-sized triangle VBIB");
    }, vbPixels);
    TestCheck(vbPixels.Data.Size() == pixels.Data.Size() &&
              memcmp(vbPixels.Data.Begin(), pixels.Data.Begin(), pixels.Data.Size()) == 0,
              "point-sized triangle VBIB matches transient geometry");
    TestCheck(ctx->DeleteObject(ib, CKRST_OBJ_INDEXBUFFER),
              "delete point-sized triangle IB");
    TestCheck(ctx->DeleteObject(vb, CKRST_OBJ_VERTEXBUFFER),
              "delete point-sized triangle VB");

    data.Flags = CKRST_DP_TR_VC;
    ctx->SetRenderState(VXRENDERSTATE_POINTSIZE, FloatBits(0.25f));
    ctx->SetRenderState(VXRENDERSTATE_POINTSCALEENABLE, TRUE);
    ctx->SetRenderState(VXRENDERSTATE_POINTSCALE_A, FloatBits(1.0f));
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data),
                  "draw distance-scaled point-filled triangle");
    }, pixels);
    TestCheck(PixelNear(pixels, 22, 48, 255, 0, 0) &&
              PixelNear(pixels, 26, 48, 0, 0, 0),
              "point-filled triangles apply viewport-height point scaling");
    ctx->SetRenderState(VXRENDERSTATE_POINTSCALEENABLE, FALSE);
    ctx->SetRenderState(VXRENDERSTATE_POINTSIZE, FloatBits(20.0f));

    float screenPositions[3][4] = {
        {16.0f, 48.0f, 0.5f, 1.0f},
        {48.0f, 48.0f, 0.5f, 1.0f},
        {16.0f, 16.0f, 0.5f, 1.0f}
    };
    VxDrawPrimitiveData screenData = {};
    screenData.VertexCount = 3;
    screenData.Flags = CKRST_DP_CL_VCT;
    screenData.PositionPtr = screenPositions;
    screenData.PositionStride = sizeof(screenPositions[0]);
    screenData.ColorPtr = const_cast<CKDWORD *>(colors);
    screenData.ColorStride = sizeof(CKDWORD);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &screenData),
                  "draw pretransformed point-filled triangle");
    }, pixels);
    TestCheck(PixelNear(pixels, 22, 48, 255, 0, 0),
              "pretransformed point fill uses pixel-space point size");
    TestCheck(ReadStats(ctx).Diagnostics[CKRST_DIAG_APPROX_FILLMODE_POINT] == before,
              "scaled and pretransformed point fill have no approximation");

    CKTextureDesc spriteDesc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, spriteDesc.Format);
    spriteDesc.Format.Width = 2;
    spriteDesc.Format.Height = 1;
    spriteDesc.Format.BytesPerLine = 8;
    spriteDesc.MipMapCount = 1;
    spriteDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
    CKDWORD spriteTexture = 0;
    TestCheck(ctx->CreateTexture(&spriteDesc, &spriteTexture),
              "create point-fill sprite texture");
    CKDWORD spriteTexels[2] = {0xffff0000u, 0xff0000ffu};
    VxImageDescEx spriteImage = spriteDesc.Format;
    spriteImage.Image = reinterpret_cast<CKBYTE *>(spriteTexels);
    TestCheck(ctx->LoadTexture(spriteTexture, spriteImage, 0, CKRST_CUBEFACE_XPOS, NULL),
              "upload point-fill sprite texture");
    float sourceUV[3][2] = {{0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}};
    data.Flags = CKRST_DP_TR_CL_VCT;
    data.TexCoordPtr = sourceUV;
    data.TexCoordStride = sizeof(sourceUV[0]);
    ctx->SetRenderState(VXRENDERSTATE_POINTSPRITEENABLE, TRUE);
    TestCheck(ctx->SetTexture(spriteTexture, 0), "bind point-fill sprite texture");
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
    ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data),
                  "draw textured point-filled triangle sprites");
    }, pixels);
    TestCheck(PixelNear(pixels, 10, 48, 255, 0, 0) &&
              PixelNear(pixels, 22, 48, 0, 0, 255),
              "point-filled triangle sprites replace source UV across each point");
    TestCheck(ReadStats(ctx).Diagnostics[CKRST_DIAG_APPROX_FILLMODE_POINT] == before,
              "textured point fill has no approximation");
    ctx->SetTexture(0, 0);
    TestCheck(ctx->DeleteObject(spriteTexture, CKRST_OBJ_TEXTURE),
              "delete point-fill sprite texture");

    ctx->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);
    ctx->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_CCW);
    ctx->SetRenderState(VXRENDERSTATE_POINTSIZE, FloatBits(1.0f));
    ctx->SetRenderState(VXRENDERSTATE_POINTSPRITEENABLE, FALSE);
    ResetStage(ctx, 0);
    printf("  point-filled triangles preserve constant, vertex, scaled and sprite sizes: passed\n");
}

void CheckClippingDisablesUserPlanes(Backend &b)
{
    auto *ctx = b.Context;
    SetDiffuseState(ctx);
    VxPlane plane;
    plane.m_Normal = VxVector(1.0f, 0.0f, 0.0f);
    plane.m_D = -2.0f;
    TestCheck(ctx->SetUserClipPlane(0, plane), "set user plane outside the triangle");
    ctx->SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, 1);
    Pixels clipped, unclipped;
    for (int enabled = 0; enabled < 2; ++enabled) {
        ctx->SetRenderState(VXRENDERSTATE_CLIPPING, enabled ? FALSE : TRUE);
        RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
            CKBOOL drawn = DrawColorTriangle(ctx, kCenterTriangle, kRed);
            TestCheckf(drawn,
                       "draw with user clipping toggled, clippingOff=%d", enabled);
        }, enabled ? unclipped : clipped);
    }
    TestCheck(!PixelNear(clipped, 32, 32, 255, 0, 0) &&
                  PixelNear(unclipped, 32, 32, 255, 0, 0),
              "CLIPPING off disables user planes on the actual backend");
    ctx->SetRenderState(VXRENDERSTATE_CLIPPING, TRUE);
    plane.m_D = 0.0f;
    TestCheck(ctx->SetUserClipPlane(0, plane), "set plane across the triangle");
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kRed),
                  "draw triangle crossing a user plane");
    }, clipped);
    TestCheck(!PixelNear(clipped, 20, 32, 255, 0, 0) &&
                  PixelNear(clipped, 44, 32, 255, 0, 0),
              "user plane cuts the triangle at the interpolated boundary");

    VxPlane rightPlane;
    rightPlane.m_Normal = VxVector(-1.0f, 0.0f, 0.0f);
    rightPlane.m_D = 0.25f;
    TestCheck(ctx->SetUserClipPlane(1, rightPlane), "set second user plane");
    ctx->SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, 3);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kRed),
                  "draw triangle between two user planes");
    }, clipped);
    TestCheck(PixelNear(clipped, 37, 32, 255, 0, 0) &&
                  !PixelNear(clipped, 44, 32, 255, 0, 0),
              "two user planes preserve only their intersection");

    float screenPositions[3][4] = {
        {8.0f, 48.0f, 0.5f, 1.0f}, {56.0f, 48.0f, 0.5f, 1.0f},
        {32.0f, 8.0f, 0.5f, 1.0f}};
    VxDrawPrimitiveData screenData = {};
    screenData.VertexCount = 3;
    screenData.Flags = CKRST_DP_CL_VCT;
    screenData.PositionPtr = screenPositions;
    screenData.PositionStride = sizeof(screenPositions[0]);
    screenData.ColorPtr = const_cast<CKDWORD *>(kRed);
    screenData.ColorStride = sizeof(CKDWORD);
    plane.m_D = -32.0f;
    TestCheck(ctx->SetUserClipPlane(0, plane), "set POSITIONT user plane");
    ctx->SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, 1);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &screenData),
                  "draw POSITIONT triangle across a user plane");
    }, clipped);
    TestCheck(!PixelNear(clipped, 24, 32, 255, 0, 0) &&
                  PixelNear(clipped, 40, 32, 255, 0, 0),
              "POSITIONT user plane cuts the triangle at screen x=32");

    ctx->SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, 0);
    printf("  CLIPPING and user planes cut 3D and POSITIONT triangles: passed\n");
}

void CheckVertexBufferWrapPixels(Backend &b)
{
    CKRasterizerContext *ctx = b.Context;
    CKTextureDesc textureDesc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, textureDesc.Format);
    textureDesc.Format.Width = 4;
    textureDesc.Format.Height = 1;
    textureDesc.Format.BytesPerLine = 16;
    textureDesc.MipMapCount = 1;
    textureDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
    CKDWORD texture = 0;
    TestCheck(ctx->CreateTexture(&textureDesc, &texture), "create wrap test texture");
    CKDWORD texels[4] = {0xffff0000u, 0xff00ff00u, 0xff0000ffu, 0xffffff00u};
    VxImageDescEx image = textureDesc.Format;
    image.Image = reinterpret_cast<CKBYTE *>(texels);
    TestCheck(ctx->LoadTexture(texture, image, 0, CKRST_CUBEFACE_XPOS, NULL),
              "upload wrap test colors");

    const CKDWORD format = CKRST_DP_TR_CL_VCT;
    CKRSTVertexLayout layout;
    const CKDWORD stride = CKRSTGetVertexLayout(format, NULL, &layout);
    XArray<CKBYTE> vertices;
    vertices.Resize(stride * 3);
    memset(vertices.Begin(), 0, vertices.Size());
    for (int i = 0; i < 3; ++i) {
        CKBYTE *vertex = vertices.Begin() + i * stride;
        const float uv[2] = {i == 0 ? 0.9f : 0.1f, 0.0f};
        const CKDWORD white = 0xffffffffu;
        memcpy(vertex + layout.PositionOffset, &kCenterTriangle[i], sizeof(VxVector));
        memcpy(vertex + layout.TexcoordOffset[0], uv, sizeof(uv));
        memcpy(vertex + layout.DiffuseOffset, &white, sizeof(white));
    }
    CKVertexBufferDesc vbDesc;
    vbDesc.m_VertexFormat = format;
    vbDesc.m_MaxVertexCount = 3;
    vbDesc.m_Flags = CKRST_VB_WRITEONLY;
    CKDWORD vb = 0;
    TestCheck(ctx->CreateVertexBuffer(&vbDesc, vertices.Begin(), &vb),
              "create write-only wrap test VB");
    CKIndexBufferDesc ibDesc;
    ibDesc.m_MaxIndexCount = 3;
    ibDesc.m_Flags = CKRST_VB_WRITEONLY;
    const CKWORD triangle[3] = {0, 1, 2};
    CKDWORD ib = 0;
    TestCheck(ctx->CreateIndexBuffer(&ibDesc, triangle, &ib),
              "create write-only wrap test IB");

    SetDiffuseState(ctx);
    TestCheck(ctx->SetTexture(texture, 0), "bind wrap test texture");
    ctx->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
    ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    Pixels pixels;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitiveVB(VX_TRIANGLELIST, vb, 0, 3, NULL, 0),
                  "draw unwrapped VB reference");
    }, pixels);
    ExpectCenter(pixels, 0, 255, 0, "unwrapped VB interpolates through green texel");

    ctx->SetRenderState(VXRENDERSTATE_WRAP0, VXWRAP_U);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitiveVB(VX_TRIANGLELIST, vb, 0, 3, NULL, 0),
                  "draw wrapped VB");
    }, pixels);
    ExpectCenter(pixels, 255, 0, 0, "wrapped VB interpolates across red seam");
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitiveVBIB(VX_TRIANGLELIST, vb, ib, 0, 3, 0, 3),
                  "draw wrapped VBIB");
    }, pixels);
    ExpectCenter(pixels, 255, 0, 0, "wrapped VBIB interpolates across red seam");
    TestCheck(ReadStats(ctx).Diagnostics[CKRST_DIAG_IGNORE_WRAP] == 0,
              "VB and VBIB wrap do not report ignored state");

    ctx->SetRenderState(VXRENDERSTATE_WRAP0, 0);
    TestCheck(ctx->SetTexture(0, 0), "unbind wrap test texture");
    TestCheck(ctx->DeleteObject(ib, CKRST_OBJ_INDEXBUFFER), "delete wrap test IB");
    TestCheck(ctx->DeleteObject(vb, CKRST_OBJ_VERTEXBUFFER), "delete wrap test VB");
    TestCheck(ctx->DeleteObject(texture, CKRST_OBJ_TEXTURE), "delete wrap test texture");
    printf("  VB and VBIB WRAP0 interpolate across the texture seam: passed\n");
}

void CheckVertexBufferPointSizePixels(Backend &b)
{
    CKRasterizerContext *ctx = b.Context;
    const CKDWORD format = CKRST_DP_TR_CL_VCT;
    CKRSTVertexLayout layout;
    const CKDWORD stride = CKRSTGetVertexLayout(format, NULL, &layout);
    XArray<CKBYTE> vertex;
    vertex.Resize(stride);
    memset(vertex.Begin(), 0, stride);
    const VxVector position(0.0f, 0.0f, 0.5f);
    const CKDWORD red = 0xffff0000u;
    memcpy(vertex.Begin() + layout.PositionOffset, &position, sizeof(position));
    memcpy(vertex.Begin() + layout.DiffuseOffset, &red, sizeof(red));
    CKVertexBufferDesc vbDesc;
    vbDesc.m_VertexFormat = format;
    vbDesc.m_MaxVertexCount = 1;
    vbDesc.m_Flags = CKRST_VB_WRITEONLY;
    CKDWORD vb = 0;
    TestCheck(ctx->CreateVertexBuffer(&vbDesc, vertex.Begin(), &vb),
              "create point-size test VB");
    CKIndexBufferDesc ibDesc;
    ibDesc.m_MaxIndexCount = 1;
    ibDesc.m_Flags = CKRST_VB_WRITEONLY;
    const CKWORD pointIndex = 0;
    CKDWORD ib = 0;
    TestCheck(ctx->CreateIndexBuffer(&ibDesc, &pointIndex, &ib),
              "create point-size test IB");

    SetDiffuseState(ctx);
    ctx->SetRenderState(VXRENDERSTATE_POINTSIZE, FloatBits(24.0f));
    Pixels pixels;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitiveVB(VX_POINTLIST, vb, 0, 1, NULL, 0),
                  "draw 24-pixel VB point");
    }, pixels);
    TestCheck(PixelNear(pixels, 32, 32, 255, 0, 0) &&
              PixelNear(pixels, 42, 32, 255, 0, 0) &&
              PixelNear(pixels, 47, 32, 0, 0, 0),
              "VB point size reaches beyond the native 15-pixel limit");
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitiveVBIB(VX_POINTLIST, vb, ib, 0, 1, 0, 1),
                  "draw 24-pixel VBIB point");
    }, pixels);
    TestCheck(PixelNear(pixels, 42, 32, 255, 0, 0) &&
              PixelNear(pixels, 47, 32, 0, 0, 0),
              "VBIB point size reaches beyond the native 15-pixel limit");
    TestCheck(ReadStats(ctx).Diagnostics[CKRST_DIAG_APPROX_POINT_SIZE] == 0,
              "VB and VBIB points have no size approximation");

    ctx->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_POINT);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->DrawPrimitiveVB(VX_POINTLIST, vb, 0, 1, NULL, 0),
                  "draw 24-pixel VB point with polygon point fill selected");
    }, pixels);
    TestCheck(PixelNear(pixels, 42, 32, 255, 0, 0),
              "polygon point fill does not change an explicit point sprite");
    ctx->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);

    ctx->SetRenderState(VXRENDERSTATE_POINTSIZE, FloatBits(1.0f));
    TestCheck(ctx->DeleteObject(ib, CKRST_OBJ_INDEXBUFFER), "delete point IB");
    TestCheck(ctx->DeleteObject(vb, CKRST_OBJ_VERTEXBUFFER), "delete point VB");
    printf("  VB and VBIB point sizes beyond native limits: passed\n");
}

void CheckOrderedBufferUpdates(Backend &b)
{
    auto *ctx = b.Context;
    SetDiffuseState(ctx);
    struct Vertex { float X, Y, Z; CKDWORD Color; };
    Vertex vertices[6] = {{-1,-1,0.5f,0xffff0000}, {3,-1,0.5f,0xffff0000}, {-1,3,0.5f,0xffff0000},
                          {-1,-1,0.5f,0xff00ff00}, {3,-1,0.5f,0xff00ff00}, {-1,3,0.5f,0xff00ff00}};
    CKVertexBufferDesc desc;
    desc.m_VertexFormat = CKRST_DP_TR_VC; desc.m_MaxVertexCount = 6;
    CKDWORD vb = 0;
    TestCheck(ctx->CreateVertexBuffer(&desc, vertices, &vb), "persistent vertex buffer");
    CKViewportData viewport = {};
    viewport.ViewWidth = 20; viewport.ViewHeight = 64; viewport.ViewZMax = 1;
    Pixels pixels;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->SetViewport(&viewport), "first buffer viewport");
        TestCheck(ctx->DrawPrimitiveVB(VX_TRIANGLELIST, vb, 0, 3, NULL, 0), "VB draw before patch");
        auto *patch = static_cast<Vertex *>(ctx->LockVertexBuffer(vb, 0, 3, CKRST_LOCK_DEFAULT));
        TestCheck(patch != NULL, "lock first half of VB");
        for (int i = 0; i < 3; ++i) patch[i].Color = 0xff0000ff;
        TestCheck(ctx->UnlockVertexBuffer(vb), "upload partial VB");
        viewport.ViewX = 20;
        TestCheck(ctx->SetViewport(&viewport), "second buffer viewport");
        TestCheck(ctx->DrawPrimitiveVB(VX_TRIANGLELIST, vb, 0, 3, NULL, 0), "VB draw after patch");
        viewport.ViewX = 40;
        TestCheck(ctx->SetViewport(&viewport), "third buffer viewport");
        TestCheck(ctx->DrawPrimitiveVB(VX_TRIANGLELIST, vb, 3, 3, NULL, 0), "VB draw preserves untouched half and vertex offset");
        viewport.ViewX = 0; viewport.ViewWidth = 64;
        TestCheck(ctx->SetViewport(&viewport), "restore buffer viewport");
    }, pixels);
    TestCheck(PixelNear(pixels, 10, 32, 255, 0, 0), "VB old version remains red");
    TestCheck(PixelNear(pixels, 30, 32, 0, 0, 255), "VB patched version becomes blue");
    TestCheck(PixelNear(pixels, 50, 32, 0, 255, 0), "VB untouched vertices remain green");
    CKWORD indices[6] = {0,1,2,0,1,2};
    CKIndexBufferDesc indexDesc;
    indexDesc.m_MaxIndexCount = 6;
    CKDWORD ib = 0;
    TestCheck(ctx->CreateIndexBuffer(&indexDesc, indices, &ib), "persistent index buffer");
    viewport.ViewWidth = 20;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(ctx->SetViewport(&viewport), "first index viewport");
        TestCheck(ctx->DrawPrimitiveVBIB(VX_TRIANGLELIST, vb, ib, 0, 6, 0, 3), "IB draw before patch");
        auto *patch = static_cast<CKWORD *>(ctx->LockIndexBuffer(ib, 0, 3, CKRST_LOCK_DEFAULT));
        TestCheck(patch != NULL, "lock first half of IB");
        patch[0] = 3; patch[1] = 4; patch[2] = 5;
        TestCheck(ctx->UnlockIndexBuffer(ib), "upload partial IB");
        viewport.ViewX = 20;
        TestCheck(ctx->SetViewport(&viewport), "second index viewport");
        TestCheck(ctx->DrawPrimitiveVBIB(VX_TRIANGLELIST, vb, ib, 0, 6, 0, 3), "IB draw after patch");
        viewport.ViewX = 40;
        TestCheck(ctx->SetViewport(&viewport), "third index viewport");
        TestCheck(ctx->DrawPrimitiveVBIB(VX_TRIANGLELIST, vb, ib, 0, 6, 3, 3), "IB draw preserves untouched half and index offset");
        TestCheck(ctx->DeleteObject(ib, CKRST_OBJ_INDEXBUFFER), "delete IB referenced by pending draws");
        TestCheck(ctx->DeleteObject(vb, CKRST_OBJ_VERTEXBUFFER), "delete VB referenced by pending draws");
        CKDWORD replacement = 0;
        TestCheck(ctx->CreateIndexBuffer(&indexDesc, indices, &replacement), "reuse IB slot before submission");
        TestCheck(ctx->DeleteObject(replacement, CKRST_OBJ_INDEXBUFFER), "delete replacement IB");
        viewport.ViewX = 0; viewport.ViewWidth = 64;
        TestCheck(ctx->SetViewport(&viewport), "restore index viewport");
    }, pixels);
    TestCheck(PixelNear(pixels, 10, 32, 0, 0, 255), "IB old version selects blue vertices");
    TestCheck(PixelNear(pixels, 30, 32, 0, 255, 0), "IB new version selects green vertices");
    TestCheck(PixelNear(pixels, 50, 32, 0, 0, 255), "IB untouched indices retain blue after handle deletion");
    printf("  ordered VB/IB patches, offsets and pending deletion: passed\n");
}

void CheckBorderFiltering(Backend &b)
{
    auto *ctx = b.Context;
    SetDiffuseState(ctx);
    Textures textures;
    CreateTextures(ctx, textures);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ctx->SetTexture(textures.Transform, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSBORDER);
    ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0xff0000ff);
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_LINEAR);
    ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_LINEAR);
    float uv[3][4] = {{0,0.5f,0,0},{0,0.5f,0,0},{0,0.5f,0,0}};
    Pixels pixels;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, uv), "sample border footprint");
    }, pixels);
    ExpectCenter(pixels, 128, 0, 128, "bilinear edge blends red texel and blue border equally");
    for (auto &coord : uv) coord[0] = -1;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, uv), "sample outside border");
    }, pixels);
    ExpectCenter(pixels, 0, 0, 255, "outside border keeps actual blue color");
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
    ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        for (CKDWORD i = 0; i < 16; ++i) {
            const CKDWORD shade = i * 13u;
            ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR,
                                      0xff000000u | (shade << 16) | (shade << 8) | shade);
            TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, uv),
                      "sample a distinct nearest border color");
        }
        ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0xff14b4fau);
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, uv),
                  "sample seventeenth nearest border color");
    }, pixels);
    ExpectCenter(pixels, 20, 180, 250,
                 "seventeenth nearest border color remains exact in one frame");
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_LINEAR);
    ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_LINEAR);
    for (auto &coord : uv) coord[0] = 0;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        for (CKDWORD i = 0; i < 16; ++i) {
            const CKDWORD shade = i * 13u;
            ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR,
                                      0xff000000u | (shade << 16) | (shade << 8) | shade);
            TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, uv),
                      "sample a distinct linear border color");
        }
        ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0xff14b4fau);
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, uv),
                  "sample seventeenth linear border color");
    }, pixels);
    ExpectCenter(pixels, 138, 90, 125,
                 "seventeenth linear border color blends with the edge texel");
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_LINEARMIPLINEAR);
    ctx->SetTextureStageState(0, CKRST_TSS_MAXMIPMLEVEL, 3);
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        for (CKDWORD i = 0; i < 16; ++i) {
            const CKDWORD shade = i * 13u;
            ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR,
                                      0xff000000u | (shade << 16) | (shade << 8) | shade);
            TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, uv),
                      "sample a distinct one-level mip border color");
        }
        ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0xff14b4fau);
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, uv),
                  "sample seventeenth one-level mip border color");
    }, pixels);
    ExpectCenter(pixels, 138, 90, 125,
                 "one-level texture clamps its mip selection before border filtering");
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_LINEAR);
    ctx->SetTextureStageState(0, CKRST_TSS_MAXMIPMLEVEL, 0);
    ctx->SetTexture(textures.MirrorVolume, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT3);
    VxMatrix volumeTransform;
    Vx3DMatrixIdentity(volumeTransform);
    volumeTransform[3][2] = 0.25f;
    ctx->SetTransformMatrix(VXMATRIX_TEXTURE0, volumeTransform);
    float volumeUv[3][4] = {{0,0.5f,0,0},{0,0.5f,0,0},{0,0.5f,0,0}};
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        for (CKDWORD i = 0; i < 16; ++i) {
            const CKDWORD shade = i * 13u;
            ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR,
                                      0xff000000u | (shade << 16) | (shade << 8) | shade);
            TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, volumeUv),
                      "sample a distinct linear volume border color");
        }
        ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0xff14b4fau);
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, volumeUv),
                  "sample seventeenth linear volume border color");
    }, pixels);
    ExpectCenter(pixels, 138, 90, 125,
                 "seventeenth linear volume border color blends with the edge texel");
    ctx->SetTexture(textures.Transform, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_NONE);
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_ANISOTROPIC);
    ctx->SetTextureStageState(0, CKRST_TSS_MAXANISOTROPY, 4);
    float anisoUv[3][4] = {
        {-100.0f,0.5f,0,0}, {-80.0f,0.5f,0,0}, {-90.0f,0.5f,0,0}
    };
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        for (CKDWORD i = 0; i < 16; ++i) {
            const CKDWORD shade = i * 13u;
            ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR,
                                      0xff000000u | (shade << 16) | (shade << 8) | shade);
            TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, anisoUv),
                      "sample a distinct anisotropic border color");
        }
        ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0xff14b4fau);
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, anisoUv),
                  "sample seventeenth anisotropic border color");
    }, pixels);
    ExpectCenter(pixels, 20, 180, 250,
                 "seventeenth anisotropic border color remains exact");
    ctx->SetTexture(textures.MirrorVolume, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT3);
    ctx->SetTransformMatrix(VXMATRIX_TEXTURE0, volumeTransform);
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_LINEARMIPLINEAR);
    ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_LINEAR);
    ctx->SetTextureStageState(0, CKRST_TSS_MAXMIPMLEVEL, 1);
    ctx->SetTextureStageState(0, CKRST_TSS_ADDRESSU, VXTEXTURE_ADDRESSCLAMP);
    ctx->SetTextureStageState(0, CKRST_TSS_ADDRESSV, VXTEXTURE_ADDRESSBORDER);
    ctx->SetTextureStageState(0, CKRST_TSS_ADDRESW, VXTEXTURE_ADDRESSCLAMP);
    for (auto &coord : volumeUv) {
        coord[0] = 0.5f;
        coord[1] = 0.0f;
    }
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        for (CKDWORD i = 0; i < 16; ++i) {
            const CKDWORD shade = i * 13u;
            ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR,
                                      0xff000000u | (shade << 16) | (shade << 8) | shade);
            TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, volumeUv),
                      "sample a distinct volume mip border color");
        }
        ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0xff14b4fau);
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, volumeUv),
                  "sample seventeenth volume mip border color");
    }, pixels);
    ExpectCenter(pixels, 10, 90, 252,
                 "seventeenth volume mip border color blends with blue");
    DestroyTextures(ctx, textures);
    SetDiffuseState(ctx);
    printf("  border filtering: edge purple / outside blue\n");
}

void CheckCopyAndRectClear(Backend &b)
{
    auto *ctx = b.Context;
    SetDiffuseState(ctx);
    CKTextureDesc desc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
    desc.Format.Width = desc.Format.Height = 64;
    desc.Format.BytesPerLine = 256; desc.MipMapCount = 1;
    desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
    CKDWORD texture = 0;
    TestCheck(ctx->CreateTexture(&desc, &texture), "copy destination texture");
    BeginFrame(ctx, CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH | CKRST_CTXCLEAR_STENCIL);
    TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kGreen), "draw before copy");
    TestCheck(ctx->CopyToTexture(texture, NULL, NULL, CKRST_CUBEFACE_XPOS), "copy prior draw into texture");
    TestCheck(ctx->Clear(CKRST_CTXCLEAR_COLOR, 0xffff0000, 1, 0, 0, NULL), "clear after copy");
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ctx->SetTexture(texture, 0);
    float uv[3][4] = {{0.5f,0.5f,0,0},{0.5f,0.5f,0,0},{0.5f,0.5f,0,0}};
    TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, uv), "sample copied draw");
    EndFrame(ctx);
    Pixels pixels;
    ReadBackbuffer(ctx, pixels);
    ExpectCenter(pixels, 0, 255, 0, "draw-copy-sample sees copied green draw");
    TestCheck(PixelNear(pixels, 2, 2, 255, 0, 0), "clear after copy survives outside draw");
    SetDiffuseState(ctx);
    CKRECT rect = {16,16,48,48};
    for (unsigned samples : {0u,4u}) {
        CKRasterizerOptions options;
        options.MSAASamples = samples;
        TestCheck(ctx->SetOptions(&options), "rectangle clear sample count");
        BeginFrame(ctx, CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH | CKRST_CTXCLEAR_STENCIL);
        const VxVector cover[3] = {VxVector(-1,-1,0.5f), VxVector(3,-1,0.5f), VxVector(-1,3,0.5f)};
        TestCheck(DrawColorTriangle(ctx, cover, kBlue), "draw before rectangle clear");
        TestCheck(ctx->Clear(CKRST_CTXCLEAR_COLOR, 0xffff0000, 0, 0, 1, &rect), "rectangular color clear");
        EndFrame(ctx);
        ReadBackbuffer(ctx, pixels);
        ExpectCenter(pixels, 255, 0, 0, "rectangle cleared red");
        TestCheck(PixelNear(pixels, 8, 32, 0, 0, 255), "split render pass preserves outside blue");
    }
    CKRasterizerOptions defaults;
    TestCheck(ctx->SetOptions(&defaults), "restore options after rectangular clear");
    TestCheck(ctx->DeleteObject(texture, CKRST_OBJ_TEXTURE), "delete copied texture");
    printf("  ordered copy and rectangular clear: passed single-sample and MSAA\n");
}

void CheckLayeredTextureUpdates(Backend &b)
{
    auto *ctx = b.Context;
    for (bool volume : {false, true}) {
        CKTextureDesc desc;
        VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
        desc.Format.Width = desc.Format.Height = 4;
        desc.Format.BytesPerLine = 16;
        desc.Depth = volume ? 8 : 1;
        desc.MipMapCount = 1;
        desc.Flags = CKRST_TEXTURE_RGB | (volume ? CKRST_TEXTURE_VOLUMEMAP : CKRST_TEXTURE_CUBEMAP);
        CKDWORD texture = 0, source[16];
        TestCheck(ctx->CreateTexture(&desc, &texture), "create layered texture");
        VxImageDescEx image = desc.Format;
        image.Image = reinterpret_cast<CKBYTE *>(source);
        for (unsigned layer = 0; layer < (volume ? 8u : 6u); ++layer) {
            for (auto &pixel : source) pixel = layer ? 0xff00ff00 : 0xffff0000;
            TestCheck(ctx->LoadTexture(texture, image, 0, (CKRST_CUBEFACE)layer, NULL), "upload cube face / volume slice");
        }
        SetDiffuseState(ctx);
        ctx->SetTexture(texture, 0);
        ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
        ctx->SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSCLAMP);
        ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
        ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
        ctx->SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT3);
        auto drawSample = [&](int column, bool lastLayer, float coordinate) {
            VxMatrix transform;
            transform.SetIdentity();
            transform[3][0] = volume ? coordinate : (lastLayer ? 0.0f : 1.0f);
            transform[3][1] = volume ? coordinate : 0.0f;
            transform[3][2] = volume ? (lastLayer ? 7.5f / 8.0f : 0.5f / 8.0f) : (lastLayer ? -1.0f : 0.0f);
            ctx->SetTransformMatrix(VXMATRIX_TEXTURE(0), transform);
            VxVector positions[3] = {VxVector(-1,-1,0.5f), VxVector(-0.4f,-1,0.5f), VxVector(-0.7f,1,0.5f)};
            for (auto &position : positions) position.x += float(column) * 0.64f;
            float coords[3][4] = {};
            TestCheck(DrawTexturedTriangle(ctx, positions, kWhite, coords), "sample layered texture");
        };
        BeginFrame(ctx, CKRST_CTXCLEAR_COLOR);
        drawSample(0, false, 0.625f); // old version: red, including the future patch
        CKDWORD blue = 0xff0000ff;
        VxImageDescEx patch = image;
        patch.Width = patch.Height = 1; patch.BytesPerLine = 4;
        patch.Image = reinterpret_cast<CKBYTE *>(&blue);
        CKRECT region = {2,2,3,3}; // cube +X direction (1,0,0) selects the central texel
        TestCheck(ctx->LoadTexture(texture, patch, 0, CKRST_CUBEFACE_XPOS, &region), "patch first layer after sampling");
        drawSample(1, false, 0.625f);
        drawSample(2, true, 0.625f); // last face/slice survives version replacement
        EndFrame(ctx);
        Pixels pixels;
        ReadBackbuffer(ctx, pixels);
        TestCheckf(PixelNear(pixels, 10, 32, 255, 0, 0), "%s old layer version retained", volume ? "volume" : "cube");
        TestCheckf(PixelNear(pixels, 30, 32, 0, 0, 255), "%s patch visible to subsequent draw", volume ? "volume" : "cube");
        TestCheckf(PixelNear(pixels, 51, 32, 0, 255, 0), "%s last layer preserved", volume ? "volume" : "cube");
        ctx->SetTexture(0, 0);
        TestCheck(ctx->DeleteObject(texture, CKRST_OBJ_TEXTURE), "delete layered texture");
        ctx->SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_NONE);
    }
    printf("  ordered cube-face and eight-slice volume patches preserve other layers: passed\n");
}

void CheckMipPreservation(Backend &b)
{
    auto *ctx = b.Context;
    CKTextureDesc desc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
    desc.Format.Width = desc.Format.Height = 4;
    desc.Flags = CKRST_TEXTURE_RGB;
    desc.MipMapCount = 3;
    CKDWORD texture = 0, source[16];
    TestCheck(ctx->CreateTexture(&desc, &texture), "create explicit mip chain");
    const CKDWORD colors[] = {0xffff0000, 0xff00ff00, 0xff0000ff};
    for (unsigned mip = 0; mip < 3; ++mip) {
        for (auto &pixel : source) pixel = colors[mip];
        VxImageDescEx image = desc.Format;
        image.Width = image.Height = 4 >> mip;
        image.BytesPerLine = image.Width * 4;
        image.Image = reinterpret_cast<CKBYTE *>(source);
        TestCheck(ctx->LoadTexture(texture, image, mip, CKRST_CUBEFACE_XPOS, NULL), "upload distinct mip colors");
    }
    SetDiffuseState(ctx);
    ctx->SetTexture(texture, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_MIPNEAREST);
    ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    auto drawSample = [&](int column, int mip) {
        VxVector positions[3] = {VxVector(-1,-1,0.5f), VxVector(-0.4f,-1,0.5f), VxVector(-0.7f,1,0.5f)};
        for (auto &position : positions) position.x += float(column) * 0.64f;
        float coords[3][4] = {};
        if (mip) {
            coords[1][0] = mip == 1 ? 8.0f : 64.0f;
            coords[2][1] = mip == 1 ? 16.0f : 64.0f;
        } else {
            for (auto &coord : coords) coord[0] = coord[1] = 0.625f;
        }
        TestCheck(DrawTexturedTriangle(ctx, positions, kWhite, coords), "sample selected mip footprint");
    };
    BeginFrame(ctx, CKRST_CTXCLEAR_COLOR);
    drawSample(0, 2);
    CKDWORD white = 0xffffffff;
    VxImageDescEx patch = desc.Format;
    patch.Width = patch.Height = 1; patch.BytesPerLine = 4;
    patch.Image = reinterpret_cast<CKBYTE *>(&white);
    CKRECT region = {2,2,3,3};
    TestCheck(ctx->LoadTexture(texture, patch, 0, CKRST_CUBEFACE_XPOS, &region), "patch base after mip draw");
    drawSample(1, 1);
    drawSample(2, 0);
    EndFrame(ctx);
    Pixels pixels;
    ReadBackbuffer(ctx, pixels);
    TestCheck(PixelNear(pixels, 10, 32, 0, 0, 255), "queued draw retains old mip 2");
    TestCheck(PixelNear(pixels, 30, 32, 0, 255, 0), "base patch preserves mip 1 on new version");
    TestCheck(PixelNear(pixels, 51, 32, 255, 255, 255), "new base-level patch visible");
    BeginFrame(ctx, CKRST_CTXCLEAR_COLOR);
    drawSample(0, 2);
    EndFrame(ctx);
    ReadBackbuffer(ctx, pixels);
    TestCheck(PixelNear(pixels, 10, 32, 0, 0, 255), "base patch preserves mip 2 across submission");
    ctx->SetTexture(0, 0);
    TestCheck(ctx->DeleteObject(texture, CKRST_OBJ_TEXTURE), "delete mip chain");
    printf("  partial base update preserves explicit mip levels and queued samples: passed\n");
}

void CheckMipLodBias(Backend &b)
{
    auto *ctx = b.Context;
    CKTextureDesc desc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
    desc.Format.Width = desc.Format.Height = 4;
    desc.Flags = CKRST_TEXTURE_RGB;
    desc.MipMapCount = 3;
    CKDWORD texture = 0;
    TestCheck(ctx->CreateTexture(&desc, &texture), "create LOD bias texture");
    const CKDWORD colors[] = {0xffff0000, 0xff00ff00, 0xff0000ff};
    CKDWORD source[16];
    for (unsigned mip = 0; mip < 3; ++mip) {
        for (auto &pixel : source) pixel = colors[mip];
        VxImageDescEx image = desc.Format;
        image.Width = image.Height = 4 >> mip;
        image.BytesPerLine = image.Width * 4;
        image.Image = reinterpret_cast<CKBYTE *>(source);
        TestCheck(ctx->LoadTexture(texture, image, mip, CKRST_CUBEFACE_XPOS, NULL),
                  "upload LOD bias mip");
    }

    SetDiffuseState(ctx);
    ctx->SetTexture(texture, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_MIPNEAREST);
    ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    auto drawSample = [&](int column, float bias, CKDWORD minMip) {
        CKDWORD bits = 0;
        memcpy(&bits, &bias, sizeof(bits));
        ctx->SetTextureStageState(0, CKRST_TSS_MIPMAPLODBIAS, bits);
        ctx->SetTextureStageState(0, CKRST_TSS_MAXMIPMLEVEL, minMip);
        VxVector positions[3] = {VxVector(-1,-1,0.5f), VxVector(-0.4f,-1,0.5f), VxVector(-0.7f,1,0.5f)};
        for (auto &position : positions) position.x += float(column) * 0.64f;
        float coords[3][4] = {};
        // A nonzero footprint keeps the implicit LOD near zero, so the bias
        // can select a different mip instead of clamping an undefined LOD.
        coords[1][0] = 4.0f;
        coords[2][1] = 4.0f;
        TestCheck(DrawTexturedTriangle(ctx, positions, kWhite, coords), "sample biased mip");
    };
    BeginFrame(ctx, CKRST_CTXCLEAR_COLOR);
    drawSample(0, 0.0f, 0);
    drawSample(1, 2.0f, 0);
    drawSample(2, 0.0f, 0);
    EndFrame(ctx);
    Pixels pixels;
    ReadBackbuffer(ctx, pixels);
    CKBYTE biased[4];
    GetPixel(pixels, 30, 32, biased);
    TestCheck(PixelNear(pixels, 10, 32, 255, 0, 0), "zero LOD bias selects base mip");
    TestCheckf(PixelNear(pixels, 30, 32, 0, 0, 255),
               "LOD bias selects mip two, got BGRA=(%u,%u,%u,%u)",
               (unsigned)biased[0], (unsigned)biased[1], (unsigned)biased[2], (unsigned)biased[3]);
    TestCheck(PixelNear(pixels, 51, 32, 255, 0, 0), "reset LOD bias selects base mip");

    BeginFrame(ctx, CKRST_CTXCLEAR_COLOR);
    drawSample(0, 0.0f, 1);
    drawSample(1, 0.0f, 2);
    drawSample(2, 0.0f, 0);
    EndFrame(ctx);
    ReadBackbuffer(ctx, pixels);
    TestCheck(PixelNear(pixels, 10, 32, 0, 255, 0), "minimum mip one selects mip one");
    TestCheck(PixelNear(pixels, 30, 32, 0, 0, 255), "minimum mip two selects mip two");
    TestCheck(PixelNear(pixels, 51, 32, 255, 0, 0), "reset minimum mip selects base mip");

    ctx->SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSBORDER);
    ctx->SetTextureStageState(0, CKRST_TSS_MAXMIPMLEVEL, 2);
    float borderCoords[3][4] = {};
    for (auto &coord : borderCoords) coord[0] = coord[1] = 0.625f;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, borderCoords),
                  "draw border sampler with minimum mip");
    }, pixels);
    ExpectCenter(pixels, 0, 0, 255, "border sampler minimum mip");
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_LINEARMIPLINEAR);
    ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_LINEAR);
    ctx->SetTextureStageState(0, CKRST_TSS_ADDRESSU, VXTEXTURE_ADDRESSBORDER);
    ctx->SetTextureStageState(0, CKRST_TSS_ADDRESSV, VXTEXTURE_ADDRESSCLAMP);
    for (auto &coord : borderCoords) {
        coord[0] = 0.0f;
        coord[1] = 0.5f;
    }
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        for (CKDWORD i = 0; i < 16; ++i) {
            const CKDWORD shade = i * 13u;
            ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR,
                                      0xff000000u | (shade << 16) | (shade << 8) | shade);
            TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, borderCoords),
                      "sample a distinct mip border color");
        }
        ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0xff14b4fau);
        TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, borderCoords),
                  "sample seventeenth mip border color");
    }, pixels);
    ExpectCenter(pixels, 10, 90, 252,
                 "seventeenth mip border color blends with the selected blue level");
    ctx->SetTextureStageState(0, CKRST_TSS_MIPMAPLODBIAS, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_MAXMIPMLEVEL, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSWRAP);
    ctx->SetTextureStageState(0, CKRST_TSS_ADDRESSU, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_ADDRESSV, 0);
    ctx->SetTexture(0, 0);
    TestCheck(ctx->DeleteObject(texture, CKRST_OBJ_TEXTURE), "delete LOD bias texture");
    printf("  sampler LOD bias and minimum mip select and reset explicit levels: passed\n");
}

void CheckLayeredMinimumMip(Backend &b)
{
    auto *ctx = b.Context;
    for (bool volume : {false, true}) {
        CKTextureDesc desc;
        VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
        desc.Format.Width = desc.Format.Height = 2;
        desc.Flags = CKRST_TEXTURE_RGB |
                     (volume ? CKRST_TEXTURE_VOLUMEMAP : CKRST_TEXTURE_CUBEMAP);
        desc.Depth = volume ? 2 : 1;
        desc.MipMapCount = 2;
        CKDWORD texture = 0;
        TestCheck(ctx->CreateTexture(&desc, &texture), "create layered minimum mip texture");
        for (unsigned mip = 0; mip < 2; ++mip) {
            CKDWORD source[4];
            for (auto &pixel : source) pixel = mip ? 0xff0000ff : 0xffff0000;
            VxImageDescEx image = desc.Format;
            image.Width = image.Height = 2 >> mip;
            image.BytesPerLine = image.Width * 4;
            image.Image = reinterpret_cast<CKBYTE *>(source);
            const unsigned layers = volume ? (2u >> mip) : 6u;
            for (unsigned layer = 0; layer < layers; ++layer)
                TestCheck(ctx->LoadTexture(texture, image, mip,
                                           (CKRST_CUBEFACE)layer, NULL),
                          "upload layered minimum mip");
        }
        SetDiffuseState(ctx);
        ctx->SetTexture(texture, 0);
        ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
        ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER,
                                  VXTEXTUREFILTER_MIPNEAREST);
        ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER,
                                  VXTEXTUREFILTER_NEAREST);
        ctx->SetTextureStageState(0, CKRST_TSS_ADDRESS,
                                  VXTEXTURE_ADDRESSCLAMP);
        if (volume)
            ctx->SetTextureStageState(0, CKRST_TSS_ADDRESSV,
                                      VXTEXTURE_ADDRESSBORDER);
        ctx->SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS,
                                  CKRST_TTF_COUNT3);
        float coords[3][4] = {};
        for (auto &coord : coords) {
            coord[0] = volume ? 0.625f : 1.0f;
            coord[1] = volume ? 0.625f : 0.0f;
            coord[2] = volume ? 0.25f : 0.0f;
        }
        Pixels pixels;
        for (CKDWORD minMip : {0u, 1u, 0u}) {
            ctx->SetTextureStageState(0, CKRST_TSS_MAXMIPMLEVEL, minMip);
            RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
                TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, coords),
                          "draw layered minimum mip");
            }, pixels);
            if (minMip)
                ExpectCenter(pixels, 0, 0, 255,
                             volume ? "volume minimum mip" : "cube minimum mip");
            else
                ExpectCenter(pixels, 255, 0, 0,
                             volume ? "volume base mip" : "cube base mip");
        }
        ctx->SetTextureStageState(0, CKRST_TSS_MAXMIPMLEVEL, 0);
        ctx->SetTexture(0, 0);
        TestCheck(ctx->DeleteObject(texture, CKRST_OBJ_TEXTURE),
                  "delete layered minimum mip texture");
    }
    printf("  cube and volume minimum mip sampling and reset: passed\n");
}

void CheckAnisotropyLimit(Backend &b)
{
    auto *ctx = b.Context;
    CKTextureDesc desc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
    desc.Format.Width = desc.Format.Height = 64;
    desc.Flags = CKRST_TEXTURE_RGB;
    desc.MipMapCount = 7;
    CKDWORD texture = 0;
    TestCheck(ctx->CreateTexture(&desc, &texture), "create anisotropy mip chain");
    const CKDWORD mipColors[] = {0xffff0000, 0xff00ff00, 0xff0000ff};
    for (unsigned mip = 0; mip < 7; ++mip) {
        const unsigned side = 64u >> mip;
        std::vector<CKDWORD> source(side * side, mipColors[mip < 2 ? mip : 2]);
        VxImageDescEx image = desc.Format;
        image.Width = image.Height = side;
        image.BytesPerLine = side * 4;
        image.Image = reinterpret_cast<CKBYTE *>(source.data());
        TestCheck(ctx->LoadTexture(texture, image, mip, CKRST_CUBEFACE_XPOS, NULL),
                  "upload anisotropy mip");
    }

    SetDiffuseState(ctx);
    ctx->SetTexture(texture, 0);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER,
                              VXTEXTUREFILTER_ANISOTROPIC);
    ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER,
                              VXTEXTUREFILTER_LINEAR);
    ctx->SetTextureStageState(0, CKRST_TSS_ADDRESS,
                              VXTEXTURE_ADDRESSWRAP);
    float coords[3][4] = {};
    coords[0][0] = 0.0f;
    coords[1][0] = 7.2f;
    coords[2][0] = 3.6f;
    for (auto &coord : coords) coord[1] = 0.5f;
    auto sample = [&](CKDWORD maxAnisotropy, Pixels &pixels) {
        ctx->SetTextureStageState(0, CKRST_TSS_MAXANISOTROPY, maxAnisotropy);
        RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
            TestCheck(DrawTexturedTriangle(ctx, kCenterTriangle, kWhite, coords),
                      "draw anisotropy limit");
        }, pixels);
    };
    Pixels pixels;
    sample(2, pixels);
    ExpectCenter(pixels, 0, 0, 255, "two-tap anisotropy selects coarse mip");
    sample(8, pixels);
    ExpectCenter(pixels, 255, 0, 0, "eight-tap anisotropy preserves base mip");
    sample(1, pixels);
    ExpectCenter(pixels, 0, 0, 255, "one-tap anisotropy resets to linear minification");
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER,
                              VXTEXTUREFILTER_LINEAR);
    ctx->SetTextureStageState(0, CKRST_TSS_MAXANISOTROPY, 1);
    ctx->SetTexture(0, 0);
    TestCheck(ctx->DeleteObject(texture, CKRST_OBJ_TEXTURE),
              "delete anisotropy mip chain");
    printf("  requested anisotropy levels select distinct mip footprints: passed\n");
}

void CheckLayeredAnisotropyLimit(Backend &b)
{
    auto *ctx = b.Context;
    for (bool volume : {false, true}) {
        CKTextureDesc desc;
        VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
        desc.Format.Width = desc.Format.Height = 64;
        desc.Flags = CKRST_TEXTURE_RGB |
                     (volume ? CKRST_TEXTURE_VOLUMEMAP : CKRST_TEXTURE_CUBEMAP);
        desc.Depth = volume ? 2 : 1;
        desc.MipMapCount = 7;
        CKDWORD texture = 0;
        TestCheck(ctx->CreateTexture(&desc, &texture),
                  "create layered anisotropy mip chain");
        for (unsigned mip = 0; mip < 7; ++mip) {
            const unsigned side = 64u >> mip;
            std::vector<CKDWORD> source(side * side,
                                        mip ? 0xff0000ff : 0xffff0000);
            VxImageDescEx image = desc.Format;
            image.Width = image.Height = side;
            image.BytesPerLine = side * 4;
            image.Image = reinterpret_cast<CKBYTE *>(source.data());
            const unsigned layers = volume ? (mip == 0 ? 2u : 1u) : 6u;
            for (unsigned layer = 0; layer < layers; ++layer)
                TestCheck(ctx->LoadTexture(texture, image, mip,
                                           (CKRST_CUBEFACE)layer, NULL),
                          "upload layered anisotropy mip");
        }
        SetDiffuseState(ctx);
        ctx->SetTexture(texture, 0);
        ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
        ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER,
                                  VXTEXTUREFILTER_ANISOTROPIC);
        ctx->SetTextureStageState(0, CKRST_TSS_MAGFILTER,
                                  VXTEXTUREFILTER_LINEAR);
        ctx->SetTextureStageState(0, CKRST_TSS_ADDRESS,
                                  VXTEXTURE_ADDRESSWRAP);
        ctx->SetTextureStageState(0, CKRST_TSS_ADDRESSU, 0);
        ctx->SetTextureStageState(0, CKRST_TSS_ADDRESSV, 0);
        ctx->SetTextureStageState(0, CKRST_TSS_ADDRESW, 0);
        ctx->SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS,
                                  CKRST_TTF_COUNT3);
        VxVector positions[3] = {
            VxVector(-0.18f, -0.9f, 0.5f),
            VxVector(0.18f, -0.9f, 0.5f),
            VxVector(0.0f, 0.9f, 0.5f),
        };
        float coords[3][4] = {};
        for (auto &coord : coords) {
            coord[1] = volume ? 0.5f : 0.0f;
            coord[2] = volume ? 0.25f : 0.0f;
        }
        if (volume) {
            coords[0][0] = 0.0f;
            coords[1][0] = 1.4f;
            coords[2][0] = 0.7f;
        } else {
            for (auto &coord : coords) coord[0] = 1.0f;
            coords[0][1] = -0.75f;
            coords[1][1] = 0.75f;
        }
        Pixels pixels;
        auto sample = [&](CKDWORD maxAnisotropy, CKDWORD minMip) {
            ctx->SetTextureStageState(0, CKRST_TSS_MAXANISOTROPY,
                                      maxAnisotropy);
            ctx->SetTextureStageState(0, CKRST_TSS_MAXMIPMLEVEL, minMip);
            RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
                TestCheck(DrawTexturedTriangle(ctx, positions, kWhite, coords),
                          "draw layered anisotropy limit");
            }, pixels);
        };
        sample(2, 0);
        ExpectCenter(pixels, 0, 0, 255,
                     volume ? "volume two-tap anisotropy" : "cube two-tap anisotropy");
        sample(8, 0);
        ExpectCenter(pixels, 255, 0, 0,
                     volume ? "volume eight-tap anisotropy" : "cube eight-tap anisotropy");
        sample(8, 1);
        ExpectCenter(pixels, 0, 0, 255,
                     volume ? "volume anisotropy minimum mip" : "cube anisotropy minimum mip");
        if (volume) {
            ctx->SetTextureStageState(0, CKRST_TSS_ADDRESSV,
                                      VXTEXTURE_ADDRESSBORDER);
            ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0xff00ff00);
            for (auto &coord : coords) coord[1] = 0.0f;
            sample(8, 0);
            ExpectCenter(pixels, 128, 128, 0,
                         "volume anisotropy blends the border per tap");
            ctx->SetTextureStageState(0, CKRST_TSS_ADDRESSV, 0);
            ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0);
        }
        ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER,
                                  VXTEXTUREFILTER_LINEAR);
        ctx->SetTextureStageState(0, CKRST_TSS_MAXANISOTROPY, 1);
        ctx->SetTextureStageState(0, CKRST_TSS_MAXMIPMLEVEL, 0);
        ctx->SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS,
                                  0);
        ctx->SetTexture(0, 0);
        TestCheck(ctx->DeleteObject(texture, CKRST_OBJ_TEXTURE),
                  "delete layered anisotropy mip chain");
    }
    printf("  cube and volume anisotropy levels and minimum mip: passed\n");
}

void CheckMemoryCopyPixelIdentity(Backend &b)
{
    auto *ctx = b.Context;
    CKDWORD source[19 * 13];
    for (unsigned i = 0; i < 19 * 13; ++i)
        source[i] = 0xff000000u | ((i * 73u & 255u) << 16) | ((i * 37u & 255u) << 8) | (i * 19u & 255u);
    VxImageDescEx image;
    VxPixelFormat2ImageDesc(_32_ARGB8888, image);
    image.Width = 19;
    image.Height = 13;
    image.BytesPerLine = 19 * 4;
    image.Image = reinterpret_cast<CKBYTE *>(source);
    const CKRECT rect = {17, 19, 36, 32};
    const VxVector cover[3] = {VxVector(-1,-1,0.5f), VxVector(3,-1,0.5f), VxVector(-1,3,0.5f)};
    for (bool inheritedState : {false, true}) {
        SetDiffuseState(ctx);
        BeginFrame(ctx, CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH | CKRST_CTXCLEAR_STENCIL);
        TestCheck(DrawColorTriangle(ctx, cover, kBlue), "background before memory copy");
        if (inheritedState) {
            ctx->SetRenderState(VXRENDERSTATE_STENCILENABLE, TRUE);
            ctx->SetRenderState(VXRENDERSTATE_STENCILFUNC, VXCMP_NEVER);
            ctx->SetRenderState(VXRENDERSTATE_COLORWRITEENABLE, 0);
            ctx->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_WIREFRAME);
            ctx->SetTextureStageState(0, CKRST_TSS_TEXCOORDINDEX, 1);
            ctx->SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT2);
            VxMatrix transform;
            transform.SetIdentity();
            transform[3][0] = 0.75f;
            ctx->SetTransformMatrix(VXMATRIX_TEXTURE(0), transform);
        }
        TestCheck(ctx->CopyFromMemoryBuffer(&rect, VXBUFFER_BACKBUFFER, image) == sizeof(source), "copy exact image");
        if (inheritedState)
            TestCheck(DrawColorTriangle(ctx, cover, kRed), "original rejecting draw state restored after copy");
        EndFrame(ctx);
        Pixels pixels;
        ReadBackbuffer(ctx, pixels);
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
            const CKDWORD expected = x >= 17 && x < 36 && y >= 19 && y < 32 ? source[(y - 19) * 19 + x - 17] : 0xff0000ff;
            CKBYTE actual[4];
            GetPixel(pixels, x, y, actual);
            TestCheckf(actual[0] == (expected & 255) && actual[1] == ((expected >> 8) & 255) &&
                       actual[2] == ((expected >> 16) & 255),
                       "memory copy pixel (%d,%d), inherited=%d: expected %06x, got %02x%02x%02x",
                       x, y, inheritedState, expected & 0xffffff, actual[2], actual[1], actual[0]);
        }
        ctx->SetRenderState(VXRENDERSTATE_STENCILENABLE, FALSE);
        ctx->SetRenderState(VXRENDERSTATE_COLORWRITEENABLE, CKRST_COLORWRITE_ALL);
        ctx->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);
        ctx->SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_NONE);
    }
    printf("  exact memory copy, pixel placement and state isolation: passed\n");
}

void CheckScaledTextureCopies(Backend &b)
{
    auto *ctx = b.Context;
    CKDWORD source[19 * 13], initial[64 * 64];
    for (unsigned i = 0; i < 19 * 13; ++i)
        source[i] = 0xff000000u | ((i * 73u & 255u) << 16) | ((i * 37u & 255u) << 8) | (i * 19u & 255u);
    for (auto &pixel : initial) pixel = 0xff00ffff;
    VxImageDescEx image;
    VxPixelFormat2ImageDesc(_32_ARGB8888, image);
    image.Width = 19; image.Height = 13; image.BytesPerLine = 19 * 4;
    image.Image = reinterpret_cast<CKBYTE *>(source);
    const CKRECT uploadRect = {17, 19, 36, 32};
    const VxRect sourceRect(17, 19, 36, 32);
    const VxRect destinations[] = {VxRect(3, 5, 26, 28), VxRect(37, 42, 48, 49)};
    const VxVector cover[3] = {VxVector(-1,-1,0.5f), VxVector(3,-1,0.5f), VxVector(-1,3,0.5f)};

    for (unsigned samples : {0u, 4u}) for (bool inFrame : {true, false}) {
        CKRasterizerOptions options;
        options.MSAASamples = samples;
        TestCheck(ctx->SetOptions(&options), "scaled copy sample count");
        CKTextureDesc desc;
        VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
        desc.Format.Width = desc.Format.Height = 64;
        desc.Format.BytesPerLine = 256; desc.MipMapCount = 1;
        desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_RENDERTARGET;
        CKDWORD texture = 0;
        TestCheck(ctx->CreateTexture(&desc, &texture), "scaled copy destination");
        auto contents = desc.Format;
        contents.Image = reinterpret_cast<CKBYTE *>(initial);
        TestCheck(ctx->LoadTexture(texture, contents, 0, CKRST_CUBEFACE_XPOS, NULL), "initialize preserved destination");
        SetDiffuseState(ctx);
        BeginFrame(ctx, CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH | CKRST_CTXCLEAR_STENCIL);
        TestCheck(DrawColorTriangle(ctx, cover, kBlue), "draw before scaled copy");
        TestCheck(ctx->CopyFromMemoryBuffer(&uploadRect, VXBUFFER_BACKBUFFER, image) == sizeof(source), "upload scaled copy pattern");
        ctx->SetRenderState(VXRENDERSTATE_STENCILENABLE, TRUE);
        ctx->SetRenderState(VXRENDERSTATE_STENCILFUNC, VXCMP_NEVER);
        ctx->SetRenderState(VXRENDERSTATE_COLORWRITEENABLE, 0);
        ctx->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_WIREFRAME);
        if (!inFrame) EndFrame(ctx);
        for (const auto &destination : destinations)
            TestCheck(ctx->CopyToTexture(texture, &sourceRect, &destination, CKRST_CUBEFACE_XPOS),
                      "enlarge / shrink a cropped source into a partial destination");
        if (inFrame) {
            TestCheck(DrawColorTriangle(ctx, cover, kRed), "copy restores the rejecting draw state");
            EndFrame(ctx);
        }
        Pixels pixels;
        ReadBackbuffer(ctx, pixels);
        TestCheck(PixelNear(pixels, 5, 5, 0, 0, 255), "copy does not redirect subsequent draws or alter the source");
        ctx->SetRenderState(VXRENDERSTATE_STENCILENABLE, FALSE);
        ctx->SetRenderState(VXRENDERSTATE_COLORWRITEENABLE, CKRST_COLORWRITE_ALL);
        ctx->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);
        TestCheck(ctx->SetTargetTexture(texture, 0, 0, CKRST_CUBEFACE_XPOS), "read scaled destination");
        ReadBackbuffer(ctx, pixels);
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
            CKDWORD expected = 0xff00ffff;
            for (const auto &rect : destinations) {
                const int left = (int)rect.left, top = (int)rect.top;
                const int width = (int)(rect.right - rect.left), height = (int)(rect.bottom - rect.top);
                if (x >= left && x < left + width && y >= top && y < top + height) {
                    const int sx = ((x - left) * 2 + 1) * 19 / (width * 2);
                    const int sy = ((y - top) * 2 + 1) * 13 / (height * 2);
                    expected = source[sy * 19 + sx];
                }
            }
            CKBYTE actual[4];
            GetPixel(pixels, x, y, actual);
            TestCheckf(actual[0] == (expected & 255) && actual[1] == ((expected >> 8) & 255) &&
                       actual[2] == ((expected >> 16) & 255),
                       "scaled copy pixel (%d,%d), MSAA=%u inFrame=%d: expected %06x, got %02x%02x%02x",
                       x, y, samples, inFrame, expected & 0xffffff, actual[2], actual[1], actual[0]);
        }
        TestCheck(ctx->SetTargetTexture(0, 0, 0, CKRST_CUBEFACE_XPOS), "restore window after scaled copy");
        TestCheck(ctx->DeleteObject(texture, CKRST_OBJ_TEXTURE), "delete scaled destination");
    }
    CKRasterizerOptions defaults;
    TestCheck(ctx->SetOptions(&defaults), "restore options after scaling");
    SetDiffuseState(ctx);
    printf("  exact cropped scaling, enlargement / shrink, untouched regions and state: passed in/out of frame, MSAA 0/4\n");
}

void CheckIndependentAttachmentClears(Backend &b)
{
    auto *ctx = b.Context;
    CKRECT rect = {16,16,48,48};
    const VxVector nearCover[3] = {VxVector(-1,-1,0.25f), VxVector(3,-1,0.25f), VxVector(-1,3,0.25f)};
    const VxVector farCover[3] = {VxVector(-1,-1,0.5f), VxVector(3,-1,0.5f), VxVector(-1,3,0.5f)};
    for (unsigned samples : {0u,4u}) {
        CKRasterizerOptions options;
        options.MSAASamples = samples;
        TestCheck(ctx->SetOptions(&options), "attachment clear sample count");
        SetDiffuseState(ctx);
        ctx->SetRenderState(VXRENDERSTATE_STENCILENABLE, FALSE);
        ctx->SetRenderState(VXRENDERSTATE_ZENABLE, TRUE);
        ctx->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, TRUE);
        ctx->SetRenderState(VXRENDERSTATE_ZFUNC, VXCMP_LESS);
        BeginFrame(ctx, CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH | CKRST_CTXCLEAR_STENCIL);
        TestCheck(DrawColorTriangle(ctx, nearCover, kBlue), "initialize color and near depth");
        TestCheck(ctx->Clear(CKRST_CTXCLEAR_DEPTH, 0xff00ff00, 0.75f, 9, 1, &rect), "rectangular depth-only clear");
        TestCheck(DrawColorTriangle(ctx, farCover, kRed), "depth test after depth-only clear");
        EndFrame(ctx);
        Pixels pixels;
        ReadBackbuffer(ctx, pixels);
        ExpectCenter(pixels, 255, 0, 0, "depth clear admits the farther draw inside");
        TestCheck(PixelNear(pixels, 8, 32, 0, 0, 255), "depth clear preserves outside color and depth");

        SetDiffuseState(ctx);
        ctx->SetRenderState(VXRENDERSTATE_STENCILENABLE, TRUE);
        ctx->SetRenderState(VXRENDERSTATE_STENCILFUNC, VXCMP_ALWAYS);
        ctx->SetRenderState(VXRENDERSTATE_STENCILREF, 7);
        ctx->SetRenderState(VXRENDERSTATE_STENCILMASK, 255);
        ctx->SetRenderState(VXRENDERSTATE_STENCILWRITEMASK, 255);
        ctx->SetRenderState(VXRENDERSTATE_STENCILPASS, VXSTENCILOP_REPLACE);
        ctx->SetRenderState(VXRENDERSTATE_STENCILFAIL, VXSTENCILOP_KEEP);
        ctx->SetRenderState(VXRENDERSTATE_STENCILZFAIL, VXSTENCILOP_KEEP);
        BeginFrame(ctx, CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH | CKRST_CTXCLEAR_STENCIL);
        TestCheck(DrawColorTriangle(ctx, nearCover, kBlue), "initialize color and stencil");
        TestCheck(ctx->Clear(CKRST_CTXCLEAR_STENCIL, 0xff00ff00, 0, 3, 1, &rect), "rectangular stencil-only clear");
        ctx->SetRenderState(VXRENDERSTATE_STENCILFUNC, VXCMP_EQUAL);
        ctx->SetRenderState(VXRENDERSTATE_STENCILREF, 3);
        ctx->SetRenderState(VXRENDERSTATE_STENCILPASS, VXSTENCILOP_KEEP);
        TestCheck(DrawColorTriangle(ctx, farCover, kRed), "stencil test after stencil-only clear");
        EndFrame(ctx);
        ReadBackbuffer(ctx, pixels);
        ExpectCenter(pixels, 255, 0, 0, "stencil clear admits the draw inside");
        TestCheck(PixelNear(pixels, 8, 32, 0, 0, 255), "stencil clear preserves outside color and stencil");
        ctx->SetRenderState(VXRENDERSTATE_STENCILENABLE, FALSE);
    }
    CKRasterizerOptions defaults;
    TestCheck(ctx->SetOptions(&defaults), "restore options after attachment clears");
    SetDiffuseState(ctx);
    printf("  independent rectangular depth/stencil clears: passed single-sample and MSAA\n");
}

void CheckStencilWriteMasks(Backend &b)
{
    auto *ctx = b.Context;
    const CKDWORD approximationsBefore =
        ReadStats(ctx).Diagnostics[CKRST_DIAG_APPROX_STENCIL_WRITE_MASK];
    SetDiffuseState(ctx);
    ctx->SetRenderState(VXRENDERSTATE_STENCILENABLE, TRUE);
    ctx->SetRenderState(VXRENDERSTATE_STENCILFUNC, VXCMP_ALWAYS);
    ctx->SetRenderState(VXRENDERSTATE_STENCILMASK, 0xFF);
    ctx->SetRenderState(VXRENDERSTATE_STENCILWRITEMASK, 0xFF);
    ctx->SetRenderState(VXRENDERSTATE_STENCILPASS, VXSTENCILOP_REPLACE);
    ctx->SetRenderState(VXRENDERSTATE_STENCILFAIL, VXSTENCILOP_KEEP);
    ctx->SetRenderState(VXRENDERSTATE_STENCILZFAIL, VXSTENCILOP_KEEP);
    ctx->SetRenderState(VXRENDERSTATE_STENCILREF, 0xA5);

    BeginFrame(ctx, CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH | CKRST_CTXCLEAR_STENCIL);
    TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kRed), "seed all stencil bits");
    ctx->SetRenderState(VXRENDERSTATE_STENCILREF, 0xF0);
    ctx->SetRenderState(VXRENDERSTATE_STENCILWRITEMASK, 0x0F);
    TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kBlue), "write low stencil nibble only");
    ctx->SetRenderState(VXRENDERSTATE_STENCILFUNC, VXCMP_EQUAL);
    ctx->SetRenderState(VXRENDERSTATE_STENCILREF, 0xA0);
    ctx->SetRenderState(VXRENDERSTATE_STENCILWRITEMASK, 0);
    ctx->SetRenderState(VXRENDERSTATE_STENCILPASS, VXSTENCILOP_KEEP);
    TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kGreen),
              "compare preserved high stencil nibble");
    EndFrame(ctx);
    Pixels pixels;
    ReadBackbuffer(ctx, pixels);
    ExpectCenter(pixels, 0, 255, 0, "partial stencil write mask preserves unselected bits");

    ctx->SetRenderState(VXRENDERSTATE_STENCILFUNC, VXCMP_ALWAYS);
    ctx->SetRenderState(VXRENDERSTATE_STENCILREF, 0xA0);
    ctx->SetRenderState(VXRENDERSTATE_STENCILWRITEMASK, 0xFF);
    ctx->SetRenderState(VXRENDERSTATE_STENCILPASS, VXSTENCILOP_REPLACE);
    BeginFrame(ctx, CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH | CKRST_CTXCLEAR_STENCIL);
    TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kRed), "seed stencil before zero mask");
    ctx->SetRenderState(VXRENDERSTATE_STENCILREF, 0xFF);
    ctx->SetRenderState(VXRENDERSTATE_STENCILWRITEMASK, 0);
    TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kBlue), "draw with zero stencil write mask");
    ctx->SetRenderState(VXRENDERSTATE_STENCILFUNC, VXCMP_EQUAL);
    ctx->SetRenderState(VXRENDERSTATE_STENCILREF, 0xA0);
    ctx->SetRenderState(VXRENDERSTATE_STENCILPASS, VXSTENCILOP_KEEP);
    TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kGreen),
              "compare stencil after zero write mask");
    EndFrame(ctx);
    ReadBackbuffer(ctx, pixels);
    ExpectCenter(pixels, 0, 255, 0, "zero stencil write mask preserves all bits");
    TestCheck(ReadStats(ctx).Diagnostics[CKRST_DIAG_APPROX_STENCIL_WRITE_MASK] ==
                  approximationsBefore,
              "both backends submit stencil masks without approximation diagnostics");

    ctx->SetRenderState(VXRENDERSTATE_STENCILENABLE, FALSE);
    ctx->SetRenderState(VXRENDERSTATE_STENCILWRITEMASK, 0xFF);
    printf("  partial and zero stencil write masks preserve stencil bits: passed\n");
}

// ---------------------------------------------------------------------------
// Caps, resize and asynchronous readback
// ---------------------------------------------------------------------------

void CheckDriverCaps(const Backend &b)
{
    CKRasterizerDriverDesc driverDesc = {};
    CKRasterizerNativeCapsDesc nativeCaps;
    TestCheck(b.Driver->GetDesc(&driverDesc) && driverDesc.CapsFinal,
              "caps refreshed by context creation");
    TestCheck(b.Driver->GetNativeCaps(&nativeCaps) &&
                  nativeCaps.MaxTextureSize > 0,
              "texture limits lowered to the backend");
    TestCheck(b.Driver->GetTextureFormatCount() > 0,
              "texture formats reported");
    CKRasterizerCapsDesc publicCaps;
    TestCheck(b.Context->GetCaps(&publicCaps) && publicCaps.MaxTextureStages >= 1 &&
                  publicCaps.MaxTextureStages <= CKRST_MAX_TEXTURE_STAGES,
              "public rasterizer caps");
}

CKRasterizerContextDesc ReadContextDesc(CKRasterizerContext *context)
{
    CKRasterizerContextDesc desc = {};
    TestCheck(context && context->GetDesc(&desc), "context description query");
    return desc;
}

struct ReadbackCapture {
    int Calls;
    int Width;
    int Height;
    CKBOOL Success;
    Pixels Image;
    ReadbackCapture() : Calls(0), Width(0), Height(0), Success(FALSE) {}
    static void Callback(void *user, const CKRECT *, VXBUFFER_TYPE, const VxImageDescEx *image, CKBOOL ok)
    {
        ReadbackCapture *capture = static_cast<ReadbackCapture *>(user);
        ++capture->Calls;
        capture->Success = ok;
        if (image) {
            capture->Width = image->Width;
            capture->Height = image->Height;
            capture->Image.Width = image->Width;
            capture->Image.Height = image->Height;
            capture->Image.Data.Resize(image->Width * image->Height * 4);
            for (int row = 0; row < image->Height; ++row)
                memcpy(capture->Image.Data.Begin() + row * image->Width * 4,
                       image->Image + row * image->BytesPerLine, image->Width * 4);
        }
    }
};

void CheckOrderedReadbacks(Backend &b)
{
    CKRasterizerContext *ctx = b.Context;
    SetDiffuseState(ctx);
    ReadbackCapture red, blue;
    BeginFrame(ctx, CKRST_CTXCLEAR_COLOR);
    TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kRed), "draw before first snapshot");
    TestCheck(ctx->RequestReadback(NULL, VXBUFFER_BACKBUFFER, ReadbackCapture::Callback, &red), "first snapshot");
    TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kBlue), "draw between snapshots");
    CKRECT crop = {28, 28, 36, 36};
    TestCheck(ctx->RequestReadback(&crop, VXBUFFER_BACKBUFFER, ReadbackCapture::Callback, &blue), "second cropped snapshot");
    TestCheck(red.Calls == 0 && blue.Calls == 0, "callbacks are deferred to a frame boundary");
    TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kGreen), "draw after snapshots");
    EndFrame(ctx);
    Pixels finalImage;
    ReadBackbuffer(ctx, finalImage);
    ExpectCenter(finalImage, 0, 255, 0, "later draws retain the scene attachment");
    const Uint64 deadline = SDL_GetTicks() + 5000;
    while ((red.Calls == 0 || blue.Calls == 0) && SDL_GetTicks() < deadline) {
        TestCheck(ctx->BackToFront(FALSE), "submit pending snapshots");
        SDL_PumpEvents();
        SDL_Delay(1);
    }
    TestCheck(red.Calls == 1 && red.Success, "first snapshot completes once");
    TestCheck(blue.Calls == 1 && blue.Success && blue.Width == 8 && blue.Height == 8,
              "second snapshot completes once with its crop");
    ExpectCenter(red.Image, 255, 0, 0, "first snapshot retains earlier draw");
    TestCheck(PixelNear(blue.Image, 4, 4, 0, 0, 255), "second snapshot retains intermediate draw");
}

void CheckReadbackShutdown(Backend &b)
{
    ReadbackCapture capture;
    BeginFrame(b.Context, CKRST_CTXCLEAR_COLOR);
    TestCheck(DrawColorTriangle(b.Context, kCenterTriangle, kGreen), "draw before shutdown snapshot");
    TestCheck(b.Context->RequestReadback(NULL, VXBUFFER_BACKBUFFER, ReadbackCapture::Callback, &capture),
              "readback before shutdown");
    TestCheck(b.Context->BeginShutdown(), "shutdown with an unsubmitted readback");
    TestCheck(capture.Calls == 1 && !capture.Success, "shutdown cancels the consumer exactly once");
    TestCheck(b.Context->BeginShutdown() && capture.Calls == 1, "repeated shutdown does not repeat the callback");
}

void CheckResizeAndReadback(Backend &b)
{
    CKRasterizerContext *ctx = b.Context;
    SetDiffuseState(ctx);
    Pixels beforeResize;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kGreen), "draw before pending resize");
    }, beforeResize);
    ReadbackCapture pendingResize;
    TestCheck(ctx->RequestReadback(NULL, VXBUFFER_BACKBUFFER, ReadbackCapture::Callback, &pendingResize),
              "queue readback immediately before target destruction");
    TestCheck(pendingResize.Calls == 0, "resize starts with an outstanding callback");
    TestCheck(SDL_SetWindowSize(b.Window, 320, 640) && SDL_SyncWindow(b.Window), "portrait window resize");
    TestCheck(ctx->Resize(0, 0, 48, 96, VX_RESIZE_NOMOVE), "portrait context resize with Player flags");
    const CKRasterizerContextDesc resized = ReadContextDesc(ctx);
    TestCheck(resized.Width == 48 && resized.Height == 96,
              "resized context size");

    SetDiffuseState(ctx);
    ReadbackCapture capture;
    BeginFrame(ctx, CKRST_CTXCLEAR_COLOR);
    TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kGreen), "draw after resize");
    TestCheck(ctx->RequestReadback(NULL, VXBUFFER_BACKBUFFER, ReadbackCapture::Callback, &capture), "RequestReadback");
    TestCheck(ctx->EndScene(), "EndScene");
    // The callback arrives at a frame boundary once the backend has the pixels.
    for (int i = 0; i < 16 && capture.Calls == 0; ++i) {
        TestCheck(ctx->BackToFront(FALSE), "BackToFront while waiting for the readback");
        if (capture.Calls == 0) {
            BeginFrame(ctx, CKRST_CTXCLEAR_COLOR);
            TestCheck(ctx->EndScene(), "EndScene (readback wait)");
        }
    }
    TestCheckf(capture.Calls == 1 && capture.Success && capture.Width == 48 && capture.Height == 96,
               "asynchronous readback: calls=%d ok=%d size=%dx%d", capture.Calls, (int)capture.Success,
               capture.Width, capture.Height);

    Pixels pixels;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kGreen), "draw after resize");
    }, pixels);
    TestCheck(pixels.Width == 48 && pixels.Height == 96, "synchronous readback follows the new size");
    TestCheckf(PixelNear(pixels, 24, 48, 0, 255, 0), "portrait frame centre must be green");
#ifndef CKRE_PIXEL_SDL_GPU
    auto *native = static_cast<CKBgfxRasterizerContext *>(ctx);
    const CKRECT presented = native->GetWindowViewRectForTests();
    int drawableWidth = 0, drawableHeight = 0;
    TestCheck(SDL_GetWindowSizeInPixels(b.Window, &drawableWidth, &drawableHeight), "query actual drawable size");
    TestCheck(presented.left == 0 && presented.top == 0 && presented.right == drawableWidth && presented.bottom == drawableHeight,
              "bgfx presents the logical image across the whole drawable window");
#endif
    TestCheck(ctx->IsIdle(), "idle after the readbacks");
    TestCheck(pendingResize.Calls == 1 && pendingResize.Success && pendingResize.Width == 64 && pendingResize.Height == 64,
              "outstanding readback retains its old dimensions across resize");
    ExpectCenter(pendingResize.Image, 0, 255, 0, "outstanding readback retains pixels across resize");
}

// Pixels on the frame that are neither the background nor the flat green of
// the triangle: a multisampled scene target blends the diagonal edge.
int CountBlendedGreenPixels(const Pixels &pixels)
{
    int count = 0;
    for (int y = 0; y < pixels.Height; ++y) {
        for (int x = 0; x < pixels.Width; ++x) {
            CKBYTE bgra[4];
            GetPixel(pixels, x, y, bgra);
            if (bgra[1] > 24 && bgra[1] < 232 && bgra[2] < 24 && bgra[0] < 24)
                ++count;
        }
    }
    return count;
}

// Presentation options (spec 4.4): the scene renders into the internal scene
// target (MSAA, RenderScale) and the readback still delivers window-size
// pixels of the presented frame.
void CheckPresentation(Backend &b)
{
    CKRasterizerContext *ctx = b.Context;
    SetDiffuseState(ctx);
    // Hypotenuse from the bottom-left to the top-right corner.
    const VxVector diagonal[3] = {VxVector(-0.9f, -0.9f, 0.5f), VxVector(0.9f, -0.9f, 0.5f), VxVector(0.9f, 0.9f, 0.5f)};

    Pixels plain;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawColorTriangle(ctx, diagonal, kGreen), "diagonal triangle");
    }, plain);
    const int plainBlended = CountBlendedGreenPixels(plain);
    TestCheckf(PixelNear(plain, 40, 80, 0, 255, 0), "inside of the diagonal triangle must be green");

    CKRasterizerCapsDesc available;
    TestCheck(ctx->GetCaps(&available), "MSAA capabilities");
    printf("  reported max samples: %u\n", unsigned(available.MaxMSAASamples));
    CKRasterizerOptions options;
    options.MSAASamples = 3;
    TestCheck(!ctx->SetOptions(&options), "nonrepresentable MSAA sample count rejected");
    options.MSAASamples = 32;
    TestCheck(!ctx->SetOptions(&options), "MSAA sample count above device limit rejected");
    options.MSAASamples = 4;
    TestCheck(ctx->SetOptions(&options), "SetOptions(MSAA 4)");
    Pixels msaa;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawColorTriangle(ctx, diagonal, kGreen), "diagonal triangle (MSAA)");
    }, msaa);
    const int msaaBlended = CountBlendedGreenPixels(msaa);
    const CKDWORD msaaApproximated =
        ReadStats(ctx).Diagnostics[CKRST_DIAG_APPROX_MSAA];
    TestCheckf(msaa.Width == plain.Width && msaa.Height == plain.Height, "MSAA readback keeps the window size");
    TestCheckf(PixelNear(msaa, 40, 80, 0, 255, 0), "inside of the MSAA triangle must be green");
    TestCheckf(msaaApproximated == 0 && msaaBlended >= plainBlended + 16,
               "MSAA 4 must blend the diagonal edge without fallback (plain=%d msaa=%d approximated=%u)",
               plainBlended, msaaBlended, (unsigned)msaaApproximated);
    printf("  msaa: blended pixels plain=%d msaa=%d approximated=%u\n", plainBlended, msaaBlended,
           (unsigned)msaaApproximated);

    options.MSAASamples = 0;
    options.RenderScale = 0.5f;
    TestCheck(ctx->SetOptions(&options), "SetOptions(RenderScale 0.5)");
    Pixels scaled;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawColorTriangle(ctx, diagonal, kGreen), "diagonal triangle (RenderScale 0.5)");
    }, scaled);
    TestCheckf(scaled.Width == plain.Width && scaled.Height == plain.Height, "scaled readback keeps the window size");
    TestCheckf(PixelNear(scaled, 40, 80, 0, 255, 0), "inside of the scaled triangle must be green");
    TestCheckf(PixelNear(scaled, 8, 16, 0, 0, 0), "outside of the scaled triangle must stay black");

    CKRasterizerOptions defaults;
    TestCheck(ctx->SetOptions(&defaults), "SetOptions(defaults)");
    TestCheck(ctx->IsIdle(), "idle after the presentation checks");
}

// D3D viewport (spec 4.4): a sub-rectangle viewport moves and clips the
// scene; RenderScale must not change where it lands in the window.
void CheckViewport(Backend &b)
{
    CKRasterizerContext *ctx = b.Context;
    SetDiffuseState(ctx);
    const CKRasterizerContextDesc contextDesc = ReadContextDesc(ctx);
    const int width = contextDesc.Width;
    const int height = contextDesc.Height;
    CKViewportData rightHalf;
    rightHalf.ViewX = width / 2;
    rightHalf.ViewY = 0;
    rightHalf.ViewWidth = width / 2;
    rightHalf.ViewHeight = height;
    rightHalf.ViewZMin = 0.0f;
    rightHalf.ViewZMax = 1.0f;
    CKViewportData full = rightHalf;
    full.ViewX = 0;
    full.ViewWidth = width;

    const float scales[2] = {1.0f, 0.5f};
    for (int i = 0; i < 2; ++i) {
        CKRasterizerOptions options;
        options.RenderScale = scales[i];
        TestCheck(ctx->SetOptions(&options), "SetOptions(RenderScale)");
        // Larger than the viewport: the viewport edge, not the triangle, clips it.
        const VxVector covering[3] = {VxVector(-3.0f, -3.0f, 0.5f), VxVector(3.0f, -3.0f, 0.5f), VxVector(0.0f, 3.0f, 0.5f)};
        Pixels pixels;
        RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
            TestCheck(ctx->SetViewport(&rightHalf), "right-half viewport");
            TestCheck(DrawColorTriangle(ctx, covering, kGreen), "triangle in the right half");
            TestCheck(ctx->SetViewport(&full), "full viewport");
        }, pixels);
        TestCheckf(PixelNear(pixels, width * 3 / 4, height * 3 / 5, 0, 255, 0),
                   "RenderScale %.1f: the viewport triangle must land in the right half", scales[i]);
        TestCheckf(PixelNear(pixels, width / 4, height * 3 / 5, 0, 0, 0),
                   "RenderScale %.1f: the left half must stay clear", scales[i]);
        TestCheckf(PixelNear(pixels, width / 2 - 2, height * 3 / 5, 0, 0, 0) &&
                       PixelNear(pixels, width / 2 + 1, height * 3 / 5, 0, 255, 0),
                   "RenderScale %.1f: the viewport must clip at its left edge", scales[i]);
    }
    CKRasterizerOptions defaults;
    TestCheck(ctx->SetOptions(&defaults), "SetOptions(defaults)");
}

// The very first frame of a context, engine style: overlay phase, then a
// present that switches the swap chain to vsync (bgfx::reset). The frame must
// still be readable right away.
void CheckFirstFrame(Backend &b)
{
    CKRasterizerContext *ctx = b.Context;
    SetDiffuseState(ctx);
    BeginFrame(ctx, CKRST_CTXCLEAR_COLOR);
    TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kGreen), "first-frame draw");
    TestCheck(ctx->EndScene(), "first-frame EndScene");
    TestCheck(ctx->BeginOverlayPhase(), "first-frame BeginOverlayPhase");
    TestCheck(ctx->BackToFront(TRUE), "first-frame BackToFront (vsync)");
    Pixels first;
    ReadBackbuffer(ctx, first);
    TestCheck(PixelNear(first, kWidth / 2, kHeight / 2, 0, 255, 0),
              "the first frame (with a present-sync change) reads back its content");
    TestCheck(PixelNear(first, 2, 2, 0, 0, 0), "the first frame's clear colour reads back");
}

// Render-target readback (spec 5.8): the bound 2D target texture is read in
// texture pixels and in the D3D (top-down) layout on every backend.
void CheckRenderTargetReadback(Backend &b)
{
    CKRasterizerContext *ctx = b.Context;
    SetDiffuseState(ctx);
    CKTextureDesc desc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
    desc.Format.Width = 32;
    desc.Format.Height = 32;
    desc.Format.BytesPerLine = 32 * 4;
    desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_RENDERTARGET;
    desc.MipMapCount = 1;
    CKDWORD rt = 0;
    TestCheck(ctx->CreateTexture(&desc, &rt) && rt != 0, "render target texture");
    TestCheck(ctx->SetTargetTexture(rt, 32, 32, CKRST_CUBEFACE_XPOS), "SetTargetTexture");
    CKViewportData full;
    full.ViewX = 0;
    full.ViewY = 0;
    full.ViewWidth = 32;
    full.ViewHeight = 32;
    full.ViewZMin = 0.0f;
    full.ViewZMax = 1.0f;
    TestCheck(ctx->SetViewport(&full), "target viewport");
    // Upper half of the target only.
    const VxVector upper[3] = {VxVector(-1.0f, 0.0f, 0.5f), VxVector(1.0f, 0.0f, 0.5f), VxVector(0.0f, 1.0f, 0.5f)};
    Pixels pixels;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawColorTriangle(ctx, upper, kGreen), "triangle into the target");
    }, pixels);
    TestCheck(pixels.Width == 32 && pixels.Height == 32, "target readback has the texture size");
    TestCheckf(PixelNear(pixels, 16, 6, 0, 255, 0), "target readback: top of the triangle must be green");
    TestCheckf(PixelNear(pixels, 16, 26, 0, 0, 0), "target readback: the lower half must stay black (top-down layout)");
    TestCheck(ctx->SetTargetTexture(0, 0, 0, CKRST_CUBEFACE_XPOS), "release target");
    const CKRasterizerContextDesc contextDesc = ReadContextDesc(ctx);
    full.ViewWidth = contextDesc.Width;
    full.ViewHeight = contextDesc.Height;
    TestCheck(ctx->SetViewport(&full), "window viewport");
    TestCheck(ctx->DeleteObject(rt, CKRST_OBJ_TEXTURE), "delete render target");
    // The window readback still works afterwards.
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawColorTriangle(ctx, kCenterTriangle, kGreen), "window triangle after the target");
    }, pixels);
    TestCheck(pixels.Width == contextDesc.Width &&
                  pixels.Height == contextDesc.Height,
              "window readback size");
    TestCheckf(PixelNear(pixels, contextDesc.Width / 2,
                         contextDesc.Height * 3 / 5, 0, 255, 0),
               "window readback after the target");
}

void CheckTypedPersistentBufferUpdates(Backend &b)
{
#ifdef CKRE_PIXEL_SDL_GPU
    CKSdlGpuRasterizerContext *backend =
        static_cast<CKSdlGpuRasterizerContext *>(b.Context);
#else
    CKBgfxRasterizerContext *backend = static_cast<CKBgfxRasterizerContext *>(b.Context);
#endif
    float vertices[9] = {0.0f};
    CKWORD indices[3] = {0, 1, 2};

    CKBufferDesc vbDesc;
    vbDesc.Kind = CKRST_BUFFER_VERTEX;
    vbDesc.Size = sizeof(vertices);
    vbDesc.Stride = sizeof(float) * 3;
    vbDesc.Dynamic = TRUE;
    CKDWORD vb = 0;
    TestCheck(backend->CreateBuffer(&vbDesc, &vb) == CK_OK && vb != 0,
              "real backend creates a persistent vertex buffer");

    CKBufferDesc ibDesc;
    ibDesc.Kind = CKRST_BUFFER_INDEX;
    ibDesc.Size = sizeof(indices);
    ibDesc.Dynamic = TRUE;
    CKDWORD ib = 0;
    TestCheck(backend->CreateBuffer(&ibDesc, &ib) == CK_OK && ib != 0,
              "real backend creates a persistent index buffer");
    TestCheck(vb == ib, "vertex and index handle namespaces reproduce the numeric collision");
    CKBufferUpdateDesc update;
    update.Kind = CKRST_BUFFER_INDEX;
    update.Buffer = ib;
    update.Size = sizeof(indices);
    update.Data = indices;
    TestCheck(backend->UpdateBuffer(&update) == CK_OK,
              "typed index update does not resolve through the vertex namespace");
    update = CKBufferUpdateDesc();
    update.Kind = CKRST_BUFFER_VERTEX;
    update.Buffer = vb;
    update.Size = sizeof(vertices);
    update.Data = vertices;
    TestCheck(backend->UpdateBuffer(&update) == CK_OK,
              "typed vertex update resolves through the vertex namespace");
    TestCheck(backend->DestroyObject(ib, CKRST_OBJ_INDEXBUFFER) == CK_OK,
              "destroy persistent index buffer");
    TestCheck(backend->DestroyObject(vb, CKRST_OBJ_VERTEXBUFFER) == CK_OK,
              "destroy persistent vertex buffer");
}


#ifndef CKRE_PIXEL_SDL_GPU
void CheckViewExhaustionFailsWithoutOpeningAFrame(Backend &b)
{
    CKBgfxRasterizerContext *backend = static_cast<CKBgfxRasterizerContext *>(b.Context);
    const CKDWORD previousView = backend->ExchangeNextViewForTests();
    CKRenderPassDesc pass;
    pass.Rect.right = kWidth;
    pass.Rect.bottom = kHeight;
    TestCheck(backend->BeginPass(&pass) == CKERR_OUTOFMEMORY,
              "view exhaustion must reject the new pass");
    TestCheck(backend->IsIdle(), "rejected pass must not leave a frame in progress");
    backend->ExchangeNextViewForTests(previousView);
}

#endif

// ---------------------------------------------------------------------------

void BackendRendersFixedFunctionSemantics()
{
#ifdef CKRE_PIXEL_SDL_GPU
    const char *requestedBackend = GetEnvValue("CKRE_SDL_GPU_DRIVER");
    if (!requestedBackend) requestedBackend = "direct3d12";
#else
    const char *requestedBackend = GetEnvValue("CKRE_RUNTIME_BACKEND");
    if (!requestedBackend)
        requestedBackend = GetEnvValue("CKRE_BGFX_RUNTIME_BACKEND");
    if (!requestedBackend)
        requestedBackend = GetEnvValue("CKBGFX_RENDERER_BACKEND");
    if (!requestedBackend)
        requestedBackend = "opengl";
    SetEnvValue("CKBGFX_RENDERER_BACKEND", requestedBackend);
    #endif
    printf("  backend: %s\n", requestedBackend);

    TestCheckf(SDL_Init(SDL_INIT_VIDEO), "SDL video init failed: %s", SDL_GetError());

    Samples samples;
    Backend backend;
    if (OpenBackend(backend, kWidth, kHeight)) {
        if (EnvFlagEnabled("CKRE_GPU_TEST_INTERACTIVE_START")) {
            SDL_SetWindowTitle(backend.Window, "rasterizer3-pixels - press Enter to start");
            std::puts("  waiting for foreground Enter before GPU cases");
            std::fflush(stdout);
            bool start = false;
            const Uint64 deadline = SDL_GetTicks() + 120000;
            while (!start && SDL_GetTicks() < deadline) {
                SDL_Event event;
                while (SDL_PollEvent(&event))
                    if (event.type == SDL_EVENT_KEY_DOWN && event.key.windowID == SDL_GetWindowID(backend.Window) &&
                        event.key.key == SDLK_RETURN && (SDL_GetWindowFlags(backend.Window) & SDL_WINDOW_INPUT_FOCUS))
                        start = true;
                SDL_Delay(10);
            }
            TestCheck(start, "foreground start was not received");
            SDL_SetWindowTitle(backend.Window, "rasterizer3-pixels");
        }
        CheckDriverCaps(backend);
        CheckFirstFrame(backend);
        CheckTypedPersistentBufferUpdates(backend);
        RunPixelCases(backend.Context, "uber", samples);
        CheckOrderedTextureUpdates(backend);
        CheckPaddedTextureUpload(backend);
        CheckOrderedBufferUpdates(backend);
        CheckVertexBufferWrapPixels(backend);
        CheckVertexBufferPointSizePixels(backend);
        CheckLineTopologyWithPointFill(backend);
        CheckVertexBufferPointFilledStripsAndFans(backend);
        CheckPointFillTriangleCulling(backend);
        CheckPointFilledTriangleSizes(backend);
        CheckClippingDisablesUserPlanes(backend);
        CheckBorderFiltering(backend);
        CheckCopyAndRectClear(backend);
        CheckLayeredTextureUpdates(backend);
        CheckMipPreservation(backend);
        CheckMipLodBias(backend);
        CheckLayeredMinimumMip(backend);
        CheckAnisotropyLimit(backend);
        CheckLayeredAnisotropyLimit(backend);
        CheckMemoryCopyPixelIdentity(backend);
        CheckScaledTextureCopies(backend);
        CheckIndependentAttachmentClears(backend);
        CheckStencilWriteMasks(backend);
        CheckOrderedReadbacks(backend);
        CheckResizeAndReadback(backend);
        CheckPresentation(backend);
        CheckViewport(backend);
        CheckRenderTargetReadback(backend);
#ifndef CKRE_PIXEL_SDL_GPU
        CheckViewExhaustionFailsWithoutOpeningAFrame(backend);
#endif
        fflush(stdout);
        if (EnvFlagEnabled("CKRE_GPU_TEST_HOLD")) {
            // Preserve a visible final frame for desktop evidence, still pumping events.
            const Uint64 end = SDL_GetTicks() + 120000;
            bool quit = false;
            while (!quit && SDL_GetTicks() < end) {
                SDL_Event event;
                while (SDL_PollEvent(&event)) if (event.type == SDL_EVENT_QUIT) quit = true;
                SDL_Delay(16);
            }
        }
        CheckReadbackShutdown(backend);
    }
    CloseBackend(backend);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    printf("  coverage: pixelCases=%d tolerance=%d\n", (int)SAMPLE_COUNT, kTolerance);
}

} // namespace

int main(int argc, char **argv)
{
    const bool visible = argc > 1 && strcmp(argv[1], "--visible") == 0;
    if (!visible && !EnvFlagEnabled("CKRE_RUN_OPENGL_RUNTIME_TESTS") && !EnvFlagEnabled("CKRE_RUN_BGFX_BACKEND_RUNTIME_TESTS")) {
        printf("SKIPPED: set CKRE_RUN_BGFX_BACKEND_RUNTIME_TESTS=1 to run the real-backend pixel gate.\n");
        return 77;
    }

    TestFramework tests;
    tests.Run("backend renders the fixed-function semantics through the private interface",
              &BackendRendersFixedFunctionSemantics);
    return tests.ExitCode();
}
