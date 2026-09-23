// CKRasterizer public Context conformance tests.
//
// The cases drive the public CKRasterizerContext methods, then use explicit
// white-box access to inspect fixed-function state and recording output.
// Draw order, pass splitting, clear rectangles, targets, presents and
// readbacks remain observable without exposing test access in production APIs.
// The direct NULL implementation is checked separately.

#include <stdio.h>
#include <math.h>
#include <string.h>

#include "FFPRecordingContext.h"
#include "CKNullRasterizer.h"
#include "CKRasterizerCapsBaseline.h"
#include "FFPRecordingHarness.h"
#include "TestTriangleMultiset.h"

namespace {

struct Fixture {
    CKRasterizer *Rasterizer;
    FFPRecordingDriver *Driver;
    FFPRecordingContext *Context;
    FFPRecordingBackend *Backend;

    Fixture() : Rasterizer(NULL), Driver(NULL), Context(NULL), Backend(NULL)
    {
        TestCheck(World.CreateContext(640, 480),
                  "recording Context creation");
        Rasterizer = World.Rasterizer;
        Driver = World.Driver;
        Context = World.Context;
        Backend = World.Backend;
        TestCheck(Context != NULL && Backend != NULL, "fixture objects");
    }

    FFPRecordingWorld World;
};

CKDWORD Diag(FFPRecordingContext *ctx, CKRST_DIAGNOSTIC kind)
{
    CKRenderStats stats = {};
    ctx->GetStats(stats);
    return stats.Diagnostics[kind];
}

CKRenderStats ReadStats(CKRasterizerContext *ctx)
{
    CKRenderStats stats = {};
    ctx->GetStats(stats);
    return stats;
}

CKDWORD BoundTexture(CKRasterizerContext *ctx, int stage)
{
    CKDWORD texture = 0xFFFFFFFFu;
    TestCheck(ctx->GetTexture(stage, &texture), "GetTexture");
    return texture;
}

VxMatrix Matrix(CKRasterizerContext *ctx, VXMATRIX_TYPE type)
{
    VxMatrix m;
    Vx3DMatrixIdentity(m);
    TestCheck(ctx->GetTransformMatrix(type, m), "GetTransformMatrix");
    return m;
}

// --- Backend log ------------------------------------------------------------

// Draws are the recording backend's draws (one per public draw call; the
// postprocess composite would be one too, but the default options never
// composite).
int CountDraws(const Fixture &f)
{
    return (int)f.Backend->Log.Draws.size();
}

const FFPDrawRecord *FindDraw(const Fixture &f, int ordinal)
{
    const std::vector<FFPDrawRecord> &draws = f.Backend->Log.Draws;
    return ordinal >= 0 && (size_t)ordinal < draws.size() ? &draws[ordinal] : NULL;
}

// Clears are the pass clears with non-zero flags (every pass configures its
// clear; draw passes use flags 0).
int CountClears(const Fixture &f)
{
    int count = 0;
    for (size_t i = 0; i < f.Backend->PassClears.size(); ++i)
        if (f.Backend->PassClears[i].Flags != 0)
            ++count;
    return count;
}

const FFPPassClearRecord *FindClear(const Fixture &f, int ordinal)
{
    for (size_t i = 0; i < f.Backend->PassClears.size(); ++i) {
        if (f.Backend->PassClears[i].Flags == 0)
            continue;
        if (ordinal == 0)
            return &f.Backend->PassClears[i];
        --ordinal;
    }
    return NULL;
}

// Every begun pass is recorded once.
int CountPasses(const Fixture &f)
{
    return (int)f.Backend->Log.PassOrder.size();
}

// Passes that draw into the swap chain (frame buffer 0) within the recorded
// frames. Passes restart per frame, so this only reads right for one frame.
int CountBackbufferPasses(const Fixture &f)
{
    int count = 0;
    for (size_t i = 0; i < f.Backend->Log.PassOrder.size(); ++i)
        if (f.Backend->PassTarget(f.Backend->Log.PassOrder[i]) == 0)
            ++count;
    return count;
}

int CountPresents(const Fixture &f)
{
    return (int)f.Backend->Frames.size();
}

// --- Resource helpers -------------------------------------------------------

CKDWORD CreateTexture2D(CKRasterizerContext *ctx, int w, int h, CKDWORD flags, CKDWORD mips)
{
    CKTextureDesc desc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
    desc.Format.Width = w;
    desc.Format.Height = h;
    desc.Format.BytesPerLine = w * 4;
    desc.Flags = flags;
    desc.MipMapCount = mips;
    CKDWORD handle = 0;
    TestCheck(ctx->CreateTexture(&desc, &handle), "CreateTexture failed");
    TestCheck(handle != 0, "texture handle is 0");
    return handle;
}

CKDWORD CreateVB(CKRasterizerContext *ctx, CKDWORD format, CKDWORD count)
{
    CKVertexBufferDesc desc;
    desc.m_VertexFormat = format;
    desc.m_MaxVertexCount = count;
    CKDWORD handle = 0;
    TestCheck(ctx->CreateVertexBuffer(&desc, NULL, &handle), "CreateVertexBuffer failed");
    TestCheck(handle != 0, "vertex buffer handle is 0");
    return handle;
}

CKDWORD CreateIB(CKRasterizerContext *ctx, CKDWORD count)
{
    CKIndexBufferDesc desc;
    desc.m_MaxIndexCount = count;
    CKDWORD handle = 0;
    TestCheck(ctx->CreateIndexBuffer(&desc, NULL, &handle), "CreateIndexBuffer failed");
    TestCheck(handle != 0, "index buffer handle is 0");
    return handle;
}

void DrawTriangle(CKRasterizerContext *ctx)
{
    static VxVector positions[3] = {VxVector(0, 0, 0), VxVector(1, 0, 0), VxVector(0, 1, 0)};
    VxDrawPrimitiveData data;
    memset(&data, 0, sizeof(data));
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TR_CL_V;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    TestCheck(ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data), "DrawPrimitive failed");
}

// ---------------------------------------------------------------------------
// Lifecycle and caps
// ---------------------------------------------------------------------------

CKRasterizerContextDesc ReadContextDesc(CKRasterizerContext *context)
{
    CKRasterizerContextDesc desc = {};
    TestCheck(context && context->GetDesc(&desc), "context description query");
    return desc;
}

void TestLifecycle()
{
    Fixture f;
    CKRasterizerContextDesc contextDesc = ReadContextDesc(f.Context);
    TestCheck(contextDesc.Width == 640 && contextDesc.Height == 480,
              "context size snapshot");
    TestCheck(contextDesc.Bpp == 32 && contextDesc.ZBpp == 24 &&
                  contextDesc.StencilBpp == 8,
              "context format snapshot");
    TestCheck(f.Context->GetDeviceStatus() == CK_OK, "device status");
    TestCheck(f.Context->IsIdle(), "idle after create");
    TestCheck(!f.Context->Create(NULL, 0, 0, 10, 10, 32, FALSE, 0, 24, 8), "second Create must fail");
    TestCheck(f.Driver->GetContextCount() == 1, "driver tracks its context");

    TestCheck(f.Context->Resize(5, 6, 800, 600, 0), "Resize failed");
    contextDesc = ReadContextDesc(f.Context);
    TestCheck(contextDesc.PosX == 5 && contextDesc.PosY == 6 &&
                  contextDesc.Width == 800 && contextDesc.Height == 600,
              "resized context snapshot");

    CKRasterizerCapsDesc caps;
    TestCheck(f.Context->GetCaps(&caps), "GetCaps failed");
    TestCheck(caps.MaxTextureStages >= 1 && caps.MaxTextureStages <= CKRST_MAX_TEXTURE_STAGES, "caps stages");
    TestCheck(caps.MaxLights == CKRST_MAX_LIGHTS && caps.MaxUserClipPlanes == CKRST_MAX_USER_CLIP_PLANES, "fixed caps");
    CKRasterizerNativeCapsDesc nativeCaps;
    TestCheck(f.Driver->GetNativeCaps(&nativeCaps), "GetNativeCaps failed");
    TestCheck((caps.Features & CKRST_CAPS_TEXTURE_DXT) == (nativeCaps.Features & CKRST_CAPS_TEXTURE_DXT),
              "context DXT capability must match the formats supported by its driver");
    TestCheck(!f.Context->GetCaps(NULL), "GetCaps(NULL) must fail");
    TestCheck(CKRST_INTERFACE_REVISION == 0x00060000u,
              "direct concrete-context revision value");

    // A second context on the same driver is allowed for the recording backend.
    CKRasterizerContext *second = f.Driver->CreateContext();
    TestCheck(second != NULL, "second context");
    TestCheck(f.Driver->DestroyContext(second), "DestroyContext");
    TestCheck(!f.Driver->DestroyContext(second), "DestroyContext twice must fail");
    TestCheck(f.Driver->GetContextCount() == 1, "second context removed from the driver");
}

void TestResizeFlags()
{
    Fixture f;
    TestCheck(f.Context->Resize(5, 6, 800, 600, 0), "initial resize");
    TestCheck(f.Context->Resize(-100, -200, 1280, 720, VX_RESIZE_NOMOVE), "resize without moving");
    CKRasterizerContextDesc contextDesc = ReadContextDesc(f.Context);
    TestCheck(contextDesc.PosX == 5 && contextDesc.PosY == 6 &&
                  contextDesc.Width == 1280 && contextDesc.Height == 720,
              "NOMOVE ignores position and updates extent");
    CKViewportData viewport = f.Context->GetViewportForTests();
    TestCheck(viewport.ViewWidth == 1280 && viewport.ViewHeight == 720,
              "viewport follows the new extent");
    viewport.ViewX = 10;
    viewport.ViewWidth = 100;
    TestCheck(f.Context->SetViewport(&viewport), "set custom viewport");
    TestCheck(f.Context->Resize(7, 8, -1, 0, VX_RESIZE_NOSIZE), "move without resizing");
    contextDesc = ReadContextDesc(f.Context);
    TestCheck(contextDesc.PosX == 7 && contextDesc.PosY == 8 &&
                  contextDesc.Width == 1280 && contextDesc.Height == 720,
              "NOSIZE ignores invalid size arguments");
    viewport = f.Context->GetViewportForTests();
    TestCheck(viewport.ViewX == 10 && viewport.ViewWidth == 100,
              "move preserves a custom viewport");
    TestCheck(f.Context->Resize(-1, -1, -1, -1, VX_RESIZE_NOMOVE | VX_RESIZE_NOSIZE), "combined flags are a no-op");
    TestCheck(!f.Context->Resize(9, 10, 0, 10, 0), "zero extent rejected");
    TestCheck(!f.Context->Resize(9, 10, 32, 32, 4), "unknown flags rejected");
    f.Backend->FailResize = TRUE;
    TestCheck(!f.Context->Resize(9, 10, 320, 240, 0), "backend failure propagated");
    contextDesc = ReadContextDesc(f.Context);
    TestCheck(contextDesc.PosX == 7 && contextDesc.PosY == 8 &&
                  contextDesc.Width == 1280 && contextDesc.Height == 720,
              "failed resize preserves context dimensions");
}

// The built-in NULL backend (engine fallback when no plugin loads) must report
// the capability baseline like any driver.
void TestNullBackendDriverCaps()
{
    CKRasterizerInfo info;
    CKNullRasterizerGetInfo(&info);
    TestCheck(info.InterfaceRevision == CKRST_INTERFACE_REVISION, "interface revision");
    TestCheck(info.StartFct != NULL && info.CloseFct != NULL, "entry points");

    CKRasterizer *rasterizer = info.StartFct(NULL);
    TestCheck(rasterizer != NULL, "NULL rasterizer start");
    if (!rasterizer)
        return;
    TestCheck(rasterizer->GetDriverCount() == 1, "NULL rasterizer exposes one driver");
    CKRasterizerDriver *driver = rasterizer->GetDriver(0);
    TestCheck(driver != NULL, "driver 0 missing");
    if (!driver) {
        info.CloseFct(rasterizer);
        return;
    }

    CKRasterizerDriverDesc driverDesc = {};
    TestCheck(driver->GetDesc(&driverDesc), "driver description query");
    TestCheck(!driverDesc.Hardware && driverDesc.CapsFinal,
              "NULL driver publishes final software capabilities");
    TestCheck(strcmp(driverDesc.Description.CStr(), "NULL Rasterizer") == 0,
              "NULL driver description");

    CKRasterizerNativeCapsDesc caps;
    TestCheck(driver->GetNativeCaps(&caps), "native caps query");
    TestCheck(caps.MaxTextureStages >= 1, "MaxTextureStages");
    TestCheck(caps.MaxTextureSize >= 256, "MaxTextureSize");
    TestCheck(caps.MaxLights >= 1, "MaxLights");
    TestCheck(driver->GetTextureFormatCount() >= 1, "texture formats");
    TestCheck(driver->GetDisplayModeCount() >= 1, "display modes");
    CKTextureDesc textureFormat;
    VxDisplayMode displayMode;
    TestCheck(driver->GetTextureFormat(0, &textureFormat),
              "first texture format query");
    TestCheck(driver->GetDisplayMode(0, &displayMode),
              "first display mode query");

    // The fallback must run an empty frame.
    CKRasterizerContext *context = driver->CreateContext();
    TestCheck(context != NULL, "NULL backend context");
    if (context) {
        TestCheck(context->Create(NULL, 0, 0, 320, 240, 32, FALSE, 60, 24, 8), "NULL backend Create");
        TestCheck(context->Clear(CKRST_CTXCLEAR_ALL, 0, 1.0f, 0, 0, NULL), "NULL backend Clear");
        TestCheck(context->BeginScene() && context->EndScene(), "NULL backend scene");
        TestCheck(context->BackToFront(FALSE), "NULL backend present");
        TestCheck(driver->DestroyContext(context), "NULL backend DestroyContext");
    }
    info.CloseFct(rasterizer);
}

struct NullReadbackCapture {
    int Calls;
    CKBOOL Success;
    int Width;
    int Height;

    NullReadbackCapture() : Calls(0), Success(FALSE), Width(0), Height(0) {}

    static void Callback(void *user, const CKRECT *, VXBUFFER_TYPE,
                         const VxImageDescEx *image, CKBOOL success)
    {
        NullReadbackCapture *capture = static_cast<NullReadbackCapture *>(user);
        ++capture->Calls;
        capture->Success = success;
        if (image) {
            capture->Width = image->Width;
            capture->Height = image->Height;
        }
    }
};

