// CKRasterizer v3 contract conformance tests (spec 7.2), run against the NULL
// rasterizer in CKRasterizerLib3. Phase 1 points the same tests at the
// translation core + NULL device without changing them.

#include <stdio.h>
#include <string.h>

#include "CKNullRasterizer.h"
#include "CKRasterizerCapsBaseline.h"
#include "TestTriangleMultiset.h"

namespace {

struct Fixture {
    CKRasterizer *Rasterizer;
    CKNullRasterizerDriver *Driver;
    CKNullRasterizerContext *Context;

    Fixture() : Rasterizer(NULL), Driver(NULL), Context(NULL)
    {
        Rasterizer = CKNullRasterizerStart(NULL);
        TestCheck(Rasterizer != NULL, "CKNullRasterizerStart failed");
        TestCheck(Rasterizer->GetDriverCount() == 1, "NULL rasterizer exposes one driver");
        Driver = static_cast<CKNullRasterizerDriver *>(Rasterizer->GetDriver(0));
        TestCheck(Driver != NULL, "driver 0 missing");
        Context = static_cast<CKNullRasterizerContext *>(Driver->CreateContext());
        TestCheck(Context != NULL, "CreateContext failed");
        TestCheck(Context->Create(NULL, 0, 0, 640, 480, 32, FALSE, 60, 24, 8), "Create failed");
        Context->ClearEvents();
    }

