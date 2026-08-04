#include "CKTransientGeometry.h"
#include "CKRenderEngineTypes.h"
#include "CKVertexLayoutCache.h"
#include "CKRenderFrameCostStats.h"
#include "CKRenderSettings.h"
#include "RCKRenderContext.h"
#include "FFPDiagnosticHarness.h"
#include "TestTriangleMultiset.h"

#include <cstring>

struct TransientGeometryHarness {
    FFPDiagnosticDriver Driver;
    FFPDiagnosticContext Context;
    CKVertexLayoutCache LayoutCache;
    CKTransientGeometry Geometry;

    TransientGeometryHarness()
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

static void QuadUsesGenericFanPath()
{
    TransientGeometryHarness harness;
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
              "2D quad generic prepare should succeed");

    CKRenderFrameCostStatsSnapshot stats = EndStatsSample();
#if CKRE_ENABLE_FRAME_COST_STATS
    TestCheck(stats.TransientPrepareCalls == 1,
              "2D quad should count one transient prepare");
    TestCheck(stats.TransientFanToListConversions == 1,
              "2D quad should use generic fan conversion");
#endif

    TestCheck(harness.Context.Encoder.LastVertexBytes.size() == 4 * 40,
              "generic path should write four 40-byte vertices");
    TestCheck(harness.Context.Encoder.LastIndexBytes.size() == 6 * sizeof(CKWORD),
              "generic path should write six 16-bit indices");

    const CKBYTE *vb = harness.Context.Encoder.LastVertexBytes.data();
    for (int i = 0; i < 4; ++i) {
        const CKBYTE *vertex = vb + i * 40;
        TestCheck(ReadFloat(vertex + 0) == positions[i * 4 + 0] &&
                  ReadFloat(vertex + 4) == positions[i * 4 + 1] &&
                  ReadFloat(vertex + 8) == positions[i * 4 + 2] &&
                  ReadFloat(vertex + 12) == positions[i * 4 + 3],
                  "generic path positionT must match input");
        TestCheck(ReadFloat(vertex + 16) == texcoords[i * 2 + 0] &&
                  ReadFloat(vertex + 20) == texcoords[i * 2 + 1] &&
                  ReadFloat(vertex + 24) == 0.0f &&
                  ReadFloat(vertex + 28) == 0.0f,
                  "generic path texcoord0 must match float4 packing");
        TestCheck(ReadDword(vertex + 32) == ArgbToAbgr(colors[i]),
                  "generic path diffuse color must convert ARGB to ABGR");
        TestCheck(ReadDword(vertex + 36) == 0xFF000000,
                  "generic path specular default must match positionT default");
    }

    const CKWORD *ib = (const CKWORD *)harness.Context.Encoder.LastIndexBytes.data();
    TestCheck(ib[0] == 0 && ib[1] == 1 && ib[2] == 2 &&
              ib[3] == 0 && ib[4] == 2 && ib[5] == 3,
              "generic path must preserve triangle fan winding");
}

static void IndexedTriangleFanUsesGenericPath()
{
    TransientGeometryHarness harness;
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
    TestCheck(stats.TransientFanToListConversions == 1,
              "indexed triangle fan should use generic fan conversion");
#endif
}

static void PairedTransientCapacityIsCheckedBeforeAllocation()
{
    TransientGeometryHarness harness;
    VxDrawPrimitiveData data;
    float positions[16];
    float texcoords[8];
    CKDWORD colors[4];

    FillQuadInput(positions, texcoords, colors);
    InitQuadData(&data, positions, texcoords, colors);
    harness.Context.TransientVertexCapacity = 4;
    harness.Context.TransientIndexCapacity = 5;

    TestCheck(harness.Geometry.Prepare(&harness.Context.Encoder,
                                       VX_TRIANGLEFAN,
                                       NULL,
                                       4,
                                       &data) == FALSE,
              "paired transient prepare must fail when the index budget is short");
    TestCheck(harness.Context.TransientVertexAllocations == 0 &&
                  harness.Context.TransientIndexAllocations == 0,
              "paired transient capacity failure must not consume either budget");
}