void TestNullBackendResources()
{
    CKRasterizerInfo info;
    CKNullRasterizerGetInfo(&info);
    CKRasterizer *rasterizer = info.StartFct(NULL);
    TestCheck(rasterizer != NULL, "NULL rasterizer start");
    if (!rasterizer)
        return;

    CKRasterizerDriver *driver = rasterizer->GetDriver(0);
    CKRasterizerContext *context = driver ? driver->CreateContext() : NULL;
    TestCheck(context != NULL, "NULL context");
    if (!context) {
        info.CloseFct(rasterizer);
        return;
    }
    TestCheck(context->Create(NULL, 0, 0, 64, 48, 32, FALSE, 60, 24, 8),
              "NULL context create");

    const CKDWORD texture = CreateTexture2D(context, 8, 8, 0, 1);
    const CKDWORD vertexBuffer = CreateVB(context, CKRST_DP_TR_CL_V, 4);
    const CKDWORD indexBuffer = CreateIB(context, 6);
    CKBYTE *vertices = static_cast<CKBYTE *>(
        context->LockVertexBuffer(vertexBuffer, 1, 2, CKRST_LOCK_DISCARD));
    TestCheck(vertices != NULL, "lock NULL vertex buffer range");
    if (vertices)
        vertices[0] = 0x5a;
    TestCheck(context->UnlockVertexBuffer(vertexBuffer), "unlock NULL vertex buffer");
    CKWORD *indices = static_cast<CKWORD *>(
        context->LockIndexBuffer(indexBuffer, 2, 3, CKRST_LOCK_DISCARD));
    TestCheck(indices != NULL, "lock NULL index buffer range");
    if (indices)
        indices[0] = 2;
    TestCheck(context->UnlockIndexBuffer(indexBuffer), "unlock NULL index buffer");

    NullReadbackCapture capture;
    CKRECT rect = {1, 2, 3, 5};
    TestCheck(context->RequestReadback(&rect, VXBUFFER_BACKBUFFER,
                                       &NullReadbackCapture::Callback, &capture),
              "queue NULL readback");
    TestCheck(context->BackToFront(FALSE), "deliver NULL readback");
    TestCheck(capture.Calls == 1 && capture.Success &&
                  capture.Width == 2 && capture.Height == 3,
              "NULL readback result");

    TestCheck(context->FlushObjects(CKRST_OBJ_VERTEXBUFFER | CKRST_OBJ_INDEXBUFFER),
              "flush NULL buffers");
    TestCheck(context->LockVertexBuffer(vertexBuffer, 0, 1, CKRST_LOCK_DISCARD) == NULL &&
                  context->LockIndexBuffer(indexBuffer, 0, 1, CKRST_LOCK_DISCARD) == NULL,
              "flushed NULL buffers are dead");
    CKTextureDesc textureDesc;
    TestCheck(context->GetTextureDesc(texture, &textureDesc),
              "buffer flush preserves NULL texture");
    TestCheck(context->FlushObjects(CKRST_OBJ_TEXTURE), "flush NULL textures");
    TestCheck(!context->GetTextureDesc(texture, &textureDesc),
              "flushed NULL texture is dead");

    TestCheck(driver->DestroyContext(context), "destroy NULL context");
    info.CloseFct(rasterizer);
}

void TestLowerCapsHelper()
{
    Vx3DCapsDesc caps;
    Vx3DCapsDesc limits;
    memset(&caps, 0, sizeof(caps));
    memset(&limits, 0, sizeof(limits));
    caps.MaxTextureWidth = 4096;
    caps.MaxClipPlanes = 6;
    caps.MinTextureWidth = 1;
    caps.RasterCaps = 0x12345;
    limits.MaxTextureWidth = 2048;
    limits.MaxClipPlanes = 0; // 0 = no limit
    limits.MinTextureWidth = 4;
    limits.RasterCaps = 0;
    CKRSTLowerCapsToLimits(&caps, &limits);
    TestCheck(caps.MaxTextureWidth == 2048, "lowered MaxTextureWidth");
    TestCheck(caps.MaxClipPlanes == 6, "zero limit leaves value");
    TestCheck(caps.MinTextureWidth == 4, "min size raised");
    TestCheck(caps.RasterCaps == 0x12345, "bit fields untouched");
}

// ---------------------------------------------------------------------------
// Defaults and Set / Get round trips
// ---------------------------------------------------------------------------

void TestRenderStateDefaults()
{
    Fixture f;
    for (CKDWORD s = 0; s < (CKDWORD)VXRENDERSTATE_MAXSTATE; ++s) {
        CKDWORD value = 0xCDCDCDCD;
        TestCheck(f.Context->GetRenderState((VXRENDERSTATETYPE)s, &value), "GetRenderState on valid type");
        TestCheck(value == CKRSTDefaultRenderStateValue((VXRENDERSTATETYPE)s), "default render state value");
    }
    // Spot checks against the original InitDefaultRenderStatesValue table.
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_SHADEMODE) == 2, "SHADEMODE default 2");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_SRCBLEND) == 2, "SRCBLEND default ONE");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_DESTBLEND) == 1, "DESTBLEND default ZERO");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_ALPHAFUNC) == 8, "ALPHAFUNC default ALWAYS");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_STENCILFUNC) == 8, "STENCILFUNC default ALWAYS");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_STENCILMASK) == 0xFFFFFFFFu, "STENCILMASK default");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_STENCILWRITEMASK) == 0xFFFFFFFFu, "STENCILWRITEMASK default");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_FILLMODE) == 3, "FILLMODE default SOLID");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_CULLMODE) == 3, "CULLMODE default CCW");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_ZFUNC) == 4, "ZFUNC default LESSEQUAL");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_ZENABLE) == 1, "ZENABLE default");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_ZWRITEENABLE) == 1, "ZWRITEENABLE default");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_STENCILFAIL) == 1, "STENCILFAIL default KEEP");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_TEXTUREFACTOR) == 0xFF000000u, "TEXTUREFACTOR default A_MASK");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_CLIPPING) == 1, "CLIPPING default");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_LIGHTING) == 1, "LIGHTING default");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_LOCALVIEWER) == 1, "LOCALVIEWER default");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_NORMALIZENORMALS) == 1, "NORMALIZENORMALS default");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_TEXTUREPERSPECTIVE) == 0, "TEXTUREPERSPECTIVE default 0");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_ALPHABLENDENABLE) == 0, "ALPHABLENDENABLE default 0");
    TestCheck(CKRSTDefaultRenderStateValue(VXRENDERSTATE_COLORWRITEENABLE) == CKRST_COLORWRITE_ALL, "COLORWRITEENABLE default all");
    TestCheck((CKDWORD)VXRENDERSTATE_COLORWRITEENABLE == 168, "COLORWRITEENABLE value 168");
}

void TestTextureStageDefaults()
{
    Fixture f;
    for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) {
        for (CKDWORD tss = CKRST_TSS_OP; tss < (CKDWORD)CKRST_TSS_MAXSTATE; ++tss) {
            CKDWORD value = 0xCDCDCDCD;
            TestCheck(f.Context->GetTextureStageState(stage, (CKRST_TEXTURESTAGESTATETYPE)tss, &value),
                      "GetTextureStageState on valid type");
            TestCheck(value == CKRSTDefaultTextureStageStateValue(stage, (CKRST_TEXTURESTAGESTATETYPE)tss),
                      "default stage state value");
        }
        CKDWORD v = 0;
        f.Context->GetTextureStageState(stage, CKRST_TSS_TEXCOORDINDEX, &v);
        TestCheck(v == (CKDWORD)stage, "TEXCOORDINDEX defaults to stage index");
    }
    TestCheck(CKRSTDefaultTextureStageStateValue(0, CKRST_TSS_OP) == CKRST_TOP_MODULATE, "stage 0 OP MODULATE");
    TestCheck(CKRSTDefaultTextureStageStateValue(1, CKRST_TSS_OP) == CKRST_TOP_DISABLE, "stage 1 OP DISABLE");
    TestCheck(CKRSTDefaultTextureStageStateValue(0, CKRST_TSS_AOP) == CKRST_TOP_SELECTARG1, "stage 0 AOP SELECTARG1");
    TestCheck(CKRSTDefaultTextureStageStateValue(3, CKRST_TSS_ADDRESSU) == VXTEXTURE_ADDRESSWRAP, "ADDRESSU WRAP");
    TestCheck(CKRSTDefaultTextureStageStateValue(3, CKRST_TSS_MINFILTER) == VXTEXTUREFILTER_NEAREST, "MINFILTER NEAREST");
}

void TestRenderStateRoundTrip()
{
    Fixture f;
    static const CKDWORD values[] = {0, 1, 2, 0x7F, 0xDEADBEEF, 0xFFFFFFFFu};
    for (CKDWORD s = 0; s < (CKDWORD)VXRENDERSTATE_MAXSTATE; ++s) {
        for (size_t v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
            TestCheck(f.Context->SetRenderState((VXRENDERSTATETYPE)s, values[v]), "SetRenderState accepts any value");
            CKDWORD got = ~values[v];
            TestCheck(f.Context->GetRenderState((VXRENDERSTATETYPE)s, &got), "GetRenderState");
            TestCheck(got == values[v], "render state stored verbatim");
        }
    }
    TestCheck(Diag(f.Context, CKRST_DIAG_INVALID_RENDER_STATE) == 0, "no invalid state diagnostics");

    // InitDefaultRenderStatesValue restores every default.
    f.Context->InitDefaultRenderStatesValue();
    for (CKDWORD s = 0; s < (CKDWORD)VXRENDERSTATE_MAXSTATE; ++s) {
        CKDWORD got = 0;
        f.Context->GetRenderState((VXRENDERSTATETYPE)s, &got);
        TestCheck(got == CKRSTDefaultRenderStateValue((VXRENDERSTATETYPE)s), "defaults restored");
    }
}

void TestTextureStageRoundTrip()
{
    Fixture f;
    static const CKDWORD values[] = {0, 1, 0x1F, 0x00030001, 0xFFFFFFFFu};
    for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) {
        for (CKDWORD tss = CKRST_TSS_OP; tss < (CKDWORD)CKRST_TSS_MAXSTATE; ++tss) {
            for (size_t v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
                TestCheck(f.Context->SetTextureStageState(stage, (CKRST_TEXTURESTAGESTATETYPE)tss, values[v]),
                          "SetTextureStageState accepts any value");
                CKDWORD got = ~values[v];
                TestCheck(f.Context->GetTextureStageState(stage, (CKRST_TEXTURESTAGESTATETYPE)tss, &got), "Get TSS");
                TestCheck(got == values[v], "stage state stored verbatim");
            }
        }
    }
    // ADDRESS fans out to U / V / W.
    TestCheck(f.Context->SetTextureStageState(2, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSCLAMP), "set ADDRESS");
    CKDWORD u = 0, v = 0, w = 0;
    f.Context->GetTextureStageState(2, CKRST_TSS_ADDRESSU, &u);
    f.Context->GetTextureStageState(2, CKRST_TSS_ADDRESSV, &v);
    f.Context->GetTextureStageState(2, CKRST_TSS_ADDRESW, &w);
    TestCheck(u == VXTEXTURE_ADDRESSCLAMP && v == VXTEXTURE_ADDRESSCLAMP && w == VXTEXTURE_ADDRESSCLAMP,
              "ADDRESS sets U, V and W");
    TestCheck(f.Context->SetTextureStageState(2, CKRST_TSS_ADDRESSV, VXTEXTURE_ADDRESSMIRROR), "set ADDRESSV");
    f.Context->GetTextureStageState(2, CKRST_TSS_ADDRESSU, &u);
    f.Context->GetTextureStageState(2, CKRST_TSS_ADDRESSV, &v);
    TestCheck(u == VXTEXTURE_ADDRESSCLAMP && v == VXTEXTURE_ADDRESSMIRROR, "ADDRESSV independent");

    // TEXTUREMAPBLEND replaces the explicit combine states: they read 0.
    TestCheck(f.Context->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_ADD), "explicit OP");
    TestCheck(f.Context->SetTextureStageState(0, CKRST_TSS_TEXTUREMAPBLEND, VXTEXTUREBLEND_MODULATE), "TEXTUREMAPBLEND");
    CKDWORD op = 0xCDCDCDCD;
    f.Context->GetTextureStageState(0, CKRST_TSS_OP, &op);
    TestCheck(op == 0, "combine states read 0 after TEXTUREMAPBLEND");
    // A non-zero STAGEBLEND stores the derived combine states.
    TestCheck(f.Context->SetTextureStageState(1, CKRST_TSS_STAGEBLEND, STAGEBLEND(VXBLEND_ZERO, VXBLEND_SRCCOLOR)), "STAGEBLEND");
    CKDWORD aop = 0;
    f.Context->GetTextureStageState(1, CKRST_TSS_OP, &op);
    f.Context->GetTextureStageState(1, CKRST_TSS_AOP, &aop);
    TestCheck(op == CKRST_TOP_MODULATE && aop == CKRST_TOP_SELECTARG2, "STAGEBLEND derives the combine states");

    // InitDefaultRenderStatesValue restores the stage defaults too.
    f.Context->InitDefaultRenderStatesValue();
    for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) {
        for (CKDWORD tss = CKRST_TSS_OP; tss < (CKDWORD)CKRST_TSS_MAXSTATE; ++tss) {
            CKDWORD got = 0;
            f.Context->GetTextureStageState(stage, (CKRST_TEXTURESTAGESTATETYPE)tss, &got);
            TestCheck(got == CKRSTDefaultTextureStageStateValue(stage, (CKRST_TEXTURESTAGESTATETYPE)tss),
                      "stage defaults restored");
        }
    }
}

