#include "CKTransientGeometry.h"
#include "CKRenderEngineTypes.h"
#include "CKVertexLayoutCache.h"
#include "CKRenderFrameCostStats.h"
#include "CKRenderSettings.h"
#include "FFPDiagnosticHarness.h"
#include "TestTriangleMultiset.h"

#include <cstring>

struct QuadFastPathHarness {
    FFPDiagnosticDriver Driver;
    FFPDiagnosticContext Context;
    CKVertexLayoutCache LayoutCache;
    CKTransientGeometry Geometry;

    QuadFastPathHarness()
        : Driver(), Context(&Driver), LayoutCache(), Geometry()
    {
        LayoutCache.Init(&Context);
        Geometry.Init(&Context, &LayoutCache);
    }
};

static void InitQuadData(VxDrawPrimitiveData *data,
                         float *positions,
                         float *texcoords,
                         CKDWORD *colors)
{
    memset(data, 0, sizeof(*data));
    data->VertexCount = 4;
    data->Flags = CKRST_DP_CL_VCT;
    data->PositionPtr = positions;
    data->PositionStride = 4 * sizeof(float);
    data->TexCoordPtr = texcoords;
    data->TexCoordStride = 2 * sizeof(float);
    data->ColorPtr = colors;
    data->ColorStride = sizeof(CKDWORD);
}

static CKDWORD ReadDword(const CKBYTE *data)
{
    CKDWORD value = 0;
    memcpy(&value, data, sizeof(value));
    return value;
}

static float ReadFloat(const CKBYTE *data)
{
    float value = 0.0f;
    memcpy(&value, data, sizeof(value));
    return value;
}

static void FillQuadInput(float *positions, float *texcoords, CKDWORD *colors)
{
    const float pos[16] = {
        10.0f, 20.0f, 0.0f, 1.0f,
        30.0f, 20.0f, 0.0f, 1.0f,
        30.0f, 40.0f, 0.0f, 1.0f,
        10.0f, 40.0f, 0.0f, 1.0f
    };
    const float uv[8] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f
    };
    const CKDWORD argb[4] = {
        0xFF112233,
        0x80445566,
        0x20778899,
        0xFFAABBCC
    };
    memcpy(positions, pos, sizeof(pos));
    memcpy(texcoords, uv, sizeof(uv));
    memcpy(colors, argb, sizeof(argb));
}

static CKDWORD ArgbToAbgr(CKDWORD argb)
{
    return (argb & 0xFF00FF00) |
           ((argb & 0x00FF0000) >> 16) |
           ((argb & 0x000000FF) << 16);
}

static void BeginStatsSample()
{
#if CKRE_ENABLE_FRAME_COST_STATS
    CKRenderSettingsClearOverridesForTests();
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "Enabled", "1");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "WarmupFrames", "0");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "SampleFrames", "1");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "Output", "none");
    CKRenderFrameCostStatsResetForTests();
    CKRenderFrameCostStatsSetOutputEnabledForTests(FALSE);
    CKRenderFrameCostStatsBeginRenderFrame(0, 0, 0, 0);
#endif
}

static CKRenderFrameCostStatsSnapshot EndStatsSample()
{
#if CKRE_ENABLE_FRAME_COST_STATS
    CKRenderFrameCostStatsEndRenderFrame();
    CKRenderFrameCostStatsSnapshot snapshot;
    CKRenderFrameCostStatsCopySnapshot(&snapshot);
    CKRenderSettingsClearOverridesForTests();
    CKRenderFrameCostStatsResetForTests();
    return snapshot;
#else
    CKRenderFrameCostStatsSnapshot snapshot = {};
    return snapshot;
#endif
}