static void ExtendedTexcoordDataUsesGenericPath()
{
    TransientGeometryHarness harness;
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
    TestCheck(stats.TransientFanToListConversions == 1,
              "multistage quad should use generic fan conversion");
#endif
}

static void FourComponentTexcoordQuadUsesGenericPath()
{
    TransientGeometryHarness harness;
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
    TestCheck(stats.TransientFanToListConversions == 1,
              "four-component texcoord fallback should use generic fan conversion");
#endif
}

static void MultipleTextureStagesApplyIndependentWrapModes()
{
    TransientGeometryHarness harness;
    VxDrawPrimitiveData data = {};
    float positions[12] = {
        0.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f
    };
    float texcoords0[6] = {
        0.2f, 0.3f,
        0.4f, 0.5f,
        0.6f, 0.7f
    };
    float texcoords1[9] = {
        0.9f, 0.0f, 0.9f,
        0.1f, 0.0f, 0.1f,
        0.15f, 0.0f, 0.15f
    };
    CKBYTE texcoordCounts[CKRST_MAX_STAGES] = {};
    CKDWORD wrapModes[CKRST_MAX_STAGES] = {};

    data.VertexCount = 3;
    data.Flags = CKRST_DP_CL_V | CKRST_DP_STAGES0 | CKRST_DP_STAGES1;
    data.PositionPtr = positions;
    data.PositionStride = 4 * sizeof(float);
    data.TexCoordPtr = texcoords0;
    data.TexCoordStride = 2 * sizeof(float);
    data.TexCoordPtrs[0] = texcoords1;
    data.TexCoordStrides[0] = 3 * sizeof(float);
    texcoordCounts[0] = 2;
    texcoordCounts[1] = 3;
    wrapModes[1] = VXWRAP_U | VXWRAP_S;

    TestCheck(harness.Geometry.Prepare(&harness.Context.Encoder,
                                       VX_TRIANGLELIST,
                                       NULL,
                                       0,
                                       &data,
                                       0,
                                       FALSE,
                                       NULL,
                                       texcoordCounts,
                                       wrapModes) == TRUE,
              "independent stage wrap prepare should succeed");

    const CKDWORD stride = 56;
    TestCheck(harness.Context.Encoder.LastVertexBytes.size() == stride * 3,
              "independent stage wrap should emit one expanded triangle");
    if (harness.Context.Encoder.LastVertexBytes.size() == stride * 3) {
        const CKBYTE *vertices = harness.Context.Encoder.LastVertexBytes.data();
        TestCheck(ReadFloat(vertices + 16) == 0.2f &&
                  ReadFloat(vertices + stride + 16) == 0.4f &&
                  ReadFloat(vertices + stride * 2 + 16) == 0.6f,
                  "stage zero coordinates must remain unchanged");
        TestCheck(ReadFloat(vertices + 32) == 0.9f &&
                  ReadFloat(vertices + stride + 32) == 1.1f &&
                  ReadFloat(vertices + stride * 2 + 32) == 1.15f,
                  "stage one U wrap must remove interpolation discontinuity");
        TestCheck(ReadFloat(vertices + 40) == 0.9f &&
                  ReadFloat(vertices + stride + 40) == 1.1f &&
                  ReadFloat(vertices + stride * 2 + 40) == 1.15f,
                  "stage one S wrap must support three-component coordinates");
    }
}