void TestInvalidStateTypesLeaveStateUnchanged()
{
    Fixture f;
    f.Context->SetRenderState(VXRENDERSTATE_ZENABLE, 0);
    f.Context->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_ADD);

    CKDWORD before[VXRENDERSTATE_MAXSTATE];
    for (CKDWORD s = 0; s < (CKDWORD)VXRENDERSTATE_MAXSTATE; ++s)
        f.Context->GetRenderState((VXRENDERSTATETYPE)s, &before[s]);

    const CKDWORD invalidDiags = Diag(f.Context, CKRST_DIAG_INVALID_RENDER_STATE);
    TestCheck(!f.Context->SetRenderState((VXRENDERSTATETYPE)VXRENDERSTATE_MAXSTATE, 1), "MAXSTATE rejected");
    TestCheck(!f.Context->SetRenderState((VXRENDERSTATETYPE)0x7FFFFFFF, 1), "huge type rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_INVALID_RENDER_STATE) == invalidDiags + 2, "invalid render state counted");
    CKDWORD dummy = 0;
    TestCheck(!f.Context->GetRenderState((VXRENDERSTATETYPE)VXRENDERSTATE_MAXSTATE, &dummy), "Get MAXSTATE rejected");

    TestCheck(!f.Context->SetTextureStageState(CKRST_MAX_TEXTURE_STAGES, CKRST_TSS_OP, 1), "stage 8 rejected");
    TestCheck(!f.Context->SetTextureStageState(-1, CKRST_TSS_OP, 1), "stage -1 rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_INVALID_STAGE_INDEX) == 2, "invalid stage index counted");
    TestCheck(!f.Context->SetTextureStageState(0, (CKRST_TEXTURESTAGESTATETYPE)0, 1), "TSS 0 rejected");
    TestCheck(!f.Context->SetTextureStageState(0, CKRST_TSS_MAXSTATE, 1), "TSS MAXSTATE rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_INVALID_STAGE_STATE) == 2, "invalid stage state counted");

    for (CKDWORD s = 0; s < (CKDWORD)VXRENDERSTATE_MAXSTATE; ++s) {
        CKDWORD after = 0;
        f.Context->GetRenderState((VXRENDERSTATETYPE)s, &after);
        TestCheck(after == before[s], "render state unchanged after invalid Set");
    }
    CKDWORD op = 0;
    f.Context->GetTextureStageState(0, CKRST_TSS_OP, &op);
    TestCheck(op == CKRST_TOP_ADD, "stage state unchanged after invalid Set");

    // Subsequent draws are still submitted.
    f.Context->BeginScene();
    DrawTriangle(f.Context);
    f.Context->EndScene();
    TestCheck(CountDraws(f) == 1, "draw submitted after invalid state calls");
}

void TestMatricesLightsClipPlanes()
{
    Fixture f;
    VxMatrix m;
    Vx3DMatrixIdentity(m);
    m[3][0] = 5.0f;
    TestCheck(f.Context->SetTransformMatrix(VXMATRIX_WORLD, m), "set WORLD");
    TestCheck(Matrix(f.Context, VXMATRIX_WORLDMATRIX(0))[3][0] == 5.0f, "WORLD aliases WORLDMATRIX(0)");
    m[3][0] = 7.0f;
    TestCheck(f.Context->SetTransformMatrix(VXMATRIX_WORLDMATRIX(0), m), "set WORLDMATRIX(0)");
    TestCheck(Matrix(f.Context, VXMATRIX_WORLD)[3][0] == 7.0f, "WORLDMATRIX(0) aliases WORLD");
    m[3][0] = 9.0f;
    TestCheck(f.Context->SetTransformMatrix(VXMATRIX_WORLDMATRIX(3), m), "set WORLDMATRIX(3)");
    TestCheck(Matrix(f.Context, VXMATRIX_WORLDMATRIX(3))[3][0] == 9.0f, "WORLDMATRIX(3) stored");
    TestCheck(Matrix(f.Context, VXMATRIX_WORLD)[3][0] == 7.0f, "WORLDMATRIX(3) does not alias WORLD");
    TestCheck(f.Context->SetTransformMatrix(VXMATRIX_VIEW, m), "set VIEW");
    TestCheck(Matrix(f.Context, VXMATRIX_VIEW)[3][0] == 9.0f, "VIEW stored");
    TestCheck(f.Context->SetTransformMatrix(VXMATRIX_PROJECTION, m), "set PROJECTION");
    for (int i = 0; i < CKRST_MAX_TEXTURE_STAGES; ++i) {
        m[3][0] = 10.0f + (float)i;
        TestCheck(f.Context->SetTransformMatrix(VXMATRIX_TEXTURE(i), m), "set TEXTURE(i)");
        TestCheck(Matrix(f.Context, VXMATRIX_TEXTURE(i))[3][0] == 10.0f + (float)i, "TEXTURE(i) stored");
    }
    TestCheck(!f.Context->SetTransformMatrix((VXMATRIX_TYPE)0, m), "matrix type 0 rejected");
    TestCheck(!f.Context->SetTransformMatrix((VXMATRIX_TYPE)4, m), "matrix type 4 rejected");
    TestCheck(!f.Context->SetTransformMatrix((VXMATRIX_TYPE)24, m), "matrix type 24 rejected");
    TestCheck(!f.Context->SetTransformMatrix(VXMATRIX_WORLDMATRIX(CKRST_MAX_WORLD_MATRICES), m), "WORLDMATRIX(4) rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_INVALID_MATRIX_TYPE) == 4, "invalid matrix types counted");
    TestCheck(!f.Context->GetTransformMatrix((VXMATRIX_TYPE)4, m), "GetTransformMatrix rejects type 4");
    TestCheck(Diag(f.Context, CKRST_DIAG_INVALID_MATRIX_TYPE) == 5, "invalid getter matrix type counted");
    TestCheck((CKDWORD)VXMATRIX_WORLD == 1 && (CKDWORD)VXMATRIX_TEXTURE0 == 16 && (CKDWORD)VXMATRIX_WMAT == 256,
              "v1 matrix enumeration values");

    CKLightData light;
    memset(&light, 0, sizeof(light));
    light.Type = VX_LIGHTPOINT;
    light.Range = 12.0f;
    for (CKDWORD i = 0; i < CKRST_MAX_LIGHTS; ++i) {
        TestCheck(f.Context->SetLight(i, &light), "SetLight in range");
        TestCheck(f.Context->EnableLight(i, TRUE), "EnableLight in range");
        TestCheck(f.Context->IsLightEnabledForTests(i) && f.Context->GetLightForTests(i).Range == 12.0f, "light stored");
    }
    TestCheck(f.Context->EnableLight(3, FALSE) && !f.Context->IsLightEnabledForTests(3), "light disabled");
    TestCheck(!f.Context->SetLight(CKRST_MAX_LIGHTS, &light), "SetLight out of range");
    TestCheck(!f.Context->EnableLight(CKRST_MAX_LIGHTS, TRUE), "EnableLight out of range");
    TestCheck(!f.Context->SetLight(0, NULL), "SetLight NULL");
    TestCheck(Diag(f.Context, CKRST_DIAG_INVALID_LIGHT_INDEX) == 2, "invalid light index counted");

    CKMaterialData material;
    memset(&material, 0, sizeof(material));
    material.SpecularPower = 3.0f;
    TestCheck(f.Context->SetMaterial(&material), "SetMaterial");
    TestCheck(f.Context->GetMaterialForTests().SpecularPower == 3.0f, "material stored");
    // NULL selects the default material (white diffuse / ambient), like the
    // engine's SetCurrentMaterial(NULL).
    TestCheck(f.Context->SetMaterial(NULL), "SetMaterial NULL");
    TestCheck(f.Context->GetMaterialForTests().SpecularPower == 0.0f &&
              f.Context->GetMaterialForTests().Diffuse.r == 1.0f, "default material");

    CKViewportData viewport;
    viewport.ViewX = 1; viewport.ViewY = 2; viewport.ViewWidth = 3; viewport.ViewHeight = 4;
    viewport.ViewZMin = 0.0f; viewport.ViewZMax = 1.0f;
    TestCheck(f.Context->SetViewport(&viewport), "SetViewport");
    TestCheck(f.Context->GetViewportForTests().ViewWidth == 3, "viewport stored");
    TestCheck(!f.Context->SetViewport(NULL), "SetViewport NULL rejected");

    VxPlane plane(VxVector(0, 1, 0), 2.0f);
    for (CKDWORD i = 0; i < CKRST_MAX_USER_CLIP_PLANES; ++i) {
        TestCheck(f.Context->SetUserClipPlane(i, plane), "SetUserClipPlane in range");
        VxPlane got;
        TestCheck(f.Context->GetUserClipPlane(i, got), "GetUserClipPlane");
        TestCheck(got.m_D == 2.0f, "clip plane stored");
    }
    TestCheck(!f.Context->SetUserClipPlane(CKRST_MAX_USER_CLIP_PLANES, plane), "clip plane out of range");
    TestCheck(Diag(f.Context, CKRST_DIAG_INVALID_CLIP_PLANE_INDEX) == 1, "invalid clip plane counted");
}

void TestTexcoordIndexHelpers()
{
    const CKDWORD packed = CKRSTPackTexcoordIndex(3, CKRST_TCI_CAMERASPACEREFLECTIONVECTOR >> 16);
    TestCheck(packed == (3u | CKRST_TCI_CAMERASPACEREFLECTIONVECTOR), "packed value");
    TestCheck(CKRSTTexcoordIndex(packed) == 3, "unpack index");
    TestCheck(CKRSTTexcoordGeneration(packed) == 3, "unpack generation");
    TestCheck(CKRSTTexcoordGeneration(5) == 0, "pass through has no generation");
    TestCheck(CKRST_TCI_SPHEREMAP == 0x00040000u, "sphere map value");
}

// ---------------------------------------------------------------------------
// Canonical vertex layout
// ---------------------------------------------------------------------------

void TestVertexLayout()
{
    CKRSTVertexLayout l;

    // Position + normal + one 2D texcoord set (CKVertex-like, 32 bytes).
    TestCheck(CKRSTGetVertexLayout(CKRST_DP_TR_CL_VNT, NULL, &l) == 32, "VNT stride");
    TestCheck(l.PositionOffset == 0 && l.PositionComponents == 3, "VNT position");
    TestCheck(l.NormalOffset == 12, "VNT normal");
    TestCheck(l.DiffuseOffset == -1 && l.SpecularOffset == -1, "VNT no colors");
    TestCheck(l.TexcoordCount == 1 && l.TexcoordOffset[0] == 24 && l.TexcoordDims[0] == 2, "VNT texcoord");

    // Position + diffuse + specular + texcoord.
    TestCheck(CKRSTGetVertexLayout(CKRST_DP_TR_CL_VCST, NULL, &l) == 28, "VCST stride");
    TestCheck(l.DiffuseOffset == 12 && l.SpecularOffset == 16 && l.TexcoordOffset[0] == 20, "VCST offsets");

    // Pre-transformed: xyzw + diffuse + specular + texcoord = 32 bytes.
    TestCheck(CKRSTGetVertexLayout(CKRST_DP_CL_VCST, NULL, &l) == 32, "TL stride");
    TestCheck(l.PositionComponents == 4 && l.DiffuseOffset == 16 && l.SpecularOffset == 20 &&
              l.TexcoordOffset[0] == 24, "TL offsets");
    TestCheck(l.NormalOffset == -1 && l.WeightCount == 0, "TL has no normal / weights");

    // Pre-transformed ignores LIGHT and weights.
    TestCheck(CKRSTGetVertexLayout(CKRST_DP_LIGHT | CKRST_DP_WEIGHTS2, NULL, &l) == 16, "TL ignores normal and weights");

    // Skinning: 2 weights + palette indices + normal.
    TestCheck(CKRSTGetVertexLayout(CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_IWEIGHT(2), NULL, &l) == 36, "skin stride");
    TestCheck(l.WeightOffset == 12 && l.WeightCount == 2 && l.BlendIndexOffset == 20 && l.NormalOffset == 24, "skin offsets");
    TestCheck(CKRSTGetVertexLayout(CKRST_DP_TRANSFORM | CKRST_DP_WEIGHT(5), NULL, &l) == 32, "five weights");
    TestCheck(l.WeightCount == 5 && l.BlendIndexOffset == -1, "five weights, no indices");

    // Tweening adds a second position (and normal when lit).
    TestCheck(CKRSTGetVertexLayout(CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_TWEEN, NULL, &l) == 48, "tween stride");
    TestCheck(l.TweenPositionOffset == 24 && l.TweenNormalOffset == 36, "tween offsets");
    TestCheck(CKRSTGetVertexLayout(CKRST_DP_TRANSFORM | CKRST_DP_TWEEN, NULL, &l) == 24, "tween without normal");
    TestCheck(l.TweenNormalOffset == -1, "no tween normal without normal");

    // Point size sits between normal and colors.
    TestCheck(CKRSTGetVertexLayout(CKRST_DP_TRANSFORM | CKRST_DP_PSIZE | CKRST_DP_DIFFUSE, NULL, &l) == 20, "psize stride");
    TestCheck(l.PointSizeOffset == 12 && l.DiffuseOffset == 16, "psize offsets");

    // Two texcoord sets with explicit dimensions.
    CKBYTE dims[CKRST_MAX_TEXTURE_STAGES] = {3, 1, 0, 0, 0, 0, 0, 0};
    TestCheck(CKRSTGetVertexLayout(CKRST_DP_TRANSFORM | CKRST_DP_STAGES1, dims, &l) == 28, "two texcoord sets");
    TestCheck(l.TexcoordCount == 2 && l.TexcoordOffset[0] == 12 && l.TexcoordDims[0] == 3 &&
              l.TexcoordOffset[1] == 24 && l.TexcoordDims[1] == 1, "texcoord dims");

    // Eight sets, default 2 floats each.
    TestCheck(CKRSTGetVertexLayout(CKRST_DP_TRANSFORM | CKRST_DP_STAGES7, NULL, &l) == 12 + 8 * 8, "eight sets");
    TestCheck(l.TexcoordCount == 8, "eight texcoord sets counted");

    TestCheck(CKRSTGetVertexSize(CKRST_DP_TR_CL_VNT, NULL) == 32, "CKRSTGetVertexSize");
}

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

void TestTextures()
{
    Fixture f;
    const CKDWORD tex = CreateTexture2D(f.Context, 64, 32, 0, 0);
    CKTextureDesc desc;
    TestCheck(f.Context->GetTextureDesc(tex, &desc), "GetTextureDesc");
    TestCheck(desc.Format.Width == 64 && desc.Format.Height == 32, "texture size");
    TestCheck(desc.MipMapCount == 1, "MipMapCount 0 means one level");
    TestCheck((desc.Flags & CKRST_TEXTURE_VALID) != 0, "VALID flag set");

    const CKDWORD mipTex = CreateTexture2D(f.Context, 64, 32, 0, CKRST_MIPMAP_GENERATE);
    TestCheck(f.Context->GetTextureDesc(mipTex, &desc), "GetTextureDesc mip");
    TestCheck(desc.MipMapCount == CKRST_MIPMAP_GENERATE, "generated mip chain request kept in the descriptor");

    // Cube maps must be square.
    CKTextureDesc cube;
    VxPixelFormat2ImageDesc(_32_ARGB8888, cube.Format);
    cube.Format.Width = 64;
    cube.Format.Height = 32;
    cube.Flags = CKRST_TEXTURE_CUBEMAP;
    CKDWORD handle = 0;
    TestCheck(!f.Context->CreateTexture(&cube, &handle) && handle == 0, "non-square cube rejected");
    cube.Format.Height = 64;
    TestCheck(f.Context->CreateTexture(&cube, &handle) && handle != 0, "square cube accepted");
    const CKDWORD cubeTex = handle;

    // Uploads.
    XArray<CKBYTE> pixels;
    pixels.Resize(64 * 32 * 4);
    VxImageDescEx image;
    VxPixelFormat2ImageDesc(_32_ARGB8888, image);
    image.Width = 64;
    image.Height = 32;
    image.BytesPerLine = 64 * 4;
    image.Image = pixels.Begin();
    const CKDWORD uploadsBefore = f.Backend->UpdatedTextureCount;
    TestCheck(f.Context->LoadTexture(tex, image, 0, CKRST_CUBEFACE_XPOS, NULL), "LoadTexture level 0");
    TestCheck(f.Backend->UpdatedTextureCount == uploadsBefore + 1 && f.Backend->LastUpdatedTexture == tex,
              "upload reached the device");
    TestCheck(!f.Context->LoadTexture(tex, image, 1, CKRST_CUBEFACE_XPOS, NULL), "level 1 of a 1-level texture rejected");
    TestCheck(!f.Context->LoadTexture(tex, image, 0, CKRST_CUBEFACE_YNEG, NULL), "cube face on 2D texture rejected");
    CKRECT region = {8, 8, 16, 16};
    TestCheck(f.Context->LoadTexture(tex, image, 0, CKRST_CUBEFACE_XPOS, &region), "region upload");
    TestCheck(f.Backend->LastTextureUpdateHadRegion && f.Backend->LastTextureUpdateRegion.right == 16, "region forwarded");
    CKRECT bad = {60, 0, 70, 8};
    TestCheck(!f.Context->LoadTexture(tex, image, 0, CKRST_CUBEFACE_XPOS, &bad), "out-of-bounds region rejected");
    TestCheck(!f.Context->LoadTexture(0, image, 0, CKRST_CUBEFACE_XPOS, NULL), "handle 0 rejected");
    image.Width = 64;
    image.Height = 64;
    image.BytesPerLine = 64 * 4;
    pixels.Resize(64 * 64 * 4);
    image.Image = pixels.Begin();
    for (int face = 0; face < CKRST_CUBEFACE_COUNT; ++face)
        TestCheck(f.Context->LoadTexture(cubeTex, image, 0, (CKRST_CUBEFACE)face, NULL), "cube face upload");
    TestCheck(!f.Context->LoadTexture(cubeTex, image, 0, (CKRST_CUBEFACE)6, NULL), "face 6 rejected");

    // Binding and deletion.
    TestCheck(f.Context->SetResourceName(tex, CKRST_OBJ_TEXTURE, "diffuse"),
              "name live texture");
    const CKRecordingObject *namedTexture =
        static_cast<const CKRecordingBackend *>(f.Backend)->FindObject(tex);
    TestCheck(namedTexture && namedTexture->Name == "diffuse",
              "resource name reaches the implementation");
    TestCheck(!f.Context->SetResourceName(tex, CKRST_OBJ_TEXTURE, NULL),
              "null resource name rejected");
    TestCheck(!f.Context->SetResourceName(tex, CKRST_OBJ_VERTEXBUFFER, "wrong kind"),
              "resource name rejects wrong kind");
    TestCheck(!f.Context->SetResourceName(0, CKRST_OBJ_TEXTURE, "invalid"),
              "resource name rejects handle zero");
    TestCheck(f.Context->SetTexture(tex, 0), "SetTexture");
    TestCheck(BoundTexture(f.Context, 0) == tex, "bound");
    TestCheck(!f.Context->SetTexture(0xBAD, 1), "unknown handle rejected");
    TestCheck(!f.Context->SetTexture(tex, CKRST_MAX_TEXTURE_STAGES), "stage 8 rejected");
    TestCheck(f.Context->SetTexture(0, 1), "unbind with 0");
    TestCheck(BoundTexture(f.Context, 1) == 0, "stage 1 unbound");
    CKDWORD dummy = 0;
    TestCheck(!f.Context->GetTexture(CKRST_MAX_TEXTURE_STAGES, &dummy), "GetTexture stage 8 rejected");
    TestCheck(!f.Context->GetTexture(0, NULL), "GetTexture NULL rejected");
    TestCheck(!f.Context->DeleteObject(tex, CKRST_OBJ_VERTEXBUFFER), "wrong type rejected");
    TestCheck(f.Context->DeleteObject(tex, CKRST_OBJ_TEXTURE), "DeleteObject");
    TestCheck(!f.Context->SetResourceName(tex, CKRST_OBJ_TEXTURE, "stale"),
              "resource name rejects deleted handle");
    TestCheck(BoundTexture(f.Context, 0) == 0, "deleted texture unbound");
    TestCheck(!f.Context->DeleteObject(tex, CKRST_OBJ_TEXTURE), "double delete rejected");
    TestCheck(!f.Context->GetTextureDesc(tex, &desc), "stale handle rejected");
    TestCheck(f.Context->GetLiveResourceCountForTests(CKRST_OBJ_TEXTURE) == 2, "two textures left");
    TestCheck(f.Context->FlushObjects(CKRST_OBJ_ALL), "FlushObjects all public resources");
    TestCheck(!f.Context->FlushObjects((CKRST_OBJECTMASK)0x00000002u),
              "unsupported object mask rejected");
    TestCheck(f.Context->GetLiveResourceCountForTests(CKRST_OBJ_ALL) == 0, "all textures flushed");

    // A deleted handle stays invalid even after new allocations.
    const CKDWORD again = CreateTexture2D(f.Context, 8, 8, 0, 0);
    TestCheck(again != tex, "new texture gets a new handle");
    TestCheck(!f.Context->GetTextureDesc(tex, &desc), "old handle still invalid after reuse");
}

void TestVolumeSliceUploads()
{
    Fixture f;
    CKTextureDesc desc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
    desc.Format.Width = desc.Format.Height = 4;
    desc.Format.BytesPerLine = 16;
    desc.Depth = 8;
    desc.MipMapCount = 3;
    desc.Flags = CKRST_TEXTURE_VOLUMEMAP | CKRST_TEXTURE_RGB;
    CKDWORD texture = 0, color = 0xff123456;
    TestCheck(f.Context->CreateTexture(&desc, &texture), "create volume texture");
    VxImageDescEx image = desc.Format;
    image.Width = image.Height = 1;
    image.BytesPerLine = 4;
    image.Image = reinterpret_cast<CKBYTE *>(&color);
    TestCheck(f.Context->LoadTexture(texture, image, 0, (CKRST_CUBEFACE)7, NULL), "upload last base slice beyond cube-face range");
    TestCheck(f.Backend->LastUpdateFace == 7, "volume slice forwarded to backend");
    TestCheck(f.Context->LoadTexture(texture, image, 1, (CKRST_CUBEFACE)3, NULL), "upload last mip-1 slice");
    TestCheck(f.Backend->LastUpdateMip == 1 && f.Backend->LastUpdateFace == 3, "mip and slice forwarded");
    TestCheck(!f.Context->LoadTexture(texture, image, 0, (CKRST_CUBEFACE)8, NULL), "reject base slice past depth");
    TestCheck(!f.Context->LoadTexture(texture, image, 1, (CKRST_CUBEFACE)4, NULL), "mip depth shrinks");
    TestCheck(!f.Context->LoadTexture(texture, image, 2, (CKRST_CUBEFACE)-1, NULL), "reject negative slice");
    TestCheck(!f.Context->LoadTexture(texture, image, 32, CKRST_CUBEFACE_XPOS, NULL), "reject invalid mip before shifting");
}

void TestBuffers()
{
    Fixture f;
    const CKDWORD vb = CreateVB(f.Context, CKRST_DP_TR_CL_VNT, 16);
    CKVertexBufferDesc vbDesc;
    TestCheck(f.Context->GetVertexBufferDesc(vb, &vbDesc), "vertex buffer descriptor");
    TestCheck(vbDesc.m_VertexSize == 32, "vertex size filled from the canonical layout");
    TestCheck(vbDesc.m_MaxVertexCount == 16, "vertex count kept");
    TestCheck(!f.Context->GetVertexBufferDesc(vb, NULL), "vertex buffer descriptor rejects NULL output");
    TestCheck(!f.Context->GetVertexBufferDesc(0, &vbDesc), "vertex buffer descriptor rejects handle zero");

    CKVertexBufferDesc mismatch;
    mismatch.m_VertexFormat = CKRST_DP_TR_CL_VNT;
    mismatch.m_MaxVertexCount = 4;
    mismatch.m_VertexSize = 20;
    CKDWORD handle = 0;
    TestCheck(!f.Context->CreateVertexBuffer(&mismatch, NULL, &handle), "vertex size mismatch rejected");
    mismatch.m_MaxVertexCount = 0;
    mismatch.m_VertexSize = 0;
    TestCheck(!f.Context->CreateVertexBuffer(&mismatch, NULL, &handle), "zero vertex count rejected");

    void *mem = f.Context->LockVertexBuffer(vb, 4, 8, CKRST_LOCK_DEFAULT);
    TestCheck(mem != NULL, "Lock returns memory");
    TestCheck(f.Context->LockVertexBuffer(vb, 0, 1, CKRST_LOCK_DEFAULT) == NULL, "double lock rejected");
    memset(mem, 0xAB, 8 * 32);
    TestCheck(f.Context->UnlockVertexBuffer(vb), "Unlock");
    TestCheck(!f.Context->UnlockVertexBuffer(vb), "Unlock when not locked rejected");
    const CKBYTE *check = static_cast<const CKBYTE *>(f.Context->LockVertexBuffer(vb, 0, 0, CKRST_LOCK_NOOVERWRITE));
    TestCheck(check != NULL, "lock whole buffer with count 0");
    TestCheck(check[4 * 32] == 0xAB && check[12 * 32 - 1] == 0xAB && check[0] == 0, "lock memory persists");
    TestCheck(f.Context->UnlockVertexBuffer(vb), "Unlock 2");
    TestCheck(f.Context->LockVertexBuffer(vb, 16, 1, CKRST_LOCK_DEFAULT) == NULL, "lock past end rejected");
    TestCheck(f.Context->LockVertexBuffer(vb, 8, 9, CKRST_LOCK_DEFAULT) == NULL, "lock range overflow rejected");
    TestCheck(f.Context->LockVertexBuffer(0, 0, 1, CKRST_LOCK_DEFAULT) == NULL, "lock handle 0 rejected");

    const CKDWORD ib = CreateIB(f.Context, 6);
    CKIndexBufferDesc ibDesc;
    TestCheck(f.Context->GetIndexBufferDesc(ib, &ibDesc), "index buffer descriptor");
    TestCheck(ibDesc.m_MaxIndexCount == 6, "index count kept");
    TestCheck(!f.Context->GetIndexBufferDesc(ib, NULL), "index buffer descriptor rejects NULL output");
    TestCheck(!f.Context->GetIndexBufferDesc(vb, &ibDesc), "index buffer descriptor rejects wrong kind");
    CKWORD *indices = static_cast<CKWORD *>(f.Context->LockIndexBuffer(ib, 0, 6, CKRST_LOCK_DISCARD));
    TestCheck(indices != NULL, "lock IB");
    for (int i = 0; i < 6; ++i)
        indices[i] = (CKWORD)i;
    TestCheck(f.Context->UnlockIndexBuffer(ib), "unlock IB");
    TestCheck(f.Context->LockIndexBuffer(ib, 6, 1, CKRST_LOCK_DEFAULT) == NULL, "IB lock past end rejected");

    // Draws from buffers.
    f.Context->BeginScene();
    TestCheck(f.Context->DrawPrimitiveVB(VX_TRIANGLELIST, vb, 0, 6, NULL, 0), "DrawPrimitiveVB");
    TestCheck(f.Context->DrawPrimitiveVBIB(VX_TRIANGLELIST, vb, ib, 0, 16, 0, 6), "DrawPrimitiveVBIB");
    TestCheck(!f.Context->DrawPrimitiveVB(VX_TRIANGLELIST, vb, 14, 6, NULL, 0), "VB range overflow rejected");
    TestCheck(!f.Context->DrawPrimitiveVBIB(VX_TRIANGLELIST, vb, ib, 0, 16, 3, 6), "IB range overflow rejected");
    TestCheck(!f.Context->DrawPrimitiveVB(VX_TRIANGLELIST, 0x12345, 0, 3, NULL, 0), "bad VB handle rejected");
    TestCheck(!f.Context->DrawPrimitiveVBIB(VX_TRIANGLELIST, vb, vb, 0, 16, 0, 6), "VB used as IB rejected");
    TestCheck(!f.Context->DrawPrimitiveVB(VX_TRIANGLELIST, vb, 0, 2, NULL, 0), "two vertices make no triangle");
    TestCheck(!f.Context->DrawPrimitiveVB((VXPRIMITIVETYPE)7, vb, 0, 6, NULL, 0), "unknown topology rejected");
    f.Context->LockVertexBuffer(vb, 0, 1, CKRST_LOCK_DEFAULT);
    TestCheck(!f.Context->DrawPrimitiveVB(VX_TRIANGLELIST, vb, 0, 6, NULL, 0), "draw from locked VB rejected");
    f.Context->UnlockVertexBuffer(vb);
    f.Context->EndScene();
    TestCheck(CountDraws(f) == 2, "two draws recorded");
    TestCheck(Diag(f.Context, CKRST_DIAG_REJECT_INVALID_HANDLE) == 3, "invalid handle rejections counted");
    TestCheck(Diag(f.Context, CKRST_DIAG_REJECT_INVALID_PARAMETER) == 12, "invalid parameter rejections counted");

    TestCheck(f.Context->DeleteObject(ib, CKRST_OBJ_INDEXBUFFER), "delete IB");
    TestCheck(f.Context->DeleteObject(vb, CKRST_OBJ_VERTEXBUFFER), "delete VB");
    TestCheck(!f.Context->GetIndexBufferDesc(ib, &ibDesc), "deleted index buffer descriptor rejected");
    TestCheck(!f.Context->GetVertexBufferDesc(vb, &vbDesc), "deleted vertex buffer descriptor rejected");
    TestCheck(f.Context->GetLiveResourceCountForTests(CKRST_OBJ_ALL) == 0, "buffers released");
}

void TestDrawPrimitiveValidation()
{
    Fixture f;
    f.Context->BeginScene();
    VxDrawPrimitiveData data;
    memset(&data, 0, sizeof(data));
    TestCheck(!f.Context->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, NULL), "NULL data rejected");
    TestCheck(!f.Context->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data), "zero vertices rejected");
    VxVector positions[4];
    data.VertexCount = 4;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    data.Flags = CKRST_DP_TR_CL_V;
    TestCheck(f.Context->DrawPrimitive(VX_TRIANGLEFAN, NULL, 0, &data), "fan accepted");
    TestCheck(f.Context->DrawPrimitive(VX_TRIANGLESTRIP, NULL, 0, &data), "strip accepted");
    TestCheck(f.Context->DrawPrimitive(VX_LINELIST, NULL, 0, &data), "line list accepted");
    TestCheck(f.Context->DrawPrimitive(VX_LINESTRIP, NULL, 0, &data), "line strip accepted");
    TestCheck(f.Context->DrawPrimitive(VX_POINTLIST, NULL, 0, &data), "point list accepted");
    CKWORD indices[6] = {0, 1, 2, 0, 2, 3};
    TestCheck(f.Context->DrawPrimitive(VX_TRIANGLELIST, indices, 6, &data), "indexed list accepted");
    TestCheck(!f.Context->DrawPrimitive(VX_TRIANGLELIST, indices, 2, &data), "two indices make no triangle");
    TestCheck(!f.Context->DrawPrimitive((VXPRIMITIVETYPE)7, NULL, 0, &data), "unknown topology rejected");
    f.Context->EndScene();
    TestCheck(CountDraws(f) == 6, "six draws submitted");
    f.Context->BackToFront(FALSE);
    const CKRenderStats stats = ReadStats(f.Context);
    TestCheck(stats.DrawCalls == 6, "stats draw calls");
    // fan 2 + strip 2 + line list 2 + line strip 3 + points 4 + indexed list 2
    TestCheck(stats.Primitives == 15, "stats primitive count per topology");
    TestCheck(Diag(f.Context, CKRST_DIAG_REJECT_INVALID_PARAMETER) == 4, "invalid parameter rejections counted");
}

// ---------------------------------------------------------------------------
// Approximations (spec 1.4 item 7, appendices C / D): states the backends
// cannot express still draw and count one APPROX_* / IGNORE_* diagnostic.
// ---------------------------------------------------------------------------

struct ApproximationCase {
    const char *Name;
    CKRST_DIAGNOSTIC Diagnostic;
    void (*Setup)(CKRasterizerContext *ctx, CKDWORD texture);
};

void DrawTexturedTriangle(CKRasterizerContext *ctx)
{
    static VxVector positions[3] = {VxVector(0, 0, 0), VxVector(1, 0, 0), VxVector(0, 1, 0)};
    static CKDWORD colors[3] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    static float uvs[3][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
    VxDrawPrimitiveData data;
    memset(&data, 0, sizeof(data));
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TR_CL_VCT;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    data.ColorPtr = colors;
    data.ColorStride = sizeof(colors[0]);
    data.TexCoordPtr = uvs;
    data.TexCoordStride = sizeof(uvs[0]);
    TestCheck(ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data), "DrawPrimitive (textured)");
}

void SetupDither(CKRasterizerContext *ctx, CKDWORD) { ctx->SetRenderState(VXRENDERSTATE_DITHERENABLE, TRUE); }
void SetupZBias(CKRasterizerContext *ctx, CKDWORD) { ctx->SetRenderState(VXRENDERSTATE_ZBIAS, 4); }
void SetupLinePattern(CKRasterizerContext *ctx, CKDWORD) { ctx->SetRenderState(VXRENDERSTATE_LINEPATTERN, 0x00FF0001u); }
void SetupEdgeAntialias(CKRasterizerContext *ctx, CKDWORD) { ctx->SetRenderState(VXRENDERSTATE_EDGEANTIALIAS, TRUE); }
void SetupClippingOff(CKRasterizerContext *ctx, CKDWORD) { ctx->SetRenderState(VXRENDERSTATE_CLIPPING, FALSE); }
void SetupSoftwareVP(CKRasterizerContext *ctx, CKDWORD) { ctx->SetRenderState(VXRENDERSTATE_SOFTWAREVPROCESSING, TRUE); }
void SetupFillPoint(CKRasterizerContext *ctx, CKDWORD) { ctx->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_POINT); }
void SetupStencilWriteMask(CKRasterizerContext *ctx, CKDWORD)
{
    ctx->SetRenderState(VXRENDERSTATE_STENCILENABLE, TRUE);
    ctx->SetRenderState(VXRENDERSTATE_STENCILPASS, VXSTENCILOP_REPLACE);
    ctx->SetRenderState(VXRENDERSTATE_STENCILWRITEMASK, 0x0F);
}
void SetupAffineTexcoords(CKRasterizerContext *ctx, CKDWORD) { ctx->SetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE, FALSE); }
void SetupStageBlend(CKRasterizerContext *ctx, CKDWORD)
{
    ctx->SetTextureStageState(0, CKRST_TSS_STAGEBLEND, STAGEBLEND(VXBLEND_SRCCOLOR, VXBLEND_DESTALPHA));
}
void SetupSamplerLod(CKRasterizerContext *ctx, CKDWORD)
{
    const float bias = 1.0f;
    CKDWORD bits = 0;
    memcpy(&bits, &bias, sizeof(bits));
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_MIPLINEAR);
    ctx->SetTextureStageState(0, CKRST_TSS_MIPMAPLODBIAS, bits);
}
void SetupAnisotropy(CKRasterizerContext *ctx, CKDWORD)
{
    ctx->SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_ANISOTROPIC);
    ctx->SetTextureStageState(0, CKRST_TSS_MAXANISOTROPY, 8);
}
void SetupMirrorOnce(CKRasterizerContext *ctx, CKDWORD) { ctx->SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSMIRRORONCE); }
void SetupAlphaBumpOp(CKRasterizerContext *ctx, CKDWORD)
{
    ctx->SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_BUMPENVMAP);
    ctx->SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);
}
void SetupBumpWithoutDuDv(CKRasterizerContext *ctx, CKDWORD)
{
    ctx->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_BUMPENVMAP);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_CURRENT);
}
void SetupTweenWithoutStreams(CKRasterizerContext *ctx, CKDWORD) { ctx->SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_TWEENING); }
void SetupBlendWithoutWeights(CKRasterizerContext *ctx, CKDWORD) { ctx->SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_1WEIGHTS); }

