#include <gtest/gtest.h>
#include "CKRenderEngineTypes.h"
#include "../src/CKVertexPacking.h"
#include <string.h>

namespace {
float PackedFloat(const CKBYTE *bytes, int offset) {
    float value;
    memcpy(&value, bytes + offset, sizeof(value));
    return value;
}
CKDWORD PackedDword(const CKBYTE *bytes, int offset) {
    CKDWORD value;
    memcpy(&value, bytes + offset, sizeof(value));
    return value;
}
void ExpectFloats(const CKBYTE *bytes, int offset, const float *expected, int count) {
    for (int c = 0; c < count; ++c) EXPECT_FLOAT_EQ(expected[c], PackedFloat(bytes, offset + c * 4));
}
void ExpectOutsideUnchanged(const CKBYTE *bytes, int size, int begin, int end) {
    for (int i = 0; i < size; ++i)
        if (i < begin || i >= end) EXPECT_EQ(0xa5u, bytes[i]) << "Byte " << i;
}

struct PackingStreams {
    float Position[3][7], Normal[3][5], UV[8][40];
    CKDWORD Diffuse[3][2], Specular[3][3];
    VxDrawPrimitiveData Data = {};
    PackingStreams() {
        memset(this->Position, 0x7d, sizeof(Position));
        memset(this->Normal, 0x7d, sizeof(Normal));
        memset(this->UV, 0x7d, sizeof(UV));
        for (int vertex = 0; vertex < 3; ++vertex) {
            for (int c = 0; c < 4; ++c) Position[vertex][c] = 100.0f + vertex * 10 + c;
            for (int c = 0; c < 3; ++c) Normal[vertex][c] = 300.0f + vertex * 10 + c;
            Diffuse[vertex][0] = 0x10203040u + vertex;
            Specular[vertex][0] = 0x50607080u + vertex;
            for (int stage = 0; stage < 8; ++stage)
                for (int c = 0; c < 4; ++c) UV[stage][vertex * (4 + stage) + c] = stage * 100 + vertex * 10 + c + 0.25f;
        }
        Data.VertexCount = 3;
        Data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR | CKRST_DP_STAGES1;
        Data.PositionPtr = Position; Data.PositionStride = sizeof(Position[0]);
        Data.NormalPtr = Normal; Data.NormalStride = sizeof(Normal[0]);
        Data.ColorPtr = Diffuse; Data.ColorStride = sizeof(Diffuse[0]);
        Data.SpecularColorPtr = Specular; Data.SpecularColorStride = sizeof(Specular[0]);
        Data.TexCoordPtr = UV[0]; Data.TexCoordStride = 4 * sizeof(float);
        for (int stage = 1; stage < 8; ++stage) {
            Data.TexCoordPtrs[stage - 1] = UV[stage];
            Data.TexCoordStrides[stage - 1] = (4 + stage) * sizeof(float);
        }
    }
};

TEST(VertexPackingTest, FormatFiltersNonVertexFlagsAndOnlyUnavailableNormalAndTweenSets) {
    PackingStreams source;
    const CKDWORD expected = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR |
        CKRST_DP_STAGES7 | CKRST_DP_WEIGHTS5 | CKRST_DP_MATRIXPAL | CKRST_DP_PSIZE | CKRST_DP_TWEEN;
    source.Data.Flags = expected | CKRST_DP_DOCLIP | CKRST_DP_VBUFFER | 0x80000000u;
    for (int normal = 0; normal < 2; ++normal) {
        for (int tween = 0; tween < 2; ++tween) {
            source.Data.NormalPtr = normal ? source.Normal : nullptr;
            source.Data.TweenPositionPtr = tween ? source.Position : nullptr;
            CKDWORD actualExpected = expected;
            if (!normal) actualExpected &= ~CKRST_DP_LIGHT;
            if (!tween) actualExpected &= ~CKRST_DP_TWEEN;
            EXPECT_EQ(actualExpected, CKRSTVertexFormatFromDrawData(&source.Data));
            EXPECT_EQ(expected | CKRST_DP_DOCLIP | CKRST_DP_VBUFFER | 0x80000000u, source.Data.Flags);
        }
    }
    EXPECT_EQ(0u, CKRSTVertexFormatFromDrawData(nullptr));
}

TEST(VertexPackingTest, SingleVertexUsesIndependentSourceAndDestinationIndicesAndEveryAttributeStride) {
    PackingStreams source;
    CKRSTVertexLayout layout;
    ASSERT_EQ(48u, CKRSTGetVertexLayout(source.Data.Flags, nullptr, &layout));
    alignas(16) CKBYTE bytes[192]; memset(bytes, 0xa5, sizeof(bytes));
    CKRSTPackVertex(layout, bytes + 16, 1, &source.Data, 2);
    const CKBYTE *vertex = bytes + 16 + 48;
    ExpectFloats(vertex, 0, source.Position[2], 3);
    ExpectFloats(vertex, 12, source.Normal[2], 3);
    EXPECT_EQ(source.Diffuse[2][0], PackedDword(vertex, 24));
    EXPECT_EQ(source.Specular[2][0], PackedDword(vertex, 28));
    ExpectFloats(vertex, 32, source.UV[0] + 2 * 4, 2);
    ExpectFloats(vertex, 40, source.UV[1] + 2 * 5, 2);
    ExpectOutsideUnchanged(bytes, sizeof(bytes), 64, 112);
    EXPECT_FLOAT_EQ(120.0f, source.Position[2][0]);
    EXPECT_EQ(3, source.Data.VertexCount);
}

TEST(VertexPackingTest, BulkHonorsItsCountAndDoesNotWriteTheRemainingDestinationRecords) {
    PackingStreams source;
    CKRSTVertexLayout layout;
    ASSERT_EQ(48u, CKRSTGetVertexLayout(source.Data.Flags, nullptr, &layout));
    alignas(16) CKBYTE bytes[192]; memset(bytes, 0xa5, sizeof(bytes));
    CKRSTPackVertices(layout, bytes + 16, 2, &source.Data); // Source contains three records.
    for (int i = 0; i < 2; ++i) {
        const CKBYTE *vertex = bytes + 16 + i * 48;
        ExpectFloats(vertex, 0, source.Position[i], 3);
        ExpectFloats(vertex, 12, source.Normal[i], 3);
        EXPECT_EQ(source.Diffuse[i][0], PackedDword(vertex, 24));
        EXPECT_EQ(source.Specular[i][0], PackedDword(vertex, 28));
        ExpectFloats(vertex, 32, source.UV[0] + i * 4, 2);
        ExpectFloats(vertex, 40, source.UV[1] + i * 5, 2);
    }
    ExpectOutsideUnchanged(bytes, sizeof(bytes), 16, 112);
}

TEST(VertexPackingTest, PretransformedPositionKeepsRhwAndPlacesColorsAfterAllFourFloats) {
    PackingStreams source;
    source.Data.Flags = CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR | CKRST_DP_STAGES0;
    source.Position[1][3] = 0.25f;
    CKRSTVertexLayout layout;
    ASSERT_EQ(32u, CKRSTGetVertexLayout(source.Data.Flags, nullptr, &layout));
    alignas(16) CKBYTE bytes[64]; memset(bytes, 0xa5, sizeof(bytes));
    CKRSTPackVertex(layout, bytes + 16, 0, &source.Data, 1);
    ExpectFloats(bytes + 16, 0, source.Position[1], 4);
    EXPECT_EQ(source.Diffuse[1][0], PackedDword(bytes + 16, 16));
    EXPECT_EQ(source.Specular[1][0], PackedDword(bytes + 16, 20));
    ExpectFloats(bytes + 16, 24, source.UV[0] + 4, 2);
    ExpectOutsideUnchanged(bytes, sizeof(bytes), 16, 48);
}

TEST(VertexPackingTest, AbsentModelAttributesHaveDeterministicDefaultsIncludingTweenAndPointSize) {
    VxDrawPrimitiveData source = {};
    CKRSTVertexLayout layout;
    const CKDWORD flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_TWEEN | CKRST_DP_PSIZE |
        CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR | CKRST_DP_STAGES1;
    ASSERT_EQ(76u, CKRSTGetVertexLayout(flags, nullptr, &layout));
    alignas(16) CKBYTE bytes[112]; memset(bytes, 0xa5, sizeof(bytes));
    CKRSTPackVertex(layout, bytes + 16, 0, &source, 0);
    const float zero[] = {0, 0, 0}, normal[] = {0, 0, 1};
    ExpectFloats(bytes + 16, 0, zero, 3);
    ExpectFloats(bytes + 16, 12, normal, 3);
    ExpectFloats(bytes + 16, 24, zero, 3);
    ExpectFloats(bytes + 16, 36, normal, 3);
    EXPECT_FLOAT_EQ(1, PackedFloat(bytes + 16, 48));
    EXPECT_EQ(0xffffffffu, PackedDword(bytes + 16, 52));
    EXPECT_EQ(0u, PackedDword(bytes + 16, 56));
    for (int offset = 60; offset < 76; offset += 4) EXPECT_FLOAT_EQ(0, PackedFloat(bytes + 16, offset));
    ExpectOutsideUnchanged(bytes, sizeof(bytes), 16, 92);
}

TEST(VertexPackingTest, AbsentScreenPositionGetsUnitRhwAndOpaqueBlackSpecular) {
    VxDrawPrimitiveData source = {};
    CKRSTVertexLayout layout;
    ASSERT_EQ(28u, CKRSTGetVertexLayout(CKRST_DP_PSIZE | CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR, nullptr, &layout));
    alignas(16) CKBYTE bytes[64]; memset(bytes, 0xa5, sizeof(bytes));
    CKRSTPackVertex(layout, bytes + 16, 0, &source, 0);
    const float position[] = {0, 0, 0, 1};
    ExpectFloats(bytes + 16, 0, position, 4);
    EXPECT_FLOAT_EQ(1, PackedFloat(bytes + 16, 16));
    EXPECT_EQ(0xffffffffu, PackedDword(bytes + 16, 20));
    EXPECT_EQ(0xff000000u, PackedDword(bytes + 16, 24));
    ExpectOutsideUnchanged(bytes, sizeof(bytes), 16, 44);
}

TEST(VertexPackingTest, ZeroAttributeStridesBroadcastOneRecordWithoutChangingOtherStreams) {
    PackingStreams source;
    source.Data.PositionStride = source.Data.NormalStride = source.Data.ColorStride = 0;
    source.Data.SpecularColorStride = source.Data.TexCoordStride = 0;
    source.Data.TexCoordStrides[0] = 0;
    CKRSTVertexLayout layout;
    ASSERT_EQ(48u, CKRSTGetVertexLayout(source.Data.Flags, nullptr, &layout));
    alignas(16) CKBYTE bytes[144];
    CKRSTPackVertices(layout, bytes, 3, &source.Data);
    for (int i = 0; i < 3; ++i) {
        const CKBYTE *vertex = bytes + i * 48;
        ExpectFloats(vertex, 0, source.Position[0], 3);
        ExpectFloats(vertex, 12, source.Normal[0], 3);
        EXPECT_EQ(source.Diffuse[0][0], PackedDword(vertex, 24));
        EXPECT_EQ(source.Specular[0][0], PackedDword(vertex, 28));
        ExpectFloats(vertex, 32, source.UV[0], 2);
        ExpectFloats(vertex, 40, source.UV[1], 2);
    }
}

TEST(VertexPackingTest, EightTextureSetsKeepTheirOwnDimensionsPointersAndPaddedStrides) {
    PackingStreams source;
    const CKBYTE dims[] = {1, 2, 3, 4, 1, 2, 3, 4};
    const int offsets[] = {12, 16, 24, 36, 52, 56, 64, 76};
    CKRSTVertexLayout layout;
    ASSERT_EQ(92u, CKRSTGetVertexLayout(CKRST_DP_TRANSFORM | CKRST_DP_STAGES7, dims, &layout));
    alignas(16) CKBYTE bytes[128]; memset(bytes, 0xa5, sizeof(bytes));
    CKRSTPackVertex(layout, bytes + 16, 0, &source.Data, 1);
    ExpectFloats(bytes + 16, 0, source.Position[1], 3);
    for (int stage = 0; stage < 8; ++stage)
        ExpectFloats(bytes + 16, offsets[stage], source.UV[stage] + 4 + stage, dims[stage]);
    ExpectOutsideUnchanged(bytes, sizeof(bytes), 16, 108);
}

TEST(VertexPackingTest, FirstUvOverrideCopiesOnlyTwoValuesAndZeroesAnyExtraDimensions) {
    PackingStreams source;
    const float overrideUv[] = {-0.25f, 1.75f};
    for (CKBYTE dims = 1; dims <= 4; ++dims) {
        CKRSTVertexLayout layout;
        ASSERT_EQ(12u + dims * 4u, CKRSTGetVertexLayout(CKRST_DP_TRANSFORM | CKRST_DP_STAGES0, &dims, &layout));
        alignas(16) CKBYTE bytes[64]; memset(bytes, 0xa5, sizeof(bytes));
        CKRSTPackVertex(layout, bytes + 16, 0, &source.Data, 2, overrideUv);
        for (int c = 0; c < dims; ++c)
            EXPECT_FLOAT_EQ(c < 2 ? overrideUv[c] : 0.0f, PackedFloat(bytes + 16, 12 + c * 4));
        ExpectOutsideUnchanged(bytes, sizeof(bytes), 16, 16 + layout.Stride);
    }
    const CKBYTE dims[] = {3, 3};
    CKRSTVertexLayout layout;
    ASSERT_EQ(36u, CKRSTGetVertexLayout(CKRST_DP_TRANSFORM | CKRST_DP_STAGES1, dims, &layout));
    alignas(16) CKBYTE bytes[36];
    CKRSTPackVertex(layout, bytes, 0, &source.Data, 2, overrideUv);
    ExpectFloats(bytes, 24, source.UV[1] + 2 * 5, 3); // Later sets use their source, never the override.
}

TEST(VertexPackingTest, EveryWeightCountAndPaletteWordFollowsTheSourcePositionWithoutNormalization) {
    for (int count = 1; count <= 5; ++count) {
        for (int palette = 0; palette < 2; ++palette) {
            alignas(16) CKBYTE records[3][64]; memset(records, 0x7d, sizeof(records));
            const float position[] = {11, 12, 13};
            const float weights[] = {-0.5f, -0.25f, 0, 0.25f, 0.5f};
            const CKDWORD indices = 0x07050301u;
            memcpy(records[2], position, 12);
            memcpy(records[2] + 12, weights, count * 4);
            memcpy(records[2] + 12 + count * 4, &indices, 4);
            VxDrawPrimitiveData source = {};
            source.PositionPtr = records; source.PositionStride = sizeof(records[0]);
            CKRSTVertexLayout layout;
            const CKDWORD flags = CKRST_DP_TRANSFORM | CKRST_DP_WEIGHT(count) | CKRST_DP_DIFFUSE |
                (palette ? CKRST_DP_MATRIXPAL : 0);
            const int stride = 12 + count * 4 + palette * 4 + 4;
            ASSERT_EQ(static_cast<CKDWORD>(stride), CKRSTGetVertexLayout(flags, nullptr, &layout));
            alignas(16) CKBYTE bytes[192]; memset(bytes, 0xa5, sizeof(bytes));
            CKRSTPackVertex(layout, bytes + 16, 1, &source, 2);
            const CKBYTE *vertex = bytes + 16 + stride;
            ExpectFloats(vertex, 0, position, 3);
            ExpectFloats(vertex, 12, weights, count);
            if (palette) EXPECT_EQ(indices, PackedDword(vertex, 12 + count * 4));
            EXPECT_EQ(0xffffffffu, PackedDword(vertex, stride - 4));
            ExpectOutsideUnchanged(bytes, sizeof(bytes), 16 + stride, 16 + 2 * stride);
        }
    }
}

TEST(VertexPackingTest, PositionOnlyRecordsUseZeroWeightsAndIndicesRatherThanNeighborRecordBytes) {
    float positions[2][3] = {{1, 2, 3}, {4, 5, 6}};
    VxDrawPrimitiveData source = {};
    source.PositionPtr = positions; source.PositionStride = sizeof(positions[0]);
    CKRSTVertexLayout layout;
    ASSERT_EQ(32u, CKRSTGetVertexLayout(CKRST_DP_TRANSFORM | CKRST_DP_WEIGHTS3 |
        CKRST_DP_MATRIXPAL | CKRST_DP_DIFFUSE, nullptr, &layout));
    alignas(16) CKBYTE bytes[32]; memset(bytes, 0xa5, sizeof(bytes));
    CKRSTPackVertex(layout, bytes, 0, &source, 0);
    ExpectFloats(bytes, 0, positions[0], 3);
    for (int offset = 12; offset < 28; offset += 4) EXPECT_EQ(0u, PackedDword(bytes, offset));
    EXPECT_EQ(0xffffffffu, PackedDword(bytes, 28));
}

TEST(VertexPackingTest, WeightsRemainPresentWhenTheSourceRecordHasNoPaletteWord) {
    float records[2][5] = {{1, 2, 3, 0.25f, -0.5f}, {4, 5, 6, -0.25f, 0.5f}};
    VxDrawPrimitiveData source = {};
    source.PositionPtr = records; source.PositionStride = sizeof(records[0]);
    CKRSTVertexLayout layout;
    ASSERT_EQ(24u, CKRSTGetVertexLayout(CKRST_DP_TRANSFORM | CKRST_DP_WEIGHTS2 | CKRST_DP_MATRIXPAL, nullptr, &layout));
    alignas(16) CKBYTE bytes[24]; memset(bytes, 0xa5, sizeof(bytes));
    CKRSTPackVertex(layout, bytes, 0, &source, 1);
    ExpectFloats(bytes, 0, records[1], 5);
    EXPECT_EQ(0u, PackedDword(bytes, 20));
}

TEST(VertexPackingTest, TweenPositionAndNormalHaveIndependentStridesAndMissingNormalGetsPositiveZ) {
    PackingStreams source;
    float tweenPosition[3][8] = {}, tweenNormal[3][9] = {};
    const float position[] = {-3, 7, 19}, normal[] = {0.25f, -0.5f, 0.75f};
    memcpy(tweenPosition[2], position, 12); memcpy(tweenNormal[2], normal, 12);
    source.Data.TweenPositionPtr = tweenPosition; source.Data.TweenPositionStride = sizeof(tweenPosition[0]);
    source.Data.TweenNormalPtr = tweenNormal; source.Data.TweenNormalStride = sizeof(tweenNormal[0]);
    CKRSTVertexLayout layout;
    ASSERT_EQ(48u, CKRSTGetVertexLayout(CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_TWEEN, nullptr, &layout));
    alignas(16) CKBYTE bytes[48];
    CKRSTPackVertex(layout, bytes, 0, &source.Data, 2);
    ExpectFloats(bytes, 0, source.Position[2], 3);
    ExpectFloats(bytes, 12, source.Normal[2], 3);
    ExpectFloats(bytes, 24, position, 3);
    ExpectFloats(bytes, 36, normal, 3);
    source.Data.TweenNormalPtr = nullptr;
    CKRSTPackVertex(layout, bytes, 0, &source.Data, 2);
    const float fallback[] = {0, 0, 1};
    ExpectFloats(bytes, 24, position, 3);
    ExpectFloats(bytes, 36, fallback, 3);
}

TEST(VertexPackingTest, FloatAndColorBitsAreTransportedWithoutNumericOrChannelConversion) {
    const CKDWORD bits[] = {0x80000000u, 0x7fc12345u, 0x7f800000u};
    float position[3]; memcpy(position, bits, sizeof(bits));
    CKDWORD diffuse = 0x01020304u, specular = 0xfedcba98u;
    VxDrawPrimitiveData source = {};
    source.PositionPtr = position; source.PositionStride = sizeof(position);
    source.ColorPtr = &diffuse; source.ColorStride = 4;
    source.SpecularColorPtr = &specular; source.SpecularColorStride = 4;
    CKRSTVertexLayout layout;
    ASSERT_EQ(20u, CKRSTGetVertexLayout(CKRST_DP_TRANSFORM | CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR, nullptr, &layout));
    alignas(16) CKBYTE bytes[20];
    CKRSTPackVertices(layout, bytes, 1, &source);
    for (int c = 0; c < 3; ++c) EXPECT_EQ(bits[c], PackedDword(bytes, c * 4));
    EXPECT_EQ(diffuse, PackedDword(bytes, 12)); EXPECT_EQ(specular, PackedDword(bytes, 16));
}

TEST(VertexPackingTest, StandardInterleavedVxVerticesKeepTheCompleteThirtyTwoBytePayload) {
    VxVertex vertices[2];
    for (int i = 0; i < 2; ++i) {
        vertices[i].m_Position = VxVector(1.0f + i, -2.0f - i, 3.0f + i);
        vertices[i].m_Normal = VxVector(-0.25f, 0.5f, 0.75f);
        vertices[i].m_UV = Vx2DVector(i * 0.25f, i * 0.5f);
    }
    VxDrawPrimitiveData source = {};
    source.VertexCount = 2;
    source.PositionPtr = &vertices[0].m_Position; source.PositionStride = sizeof(VxVertex);
    source.NormalPtr = &vertices[0].m_Normal; source.NormalStride = sizeof(VxVertex);
    source.TexCoordPtr = &vertices[0].m_UV; source.TexCoordStride = sizeof(VxVertex);
    CKRSTVertexLayout layout;
    ASSERT_EQ(32u, CKRSTGetVertexLayout(CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_STAGES0, nullptr, &layout));
    ASSERT_EQ(32u, sizeof(VxVertex));
    alignas(16) CKBYTE bytes[96]; memset(bytes, 0xa5, sizeof(bytes));
    CKRSTPackVertices(layout, bytes + 16, 2, &source);
    EXPECT_EQ(0, memcmp(vertices, bytes + 16, sizeof(vertices)));
    ExpectOutsideUnchanged(bytes, sizeof(bytes), 16, 80);
}

TEST(VertexPackingTest, ZeroCountBulkLeavesAllDestinationBytesUntouched) {
    PackingStreams source;
    CKRSTVertexLayout layout;
    CKRSTGetVertexLayout(source.Data.Flags, nullptr, &layout);
    alignas(16) CKBYTE bytes[64]; memset(bytes, 0xa5, sizeof(bytes));
    CKRSTPackVertices(layout, bytes + 16, 0, &source.Data);
    ExpectOutsideUnchanged(bytes, sizeof(bytes), 0, 0);
}
} // namespace