static void LineWrapHandlesLargeCoordinateSpans()
{
    TransientGeometryHarness harness;
    VxDrawPrimitiveData data = {};
    float positions[8] = {
        0.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f
    };
    float texcoords[4] = {0.9f, 0.0f, -31.9f, 0.0f};
    CKDWORD wrapModes[CKRST_MAX_STAGES] = {};
    wrapModes[0] = VXWRAP_U;

    data.VertexCount = 2;
    data.Flags = CKRST_DP_CL_V | CKRST_DP_STAGES0;
    data.PositionPtr = positions;
    data.PositionStride = 4 * sizeof(float);
    data.TexCoordPtr = texcoords;
    data.TexCoordStride = 2 * sizeof(float);

    TestCheck(harness.Geometry.Prepare(&harness.Context.Encoder,
                                       VX_LINELIST, NULL, 0, &data,
                                       VXWRAP_U, FALSE, NULL, NULL,
                                       wrapModes) == TRUE,
              "line wrap prepare should succeed");
    const size_t vertexBytes = harness.Context.Encoder.LastVertexBytes.size();
    TestCheck(vertexBytes > 0 && (vertexBytes & 1u) == 0,
              "line wrap must emit one expanded line segment");
    if (vertexBytes > 0 && (vertexBytes & 1u) == 0) {
        const size_t stride = vertexBytes / 2;
        const CKBYTE *vertices = harness.Context.Encoder.LastVertexBytes.data();
        TestCheck(fabs(ReadFloat(vertices + stride + 16) - 1.1f) < 0.0001f,
                  "line wrap must normalize arbitrary integer coordinate spans");
    }
}

static void LargePointSpriteBatchUses32BitIndices()
{
    TransientGeometryHarness harness;
    const int pointCount = 16385;
    std::vector<VxVector> positions(pointCount, VxVector(0.0f, 0.0f, 0.0f));
    VxDrawPrimitiveData data = {};
    CKFFPointSpriteParams params = {};
    params.Size = 1.0f;
    params.MinSize = 1.0f;
    params.MaxSize = 1.0f;
    params.World.Identity();
    params.View.Identity();

    data.VertexCount = pointCount;
    data.Flags = CKRST_DP_TRANSFORM;
    data.PositionPtr = positions.data();
    data.PositionStride = sizeof(VxVector);

    TestCheck(harness.Geometry.Prepare(&harness.Context.Encoder,
                                       VX_POINTLIST, NULL, 0, &data,
                                       0, TRUE, &params, NULL) == TRUE,
              "large point sprite batch should prepare with 32-bit indices");
    const size_t expectedIndexBytes = (size_t)pointCount * 6 * sizeof(CKDWORD);
    TestCheck(harness.Context.Encoder.LastIndexBytes.size() == expectedIndexBytes,
              "large point sprite batch must allocate a 32-bit index buffer");
    if (harness.Context.Encoder.LastIndexBytes.size() == expectedIndexBytes) {
        const CKDWORD *indices =
            (const CKDWORD *)harness.Context.Encoder.LastIndexBytes.data();
        const CKDWORD lastBase = (CKDWORD)(pointCount - 1) * 4;
        TestCheck(indices[(pointCount - 1) * 6] == lastBase &&
                  indices[(pointCount - 1) * 6 + 5] == lastBase + 3,
                  "large point sprite indices must not wrap at 65536 vertices");
    }
}

static void IndexedPointSpritesUseSelectedVertices()
{
    TransientGeometryHarness harness;
    VxVector positions[3] = {
        VxVector(1.0f, 2.0f, 0.0f),
        VxVector(10.0f, 20.0f, 0.0f),
        VxVector(30.0f, 40.0f, 0.0f)
    };
    CKWORD indices[2] = {2, 0};
    VxDrawPrimitiveData data = {};
    CKFFPointSpriteParams params = {};
    params.Size = 2.0f;
    params.MinSize = 1.0f;
    params.MaxSize = 64.0f;
    params.World.Identity();
    params.View.Identity();
    params.Projection.Identity();
    params.ViewportWidth = 2.0f;
    params.ViewportHeight = 2.0f;

    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);

    TestCheck(harness.Geometry.Prepare(&harness.Context.Encoder,
                                       VX_POINTLIST, indices, 2, &data,
                                       0, TRUE, &params, NULL) == TRUE,
              "indexed point sprites should prepare selected vertices");
    const CKBYTE *vertices = harness.Context.Encoder.LastVertexBytes.data();
    const size_t stride = harness.Context.Encoder.LastVertexBytes.size() / 8;
    TestCheck(stride > 0 &&
                  fabs(ReadFloat(vertices) - 29.0f) < 0.0001f &&
                  fabs(ReadFloat(vertices + stride * 4) - 0.0f) < 0.0001f,
              "indexed point sprites must expand vertices in index order");
}