static void FastPathPacksExpectedQuad()
{
    QuadFastPathHarness harness;
    VxDrawPrimitiveData data;
    float positions[16];
    float texcoords[8];
    CKDWORD colors[4];

    FillQuadInput(positions, texcoords, colors);
    InitQuadData(&data, positions, texcoords, colors);
    BeginStatsSample();

    TestCheck(harness.Geometry.Prepare(&harness.Context.Encoder,
                                       VX_TRIANGLEFAN,
                                       NULL,
                                       4,
                                       &data,
                                       0,
                                       FALSE,
                                       NULL,
                                       NULL) == TRUE,
              "2D quad fast path prepare should succeed");

    CKRenderFrameCostStatsSnapshot stats = EndStatsSample();
#if CKRE_ENABLE_FRAME_COST_STATS
    TestCheck(stats.TransientQuadFastPathCandidates == 1,
              "2D quad should be counted as a fast path candidate");
    TestCheck(stats.TransientQuadFastPathHits == 1,
              "2D quad should hit the fast path");
    TestCheck(stats.TransientQuadFastPathFallbacks == 0,
              "2D quad fast path should not fallback");
    TestCheck(stats.TransientFanToListConversions == 0,
              "2D quad fast path should skip generic fan conversion");
#endif

    TestCheck(harness.Context.Encoder.LastVertexBytes.size() == 4 * 40,
              "fast path should write four 40-byte vertices");
    TestCheck(harness.Context.Encoder.LastIndexBytes.size() == 6 * sizeof(CKWORD),
              "fast path should write six 16-bit indices");

    const CKBYTE *vb = harness.Context.Encoder.LastVertexBytes.data();
    for (int i = 0; i < 4; ++i) {
        const CKBYTE *vertex = vb + i * 40;
        TestCheck(ReadFloat(vertex + 0) == positions[i * 4 + 0] &&
                  ReadFloat(vertex + 4) == positions[i * 4 + 1] &&
                  ReadFloat(vertex + 8) == positions[i * 4 + 2] &&
                  ReadFloat(vertex + 12) == positions[i * 4 + 3],
                  "fast path positionT must match input");
        TestCheck(ReadFloat(vertex + 16) == texcoords[i * 2 + 0] &&
                  ReadFloat(vertex + 20) == texcoords[i * 2 + 1] &&
                  ReadFloat(vertex + 24) == 0.0f &&
                  ReadFloat(vertex + 28) == 0.0f,
                  "fast path texcoord0 must match generic float4 packing");
        TestCheck(ReadDword(vertex + 32) == ArgbToAbgr(colors[i]),
                  "fast path diffuse color must convert ARGB to ABGR");
        TestCheck(ReadDword(vertex + 36) == 0xFF000000,
                  "fast path specular default must match positionT generic path");
    }

    const CKWORD *ib = (const CKWORD *)harness.Context.Encoder.LastIndexBytes.data();
    TestCheck(ib[0] == 0 && ib[1] == 1 && ib[2] == 2 &&
              ib[3] == 0 && ib[4] == 2 && ib[5] == 3,
              "fast path must preserve triangle fan winding");
}

static void IndexedQuadFallsBackToGenericPath()
{
    QuadFastPathHarness harness;
    VxDrawPrimitiveData data;
    float positions[16];
    float texcoords[8];
    CKDWORD colors[4];
    CKWORD indices[4] = {0, 1, 2, 3};

    FillQuadInput(positions, texcoords, colors);
    InitQuadData(&data, positions, texcoords, colors);
    BeginStatsSample();

    TestCheck(harness.Geometry.Prepare(&harness.Context.Encoder,
                                       VX_TRIANGLEFAN,
                                       indices,
                                       4,
                                       &data) == TRUE,
              "indexed quad should still prepare through generic path");

    CKRenderFrameCostStatsSnapshot stats = EndStatsSample();
#if CKRE_ENABLE_FRAME_COST_STATS
    TestCheck(stats.TransientQuadFastPathHits == 0,
              "indexed quad must not hit fast path");
    TestCheck(stats.TransientFanToListConversions == 1,
              "indexed triangle fan should use generic fan conversion");
#endif
}

static void MultistageAndStaleExtendedDataFallBack()
{
    QuadFastPathHarness harness;
    VxDrawPrimitiveData data;
    float positions[16];
    float texcoords[8];
    float texcoords1[8];
    CKDWORD colors[4];

    FillQuadInput(positions, texcoords, colors);
    InitQuadData(&data, positions, texcoords, colors);
    data.TexCoordPtrs[0] = texcoords1;
    data.TexCoordStrides[0] = 2 * sizeof(float);
    BeginStatsSample();

    TestCheck(harness.Geometry.Prepare(&harness.Context.Encoder,
                                       VX_TRIANGLEFAN,
                                       NULL,
                                       4,
                                       &data) == TRUE,
              "stale extended texcoord pointer should prepare through generic path");

    CKRenderFrameCostStatsSnapshot stats = EndStatsSample();
#if CKRE_ENABLE_FRAME_COST_STATS
    TestCheck(stats.TransientQuadFastPathHits == 0,
              "stale extended texcoord pointer must block fast path");
    TestCheck(stats.TransientFanToListConversions == 1,
              "stale extended fallback should use generic fan conversion");
#endif

    InitQuadData(&data, positions, texcoords, colors);
    data.Flags = CKRST_DP_CL_VCT | CKRST_DP_STAGES1;
    data.TexCoordPtrs[0] = texcoords1;
    data.TexCoordStrides[0] = 2 * sizeof(float);
    BeginStatsSample();

    TestCheck(harness.Geometry.Prepare(&harness.Context.Encoder,
                                       VX_TRIANGLEFAN,
                                       NULL,
                                       4,
                                       &data) == TRUE,
              "multistage quad should prepare through generic path");

    stats = EndStatsSample();
#if CKRE_ENABLE_FRAME_COST_STATS
    TestCheck(stats.TransientQuadFastPathHits == 0,
              "multistage quad must not hit fast path");
#endif
}

