// rasterizer3_pixel_tests: the fixed-function semantics only a real backend
// can prove, driven exclusively through the CKRasterizer v3 contract on the
// bgfx plugin (hidden SDL window). Pixels come back through
// CopyToMemoryBuffer, so the readback path is part of the gate.
//
// Gated by CKRE_RUN_BGFX_BACKEND_RUNTIME_TESTS=1 (or the older
// CKRE_RUN_OPENGL_RUNTIME_TESTS=1); the backend comes from
// CKBGFX_RENDERER_BACKEND (default opengl). Every case runs once per shader
// mode (runtime-specialized and full-specialized) and the centre pixels must
// agree, so the two shader routes cannot drift apart.

#include "CKRasterizer.h"
#include "TestTriangleMultiset.h"

#include <SDL3/SDL.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// bgfx plugin entry (CK_LIB build of CKBgfxRasterizer).
extern void CKBgfxRasterizerGetInfo(CKRasterizerInfo *info);

namespace {

const int kWidth = 64;
const int kHeight = 64;
const int kTolerance = 24;
// CopyToMemoryBuffer reads the swap chain backbuffer, which holds a frame only
// once it has been presented into every swap-chain buffer (ckre_scene_capture
// renders 3 frames for the same reason). The phase 3 virtual backbuffer makes
// a single frame readable; until then every case renders its frame this often.
const int kPresentRepeats = 3;

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
    b.Window = SDL_CreateWindow("rasterizer3-pixels", width, height, SDL_WINDOW_HIDDEN);
    TestCheckf(b.Window != NULL, "SDL hidden window creation failed: %s", SDL_GetError());
    if (!b.Window)
        return FALSE;
    CKBgfxRasterizerGetInfo(&b.Info);
    TestCheck(b.Info.InterfaceRevision == CKRST_INTERFACE_REVISION, "plugin reports the v3 revision");
    TestCheck(b.Info.StartFct != NULL && b.Info.CloseFct != NULL, "plugin entry points");
    b.Rasterizer = b.Info.StartFct((WIN_HANDLE)b.Window);
    TestCheck(b.Rasterizer != NULL, "bgfx rasterizer must start");
    if (!b.Rasterizer)
        return FALSE;
    TestCheck(b.Rasterizer->GetDriverCount() > 0, "bgfx rasterizer exposes a driver");
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
// State and frame helpers (contract only)
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

// Renders the same frame kPresentRepeats times and reads the backbuffer.
template <class Draw>
void RenderAndRead(CKRasterizerContext *ctx, CKDWORD clearFlags, const VxMatrix *projection, Draw draw,
                   Pixels &pixels)
{
    for (int i = 0; i < kPresentRepeats; ++i) {
        BeginFrame(ctx, clearFlags, projection);
        draw();
        EndFrame(ctx);
    }
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
    Textures() : Transform(0), BumpLuminance(0) {}
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

    // Neither tween endpoint covers the centre; the half-way tween does.
    SetDiffuseState(ctx);
    ctx->SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_TWEENING);
    ctx->SetRenderState(VXRENDERSTATE_TWEENFACTOR, FloatBits(0.5f));
    const VxVector tweenFrom[3] = {VxVector(-1.9f, -0.9f, 0.5f), VxVector(-0.1f, -0.9f, 0.5f), VxVector(-1.0f, 0.9f, 0.5f)};
    const VxVector tweenTo[3] = {VxVector(0.1f, -0.9f, 0.5f), VxVector(1.9f, -0.9f, 0.5f), VxVector(1.0f, 0.9f, 0.5f)};
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawTweenTriangle(ctx, tweenFrom, tweenTo, kRed), "vertex tween draw");
    }, pixels);
    snprintf(what, sizeof(what), "[%s] half-way vertex tween covers the centre", mode);
    ExpectCenter(pixels, 255, 0, 0, what);
    Record(samples, SAMPLE_TWEEN, pixels);

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

// ---------------------------------------------------------------------------
// Caps, resize and asynchronous readback
// ---------------------------------------------------------------------------

void CheckDriverCaps(const Backend &b)
{
    const Vx3DCapsDesc &caps = b.Driver->m_3DCaps;
    TestCheck(b.Driver->m_CapsUpToDate, "caps refreshed by context creation");
    TestCheck(caps.MaxTextureWidth > 0 && caps.MaxTextureHeight > 0, "texture limits lowered to the backend");
    TestCheck(b.Driver->m_TextureFormats.Size() > 0, "texture formats reported");
    // Spec 4.9.2 / gate 15: the baseline bit fields survive context creation.
    TestCheck((caps.RasterCaps & (CKRST_RASTERCAPS_FOGRANGE | CKRST_RASTERCAPS_FOGPIXEL)) ==
                  (CKRST_RASTERCAPS_FOGRANGE | CKRST_RASTERCAPS_FOGPIXEL),
              "driver caps keep the baseline FOGRANGE | FOGPIXEL bits");
    TestCheck((caps.CKRasterizerSpecificCaps & CKRST_SPECIFICCAPS_DX8) != 0 &&
                  (caps.CKRasterizerSpecificCaps & CKRST_SPECIFICCAPS_HARDWARETL) != 0,
              "driver caps report the baseline DX8 hardware T&L level");
    CKRasterizerCapsDesc contractCaps;
    TestCheck(b.Context->GetCaps(&contractCaps) && contractCaps.MaxTextureStages >= 1 &&
                  contractCaps.MaxTextureStages <= CKRST_MAX_TEXTURE_STAGES,
              "contract caps");
}