static void PointSpritesReplaceEveryDeclaredTexcoord()
{
    TransientGeometryHarness harness;
    VxVector position(0.0f, 0.0f, 0.0f);
    float texcoord0[2] = {0.25f, 0.5f};
    float texcoord1[2] = {0.75f, 0.125f};
    VxDrawPrimitiveData data = {};
    CKFFPointSpriteParams params = {};
    params.Size = 2.0f;
    params.MinSize = 1.0f;
    params.MaxSize = 64.0f;
    params.World.Identity();
    params.View.Identity();
    params.Projection.Identity();
    params.ViewportWidth = 2.0f;
    params.ViewportHeight = 2.0f;

    data.VertexCount = 1;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_STAGES1;
    data.PositionPtr = &position;
    data.PositionStride = sizeof(position);
    data.TexCoordPtr = texcoord0;
    data.TexCoordStride = sizeof(texcoord0);
    data.TexCoordPtrs[0] = texcoord1;
    data.TexCoordStrides[0] = sizeof(texcoord1);

    TestCheck(harness.Geometry.Prepare(&harness.Context.Encoder,
                                       VX_POINTLIST, NULL, 0, &data,
                                       0, TRUE, &params, NULL) == TRUE,
              "multistage point sprite should prepare");
    const CKDWORD stride = 52;
    const CKBYTE *vertices = harness.Context.Encoder.LastVertexBytes.data();
    TestCheck(harness.Context.Encoder.LastVertexBytes.size() == stride * 4,
              "multistage point sprite must emit four complete vertices");
    if (harness.Context.Encoder.LastVertexBytes.size() == stride * 4) {
        TestCheck(ReadFloat(vertices + 12) == 0.0f &&
                      ReadFloat(vertices + 16) == 0.0f &&
                      ReadFloat(vertices + 28) == 0.0f &&
                      ReadFloat(vertices + 32) == 0.0f,
                  "first point corner must replace every texcoord with zero");
        TestCheck(ReadFloat(vertices + stride + 12) == 1.0f &&
                      ReadFloat(vertices + stride + 16) == 0.0f &&
                      ReadFloat(vertices + stride + 28) == 1.0f &&
                      ReadFloat(vertices + stride + 32) == 0.0f,
                  "second point corner must replace every texcoord with point UV");
    }
}

static void InvalidTransientIndexIsRejected()
{
    TransientGeometryHarness harness;
    VxVector positions[2] = {
        VxVector(0.0f, 0.0f, 0.0f),
        VxVector(1.0f, 0.0f, 0.0f)
    };
    CKWORD indices[2] = {0, 2};
    VxDrawPrimitiveData data = {};
    data.VertexCount = 2;
    data.Flags = CKRST_DP_TRANSFORM;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);

    TestCheck(harness.Geometry.Prepare(&harness.Context.Encoder,
                                       VX_LINELIST, indices, 2, &data) == FALSE,
              "transient geometry must reject out-of-range indices");
    TestCheck(harness.Context.Encoder.LastVertexBytes.empty(),
              "invalid indices must be rejected before transient allocation");
}