void TestApproximationsKeepDrawing()
{
    const ApproximationCase cases[] = {
        {"dither", CKRST_DIAG_IGNORE_DITHER, &SetupDither},
        {"z-bias", CKRST_DIAG_APPROX_ZBIAS, &SetupZBias},
        {"line pattern", CKRST_DIAG_IGNORE_LINEPATTERN, &SetupLinePattern},
        {"edge antialias", CKRST_DIAG_IGNORE_ANTIALIAS, &SetupEdgeAntialias},
        {"clipping off", CKRST_DIAG_IGNORE_CLIPPING_OFF, &SetupClippingOff},
        {"software vertex processing", CKRST_DIAG_IGNORE_SOFTWAREVPROCESSING, &SetupSoftwareVP},
        {"point fill mode", CKRST_DIAG_APPROX_FILLMODE_POINT, &SetupFillPoint},
        {"partial stencil write mask", CKRST_DIAG_APPROX_STENCIL_WRITE_MASK, &SetupStencilWriteMask},
        {"sampler LOD bias", CKRST_DIAG_IGNORE_SAMPLER_LOD, &SetupSamplerLod},
        {"anisotropy level", CKRST_DIAG_APPROX_ANISOTROPY, &SetupAnisotropy},
        {"bump op without DuDv texture", CKRST_DIAG_APPROX_BUMP_TEXTURE_FLAGS, &SetupBumpWithoutDuDv},
        {"tween without streams", CKRST_DIAG_APPROX_VERTEX_BLEND_TWEEN, &SetupTweenWithoutStreams},
        {"vertex blend without weights", CKRST_DIAG_APPROX_VERTEX_BLEND_WEIGHTS, &SetupBlendWithoutWeights},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Fixture f;
        CKRasterizerContext *ctx = f.Context;
        const CKDWORD texture = CreateTexture2D(ctx, 8, 8, 0, 0);
        TestCheck(ctx->SetTexture(texture, 0), "bind texture");
        ctx->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE);
        ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
        ctx->SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_DIFFUSE);
        cases[i].Setup(ctx, texture);

        TestCheck(ctx->BeginScene(), "BeginScene");
        const int drawsBefore = CountDraws(f);
        DrawTexturedTriangle(ctx);
        TestCheck(ctx->EndScene(), "EndScene");

        printf("  approximation: %s\n", cases[i].Name);
        TestCheck(CountDraws(f) == drawsBefore + 1, "the approximated draw must reach the device");
        TestCheck(Diag(f.Context, cases[i].Diagnostic) == 1, "the approximation diagnostic must count once");
        CKDWORD others = 0;
        for (CKDWORD code = CKRST_DIAG_APPROX_FILLMODE_POINT; code < CKRST_DIAG_COUNT; ++code) {
            if (code != (CKDWORD)cases[i].Diagnostic)
                others += Diag(f.Context, (CKRST_DIAGNOSTIC)code);
        }
        TestCheck(others == 0, "no other approximation diagnostic must count");
        TestCheck(Diag(f.Context, CKRST_DIAG_REJECT_UNSUPPORTED_STATE) == 0 &&
                      Diag(f.Context, CKRST_DIAG_REJECT_INVALID_PARAMETER) == 0,
                  "approximations must not count as rejections");
        TestCheck(ctx->DeleteObject(texture, CKRST_OBJ_TEXTURE), "delete texture");
    }

    // The public interface does not impose a backend-specific palette limit.
    {
        Fixture f;
        CKRasterizerContext *ctx = f.Context;
        const CKDWORD texture = CreateTexture2D(ctx, 8, 8, 0, 0);
        TestCheck(ctx->SetTexture(texture, 0), "bind texture");
        ctx->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE);
        ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
        ctx->SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_DIFFUSE);
        ctx->SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSBORDER);
        TestCheck(ctx->BeginScene(), "BeginScene");
        for (CKDWORD i = 0; i < 17; ++i) {
            ctx->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0xFF000000u | (i * 0x0Fu));
            DrawTexturedTriangle(ctx);
        }
        TestCheck(ctx->EndScene(), "EndScene");
        TestCheck(CountDraws(f) == 17, "seventeen border colour draws submitted");
        TestCheck(Diag(f.Context, CKRST_DIAG_APPROX_BORDER_COLOR) == 0, "the core preserves the seventeenth color without quantization");
    }

    // A fifth cube stage samples as unbound.
    {
        Fixture f;
        CKRasterizerContext *ctx = f.Context;
        CKDWORD cubes[5];
        for (int stage = 0; stage < 5; ++stage) {
            cubes[stage] = CreateTexture2D(ctx, 8, 8, CKRST_TEXTURE_CUBEMAP, 0);
            TestCheck(ctx->SetTexture(cubes[stage], stage), "bind cube");
            ctx->SetTextureStageState(stage, CKRST_TSS_OP, CKRST_TOP_MODULATE);
            ctx->SetTextureStageState(stage, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
            ctx->SetTextureStageState(stage, CKRST_TSS_ARG2, CKRST_TA_CURRENT);
            ctx->SetTextureStageState(stage, CKRST_TSS_TEXCOORDINDEX, 0);
        }
        TestCheck(ctx->BeginScene(), "BeginScene");
        DrawTexturedTriangle(ctx);
        TestCheck(ctx->EndScene(), "EndScene");
        TestCheck(CountDraws(f) == 1, "five cube stages still draw");
        TestCheck(Diag(f.Context, CKRST_DIAG_APPROX_SAMPLER_SLOTS) == 1, "the fifth cube stage counts one approximation");
    }

    // Backend-buffer points with fractional size expand to transient quads.
    {
        Fixture f;
        CKRasterizerContext *ctx = f.Context;
        const CKDWORD vb = CreateVB(ctx, CKRST_DP_TR_CL_V, 4);
        void *mem = ctx->LockVertexBuffer(vb, 0, 4, CKRST_LOCK_DEFAULT);
        TestCheck(mem != NULL, "lock point VB");
        // Untransformed positions only: the canonical stride is 12 bytes.
        memset(mem, 0, 4 * CKRSTGetVertexSize(CKRST_DP_TR_CL_V, NULL));
        TestCheck(ctx->UnlockVertexBuffer(vb), "unlock point VB");
        const float size = 2.5f;
        CKDWORD bits = 0;
        memcpy(&bits, &size, sizeof(bits));
        ctx->SetRenderState(VXRENDERSTATE_POINTSIZE, bits);
        ctx->SetRenderState(VXRENDERSTATE_WRAP0, VXWRAP_U);
        TestCheck(ctx->BeginScene(), "BeginScene");
        TestCheck(ctx->DrawPrimitiveVB(VX_POINTLIST, vb, 0, 4, NULL, 0), "point VB draw");
        TestCheck(ctx->EndScene(), "EndScene");
        TestCheck(CountDraws(f) == 1, "point VB draw submitted");
        TestCheck(Diag(f.Context, CKRST_DIAG_APPROX_POINT_SIZE) == 0,
                  "fractional point size expands without approximation");
        TestCheck(((f.Backend->Log.LastState.Mid >> 6) & 0x7u) == VX_TRIANGLELIST &&
                  !f.Backend->Log.LastVertexBytes.empty(),
                  "fractional point VB submits expanded quads");
        TestCheck(Diag(f.Context, CKRST_DIAG_IGNORE_WRAP) == 0,
                  "WRAP0 without texture coordinates needs no adjustment");
        TestCheck(ctx->DeleteObject(vb, CKRST_OBJ_VERTEXBUFFER), "delete VB");
    }
}