static void QuadFastPathRejectsFourComponentTexcoords()
{
    QuadFastPathHarness harness;
    VxDrawPrimitiveData data;
    float positions[16];
    float texcoords[16];
    CKDWORD colors[4];
    CKBYTE texcoordCounts[CKRST_MAX_STAGES];

    FillQuadInput(positions, texcoords, colors);
    texcoords[8] = 0.25f;
    texcoords[9] = 0.50f;
    texcoords[10] = 0.75f;
    texcoords[11] = 1.00f;
    texcoords[12] = 0.10f;
    texcoords[13] = 0.20f;
    texcoords[14] = 0.30f;
    texcoords[15] = 0.40f;
    InitQuadData(&data, positions, texcoords, colors);
    data.TexCoordStride = 4 * sizeof(float);
    memset(texcoordCounts, 0, sizeof(texcoordCounts));
    texcoordCounts[0] = 4;
    BeginStatsSample();

    TestCheck(harness.Geometry.Prepare(&harness.Context.Encoder,
                                       VX_TRIANGLEFAN,
                                       NULL,
                                       4,
                                       &data,
                                       0,
                                       FALSE,
                                       NULL,
                                       texcoordCounts) == TRUE,
              "four-component texcoord quad should prepare through generic path");

    CKRenderFrameCostStatsSnapshot stats = EndStatsSample();
#if CKRE_ENABLE_FRAME_COST_STATS
    TestCheck(stats.TransientQuadFastPathHits == 0,
              "four-component texcoord quad must not hit fast path");
    TestCheck(stats.TransientFanToListConversions == 1,
              "four-component texcoord fallback should use generic fan conversion");
#endif
}

static void InitSpriteBatchData(VxDrawPrimitiveData *data,
                                CKVertex *vertices,
                                int vertexCount,
                                CKDWORD flags)
{
    memset(data, 0, sizeof(*data));
    data->VertexCount = vertexCount;
    data->Flags = flags;
    data->PositionPtr = &vertices[0].V;
    data->PositionStride = sizeof(CKVertex);
    data->ColorPtr = &vertices[0].Diffuse;
    data->ColorStride = sizeof(CKVertex);
    data->SpecularColorPtr = &vertices[0].Specular;
    data->SpecularColorStride = sizeof(CKVertex);
    data->TexCoordPtr = &vertices[0].tu;
    data->TexCoordStride = sizeof(CKVertex);
}

static void FillSpriteBatchInput(CKVertex *vertices, int vertexCount)
{
    for (int i = 0; i < vertexCount; ++i) {
        vertices[i].V.x = (float)(i + 1);
        vertices[i].V.y = (float)(i + 2);
        vertices[i].V.z = (float)(i + 3);
        vertices[i].V.w = (float)(i + 4);
        vertices[i].Diffuse = 0xFF000000 | (CKDWORD)(i * 0x00112233);
        vertices[i].Specular = 0x80000000 | (CKDWORD)(i * 0x00030201);
        vertices[i].tu = (float)i * 0.25f;
        vertices[i].tv = (float)i * 0.5f;
    }
}