    ~Fixture()
    {
        if (Rasterizer)
            CKNullRasterizerClose(Rasterizer);
    }
};

CKDWORD Diag(CKNullRasterizerContext *ctx, CKRST_DIAGNOSTIC kind)
{
    return ctx->GetStats()->Diagnostics[kind];
}

int CountEvents(const CKNullRasterizerContext *ctx, CKNullEventKind kind)
{
    int count = 0;
    const XClassArray<CKNullEvent> &events = ctx->GetEvents();
    for (int i = 0; i < events.Size(); ++i)
        if (events[i].Kind == kind)
            ++count;
    return count;
}

const CKNullEvent *FindEvent(const CKNullRasterizerContext *ctx, CKNullEventKind kind, int ordinal)
{
    const XClassArray<CKNullEvent> &events = ctx->GetEvents();
    for (int i = 0; i < events.Size(); ++i) {
        if (events[i].Kind != kind)
            continue;
        if (ordinal == 0)
            return &events[i];
        --ordinal;
    }
    return NULL;
}

CKDWORD CreateTexture2D(CKNullRasterizerContext *ctx, int w, int h, CKDWORD flags, CKDWORD mips)
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

CKDWORD CreateVB(CKNullRasterizerContext *ctx, CKDWORD format, CKDWORD count)
{
    CKVertexBufferDesc desc;
    desc.m_VertexFormat = format;
    desc.m_MaxVertexCount = count;
    CKDWORD handle = 0;
    TestCheck(ctx->CreateVertexBuffer(&desc, NULL, &handle), "CreateVertexBuffer failed");
    TestCheck(handle != 0, "vertex buffer handle is 0");
    return handle;
}

CKDWORD CreateIB(CKNullRasterizerContext *ctx, CKDWORD count)
{
    CKIndexBufferDesc desc;
    desc.m_MaxIndexCount = count;
    CKDWORD handle = 0;
    TestCheck(ctx->CreateIndexBuffer(&desc, NULL, &handle), "CreateIndexBuffer failed");
    TestCheck(handle != 0, "index buffer handle is 0");
    return handle;
}

void DrawTriangle(CKNullRasterizerContext *ctx)
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

void TestLifecycle()
{
    Fixture f;
    TestCheck(f.Context->m_Driver == f.Driver, "m_Driver not set");
    TestCheck(f.Context->m_Width == 640 && f.Context->m_Height == 480, "size members");
    TestCheck(f.Context->m_Bpp == 32 && f.Context->m_ZBpp == 24 && f.Context->m_StencilBpp == 8, "bpp members");
    TestCheck(f.Context->GetDeviceStatus() == CK_OK, "device status");
    TestCheck(f.Context->IsIdle(), "idle after create");
    TestCheck(!f.Context->Create(NULL, 0, 0, 10, 10, 32, FALSE, 0, 24, 8), "second Create must fail");

    TestCheck(f.Context->Resize(5, 6, 800, 600, 0), "Resize failed");
    TestCheck(f.Context->m_PosX == 5 && f.Context->m_PosY == 6 && f.Context->m_Width == 800 &&
              f.Context->m_Height == 600, "Resize members");

    CKRasterizerCapsDesc caps;
    TestCheck(f.Context->GetCaps(&caps), "GetCaps failed");
    TestCheck(caps.Size == sizeof(CKRasterizerCapsDesc), "caps size");
    TestCheck(caps.MaxTextureStages == CKRST_MAX_TEXTURE_STAGES, "caps stages");
    TestCheck(!f.Context->GetCaps(NULL), "GetCaps(NULL) must fail");

    CKRasterizerInfo info;
    CKNullRasterizerGetInfo(&info);
    TestCheck(info.InterfaceRevision == CKRST_INTERFACE_REVISION, "interface revision");
    TestCheck(CKRST_INTERFACE_REVISION == 0x00030000u, "v3 revision value");
    TestCheck(info.StartFct != NULL && info.CloseFct != NULL, "entry points");

    // A second context on the same driver is allowed for the NULL rasterizer.
    CKRasterizerContext *second = f.Driver->CreateContext();
    TestCheck(second != NULL, "second context");
    TestCheck(f.Driver->DestroyContext(second), "DestroyContext");
    TestCheck(!f.Driver->DestroyContext(second), "DestroyContext twice must fail");
}

void TestDriverCaps()
{
    Fixture f;
    const Vx3DCapsDesc &caps = f.Driver->m_3DCaps;
    TestCheck(caps.MaxNumberTextureStage >= 1, "MaxNumberTextureStage");
    TestCheck(caps.MaxTextureWidth >= 256 && caps.MaxTextureHeight >= 256, "max texture size");
    TestCheck((f.Driver->m_2DCaps.Caps & CKRST_2DCAPS_WINDOWED) != 0, "2D WINDOWED cap");
    TestCheck((caps.CKRasterizerSpecificCaps & CKRST_SPECIFICCAPS_SOFTWARE) != 0, "NULL reports software");
    TestCheck(f.Driver->m_TextureFormats.Size() >= 1, "texture formats");
    TestCheck(f.Driver->m_DisplayModes.Size() >= 1, "display modes");

    Vx3DCapsDesc baseline3D;
    Vx2DCapsDesc baseline2D;
    if (CKRSTGetCapsBaseline(&baseline3D, &baseline2D)) {
        TestCheck(CKRSTGetCapsBaselineSource() != NULL, "baseline source string");
        // Bit fields must match the baseline exactly (spec 4.9.2); the NULL
        // rasterizer only rewrites the hardware / software specific bits.
        TestCheck(caps.RasterCaps == baseline3D.RasterCaps, "RasterCaps == baseline");
        TestCheck(caps.TextureCaps == baseline3D.TextureCaps, "TextureCaps == baseline");
        TestCheck(caps.TextureFilterCaps == baseline3D.TextureFilterCaps, "TextureFilterCaps == baseline");
        TestCheck(caps.TextureAddressCaps == baseline3D.TextureAddressCaps, "TextureAddressCaps == baseline");
        TestCheck(caps.StencilCaps == baseline3D.StencilCaps, "StencilCaps == baseline");
        TestCheck(caps.VertexCaps == baseline3D.VertexCaps, "VertexCaps == baseline");
        TestCheck(caps.MiscCaps == baseline3D.MiscCaps, "MiscCaps == baseline");
        TestCheck(caps.AlphaCmpCaps == baseline3D.AlphaCmpCaps, "AlphaCmpCaps == baseline");
        TestCheck(caps.ZCmpCaps == baseline3D.ZCmpCaps, "ZCmpCaps == baseline");
        TestCheck(caps.SrcBlendCaps == baseline3D.SrcBlendCaps, "SrcBlendCaps == baseline");
        TestCheck(caps.DestBlendCaps == baseline3D.DestBlendCaps, "DestBlendCaps == baseline");
        const XDWORD hwMask = CKRST_SPECIFICCAPS_HARDWARE | CKRST_SPECIFICCAPS_HARDWARETL | CKRST_SPECIFICCAPS_SOFTWARE;
        TestCheck((caps.CKRasterizerSpecificCaps & ~hwMask) == (baseline3D.CKRasterizerSpecificCaps & ~hwMask),
                  "SpecificCaps == baseline (except hw/sw bits)");
        // Numeric fields may only be lowered.
        TestCheck(caps.MaxTextureWidth <= baseline3D.MaxTextureWidth, "MaxTextureWidth <= baseline");
        TestCheck(caps.MaxClipPlanes <= baseline3D.MaxClipPlanes, "MaxClipPlanes <= baseline");
        TestCheck(caps.MaxActiveLights <= baseline3D.MaxActiveLights, "MaxActiveLights <= baseline");
        TestCheck(caps.MaxNumberTextureStage <= baseline3D.MaxNumberTextureStage, "MaxNumberTextureStage <= baseline");
        TestCheck(f.Driver->m_2DCaps.Family == baseline2D.Family, "2D Family == baseline");
        TestCheck((f.Driver->m_2DCaps.Caps & CKRST_2DCAPS_WINDOWED) == (baseline2D.Caps & CKRST_2DCAPS_WINDOWED),
                  "2D WINDOWED == baseline");
    } else {
        printf("(no caps baseline compiled in) ");
    }
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
    TestCheck(CountEvents(f.Context, CKNULL_EVENT_DRAW) == 1, "draw submitted after invalid state calls");
}

void TestMatricesLightsClipPlanes()
{
    Fixture f;
    VxMatrix m;
    Vx3DMatrixIdentity(m);
    m[3][0] = 5.0f;
    TestCheck(f.Context->SetTransformMatrix(VXMATRIX_WORLD, m), "set WORLD");
    TestCheck(f.Context->GetMatrix(VXMATRIX_WORLDMATRIX(0))[3][0] == 5.0f, "WORLD aliases WORLDMATRIX(0)");
    m[3][0] = 7.0f;
    TestCheck(f.Context->SetTransformMatrix(VXMATRIX_WORLDMATRIX(0), m), "set WORLDMATRIX(0)");
    TestCheck(f.Context->GetMatrix(VXMATRIX_WORLD)[3][0] == 7.0f, "WORLDMATRIX(0) aliases WORLD");
    m[3][0] = 9.0f;
    TestCheck(f.Context->SetTransformMatrix(VXMATRIX_WORLDMATRIX(3), m), "set WORLDMATRIX(3)");
    TestCheck(f.Context->GetMatrix(VXMATRIX_WORLDMATRIX(3))[3][0] == 9.0f, "WORLDMATRIX(3) stored");
    TestCheck(f.Context->GetMatrix(VXMATRIX_WORLD)[3][0] == 7.0f, "WORLDMATRIX(3) does not alias WORLD");
    TestCheck(f.Context->SetTransformMatrix(VXMATRIX_VIEW, m), "set VIEW");
    TestCheck(f.Context->SetTransformMatrix(VXMATRIX_PROJECTION, m), "set PROJECTION");
    for (int i = 0; i < CKRST_MAX_TEXTURE_STAGES; ++i)
        TestCheck(f.Context->SetTransformMatrix(VXMATRIX_TEXTURE(i), m), "set TEXTURE(i)");
    TestCheck(!f.Context->SetTransformMatrix((VXMATRIX_TYPE)0, m), "matrix type 0 rejected");
    TestCheck(!f.Context->SetTransformMatrix((VXMATRIX_TYPE)4, m), "matrix type 4 rejected");
    TestCheck(!f.Context->SetTransformMatrix((VXMATRIX_TYPE)24, m), "matrix type 24 rejected");
    TestCheck(!f.Context->SetTransformMatrix(VXMATRIX_WORLDMATRIX(CKRST_MAX_WORLD_MATRICES), m), "WORLDMATRIX(4) rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_INVALID_MATRIX_TYPE) == 4, "invalid matrix types counted");
    TestCheck((CKDWORD)VXMATRIX_WORLD == 1 && (CKDWORD)VXMATRIX_TEXTURE0 == 16 && (CKDWORD)VXMATRIX_WMAT == 256,
              "v1 matrix enumeration values");

    CKLightData light;
    memset(&light, 0, sizeof(light));
    light.Type = VX_LIGHTPOINT;
    light.Range = 12.0f;
    for (CKDWORD i = 0; i < CKRST_MAX_LIGHTS; ++i) {
        TestCheck(f.Context->SetLight(i, &light), "SetLight in range");
        TestCheck(f.Context->EnableLight(i, TRUE), "EnableLight in range");
        TestCheck(f.Context->IsLightEnabled(i) && f.Context->GetLight(i).Range == 12.0f, "light stored");
    }
    TestCheck(!f.Context->SetLight(CKRST_MAX_LIGHTS, &light), "SetLight out of range");
    TestCheck(!f.Context->EnableLight(CKRST_MAX_LIGHTS, TRUE), "EnableLight out of range");
    TestCheck(!f.Context->SetLight(0, NULL), "SetLight NULL");
    TestCheck(Diag(f.Context, CKRST_DIAG_INVALID_LIGHT_INDEX) == 2, "invalid light index counted");

    CKMaterialData material;
    memset(&material, 0, sizeof(material));
    material.SpecularPower = 3.0f;
    TestCheck(f.Context->SetMaterial(&material), "SetMaterial");
    TestCheck(f.Context->GetMaterial().SpecularPower == 3.0f, "material stored");
    TestCheck(!f.Context->SetMaterial(NULL), "SetMaterial NULL");

    CKViewportData viewport;
    viewport.ViewX = 1; viewport.ViewY = 2; viewport.ViewWidth = 3; viewport.ViewHeight = 4;
    TestCheck(f.Context->SetViewport(&viewport), "SetViewport");
    TestCheck(f.Context->GetViewport().ViewWidth == 3, "viewport stored");

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
    TestCheck(desc.MipMapCount == 1, "MipMapCount 0 normalizes to 1");
    TestCheck((desc.Flags & CKRST_TEXTURE_VALID) != 0, "VALID flag set");

    const CKDWORD mipTex = CreateTexture2D(f.Context, 64, 32, 0, CKRST_MIPMAP_GENERATE);
    TestCheck(f.Context->GetTextureDesc(mipTex, &desc), "GetTextureDesc mip");
    TestCheck(desc.MipMapCount == 7, "generated mip chain count for 64x32");

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
    TestCheck(f.Context->LoadTexture(tex, image, 0, CKRST_CUBEFACE_XPOS, NULL), "LoadTexture level 0");
    TestCheck(!f.Context->LoadTexture(tex, image, 1, CKRST_CUBEFACE_XPOS, NULL), "level 1 of a 1-level texture rejected");
    TestCheck(!f.Context->LoadTexture(tex, image, 0, CKRST_CUBEFACE_YNEG, NULL), "cube face on 2D texture rejected");
    CKRECT region = {8, 8, 16, 16};
    TestCheck(f.Context->LoadTexture(tex, image, 0, CKRST_CUBEFACE_XPOS, &region), "region upload");
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
    TestCheck(f.Context->SetTexture(tex, 0), "SetTexture");
    TestCheck(f.Context->GetBoundTexture(0) == tex, "bound");
    TestCheck(!f.Context->SetTexture(0xBAD, 1), "unknown handle rejected");
    TestCheck(f.Context->SetTexture(0, 1), "unbind with 0");
    TestCheck(!f.Context->DeleteObject(tex, CKRST_OBJ_VERTEXBUFFER), "wrong type rejected");
    TestCheck(f.Context->DeleteObject(tex, CKRST_OBJ_TEXTURE), "DeleteObject");
    TestCheck(f.Context->GetBoundTexture(0) == 0, "deleted texture unbound");
    TestCheck(!f.Context->DeleteObject(tex, CKRST_OBJ_TEXTURE), "double delete rejected");
    TestCheck(!f.Context->GetTextureDesc(tex, &desc), "stale handle rejected");
    TestCheck(f.Context->GetLiveResourceCount(CKRST_OBJ_TEXTURE) == 2, "two textures left");
    TestCheck(f.Context->FlushObjects(CKRST_OBJ_TEXTURE), "FlushObjects");
    TestCheck(f.Context->GetLiveResourceCount(CKRST_OBJ_ALL) == 0, "all textures flushed");

    // A slot can be reused, but the old handle stays invalid.
    const CKDWORD again = CreateTexture2D(f.Context, 8, 8, 0, 0);
    TestCheck(again != tex, "reused slot gets a new handle");
    TestCheck(!f.Context->GetTextureDesc(tex, &desc), "old handle still invalid after reuse");
}

void TestBuffers()
{
    Fixture f;
    const CKDWORD vb = CreateVB(f.Context, CKRST_DP_TR_CL_VNT, 16);
    const CKNullResource *res = f.Context->GetResource(vb);
    TestCheck(res && res->VertexBuffer.m_VertexSize == 32, "vertex size filled from layout");
    TestCheck((res->VertexBuffer.m_Flags & CKRST_VB_VALID) != 0, "VB VALID flag");

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
    TestCheck(CountEvents(f.Context, CKNULL_EVENT_DRAW) == 2, "two draws recorded");
    TestCheck(Diag(f.Context, CKRST_DIAG_REJECT_INVALID_HANDLE) == 3, "invalid handle rejections counted");
    TestCheck(Diag(f.Context, CKRST_DIAG_REJECT_INVALID_PARAMETER) == 9, "invalid parameter rejections counted");

    TestCheck(f.Context->DeleteObject(ib, CKRST_OBJ_INDEXBUFFER), "delete IB");
    TestCheck(f.Context->DeleteObject(vb, CKRST_OBJ_VERTEXBUFFER), "delete VB");
    TestCheck(f.Context->GetLiveResourceCount(CKRST_OBJ_ALL) == 0, "buffers released");
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
    TestCheck(!f.Context->DrawPrimitive(VX_TRIANGLELIST, NULL, 6, &data), "index count without indices rejected");
    f.Context->EndScene();
    const CKNullEvent *fan = FindEvent(f.Context, CKNULL_EVENT_DRAW, 0);
    const CKNullEvent *indexed = FindEvent(f.Context, CKNULL_EVENT_DRAW, 5);
    TestCheck(fan && fan->A == VX_TRIANGLEFAN && fan->B == 2, "fan primitive count");
    TestCheck(indexed && indexed->B == 2, "indexed primitive count");
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

    int seen = 0;
    CKDWORD pass = 0;
    const XClassArray<CKNullEvent> &events = f.Context->GetEvents();
    for (int i = 0; i < events.Size(); ++i) {
        if (events[i].Kind != CKNULL_EVENT_DRAW)
            continue;
        if (seen < 5) {
            char expected[16];
            sprintf(expected, "draw%d", seen);
            TestCheck(events[i].Name == expected, "draw order matches call order");
            if (seen == 0)
                pass = events[i].Pass;
            TestCheck(events[i].Pass == pass, "all draws in one pass");
        }
        ++seen;
    }
    TestCheck(seen == 6, "six draws recorded");
    TestCheck(CountEvents(f.Context, CKNULL_EVENT_PASS_BEGIN) == 1, "single pass frame");
    TestCheck(f.Context->GetStats()->FrameNumber == 1, "frame counted");
    TestCheck(f.Context->GetStats()->DrawCalls == 6, "stats draw calls");
    TestCheck(f.Context->GetStats()->Primitives == 6, "stats primitives");
    TestCheck(f.Context->GetStats()->Clears == 1, "stats clears");
    TestCheck(f.Context->IsIdle(), "idle after present");
}

void TestClearRectSemantics()
{
    Fixture f;
    CKViewportData viewport;
    viewport.ViewX = 10; viewport.ViewY = 20; viewport.ViewWidth = 100; viewport.ViewHeight = 50;
    f.Context->SetViewport(&viewport);
    TestCheck(f.Context->Clear(CKRST_CTXCLEAR_COLOR, 0xFF00FF00, 1.0f, 0, 0, NULL), "viewport clear");
    const CKNullEvent *clear = FindEvent(f.Context, CKNULL_EVENT_CLEAR, 0);
    TestCheck(clear != NULL, "clear recorded");
    TestCheck(clear->Rect.left == 10 && clear->Rect.top == 20 && clear->Rect.right == 110 && clear->Rect.bottom == 70,
              "RectCount 0 clears the current viewport");
    TestCheck(clear->B == 0xFF00FF00 && clear->A == CKRST_CTXCLEAR_COLOR, "clear parameters");
    CKRECT rects[2] = {{0, 0, 8, 8}, {16, 16, 32, 32}};
    TestCheck(f.Context->Clear(CKRST_CTXCLEAR_DEPTH, 0, 0.5f, 0, 2, rects), "two rect clear");
    TestCheck(CountEvents(f.Context, CKNULL_EVENT_CLEAR) == 3, "one event per rectangle");
    TestCheck(!f.Context->Clear(CKRST_CTXCLEAR_DEPTH, 0, 0.5f, 0, 2, NULL), "rect count without rects rejected");
}

void TestMidSceneStencilClearSplitsPass()
{
    Fixture f;
    f.Context->Clear(CKRST_CTXCLEAR_ALL, 0, 1.0f, 0, 0, NULL);
    f.Context->BeginScene();
    f.Context->SetDebugMarker("before");
    DrawTriangle(f.Context);
    TestCheck(f.Context->Clear(CKRST_CTXCLEAR_STENCIL, 0, 1.0f, 0, 0, NULL), "mid-scene stencil clear");
    f.Context->SetDebugMarker("after");
    DrawTriangle(f.Context);
    f.Context->EndScene();
    f.Context->BackToFront(FALSE);

    const CKNullEvent *before = FindEvent(f.Context, CKNULL_EVENT_DRAW, 0);
    const CKNullEvent *after = FindEvent(f.Context, CKNULL_EVENT_DRAW, 1);
    const CKNullEvent *stencilClear = FindEvent(f.Context, CKNULL_EVENT_CLEAR, 1);
    TestCheck(before && after && stencilClear, "events present");
    TestCheck(before->Name == "before" && after->Name == "after", "draw markers");
    TestCheck(before->Pass < stencilClear->Pass, "clear pass after first draw pass");
    TestCheck(stencilClear->Pass < after->Pass, "second draw pass after clear pass");
    TestCheck(stencilClear->A == CKRST_CTXCLEAR_STENCIL, "stencil-only clear recorded");
    TestCheck(CountEvents(f.Context, CKNULL_EVENT_PASS_BEGIN) == 3, "three passes: scene, clear, scene");
    TestCheck(f.Context->GetStats()->Passes == 3, "stats passes");
    TestCheck(f.Context->IsInScene() == FALSE, "scene closed");
}

void TestOverlayPhase()
{
    Fixture f;
    f.Context->BeginScene();
    DrawTriangle(f.Context);
    TestCheck(f.Context->BeginOverlayPhase(), "BeginOverlayPhase");
    TestCheck(!f.Context->BeginOverlayPhase(), "second overlay phase in a frame rejected");
    DrawTriangle(f.Context);
    f.Context->EndScene();
    f.Context->BackToFront(FALSE);
    const CKNullEvent *scene = FindEvent(f.Context, CKNULL_EVENT_DRAW, 0);
    const CKNullEvent *overlay = FindEvent(f.Context, CKNULL_EVENT_DRAW, 1);
    const CKNullEvent *begin = FindEvent(f.Context, CKNULL_EVENT_BEGIN_OVERLAY, 0);
    TestCheck(scene && overlay && begin, "overlay events");
    TestCheck(scene->Pass < begin->Pass && begin->Pass < overlay->Pass, "overlay draws in a later pass");
    TestCheck(CountEvents(f.Context, CKNULL_EVENT_PASS_BEGIN) == 3, "scene, present, overlay passes");
    // Overlay phase resets with the frame.
    f.Context->BeginScene();
    TestCheck(f.Context->BeginOverlayPhase(), "overlay allowed again next frame");
    f.Context->EndScene();
    f.Context->BackToFront(FALSE);
}

void TestPresentRequiresEndScene()
{
    Fixture f;
    f.Context->BeginScene();
    TestCheck(!f.Context->BackToFront(FALSE), "BackToFront inside scene rejected");
    TestCheck(!f.Context->Resize(0, 0, 320, 240, 0), "Resize inside scene rejected");
    CKRasterizerOptions options;
    TestCheck(!f.Context->SetOptions(&options), "SetOptions inside scene rejected");
    TestCheck(!f.Context->BeginShutdown(), "BeginShutdown inside scene rejected");
    TestCheck(!f.Context->IsIdle(), "not idle inside scene");
    f.Context->EndScene();
    TestCheck(f.Context->BackToFront(TRUE), "BackToFront after EndScene");
    const CKNullEvent *present = FindEvent(f.Context, CKNULL_EVENT_PRESENT, 0);
    TestCheck(present && present->A == 1, "vsync flag recorded");

    options.RenderScale = 9.0f;
    options.Sharpness = -1.0f;
    TestCheck(f.Context->SetOptions(&options), "SetOptions at frame boundary");
    TestCheck(f.Context->GetOptions().RenderScale == 2.0f && f.Context->GetOptions().Sharpness == 0.0f,
              "options clamped");
    options.Size = 4;
    TestCheck(!f.Context->SetOptions(&options), "wrong Size rejected");
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

    TestCheck(!f.Context->SetTargetTexture(plain, 0, 0, CKRST_CUBEFACE_XPOS), "texture without RENDERTARGET rejected");
    TestCheck(!f.Context->SetTargetTexture(rt, 0, 0, CKRST_CUBEFACE_YPOS), "cube face on 2D target rejected");
    TestCheck(!f.Context->SetTargetTexture(rt, 256, 64, CKRST_CUBEFACE_XPOS), "size larger than texture rejected");
    TestCheck(!f.Context->SetTargetTexture(cubeRt, 0, 0, (CKRST_CUBEFACE)6), "face 6 rejected");
    TestCheck(!f.Context->SetTargetTexture(0xBEEF, 0, 0, CKRST_CUBEFACE_XPOS), "unknown handle rejected");

    TestCheck(f.Context->SetTargetTexture(rt, 0, 0, CKRST_CUBEFACE_XPOS), "2D render target");
    const CKNullEvent *target = FindEvent(f.Context, CKNULL_EVENT_SET_TARGET, 0);
    TestCheck(target && target->B == 128 && target->C == 64, "size 0 means texture size");
    TestCheck(f.Context->GetTarget() == rt, "target set");
    TestCheck(!f.Context->BeginOverlayPhase(), "overlay phase rejected while a texture is the target");
    TestCheck(Diag(f.Context, CKRST_DIAG_OVERLAY_ON_TARGET) == 1, "overlay on target counted");

    f.Context->Clear(CKRST_CTXCLEAR_ALL, 0, 1.0f, 0, 0, NULL);
    f.Context->BeginScene();
    DrawTriangle(f.Context);
    f.Context->EndScene();
    TestCheck(f.Context->SetTargetTexture(cubeRt, 16, 16, CKRST_CUBEFACE_ZNEG), "cube face target with sub-size");
    f.Context->BeginScene();
    DrawTriangle(f.Context);
    f.Context->EndScene();
    TestCheck(f.Context->SetTargetTexture(0, 0, 0, CKRST_CUBEFACE_XPOS), "back to backbuffer");
    f.Context->BeginScene();
    DrawTriangle(f.Context);
    f.Context->EndScene();
    f.Context->BackToFront(FALSE);

    const CKNullEvent *d0 = FindEvent(f.Context, CKNULL_EVENT_DRAW, 0);
    const CKNullEvent *d1 = FindEvent(f.Context, CKNULL_EVENT_DRAW, 1);
    const CKNullEvent *d2 = FindEvent(f.Context, CKNULL_EVENT_DRAW, 2);
    TestCheck(d0 && d1 && d2, "three draws");
    TestCheck(d0->Target == rt && d1->Target == cubeRt && d2->Target == 0, "draw targets recorded");
    TestCheck(d0->Pass < d1->Pass && d1->Pass < d2->Pass, "target change starts a new pass");

    // Deleting the current target falls back to the backbuffer.
    TestCheck(f.Context->SetTargetTexture(rt, 0, 0, CKRST_CUBEFACE_XPOS), "target again");
    TestCheck(f.Context->DeleteObject(rt, CKRST_OBJ_TEXTURE), "delete current target");
    TestCheck(f.Context->GetTarget() == 0, "target reset after delete");
}

void TestReadbackAndCopies()
{
    Fixture f;
    VxImageDescEx image;
    memset(&image, 0, sizeof(image));
    const int required = f.Context->CopyToMemoryBuffer(NULL, VXBUFFER_BACKBUFFER, image);
    TestCheck(required == 640 * 480 * 4, "required size for whole backbuffer");
    TestCheck(image.Width == 640 && image.Height == 480 && image.BitsPerPixel == 32, "descriptor filled");
    XArray<CKBYTE> pixels;
    pixels.Resize(required);
    memset(pixels.Begin(), 0xFF, required);
    image.Image = pixels.Begin();
    TestCheck(f.Context->CopyToMemoryBuffer(NULL, VXBUFFER_BACKBUFFER, image) == required, "copy whole backbuffer");
    TestCheck(pixels[0] == 0 && pixels[required - 1] == 0, "NULL rasterizer returns a black image");

    CKRECT rect = {10, 10, 20, 30};
    VxImageDescEx sub;
    memset(&sub, 0, sizeof(sub));
    TestCheck(f.Context->CopyToMemoryBuffer(&rect, VXBUFFER_ZBUFFER, sub) == 10 * 20 * 4, "sub rect size");
    CKRECT badRect = {600, 0, 700, 10};
    TestCheck(f.Context->CopyToMemoryBuffer(&badRect, VXBUFFER_BACKBUFFER, sub) == 0, "rect outside target rejected");
    TestCheck(f.Context->CopyToMemoryBuffer(NULL, (VXBUFFER_TYPE)8, sub) == 0, "unknown buffer rejected");

    f.Context->BeginScene();
    TestCheck(f.Context->CopyToMemoryBuffer(NULL, VXBUFFER_BACKBUFFER, image) == 0, "readback inside scene rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_REJECT_SCENE_STATE) == 1, "scene state rejection counted");
    f.Context->EndScene();

    TestCheck(f.Context->CopyFromMemoryBuffer(&rect, VXBUFFER_BACKBUFFER, image) == 10 * 20 * 4, "CopyFromMemoryBuffer");
    TestCheck(f.Context->CopyFromMemoryBuffer(&rect, VXBUFFER_ZBUFFER, image) == 0, "depth upload rejected");
    TestCheck(CountEvents(f.Context, CKNULL_EVENT_COPY_FROM_MEMORY) == 1, "copy from memory recorded");

    const CKDWORD tex = CreateTexture2D(f.Context, 64, 64, 0, 0);
    VxRect src(0, 0, 64, 64);
    TestCheck(f.Context->CopyToTexture(tex, &src, NULL, CKRST_CUBEFACE_XPOS), "CopyToTexture");
    TestCheck(!f.Context->CopyToTexture(tex, &src, NULL, CKRST_CUBEFACE_XNEG), "cube face on 2D texture rejected");
    TestCheck(!f.Context->CopyToTexture(0, &src, NULL, CKRST_CUBEFACE_XPOS), "handle 0 rejected");

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
    TestCheck(f.Context->RequestReadback(NULL, VXBUFFER_BACKBUFFER, Capture::Callback, &calls), "RequestReadback");
    f.Context->BackToFront(FALSE);
    TestCheck(calls == 1, "readback callback delivered by the next present");
    TestCheck(!f.Context->RequestReadback(NULL, VXBUFFER_BACKBUFFER, NULL, NULL), "NULL callback rejected");
}

void TestShutdown()
{
    Fixture f;
    CreateTexture2D(f.Context, 8, 8, 0, 0);
    CreateVB(f.Context, CKRST_DP_TR_CL_V, 3);
    TestCheck(f.Context->BeginShutdown(), "BeginShutdown");
    TestCheck(f.Context->IsIdle(), "idle after shutdown");
    TestCheck(f.Context->GetLiveResourceCount(CKRST_OBJ_ALL) == 0, "resources flushed on shutdown");
    TestCheck(!f.Context->BeginScene(), "BeginScene after shutdown rejected");
    TestCheck(f.Driver->DestroyContext(f.Context), "DestroyContext after shutdown");
    f.Context = NULL;
}

} // namespace

int main()
{
    TestFramework framework;
    framework.Run("lifecycle", TestLifecycle);
    framework.Run("driver caps", TestDriverCaps);
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
    framework.Run("buffers", TestBuffers);
    framework.Run("draw primitive validation", TestDrawPrimitiveValidation);
    framework.Run("draw order and markers", TestDrawOrderAndMarkers);
    framework.Run("clear rect semantics", TestClearRectSemantics);
    framework.Run("mid-scene stencil clear splits pass", TestMidSceneStencilClearSplitsPass);
    framework.Run("overlay phase", TestOverlayPhase);
    framework.Run("present requires EndScene", TestPresentRequiresEndScene);
    framework.Run("render targets", TestRenderTargets);
    framework.Run("readback and copies", TestReadbackAndCopies);
    framework.Run("shutdown", TestShutdown);
    return framework.ExitCode();
}