void TestInvalidAlphaBumpOpRejected()
{
    Fixture f;
    CKRasterizerContext *ctx = f.Context;
    const CKDWORD texture = CreateTexture2D(ctx, 8, 8, 0, 0);
    TestCheck(ctx->SetTexture(texture, 0), "bind alpha bump test texture");
    ctx->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ctx->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    SetupAlphaBumpOp(ctx, texture);
    VxVector positions[3] = {VxVector(0, 0, 0), VxVector(1, 0, 0), VxVector(0, 1, 0)};
    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TR_CL_V;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    TestCheck(ctx->BeginScene(), "begin invalid alpha bump scene");
    TestCheck(!ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data),
              "alpha bump texture op rejects draw");
    ctx->SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_BUMPENVMAPLUMINANCE);
    TestCheck(!ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data),
              "alpha luminance bump texture op rejects draw");
    TestCheck(ctx->EndScene(), "end invalid alpha bump scene");
    TestCheck(CountDraws(f) == 0 &&
                  Diag(f.Context, CKRST_DIAG_APPROX_ALPHA_BUMP_OP) == 0,
              "invalid alpha bump ops never draw or silently approximate");
    TestCheck(ctx->DeleteObject(texture, CKRST_OBJ_TEXTURE),
              "delete alpha bump test texture");
}