static void SpriteBatchUsesGenericPath()
{
    QuadFastPathHarness harness;
    CKVertex vertices[8];
    CKWORD indices[12] = {
        0, 1, 2, 0, 2, 3,
        4, 5, 6, 4, 6, 7
    };
    VxDrawPrimitiveData data;

    FillSpriteBatchInput(vertices, 8);
    InitSpriteBatchData(&data, vertices, 8, CKRST_DP_TR_CL_VCST);
    BeginStatsSample();

    TestCheck(harness.Geometry.Prepare(&harness.Context.Encoder,
                                       VX_TRIANGLELIST,
                                       indices,
                                       12,
                                       &data,
                                       0,
                                       FALSE,
                                       NULL,
                                       NULL) == TRUE,
              "sprite batch generic prepare should succeed");

    CKRenderFrameCostStatsSnapshot stats = EndStatsSample();
#if CKRE_ENABLE_FRAME_COST_STATS
    TestCheck(stats.TransientSpriteBatchFastPathCandidates == 0,
              "sprite batch fast path should be disabled");
    TestCheck(stats.TransientSpriteBatchFastPathHits == 0,
              "sprite batch must not hit the disabled fast path");
    TestCheck(stats.TransientSpriteBatchFastPathFallbacks == 0,
              "disabled sprite batch fast path should not report fallback noise");
    TestCheck(stats.TransientFanToListConversions == 0,
              "sprite batch triangle list should not convert topology");
#endif

    TestCheck(harness.Context.Encoder.LastVertexBytes.size() == 8 * 36,
              "sprite batch generic path should write eight 36-byte vertices");
    TestCheck(harness.Context.Encoder.LastIndexBytes.size() == 12 * sizeof(CKWORD),
              "sprite batch generic path should copy twelve 16-bit indices");

    const CKBYTE *vb = harness.Context.Encoder.LastVertexBytes.data();
    for (int i = 0; i < 8; ++i) {
        const CKBYTE *vertex = vb + i * 36;
        TestCheck(ReadFloat(vertex + 0) == vertices[i].V.x &&
                  ReadFloat(vertex + 4) == vertices[i].V.y &&
                  ReadFloat(vertex + 8) == vertices[i].V.z,
                  "sprite batch position must match generic float3 packing");
        TestCheck(ReadFloat(vertex + 12) == vertices[i].tu &&
                  ReadFloat(vertex + 16) == vertices[i].tv &&
                  ReadFloat(vertex + 20) == 0.0f &&
                  ReadFloat(vertex + 24) == 0.0f,
                  "sprite batch texcoord0 must match generic float4 packing");
        TestCheck(ReadDword(vertex + 28) == ArgbToAbgr(vertices[i].Diffuse),
                  "sprite batch diffuse color must convert ARGB to ABGR");
        TestCheck(ReadDword(vertex + 32) == ArgbToAbgr(vertices[i].Specular),
                  "sprite batch specular color must convert ARGB to ABGR");
    }

    const CKWORD *ib = (const CKWORD *)harness.Context.Encoder.LastIndexBytes.data();
    for (int i = 0; i < 12; ++i) {
        TestCheck(ib[i] == indices[i],
                  "sprite batch generic path must preserve source indices");
    }
}

static void SpriteBatchFastPathRejectsNonBatchShape()
{
    QuadFastPathHarness harness;
    CKVertex vertices[8];
    CKWORD indices[9] = {0, 1, 2, 0, 2, 3, 4, 5, 6};
    VxDrawPrimitiveData data;

    FillSpriteBatchInput(vertices, 8);
    InitSpriteBatchData(&data, vertices, 8, CKRST_DP_TR_VCST);
    BeginStatsSample();

    TestCheck(harness.Geometry.Prepare(&harness.Context.Encoder,
                                       VX_TRIANGLELIST,
                                       indices,
                                       9,
                                       &data,
                                       0,
                                       FALSE,
                                       NULL,
                                       NULL) == TRUE,
              "non-batch triangle list should still prepare through generic path");

    CKRenderFrameCostStatsSnapshot stats = EndStatsSample();
#if CKRE_ENABLE_FRAME_COST_STATS
    TestCheck(stats.TransientSpriteBatchFastPathHits == 0,
              "non-batch triangle list must not hit sprite batch fast path");
#endif
}

int main()
{
    TestFramework tests;
    tests.Run("2D quad fast path packs expected data", &FastPathPacksExpectedQuad);
    tests.Run("indexed quad falls back", &IndexedQuadFallsBackToGenericPath);
    tests.Run("multistage and stale extended data fall back", &MultistageAndStaleExtendedDataFallBack);
    tests.Run("four-component texcoord quad falls back", &QuadFastPathRejectsFourComponentTexcoords);
    tests.Run("sprite batch uses generic path", &SpriteBatchUsesGenericPath);
    tests.Run("sprite batch fast path rejects non-batch shape", &SpriteBatchFastPathRejectsNonBatchShape);
    return tests.ExitCode();
}