struct ReadbackCapture {
    int Calls;
    int Width;
    int Height;
    CKBOOL Success;
    ReadbackCapture() : Calls(0), Width(0), Height(0), Success(FALSE) {}
    static void Callback(void *user, const CKRECT *, VXBUFFER_TYPE, const VxImageDescEx *image, CKBOOL ok)
    {
        ReadbackCapture *capture = static_cast<ReadbackCapture *>(user);
        ++capture->Calls;
        capture->Success = ok;
        if (image) {
            capture->Width = image->Width;
            capture->Height = image->Height;
        }
    }
};

void CheckResizeAndReadback(Backend &b)
{
    CKRasterizerContext *ctx = b.Context;
    TestCheck(SDL_SetWindowSize(b.Window, 48, 96) && SDL_SyncWindow(b.Window), "portrait window resize");
    TestCheck(ctx->Resize(0, 0, 48, 96, 0), "portrait context resize");
    TestCheck(ctx->m_Width == 48 && ctx->m_Height == 96, "resized context size");

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
    TestCheck(ctx->IsIdle(), "idle after the readbacks");
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

    CKRasterizerOptions options;
    options.MSAASamples = 4;
    TestCheck(ctx->SetOptions(&options), "SetOptions(MSAA 4)");
    Pixels msaa;
    RenderAndRead(ctx, CKRST_CTXCLEAR_COLOR, NULL, [&]() {
        TestCheck(DrawColorTriangle(ctx, diagonal, kGreen), "diagonal triangle (MSAA)");
    }, msaa);
    const int msaaBlended = CountBlendedGreenPixels(msaa);
    const CKDWORD msaaApproximated = ctx->GetStats()->Diagnostics[CKRST_DIAG_APPROX_MSAA];
    TestCheckf(msaa.Width == plain.Width && msaa.Height == plain.Height, "MSAA readback keeps the window size");
    TestCheckf(PixelNear(msaa, 40, 80, 0, 255, 0), "inside of the MSAA triangle must be green");
    TestCheckf(msaaApproximated != 0 || msaaBlended >= plainBlended + 16,
               "MSAA 4 must blend the diagonal edge (plain=%d msaa=%d approximated=%u)",
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

// ---------------------------------------------------------------------------

void BackendRendersFixedFunctionSemantics()
{
    const char *requestedBackend = GetEnvValue("CKRE_RUNTIME_BACKEND");
    if (!requestedBackend)
        requestedBackend = GetEnvValue("CKRE_BGFX_RUNTIME_BACKEND");
    if (!requestedBackend)
        requestedBackend = GetEnvValue("CKBGFX_RENDERER_BACKEND");
    if (!requestedBackend)
        requestedBackend = "opengl";
    SetEnvValue("CKBGFX_RENDERER_BACKEND", requestedBackend);
    printf("  backend: %s\n", requestedBackend);

    TestCheckf(SDL_Init(SDL_INIT_VIDEO), "SDL video init failed: %s", SDL_GetError());

    Samples samples;
    Backend backend;
    if (OpenBackend(backend, kWidth, kHeight)) {
        CheckDriverCaps(backend);
        RunPixelCases(backend.Context, "uber", samples);
        CheckResizeAndReadback(backend);
        CheckPresentation(backend);
    }
    CloseBackend(backend);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    printf("  coverage: pixelCases=%d tolerance=%d\n", (int)SAMPLE_COUNT, kTolerance);
}

} // namespace

int main()
{
    if (!EnvFlagEnabled("CKRE_RUN_OPENGL_RUNTIME_TESTS") && !EnvFlagEnabled("CKRE_RUN_BGFX_BACKEND_RUNTIME_TESTS")) {
        printf("SKIPPED: set CKRE_RUN_BGFX_BACKEND_RUNTIME_TESTS=1 to run the real-backend pixel gate.\n");
        return 0;
    }

    TestFramework tests;
    tests.Run("bgfx backend renders the fixed-function semantics through the v3 contract",
              &BackendRendersFixedFunctionSemantics);
    return tests.ExitCode();
}