void TestVertexBufferWrapUsesPrimitiveCoordinates()
{
    Fixture f;
    CKRasterizerContext *ctx = f.Context;
    const CKDWORD format = CKRST_DP_TR_CL_VCT;
    CKRSTVertexLayout layout;
    const CKDWORD canonicalStride = CKRSTGetVertexLayout(format, NULL, &layout);
    std::vector<CKBYTE> vertices(canonicalStride * 4, 0);
    const float u[4] = {0.9f, 0.1f, 0.2f, 0.3f};
    for (CKDWORD i = 0; i < 4; ++i) {
        const float position[3] = {float(i & 1), float(i >> 1), 0.5f};
        const float uv[2] = {u[i], 0.0f};
        const CKDWORD white = 0xffffffffu;
        CKBYTE *vertex = vertices.data() + i * canonicalStride;
        memcpy(vertex + layout.PositionOffset, position, sizeof(position));
        memcpy(vertex + layout.TexcoordOffset[0], uv, sizeof(uv));
        memcpy(vertex + layout.DiffuseOffset, &white, sizeof(white));
    }
    CKVertexBufferDesc vbDesc;
    vbDesc.m_VertexFormat = format;
    vbDesc.m_MaxVertexCount = 4;
    vbDesc.m_Flags = CKRST_VB_WRITEONLY;
    CKDWORD vb = 0;
    TestCheck(ctx->CreateVertexBuffer(&vbDesc, vertices.data(), &vb),
              "create write-only textured VB");
    void *fourth = ctx->LockVertexBuffer(vb, 3, 1, CKRST_LOCK_NOOVERWRITE);
    TestCheck(fourth != NULL, "lock fourth write-only vertex");
    if (fourth) {
        const float updatedU = 0.4f;
        memcpy(reinterpret_cast<CKBYTE *>(fourth) + layout.TexcoordOffset[0],
               &updatedU, sizeof(float));
        TestCheck(ctx->UnlockVertexBuffer(vb), "update fourth write-only vertex");
    }

    ctx->SetRenderState(VXRENDERSTATE_WRAP0, VXWRAP_U);
    TestCheck(ctx->BeginScene(), "begin wrapped VB scene");
    TestCheck(ctx->DrawPrimitiveVB(VX_TRIANGLELIST, vb, 0, 3, NULL, 0),
              "draw wrapped non-indexed VB");
    const CKDWORD nativeStride = CKFFVertexLayout::ComputeStride(
        CKFFVertexLayout::DPFlagsToFormatFlags(format, false, true));
    auto checkCoordinates = [&](float secondU, const char *message) {
        const std::vector<CKBYTE> &bytes = f.Backend->Log.LastVertexBytes;
        TestCheck(bytes.size() == nativeStride * 3, "wrapped VB uses transient vertices");
        if (bytes.size() != nativeStride * 3)
            return;
        float actual[3] = {};
        for (int i = 0; i < 3; ++i)
            memcpy(&actual[i], bytes.data() + i * nativeStride + 12, sizeof(float));
        TestCheck(fabsf(actual[0] - 0.9f) < 0.0001f &&
                  fabsf(actual[1] - secondU) < 0.0001f &&
                  fabsf(actual[2] - 1.2f) < 0.0001f, message);
    };
    checkCoordinates(1.1f, "WRAP0 adjusts each non-indexed triangle vertex");

    CKIndexBufferDesc ibDesc;
    ibDesc.m_MaxIndexCount = 3;
    ibDesc.m_Flags = CKRST_VB_WRITEONLY;
    const CKWORD initialIndices[3] = {0, 1, 2};
    CKDWORD ib = 0;
    TestCheck(ctx->CreateIndexBuffer(&ibDesc, initialIndices, &ib),
              "create write-only triangle IB");
    CKWORD *second = static_cast<CKWORD *>(ctx->LockIndexBuffer(ib, 1, 1, CKRST_LOCK_NOOVERWRITE));
    TestCheck(second != NULL, "lock second write-only index");
    if (second) {
        *second = 3;
        TestCheck(ctx->UnlockIndexBuffer(ib), "update second write-only index");
    }
    TestCheck(ctx->DrawPrimitiveVBIB(VX_TRIANGLELIST, vb, ib, 0, 4, 0, 3),
              "draw wrapped indexed VB");
    checkCoordinates(1.4f, "WRAP0 uses updated VB and IB shadow coordinates");

    for (CKBYTE dimensions : {CKBYTE(1), CKBYTE(4)}) {
        CKRSTVertexLayout dimensionLayout;
        CKBYTE texcoordDims[CKRST_MAX_TEXTURE_STAGES] = {};
        texcoordDims[0] = dimensions;
        const CKDWORD dimensionStride = CKRSTGetVertexLayout(
            format, texcoordDims, &dimensionLayout);
        std::vector<CKBYTE> dimensionVertices(dimensionStride * 3, 0);
        for (int i = 0; i < 3; ++i) {
            CKBYTE *vertex = dimensionVertices.data() + i * dimensionStride;
            const float position[3] = {float(i & 1), float(i >> 1), 0.5f};
            const float coords[4] = {i == 0 ? 0.9f : (i == 1 ? 0.1f : 0.2f),
                                     0.25f, 0.75f, 1.0f};
            const CKDWORD white = 0xffffffffu;
            memcpy(vertex + dimensionLayout.PositionOffset, position, sizeof(position));
            memcpy(vertex + dimensionLayout.TexcoordOffset[0], coords,
                   dimensions * sizeof(float));
            memcpy(vertex + dimensionLayout.DiffuseOffset, &white, sizeof(white));
        }
        CKVertexBufferDesc dimensionDesc;
        dimensionDesc.m_VertexFormat = format;
        dimensionDesc.m_MaxVertexCount = 3;
        dimensionDesc.m_Flags = CKRST_VB_WRITEONLY;
        dimensionDesc.m_TexcoordDims[0] = dimensions;
        CKDWORD dimensionVB = 0;
        TestCheck(ctx->CreateVertexBuffer(&dimensionDesc,
                                          dimensionVertices.data(), &dimensionVB),
                  "create variable-dimension texture coordinate VB");
        TestCheck(ctx->DrawPrimitiveVB(VX_TRIANGLELIST, dimensionVB, 0, 3, NULL, 0),
                  "draw wrapped variable-dimension VB");
        checkCoordinates(1.1f, "WRAP0 adjusts variable-dimension coordinates");
        const std::vector<CKBYTE> &bytes = f.Backend->Log.LastVertexBytes;
        if (bytes.size() == nativeStride * 3) {
            float actual[4] = {};
            memcpy(actual, bytes.data() + 2 * nativeStride + 12, sizeof(actual));
            TestCheck(fabsf(actual[1] - (dimensions == 4 ? 0.25f : 0.0f)) < 0.0001f &&
                      fabsf(actual[2] - (dimensions == 4 ? 0.75f : 0.0f)) < 0.0001f &&
                      fabsf(actual[3] - (dimensions == 4 ? 1.0f : 0.0f)) < 0.0001f,
                      "WRAP0 preserves the declared texture coordinate dimensions");
        }
        TestCheck(ctx->DeleteObject(dimensionVB, CKRST_OBJ_VERTEXBUFFER),
                  "delete variable-dimension texture coordinate VB");
    }
    TestCheck(ctx->EndScene(), "end wrapped VB scene");
    TestCheck(Diag(f.Context, CKRST_DIAG_IGNORE_WRAP) == 0,
              "wrapped VB draws do not ignore WRAP0");
    TestCheck(ctx->DeleteObject(ib, CKRST_OBJ_INDEXBUFFER), "delete wrapped IB");
    TestCheck(ctx->DeleteObject(vb, CKRST_OBJ_VERTEXBUFFER), "delete wrapped VB");
}

void TestVertexBufferPointExpansionUsesPerVertexSize()
{
    Fixture f;
    CKRasterizerContext *ctx = f.Context;
    const CKDWORD format = CKRST_DP_TR_CL_VCT | CKRST_DP_LIGHT | CKRST_DP_PSIZE;
    CKRSTVertexLayout layout;
    const CKDWORD stride = CKRSTGetVertexLayout(format, NULL, &layout);
    std::vector<CKBYTE> vertices(stride * 2, 0);
    for (int i = 0; i < 2; ++i) {
        CKBYTE *vertex = vertices.data() + i * stride;
        const float size = i == 0 ? 4.0f : 10.0f;
        const CKDWORD white = 0xffffffffu;
        memcpy(vertex + layout.PointSizeOffset, &size, sizeof(size));
        memcpy(vertex + layout.DiffuseOffset, &white, sizeof(white));
    }
    CKVertexBufferDesc vbDesc;
    vbDesc.m_VertexFormat = format;
    vbDesc.m_MaxVertexCount = 2;
    vbDesc.m_Flags = CKRST_VB_WRITEONLY;
    CKDWORD vb = 0;
    TestCheck(ctx->CreateVertexBuffer(&vbDesc, vertices.data(), &vb),
              "create point-size VB with normal stream");
    CKIndexBufferDesc ibDesc;
    ibDesc.m_MaxIndexCount = 1;
    ibDesc.m_Flags = CKRST_VB_WRITEONLY;
    const CKWORD secondVertex = 1;
    CKDWORD ib = 0;
    TestCheck(ctx->CreateIndexBuffer(&ibDesc, &secondVertex, &ib),
              "create indexed point selector");
    ctx->SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    TestCheck(ctx->BeginScene(), "begin point expansion scene");

    const CKDWORD nativeStride = CKFFVertexLayout::ComputeStride(
        CKFFVertexLayout::DPFlagsToFormatFlags(format, true, true));
    auto expandedWidth = [&]() {
        const std::vector<CKBYTE> &bytes = f.Backend->Log.LastVertexBytes;
        TestCheck(bytes.size() == nativeStride * 4,
                  "per-vertex point size expands to four vertices");
        if (bytes.size() != nativeStride * 4)
            return 0.0f;
        float left = 0.0f;
        float right = 0.0f;
        memcpy(&left, bytes.data(), sizeof(float));
        memcpy(&right, bytes.data() + nativeStride, sizeof(float));
        return right - left;
    };
    TestCheck(ctx->DrawPrimitiveVB(VX_POINTLIST, vb, 0, 1, NULL, 0),
              "draw first per-vertex-size point");
    const float narrow = expandedWidth();
    TestCheck(ctx->DrawPrimitiveVBIB(VX_POINTLIST, vb, ib, 0, 2, 0, 1),
              "draw indexed per-vertex-size point");
    const float wide = expandedWidth();
    TestCheck(narrow > 0.0f && wide > narrow * 2.0f,
              "indexed point uses the selected vertex's size after the normal stream");

    ctx->SetRenderState(VXRENDERSTATE_POINTSPRITEENABLE, TRUE);
    TestCheck(ctx->DrawPrimitiveVBIB(VX_POINTLIST, vb, ib, 0, 2, 0, 1),
              "draw indexed point sprite");
    TestCheck(expandedWidth() > narrow * 2.0f,
              "indexed point sprite preserves per-vertex size");
    ctx->SetRenderState(VXRENDERSTATE_POINTSPRITEENABLE, FALSE);
    ctx->SetRenderState(VXRENDERSTATE_POINTSCALEENABLE, TRUE);
    TestCheck(ctx->DrawPrimitiveVB(VX_POINTLIST, vb, 0, 1, NULL, 0),
              "draw scaled point from VB");
    TestCheck(!f.Backend->Log.LastVertexBytes.empty(),
              "point scaling uses transient expansion");
    ctx->SetRenderState(VXRENDERSTATE_POINTSCALEENABLE, FALSE);
    TestCheck(ctx->EndScene(), "end point expansion scene");
    TestCheck(Diag(f.Context, CKRST_DIAG_APPROX_POINT_SIZE) == 0,
              "VB and VBIB point modes have no size approximation");
    TestCheck(ctx->DeleteObject(ib, CKRST_OBJ_INDEXBUFFER),
              "delete point selector");
    TestCheck(ctx->DeleteObject(vb, CKRST_OBJ_VERTEXBUFFER),
              "delete point-size VB");
}

// ---------------------------------------------------------------------------
// Frame flow
// ---------------------------------------------------------------------------