static void PointScaleClampsAfterViewportConversion()
{
    const float size = CKTransientGeometry::ComputePointSpriteSizeForDistance(
        2.0f, 1.0f, 10.0f, TRUE,
        4.0f, 0.0f, 0.0f, 5.0f, 100.0f);
    TestCheck(fabsf(size - 10.0f) < 0.0001f,
              "point scaling must clamp the final screen-space size");
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
    TransientGeometryHarness harness;
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

static void NonBatchTriangleListUsesGenericPath()
{
    TransientGeometryHarness harness;
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
    TestCheck(stats.TransientPrepareCalls == 1,
              "non-batch triangle list should count one transient prepare");
#endif
}

static void TweenInputsUseDedicatedVertexAttributes()
{
    VxVector position(1.0f, 2.0f, 3.0f);
    VxVector normal(0.0f, 0.0f, 1.0f);
    VxVector tweenPosition(4.0f, 5.0f, 6.0f);
    VxVector tweenNormal(0.0f, 1.0f, 0.0f);
    VxDrawPrimitiveData data = {};
    data.VertexCount = 1;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_TWEEN;
    data.PositionPtr = &position;
    data.PositionStride = sizeof(position);
    data.NormalPtr = &normal;
    data.NormalStride = sizeof(normal);
    data.TweenPositionPtr = &tweenPosition;
    data.TweenPositionStride = sizeof(tweenPosition);
    data.TweenNormalPtr = &tweenNormal;
    data.TweenNormalStride = sizeof(tweenNormal);

    const CKDWORD formatFlags = CKFF_VF_POSITION | CKFF_VF_NORMAL |
        CKFF_VF_TWEENPOSITION | CKFF_VF_TWEENNORMAL;
    const CKDWORD stride = CKVertexLayoutCache::ComputeStride(formatFlags);
    CKBYTE vertex[48] = {};
    TestCheck(stride == sizeof(vertex),
              "Tween vertex layout must contain four float3 attributes");
    CKTransientGeometry::InterleaveVertices(
        vertex, stride, 1, formatFlags, &data);
    TestCheck(ReadFloat(vertex + 0) == 1.0f &&
                  ReadFloat(vertex + 12) == 0.0f &&
                  ReadFloat(vertex + 24) == 4.0f &&
                  ReadFloat(vertex + 28) == 5.0f &&
                  ReadFloat(vertex + 32) == 6.0f &&
                  ReadFloat(vertex + 36) == 0.0f &&
                  ReadFloat(vertex + 40) == 1.0f,
              "Tween position and normal must retain their dedicated byte ranges");
}

static void TweenPrepareKeepsDedicatedVertexAttributes()
{
    TransientGeometryHarness harness;
    VxVector position(1.0f, 2.0f, 3.0f);
    VxVector normal(0.0f, 0.0f, 1.0f);
    VxVector tweenPosition(4.0f, 5.0f, 6.0f);
    VxVector tweenNormal(0.0f, 1.0f, 0.0f);
    VxDrawPrimitiveData data = {};
    data.VertexCount = 1;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_TWEEN;
    data.PositionPtr = &position;
    data.PositionStride = sizeof(position);
    data.NormalPtr = &normal;
    data.NormalStride = sizeof(normal);
    data.TweenPositionPtr = &tweenPosition;
    data.TweenPositionStride = sizeof(tweenPosition);
    data.TweenNormalPtr = &tweenNormal;
    data.TweenNormalStride = sizeof(tweenNormal);

    TestCheck(harness.Geometry.Prepare(
                  &harness.Context.Encoder, VX_POINTLIST,
                  NULL, 0, &data, 0, FALSE, NULL, NULL),
              "Tween transient prepare must succeed");

    CKBOOL hasTangent = FALSE;
    CKBOOL hasBitangent = FALSE;
    for (size_t i = 0; i < harness.Context.LastVertexLayoutElements.size(); ++i) {
        const CK_VERTEX_ATTRIB attrib =
            harness.Context.LastVertexLayoutElements[i].Attrib;
        hasTangent = hasTangent || attrib == CKRST_ATTRIB_TANGENT;
        hasBitangent = hasBitangent || attrib == CKRST_ATTRIB_BITANGENT;
    }
    TestCheck(hasTangent && hasBitangent,
              "Tween transient prepare must retain both dedicated attributes");
    TestCheck(harness.Context.Encoder.LastVertexBytes.size() == 72,
              "Tween transient prepare must allocate the complete vertex stride");
}

static void UserDrawStructureAllocatesTweenStreams()
{
    UserDrawPrimitiveDataClass userData;
    VxDrawPrimitiveData *data = userData.GetStructure(
        (CKRST_DPFLAGS)(CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_TWEEN), 3);
    TestCheck(data && data->TweenPositionPtr && data->TweenNormalPtr,
              "GetDrawPrimitiveStructure must allocate requested tween streams");
    TestCheck(data && data->TweenPositionStride == sizeof(VxVector) &&
                  data->TweenNormalStride == sizeof(VxVector),
              "GetDrawPrimitiveStructure must expose valid tween strides");

    data = userData.GetStructure(CKRST_DP_TRANSFORM, 3);
    TestCheck(data && !data->TweenPositionPtr && !data->TweenNormalPtr,
              "GetDrawPrimitiveStructure must hide unrequested tween streams");
}

static void SubmissionCopyPreservesTweenStreams()
{
    VxVector position;
    VxVector tweenPosition;
    VxDrawPrimitiveData source = {};
    source.VertexCount = 1;
    source.Flags = CKRST_DP_TRANSFORM | CKRST_DP_TWEEN;
    source.PositionPtr = &position;
    source.PositionStride = sizeof(position);
    source.TweenPositionPtr = &tweenPosition;
    source.TweenPositionStride = sizeof(tweenPosition);

    VxDrawPrimitiveData destination;
    UserDrawPrimitiveDataClass::CopySubmissionData(destination, &source);
    TestCheck(destination.TweenPositionPtr == &tweenPosition &&
                  destination.TweenPositionStride == sizeof(tweenPosition),
              "RenderContext submission copy must preserve tween metadata");

    VxDrawPrimitiveDataSimple simple = {};
    simple.VertexCount = 1;
    simple.Flags = CKRST_DP_TRANSFORM;
    simple.PositionPtr = &position;
    simple.PositionStride = sizeof(position);
    UserDrawPrimitiveDataClass::CopySubmissionData(
        destination, (VxDrawPrimitiveData *)&simple);
    TestCheck(destination.PositionPtr == &position &&
                  !destination.TweenPositionPtr && !destination.TweenNormalPtr,
              "RenderContext submission copy must retain the simple-data contract");
}

int main()
{
    TestFramework tests;
    tests.Run("2D quad uses generic fan path", &QuadUsesGenericFanPath);
    tests.Run("indexed triangle fan uses generic path", &IndexedTriangleFanUsesGenericPath);
    tests.Run("paired transient capacity is checked before allocation",
              &PairedTransientCapacityIsCheckedBeforeAllocation);
    tests.Run("extended texcoord data uses generic path", &ExtendedTexcoordDataUsesGenericPath);
    tests.Run("four-component texcoord quad uses generic path", &FourComponentTexcoordQuadUsesGenericPath);
    tests.Run("multiple texture stages apply independent wrap modes",
              &MultipleTextureStagesApplyIndependentWrapModes);
    tests.Run("line wrap handles large coordinate spans",
              &LineWrapHandlesLargeCoordinateSpans);
    tests.Run("large point sprite batch uses 32-bit indices",
              &LargePointSpriteBatchUses32BitIndices);
    tests.Run("indexed point sprites use selected vertices",
              &IndexedPointSpritesUseSelectedVertices);
    tests.Run("point sprites replace every declared texcoord",
              &PointSpritesReplaceEveryDeclaredTexcoord);
    tests.Run("invalid transient index is rejected",
              &InvalidTransientIndexIsRejected);
    tests.Run("point scale clamps after viewport conversion",
              &PointScaleClampsAfterViewportConversion);
    tests.Run("sprite batch uses generic path", &SpriteBatchUsesGenericPath);
    tests.Run("non-batch triangle list uses generic path", &NonBatchTriangleListUsesGenericPath);
    tests.Run("tween inputs use dedicated vertex attributes",
              &TweenInputsUseDedicatedVertexAttributes);
    tests.Run("tween prepare keeps dedicated vertex attributes",
              &TweenPrepareKeepsDedicatedVertexAttributes);
    tests.Run("user draw structure allocates tween streams",
              &UserDrawStructureAllocatesTweenStreams);
    tests.Run("submission copy preserves tween streams",
              &SubmissionCopyPreservesTweenStreams);
    return tests.ExitCode();
}