void TestDrawOrderAndMarkers()
{
    Fixture f;
    TestCheck(f.Context->Clear(CKRST_CTXCLEAR_ALL, 0, 1.0f, 0, 0, NULL), "Clear");
    TestCheck(f.Context->BeginScene(), "BeginScene");
    TestCheck(!f.Context->BeginScene(), "nested BeginScene rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_REJECT_SCENE_STATE) == 1, "nested BeginScene counted");
    for (int i = 0; i < 5; ++i) {
        char name[16];
        sprintf(name, "draw%d", i);
        f.Context->SetDebugMarker(name);
        DrawTriangle(f.Context);
    }
    TestCheck(f.Context->EndScene(), "EndScene");
    TestCheck(!f.Context->EndScene(), "EndScene twice rejected");
    // Draws after EndScene (render callbacks) are still accepted.
    DrawTriangle(f.Context);
    TestCheck(f.Context->BackToFront(FALSE), "BackToFront");

    const std::vector<FFPDrawRecord> &draws = f.Backend->Log.Draws;
    // Six scene draws into the native target, then its present blit. The
    // identity scene -> native resolve is skipped at default settings.
    TestCheck(draws.size() == 7, "six draws + present recorded");
    for (size_t i = 0; i < draws.size() && i < 5; ++i) {
        char expected[16];
        sprintf(expected, "draw%d", (int)i);
        TestCheck(draws[i].Marker == expected, "draw order matches call order");
        TestCheck(draws[i].Pass == draws[0].Pass, "all scene draws in one pass");
    }
    if (draws.size() == 7) {
        TestCheck(draws[5].Marker.Length() == 0, "marker consumed by one draw");
        TestCheck(draws[5].Pass == draws[0].Pass, "post-EndScene draw stays in the scene pass");
        TestCheck(draws[0].Target != 0, "scene draws go to the native target");
        TestCheck(draws[6].Target == 0, "present draws into the swap chain");
    }
    TestCheck(CountPasses(f) == 2, "combined clear/scene pass + present");
    TestCheck(CountBackbufferPasses(f) == 1, "only the present pass touches the swap chain");
    TestCheck(CountPresents(f) == 1, "one present");
    CKRenderStats stats = ReadStats(f.Context);
    TestCheck(stats.FrameNumber == 1, "frame counted");
    TestCheck(stats.DrawCalls == 6, "stats draw calls");
    TestCheck(stats.Primitives == 6, "stats primitives");
    TestCheck(stats.Clears == 1, "stats clears");
    TestCheck(stats.Passes == 2, "stats passes");
    TestCheck(f.Context->IsIdle(), "idle after present");
}

void TestStatsAreCopied()
{
    Fixture f;
    TestCheck(f.Context->BeginScene(), "BeginScene");
    DrawTriangle(f.Context);
    TestCheck(f.Context->EndScene(), "EndScene");
    TestCheck(f.Context->BackToFront(FALSE), "BackToFront");

    CKRenderStats first = {};
    f.Context->GetStats(first);
    TestCheck(first.FrameNumber == 1 && first.DrawCalls == 1,
              "first statistics snapshot");

    first.FrameNumber = 0xFFFFFFFFu;
    first.DrawCalls = 0xFFFFFFFFu;
    CKRenderStats second = {};
    f.Context->GetStats(second);
    TestCheck(second.FrameNumber == 1 && second.DrawCalls == 1,
              "caller changes do not mutate context statistics");
}

void TestClearRectSemantics()
{
    Fixture f;
    CKViewportData viewport;
    viewport.ViewX = 10; viewport.ViewY = 20; viewport.ViewWidth = 100; viewport.ViewHeight = 50;
    viewport.ViewZMin = 0.0f; viewport.ViewZMax = 1.0f;
    f.Context->SetViewport(&viewport);
    TestCheck(f.Context->Clear(CKRST_CTXCLEAR_COLOR, 0xFF00FF00, 1.0f, 0, 0, NULL), "viewport clear");
    const FFPPassClearRecord *clear = FindClear(f, 0);
    TestCheck(clear != NULL, "clear recorded");
    if (clear) {
        TestCheck(clear->Rect.left == 10 && clear->Rect.top == 20 && clear->Rect.right == 110 && clear->Rect.bottom == 70,
                  "RectCount 0 clears the current viewport");
        TestCheck(clear->Color == 0xFF00FF00 && clear->Flags == CKRST_CTXCLEAR_COLOR, "clear parameters");
    }
    CKRECT rects[2] = {{0, 0, 8, 8}, {16, 16, 32, 32}};
    TestCheck(f.Context->Clear(CKRST_CTXCLEAR_DEPTH, 0, 0.5f, 0, 2, rects), "two rect clear");
    TestCheck(CountClears(f) == 3, "one clear per rectangle");
    const FFPPassClearRecord *second = FindClear(f, 2);
    TestCheck(second && second->Rect.left == 16 && second->Rect.bottom == 32 && second->Z == 0.5f &&
              second->Flags == CKRST_CTXCLEAR_DEPTH, "second rectangle recorded");
    TestCheck(!f.Context->Clear(CKRST_CTXCLEAR_DEPTH, 0, 0.5f, 0, 2, NULL), "rect count without rects rejected");
    // Clearing nothing is a no-op.
    TestCheck(f.Context->Clear(0, 0, 1.0f, 0, 0, NULL), "empty flags accepted");
    TestCheck(CountClears(f) == 3, "empty flags clear nothing");
    f.Context->BackToFront(FALSE);
    TestCheck(ReadStats(f.Context).Clears == 3, "stats clears");
}

void TestMidSceneStencilClearSplitsPass()
{
    Fixture f;
    f.Context->Clear(CKRST_CTXCLEAR_ALL, 0, 1.0f, 0, 0, NULL);
    f.Context->BeginScene();
    f.Context->SetDebugMarker("before");
    DrawTriangle(f.Context);
    TestCheck(f.Context->Clear(CKRST_CTXCLEAR_STENCIL, 0, 1.0f, 0x7, 0, NULL), "mid-scene stencil clear");
    f.Context->SetDebugMarker("after");
    DrawTriangle(f.Context);
    f.Context->EndScene();
    f.Context->BackToFront(FALSE);

    const FFPDrawRecord *before = FindDraw(f, 0);
    const FFPDrawRecord *after = FindDraw(f, 1);
    const FFPPassClearRecord *stencilClear = FindClear(f, 1);
    TestCheck(before && after && stencilClear, "events present");
    if (before && after && stencilClear) {
        TestCheck(before->Marker == "before" && after->Marker == "after", "draw markers");
        TestCheck(before->Pass < stencilClear->Pass, "clear pass after first draw pass");
        TestCheck(stencilClear->Pass < after->Pass, "second draw pass after clear pass");
        TestCheck(stencilClear->Flags == CKRST_CTXCLEAR_STENCIL && stencilClear->Stencil == 0x7,
                  "stencil-only clear recorded with its value");
    }
    TestCheck(CountPasses(f) == 4, "four passes: clear/scene, stencil clear, scene, present");
    TestCheck(ReadStats(f.Context).Passes == 4, "stats passes");
    TestCheck(ReadStats(f.Context).Clears == 2, "stats clears");
    TestCheck(f.Context->IsInSceneForTests() == FALSE, "scene closed");
}

void TestOverlayPhase()
{
    Fixture f;
    f.Context->BeginScene();
    TestCheck(!f.Context->BeginOverlayPhase(), "overlay phase inside the scene rejected");
    DrawTriangle(f.Context);
    f.Context->EndScene();
    TestCheck(f.Context->BeginOverlayPhase(), "BeginOverlayPhase");
    TestCheck(!f.Context->BeginOverlayPhase(), "second overlay phase in a frame rejected");
    TestCheck(!f.Context->BeginScene(), "scene cannot restart after overlay began");
    TestCheck(f.Context->IsOverlayPhaseForTests(), "rejected scene keeps overlay phase active");
    DrawTriangle(f.Context);
    f.Context->BackToFront(FALSE);
    TestCheck(!f.Context->IsOverlayPhaseForTests(), "present resets overlay phase");
    const FFPDrawRecord *scene = FindDraw(f, 0);
    const FFPDrawRecord *overlay = FindDraw(f, 1);
    TestCheck(scene && overlay, "overlay events");
    const FFPDrawRecord *present = FindDraw(f, 2);
    TestCheck(present && FindDraw(f, 3) == NULL, "scene draw, overlay draw, present");
    if (scene && overlay && present) {
        TestCheck(scene->Pass == overlay->Pass, "native-size scene and overlay share a pass");
        TestCheck(scene->Target != 0, "the scene draws into the native target");
        TestCheck(overlay->Target == scene->Target, "the overlay draws into the same native target");
        TestCheck(overlay->Rect.right == 640 && overlay->Rect.bottom == 480, "overlay pass at window size");
        TestCheck(present->Target == 0, "the present blit draws into the swap chain");
    }
    TestCheck(CountPasses(f) == 2, "combined scene/overlay and present passes");
    TestCheck(CountBackbufferPasses(f) == 1, "only the present pass touches the swap chain");
    // Overlay phase resets with the frame.
    f.Context->BeginScene();
    f.Context->EndScene();
    TestCheck(f.Context->BeginOverlayPhase(), "overlay allowed again next frame");
    f.Context->BackToFront(FALSE);
    TestCheck(CountPresents(f) == 2, "two presents");
}

void TestPresentRequiresEndScene()
{
    Fixture f;
    f.Context->BeginScene();
    TestCheck(!f.Context->BackToFront(FALSE), "BackToFront inside scene rejected");
    TestCheck(!f.Context->Resize(0, 0, 320, 240, 0), "Resize inside scene rejected");
    TestCheck(!f.Context->IsIdle(), "not idle inside scene");
    // Options may change inside the scene (render callbacks do that); they
    // are stored immediately and clamped.
    CKRasterizerOptions options;
    options.DisableTextureFiltering = TRUE;
    TestCheck(f.Context->SetOptions(&options), "SetOptions inside scene accepted");
    TestCheck(f.Context->GetOptionsForTests().DisableTextureFiltering == TRUE, "options stored inside scene");
    f.Context->EndScene();
    TestCheck(f.Context->BackToFront(TRUE), "BackToFront after EndScene");
    TestCheck(CountPresents(f) == 1 && f.Backend->Frames[0] == CKRST_PRESENT_VSYNC, "vsync flag recorded");
    TestCheck(CountPresents(f) == 1 && f.Backend->Frames[0] != CKRST_PRESENT_IMMEDIATE, "vsync is not immediate");

    options.RenderScale = 9.0f;
    options.Sharpness = -1.0f;
    TestCheck(f.Context->SetOptions(&options), "SetOptions at frame boundary");
    TestCheck(f.Context->GetOptionsForTests().RenderScale == 2.0f && f.Context->GetOptionsForTests().Sharpness == 0.0f,
              "options clamped");
    options.RenderScale = 0.1f;
    TestCheck(f.Context->SetOptions(&options), "SetOptions low render scale");
    TestCheck(f.Context->GetOptionsForTests().RenderScale == 0.5f, "render scale clamped up to 0.5");
    options.RenderScale = NAN;
    options.Sharpness = NAN;
    TestCheck(f.Context->SetOptions(&options) &&
                  f.Context->GetOptionsForTests().RenderScale == 1.0f &&
                  f.Context->GetOptionsForTests().Sharpness == 0.0f,
              "non-finite presentation options use safe defaults");
    options.RenderScale = 1.25f;
    TestCheck(f.Context->SetOptions(&options) &&
                  f.Context->GetOptionsForTests().RenderScale == 1.25f,
              "options update");
    TestCheck(!f.Context->SetOptions(NULL), "NULL options rejected");
    TestCheck(f.Context->GetOptionsForTests().RenderScale == 1.25f,
              "rejected options leave the current ones");

    f.Context->BeginScene();
    f.Context->EndScene();
    TestCheck(f.Context->BackToFront(FALSE), "immediate present");
    TestCheck(CountPresents(f) == 2 && f.Backend->Frames[1] == CKRST_PRESENT_IMMEDIATE, "immediate flag recorded");
}

void TestRenderTargets()
{
    Fixture f;
    const CKDWORD rt = CreateTexture2D(f.Context, 128, 64, CKRST_TEXTURE_RENDERTARGET, 0);
    const CKDWORD plain = CreateTexture2D(f.Context, 128, 64, 0, 0);
    CKTextureDesc cubeDesc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, cubeDesc.Format);
    cubeDesc.Format.Width = cubeDesc.Format.Height = 32;
    cubeDesc.Flags = CKRST_TEXTURE_CUBEMAP | CKRST_TEXTURE_RENDERTARGET;
    CKDWORD cubeRt = 0;
    TestCheck(f.Context->CreateTexture(&cubeDesc, &cubeRt), "cube RT");

    f.Context->BeginScene();
    TestCheck(!f.Context->SetTargetTexture(rt, 0, 0, CKRST_CUBEFACE_XPOS), "SetTargetTexture inside scene rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_INVALID_TARGET) == 1, "in-scene target change counted");
    f.Context->EndScene();
    f.Context->BackToFront(FALSE);
    f.Backend->Log.Draws.clear();
    f.Backend->Log.PassOrder.clear();

    TestCheck(!f.Context->SetTargetTexture(plain, 0, 0, CKRST_CUBEFACE_XPOS), "texture without RENDERTARGET rejected");
    TestCheck(!f.Context->SetTargetTexture(rt, 0, 0, CKRST_CUBEFACE_YPOS), "cube face on 2D target rejected");
    TestCheck(!f.Context->SetTargetTexture(rt, 256, 64, CKRST_CUBEFACE_XPOS), "size other than the texture size rejected");
    TestCheck(!f.Context->SetTargetTexture(cubeRt, 0, 0, (CKRST_CUBEFACE)6), "face 6 rejected");
    TestCheck(!f.Context->SetTargetTexture(0xBEEF, 0, 0, CKRST_CUBEFACE_XPOS), "unknown handle rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_INVALID_TARGET) == 5, "invalid targets counted");
    TestCheck(Diag(f.Context, CKRST_DIAG_REJECT_INVALID_HANDLE) == 1, "unknown target handle counted");
    TestCheck(f.Context->GetTargetForTests() == 0, "target unchanged after rejections");

    TestCheck(f.Context->SetTargetTexture(rt, 0, 0, CKRST_CUBEFACE_XPOS), "2D render target");
    TestCheck(f.Context->GetTargetForTests() == rt, "target set");
    TestCheck(!f.Context->BeginOverlayPhase(), "overlay phase rejected while a texture is the target");
    TestCheck(Diag(f.Context, CKRST_DIAG_OVERLAY_ON_TARGET) == 1, "overlay on target counted");

    f.Backend->FailCreateDepthTexture = TRUE;
    TestCheck(!f.Context->SetTargetTexture(cubeRt, 32, 32, CKRST_CUBEFACE_ZNEG) &&
                  f.Context->GetTargetForTests() == rt,
              "depth creation failure preserves the selected target");
    f.Backend->FailCreateDepthTexture = FALSE;
    f.Backend->FailCreateRenderTarget = TRUE;
    TestCheck(!f.Context->SetTargetTexture(cubeRt, 32, 32, CKRST_CUBEFACE_ZNEG) &&
                  f.Context->GetTargetForTests() == rt,
              "framebuffer creation failure preserves the selected target");
    f.Backend->FailCreateRenderTarget = FALSE;

    f.Context->Clear(CKRST_CTXCLEAR_ALL, 0, 1.0f, 0, 0, NULL);
    f.Context->BeginScene();
    DrawTriangle(f.Context);
    f.Context->EndScene();
    TestCheck(f.Context->SetTargetTexture(cubeRt, 32, 32, CKRST_CUBEFACE_ZNEG), "cube face target with explicit size");
    f.Context->BeginScene();
    DrawTriangle(f.Context);
    f.Context->EndScene();
    TestCheck(f.Context->SetTargetTexture(0, 0, 0, CKRST_CUBEFACE_XPOS), "back to backbuffer");
    TestCheck(f.Context->GetTargetForTests() == 0, "target cleared");
    f.Context->BeginScene();
    DrawTriangle(f.Context);
    f.Context->EndScene();
    f.Context->BackToFront(FALSE);

    const FFPDrawRecord *d0 = FindDraw(f, 0);
    const FFPDrawRecord *d1 = FindDraw(f, 1);
    const FFPDrawRecord *d2 = FindDraw(f, 2);
    TestCheck(d0 && d1 && d2, "three draws");
    if (d0 && d1 && d2) {
        TestCheck(d0->Target == rt && d1->Target == cubeRt, "draw targets recorded");
        TestCheck(d2->Target != 0 && d2->Target != rt && d2->Target != cubeRt,
                  "the backbuffer scene draws into the internal scene target");
        TestCheck(d0->Pass < d1->Pass && d1->Pass < d2->Pass, "target change starts a new pass");
        TestCheck(d0->Rect.right == 128 && d0->Rect.bottom == 64, "size 0 means texture size");
        TestCheck(d1->Rect.right == 32 && d1->Rect.bottom == 32, "cube face pass uses the face size");
        TestCheck(d2->Rect.right == 640 && d2->Rect.bottom == 480, "backbuffer pass uses the window size");
    }
    TestCheck(CountPasses(f) == 4, "combined clear/scene + two later scene passes + present");
    TestCheck(CountBackbufferPasses(f) == 1, "only the present pass touches the swap chain");

    // Deleting the current target falls back to the backbuffer.
    TestCheck(f.Context->SetTargetTexture(rt, 0, 0, CKRST_CUBEFACE_XPOS), "target again");
    f.Backend->FailDestroyObject = TRUE;
    TestCheck(!f.Context->SetTargetTexture(0, 0, 0, CKRST_CUBEFACE_XPOS) &&
                  f.Context->GetTargetForTests() == rt,
              "native target-release failure preserves the selected target");
    f.Backend->FailDestroyObject = FALSE;

    f.Backend->FailDestroyObjectAfter = 2;
    TestCheck(!f.Context->SetTargetTexture(0, 0, 0, CKRST_CUBEFACE_XPOS) &&
                  f.Context->GetTargetForTests() == 0,
              "partial native target release falls back to the backbuffer");
    TestCheck(f.Context->SetTargetTexture(0, 0, 0, CKRST_CUBEFACE_XPOS),
              "backbuffer selection retries retained target cleanup");
    TestCheck(f.Context->SetTargetTexture(rt, 0, 0, CKRST_CUBEFACE_XPOS) &&
                  f.Context->GetTargetForTests() == rt,
              "target can be selected again after partial release recovery");
    TestCheck(f.Context->DeleteObject(rt, CKRST_OBJ_TEXTURE), "delete current target");
    TestCheck(f.Context->GetTargetForTests() == 0, "target reset after delete");

    TestCheck(f.Context->SetTexture(plain, 0), "bind texture before failed deletion");
    f.Backend->FailDestroyObject = TRUE;
    CKTextureDesc survivingDesc;
    CKDWORD survivingBinding = 0;
    TestCheck(!f.Context->DeleteObject(plain, CKRST_OBJ_TEXTURE) &&
                  f.Context->GetTextureDesc(plain, &survivingDesc) &&
                  f.Context->GetTexture(0, &survivingBinding) &&
                  survivingBinding == plain,
              "native deletion failure preserves the public texture record and binding");
    f.Backend->FailDestroyObject = FALSE;
    TestCheck(f.Context->DeleteObject(plain, CKRST_OBJ_TEXTURE),
              "texture deletion succeeds after native recovery");
}

void TestRequiredIntermediateTargetFailure()
{
    {
        FFPRecordingWorld world;
        auto *context = static_cast<FFPRecordingContext *>(world.Driver->CreateContext());
        TestCheck(context != NULL, "context allocated for required-target test");
        auto *backend = static_cast<FFPRecordingBackend *>(context->GetBackend());
        backend->RequireIntermediateTarget = TRUE;
        backend->FailCreateTexture = TRUE;
        TestCheck(!context->Create(NULL, 0, 0, 64, 64, 32, FALSE, 0, 24, 8),
                  "context creation fails when mandatory intermediate target cannot be created");
        TestCheck(backend->Log.Draws.empty(), "failed context never draws directly to swapchain");
    }
    {
        Fixture f;
        f.Backend->RequireIntermediateTarget = TRUE;
        f.Backend->FailCreateTexture = TRUE;
        TestCheck(!f.Context->Clear(CKRST_CTXCLEAR_COLOR, 0, 1, 0, 0, NULL),
                  "frame fails when mandatory intermediate target cannot be created");
        TestCheck(f.Backend->PassClears.empty() && f.Backend->Log.Draws.empty(),
                  "failed target creation cannot silently open a swapchain pass");
    }
}

void TestMSAARequestNeverFallsBack()
{
    Fixture f;
    CKRasterizerOptions options;
    options.MSAASamples = 3;
    TestCheck(!f.Context->SetOptions(&options), "nonrepresentable MSAA sample count rejected");
    TestCheck(f.Context->GetOptionsForTests().MSAASamples == 0,
              "rejected sample count leaves presentation options unchanged");
    options.MSAASamples = 32;
    TestCheck(!f.Context->SetOptions(&options), "sample count above device capability rejected");
    TestCheck(f.Context->GetOptionsForTests().MSAASamples == 0,
              "unsupported sample count leaves presentation options unchanged");

    options.MSAASamples = 4;
    TestCheck(f.Context->SetOptions(&options), "supported sample count accepted");
    f.Backend->FailCreateTexture = TRUE;
    TestCheck(!f.Context->BeginScene(), "MSAA target creation failure rejects the frame");
    TestCheck(!f.Context->BeginScene(), "failed MSAA target remains rejected on retry");
    TestCheck(CountPasses(f) == 0 && CountDraws(f) == 0,
              "failed MSAA request cannot draw into the single-sample swapchain");
    TestCheck(Diag(f.Context, CKRST_DIAG_APPROX_MSAA) == 0,
              "MSAA request is never reported as a single-sample approximation");
    TestCheck(Diag(f.Context, CKRST_DIAG_REJECT_UNSUPPORTED_STATE) == 3,
              "invalid sample counts and unavailable MSAA target are reported");

    f.Backend->FailCreateTexture = FALSE;
    options.MSAASamples = 0;
    TestCheck(f.Context->SetOptions(&options), "disable MSAA after target failure");
    TestCheck(f.Context->BeginScene(), "normal scene recovers after failed MSAA request");
    TestCheck(f.Context->EndScene() && f.Context->BackToFront(FALSE),
              "recovered scene presents");
}

void TestReadbackAndCopies()
{
    Fixture f;
    VxImageDescEx image;
    memset(&image, 0, sizeof(image));
    const int required = f.Context->CopyToMemoryBuffer(NULL, VXBUFFER_BACKBUFFER, image);
    TestCheck(required == 640 * 480 * 4, "required size for whole backbuffer");
    TestCheck(image.Width == 640 && image.Height == 480 && image.BitsPerPixel == 32, "descriptor filled");
    TestCheck(image.Image == NULL, "size query copies nothing");
    XArray<CKBYTE> pixels;
    pixels.Resize(required > 0 ? required : 1);
    memset(pixels.Begin(), 0xFF, pixels.Size());
    image.Image = pixels.Begin();
    TestCheck(f.Context->CopyToMemoryBuffer(NULL, VXBUFFER_BACKBUFFER, image) == required, "copy whole backbuffer");
    TestCheck(required > 0 && pixels[0] == 0 && pixels[required - 1] == 0, "recording backend returns a black image");
    TestCheck(f.Context->IsIdle(), "synchronous readback leaves the context idle");

    CKRECT rect = {10, 10, 20, 30};
    VxImageDescEx sub;
    memset(&sub, 0, sizeof(sub));
    TestCheck(f.Context->CopyToMemoryBuffer(&rect, VXBUFFER_BACKBUFFER, sub) == 10 * 20 * 4, "sub rect size");
    TestCheck(sub.Width == 10 && sub.Height == 20, "sub rect descriptor");
    CKRECT badRect = {600, 0, 700, 10};
    TestCheck(f.Context->CopyToMemoryBuffer(&badRect, VXBUFFER_BACKBUFFER, sub) == 0, "rect outside target rejected");
    TestCheck(f.Context->CopyToMemoryBuffer(NULL, (VXBUFFER_TYPE)8, sub) == 0, "unknown buffer rejected");

    f.Context->BeginScene();
    TestCheck(f.Context->CopyToMemoryBuffer(NULL, VXBUFFER_BACKBUFFER, image) == 0, "readback inside scene rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_REJECT_SCENE_STATE) == 1, "scene state rejection counted");
    f.Context->EndScene();
    f.Context->BackToFront(FALSE);

    // CopyToTexture reads the target back and uploads it into the texture.
    const CKDWORD tex = CreateTexture2D(f.Context, 64, 64, 0, 0);
    VxRect src(0, 0, 64, 64);
    const CKDWORD uploads = f.Backend->UpdatedTextureCount;
    TestCheck(f.Context->CopyToTexture(tex, &src, NULL, CKRST_CUBEFACE_XPOS), "CopyToTexture");
    TestCheck(f.Backend->UpdatedTextureCount == uploads + 1 && f.Backend->LastUpdatedTexture == tex,
              "CopyToTexture uploads into the texture");
    TestCheck(!f.Context->CopyToTexture(tex, &src, NULL, CKRST_CUBEFACE_XNEG), "cube face on 2D texture rejected");
    TestCheck(!f.Context->CopyToTexture(0, &src, NULL, CKRST_CUBEFACE_XPOS), "handle 0 rejected");

    // CopyFromMemoryBuffer draws the image into the target rectangle.
    XArray<CKBYTE> small;
    small.Resize(10 * 20 * 4);
    VxImageDescEx upload;
    VxPixelFormat2ImageDesc(_32_ARGB8888, upload);
    upload.Width = 10;
    upload.Height = 20;
    upload.BytesPerLine = 10 * 4;
    upload.Image = small.Begin();
    const int drawsBefore = CountDraws(f);
    TestCheck(f.Context->CopyFromMemoryBuffer(&rect, VXBUFFER_BACKBUFFER, upload) == 10 * 20 * 4, "CopyFromMemoryBuffer");
    TestCheck(CountDraws(f) == drawsBefore + 1, "copy from memory is one draw");
    TestCheck(f.Context->CopyFromMemoryBuffer(&rect, VXBUFFER_ZBUFFER, upload) == 0, "depth upload rejected");
    TestCheck(f.Context->CopyFromMemoryBuffer(&badRect, VXBUFFER_BACKBUFFER, upload) == 0, "rect outside target rejected");
    upload.Width = 64;
    TestCheck(f.Context->CopyFromMemoryBuffer(&rect, VXBUFFER_BACKBUFFER, upload) == 0, "image size must match the rect");

    struct Capture {
        static void Callback(void *user, const CKRECT *r, VXBUFFER_TYPE buffer, const VxImageDescEx *img, CKBOOL ok)
        {
            (void)r;
            (void)buffer;
            int *count = static_cast<int *>(user);
            if (ok && img && img->Width == 640 && img->Height == 480)
                ++*count;
        }
    };
    int calls = 0;
    TestCheck(f.Context->BeginScene(), "begin scene for ordered readback");
    DrawTriangle(f.Context);
    const CKDWORD readsBefore = f.Backend->ReadTextureCount;
    TestCheck(f.Context->RequestReadback(NULL, VXBUFFER_BACKBUFFER, Capture::Callback, &calls), "RequestReadback");
    TestCheck(f.Backend->ReadTextureCount == readsBefore + 1,
              "readback is encoded at the request, before subsequent draws");
    TestCheck(calls == 0, "readback not delivered before the present");
    DrawTriangle(f.Context);
    TestCheck(f.Context->EndScene(), "end scene after ordered readback");
    f.Context->BackToFront(FALSE);
    TestCheck(calls == 1, "readback callback delivered by the next present");
    TestCheck(!f.Context->RequestReadback(NULL, VXBUFFER_BACKBUFFER, NULL, NULL), "NULL callback rejected");
    TestCheck(!f.Context->RequestReadback(&badRect, VXBUFFER_BACKBUFFER, Capture::Callback, &calls), "rect outside target rejected");
    TestCheck(!f.Context->RequestReadback(NULL, VXBUFFER_ZBUFFER, Capture::Callback, &calls), "depth readback rejected");
}

void TestScaledCopyKeepsScene()
{
    Fixture f;
    const CKDWORD texture = CreateTexture2D(f.Context, 64, 64, 0, 0);
    TestCheck(f.Context->BeginScene(), "begin scaled copy scene");
    DrawTriangle(f.Context);
    const CKDWORD target = f.Backend->Log.Draws.back().Target;
    const CKDWORD reads = f.Backend->ReadTextureCount;
    const CKDWORD uploads = f.Backend->UpdatedTextureCount;
    const CKDWORD frame = f.Backend->FrameSerial;
    VxRect source(11, 17, 30, 30), destination(5, 7, 28, 30);
    TestCheck(f.Context->CopyToTexture(texture, &source, &destination, CKRST_CUBEFACE_XPOS),
              "non-integral scaled copy within a scene");
    DrawTriangle(f.Context);
    TestCheck(f.Backend->Log.Draws.back().Target == target, "draw after scaling restores the scene target");
    TestCheck(f.Backend->ReadTextureCount == reads && f.Backend->UpdatedTextureCount == uploads &&
              f.Backend->FrameSerial == frame, "in-frame scaling never reads back, uploads or submits");

    f.Backend->FailCreateTexture = TRUE;
    TestCheck(!f.Context->CopyToTexture(texture, &source, &destination, CKRST_CUBEFACE_XPOS),
              "scratch allocation failure is reported");
    f.Backend->FailCreateTexture = FALSE;
    DrawTriangle(f.Context);
    TestCheck(f.Backend->Log.Draws.back().Target == target, "failed scaling also restores the scene target");
    TestCheck(f.Context->EndScene(), "scaling preserves the logical scene");
    TestCheck(f.Context->BackToFront(FALSE), "present after successful and failed copies");
}

void TestShutdownRejectsReadbackCallbackWork()
{
    Fixture f;
    struct Capture {
        FFPRecordingContext *Context;
        int Calls = 0;
        CKBOOL Cancelled = FALSE;
        CKBOOL AcceptedReadback = FALSE;
        CKBOOL AcceptedScene = FALSE;

        static void Callback(void *user, const CKRECT *, VXBUFFER_TYPE,
                             const VxImageDescEx *image, CKBOOL ok)
        {
            auto &capture = *static_cast<Capture *>(user);
            ++capture.Calls;
            capture.Cancelled = !ok && !image;
            if (capture.Calls == 1) {
                capture.AcceptedReadback = capture.Context->RequestReadback(
                    NULL, VXBUFFER_BACKBUFFER, Callback, user);
                capture.AcceptedScene = capture.Context->BeginScene();
            }
        }
    } capture = {f.Context};
    TestCheck(f.Context->BeginScene(), "begin scene with pending readback at shutdown");
    DrawTriangle(f.Context);
    TestCheck(f.Context->RequestReadback(NULL, VXBUFFER_BACKBUFFER, Capture::Callback, &capture),
              "request readback before shutdown");
    TestCheck(f.Context->BeginShutdown(), "shutdown completes despite callback requesting new work");
    TestCheck(capture.Calls == 1 && capture.Cancelled, "pending readback is cancelled exactly once");
    TestCheck(!capture.AcceptedReadback && !capture.AcceptedScene,
              "shutdown callback cannot start another readback or scene");
    TestCheck(f.Context->IsIdle(), "callback leaves shutdown backend idle");
    TestCheck(f.Context->GetLiveResourceCountForTests(CKRST_OBJ_ALL) == 0,
              "callback leaves no live resources after shutdown");
}

void TestShutdownAfterDeviceFailure()
{
    Fixture f;
    const CKDWORD texture = CreateTexture2D(f.Context, 8, 8, 0, 0);
    TestCheck(f.Context->BeginScene(), "begin scene before device failure");
    DrawTriangle(f.Context);
    // A failed native submission can leave a pass or draw batch outstanding.
    // It cannot become idle by submitting more commands to the failed device.
    f.Backend->DeviceStatus = CKERR_INVALIDOPERATION;
    f.Backend->FrameResult = CKERR_INVALIDOPERATION;
    f.Backend->ForceNotIdle = TRUE;
    TestCheck(f.Context->BeginShutdown(), "device failure still permits context teardown");
    TestCheck(f.Context->IsIdle(), "device teardown abandons the failed native batch");
    CKTextureDesc desc;
    TestCheck(!f.Context->GetTextureDesc(texture, &desc), "failed device resources are released");
    TestCheck(f.Context->GetLiveResourceCountForTests(CKRST_OBJ_ALL) == 0, "no resources after failed device shutdown");
    TestCheck(f.Context->BeginShutdown(), "failed device shutdown is idempotent");
    TestCheck(f.Driver->DestroyContext(f.Context), "failed device context can be removed from its driver");
    f.Context = NULL;
    TestCheck(f.Driver->GetContextCount() == 0, "failed context removed from driver");
}

void TestShutdown()
{
    Fixture f;
    CreateTexture2D(f.Context, 8, 8, 0, 0);
    CreateVB(f.Context, CKRST_DP_TR_CL_V, 3);
    f.Context->BeginScene();
    // Orderly shutdown protocol (spec 4.2): BeginShutdown ends an open scene.
    TestCheck(f.Context->BeginShutdown(), "BeginShutdown");
    TestCheck(f.Context->IsIdle(), "idle after shutdown");
    TestCheck(f.Context->GetLiveResourceCountForTests(CKRST_OBJ_ALL) == 0, "resources flushed on shutdown");
    TestCheck(!f.Context->BeginScene(), "BeginScene after shutdown rejected");
    TestCheck(f.Context->BeginShutdown(), "BeginShutdown twice is harmless");
    TestCheck(f.Driver->DestroyContext(f.Context), "DestroyContext after shutdown");
    f.Context = NULL;
    TestCheck(f.Driver->GetContextCount() == 0, "context removed from the driver");
}

} // namespace

int main()
{
    TestFramework framework;
    framework.Run("lifecycle", TestLifecycle);
    framework.Run("resize flags", TestResizeFlags);
    framework.Run("NULL backend driver caps", TestNullBackendDriverCaps);
    framework.Run("NULL backend resources", TestNullBackendResources);
    framework.Run("caps lowering helper", TestLowerCapsHelper);
    framework.Run("render state defaults", TestRenderStateDefaults);
    framework.Run("texture stage defaults", TestTextureStageDefaults);
    framework.Run("render state round trip", TestRenderStateRoundTrip);
    framework.Run("texture stage round trip", TestTextureStageRoundTrip);
    framework.Run("invalid state types", TestInvalidStateTypesLeaveStateUnchanged);
    framework.Run("matrices, lights, clip planes", TestMatricesLightsClipPlanes);
    framework.Run("texcoord index helpers", TestTexcoordIndexHelpers);
    framework.Run("canonical vertex layout", TestVertexLayout);
    framework.Run("textures", TestTextures);
    framework.Run("volume slice uploads", TestVolumeSliceUploads);
    framework.Run("buffers", TestBuffers);
    framework.Run("draw primitive validation", TestDrawPrimitiveValidation);
    framework.Run("approximations keep drawing", TestApproximationsKeepDrawing);
    framework.Run("invalid alpha bump op rejected", TestInvalidAlphaBumpOpRejected);
    framework.Run("vertex buffer wrap uses primitive coordinates", TestVertexBufferWrapUsesPrimitiveCoordinates);
    framework.Run("vertex buffer point expansion uses per-vertex size", TestVertexBufferPointExpansionUsesPerVertexSize);
    framework.Run("draw order and markers", TestDrawOrderAndMarkers);
    framework.Run("statistics are copied", TestStatsAreCopied);
    framework.Run("clear rect semantics", TestClearRectSemantics);
    framework.Run("mid-scene stencil clear splits pass", TestMidSceneStencilClearSplitsPass);
    framework.Run("overlay phase", TestOverlayPhase);
    framework.Run("present requires EndScene", TestPresentRequiresEndScene);
    framework.Run("render targets", TestRenderTargets);
    framework.Run("mandatory intermediate target failure", TestRequiredIntermediateTargetFailure);
    framework.Run("MSAA request never falls back", TestMSAARequestNeverFallsBack);
    framework.Run("readback and copies", TestReadbackAndCopies);
    framework.Run("scaled copy keeps scene", TestScaledCopyKeepsScene);
    framework.Run("shutdown rejects readback callback work", TestShutdownRejectsReadbackCallbackWork);
    framework.Run("shutdown after device failure", TestShutdownAfterDeviceFailure);
    framework.Run("shutdown", TestShutdown);
    return framework.ExitCode();
}
