#include <gtest/gtest.h>
#include <climits>
#include <cstring>
#include <new>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKGrid.h"
#include "RCKLayer.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
thread_local bool RejectHugeCellArray = false;
thread_local int HugeCellArrayCalls = 0;
thread_local size_t HugeCellArrayBytes = 0;

class InspectLayer : public RCKLayer {
public:
    InspectLayer(CKContext *context, CK_ID owner) : RCKLayer(context, nullptr, owner) {}
    void SeedFlags(CKDWORD flags) { m_Flags = flags; }
    CKDWORD Flags() const { return m_Flags; }
};

class LayerCellCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKGrid, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKLayer, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }

    void TearDown() override {
        RejectHugeCellArray = false;
        if (Context)
            CKCloseContext(Context);
        CKShutdown();
    }

    static CKStateChunk *GridDimensionsChunk(CKContext *context, RCKLayer &layer, int width) {
        CKStateChunk *chunk = CreateCKStateChunk(CKCID_GRID, nullptr);
        chunk->StartWrite();
        chunk->WriteIdentifier(CK_STATESAVE_GRIDDATA);
        chunk->WriteInt(width);
        chunk->WriteInt(1);
        chunk->WriteInt(0);
        chunk->WriteInt(0);
        chunk->WriteDword(CKGRID_FREE);
        chunk->WriteInt(1);
        XObjectArray layers;
        layers.PushBack(layer.GetID());
        layers.Save(chunk, context);
        chunk->CloseChunk();
        chunk->StartRead();
        return chunk;
    }
};
}

// Only impossible-to-service huge cell requests are intercepted. Ordinary
// runtime, SDK and GoogleTest allocations still use the normal allocator.
void *operator new[](size_t bytes, const std::nothrow_t &) noexcept {
    if (RejectHugeCellArray && static_cast<unsigned long long>(bytes) >= 0x100000000ULL) {
        ++HugeCellArrayCalls;
        HugeCellArrayBytes = bytes;
        return nullptr;
    }
    try { return ::operator new[](bytes); }
    catch (const std::bad_alloc &) { return nullptr; }
}

TEST_F(LayerCellCompletionTest, SdkFloatAndSquarePayloadsChangeExactlyFourBytesAtTheHostCellStride) {
    RCKGrid grid(Context);
    grid.SetDimensions(3, 2, 3, 2);
    RCKLayer layer(Context, nullptr, grid.GetID());
    CKLayer *api = &layer;
    CKSquare *cells = api->GetSquareArray();
    ASSERT_NE(nullptr, cells);
    unsigned char expected[6 * sizeof(CKSquare)];
    std::memset(expected, 0xa7, sizeof(expected));
    std::memset(cells, 0xa7, sizeof(expected));
    const CKDWORD words[] = {0x80000000u, 0x7fc12345u, 0x00000001u, 0x7f7fffffu};
    for (int i = 0; i < 4; ++i) {
        SCOPED_TRACE(i);
        float value;
        std::memcpy(&value, &words[i], 4);
        const int index = i + 1;
        if (i & 1)
            ASSERT_TRUE(api->SetValue2(index % 3, index / 3, &value));
        else
            api->SetValue(index % 3, index / 3, &value);
        std::memcpy(expected + index * sizeof(CKSquare), &words[i], 4);
        EXPECT_EQ(0, std::memcmp(expected, cells, sizeof(expected)));
        CKSquare result;
        unsigned char expectedResult[sizeof(CKSquare)];
        std::memset(&result, 0x5a, sizeof(result));
        std::memset(expectedResult, 0x5a, sizeof(expectedResult));
        std::memcpy(expectedResult, &words[i], 4);
        if (i & 1)
            ASSERT_TRUE(api->GetValue2(index % 3, index / 3, &result));
        else
            api->GetValue(index % 3, index / 3, &result);
        EXPECT_EQ(0, std::memcmp(expectedResult, &result, sizeof(result)));
        float output = 0;
        api->GetValue(index % 3, index / 3, &output);
        CKDWORD bits = 0;
        std::memcpy(&bits, &output, 4);
        EXPECT_EQ(words[i], bits);
    }
    // The SDK also permits direct pointer cells. Other four-byte payload
    // writes must leave the complete host pointer in its neighbor intact.
    int token = 17;
    cells[0].ptr = &token;
    CKDWORD value = 0x10203040u;
    ASSERT_TRUE(api->SetValue2(1, 0, &value));
    EXPECT_EQ(&token, cells[0].ptr);
    EXPECT_EQ(value, cells[1].dval);
    const int invalid[][2] = {{-1, 0}, {3, 0}, {0, -1}, {0, 2}, {INT_MIN, 0}, {0, INT_MAX}};
    unsigned char snapshot[6 * sizeof(CKSquare)];
    std::memcpy(snapshot, cells, sizeof(snapshot));
    // Passing the current cell itself is a legal SDK value source or output.
    // All four entry points preserve every byte during that self-copy.
    api->SetValue(1, 0, cells + 1);
    EXPECT_EQ(0, std::memcmp(snapshot, cells, sizeof(snapshot)));
    api->GetValue(1, 0, cells + 1);
    EXPECT_EQ(0, std::memcmp(snapshot, cells, sizeof(snapshot)));
    ASSERT_TRUE(api->SetValue2(1, 0, cells + 1));
    EXPECT_EQ(0, std::memcmp(snapshot, cells, sizeof(snapshot)));
    ASSERT_TRUE(api->GetValue2(1, 0, cells + 1));
    EXPECT_EQ(0, std::memcmp(snapshot, cells, sizeof(snapshot)));
    for (const auto &point : invalid) {
        EXPECT_FALSE(api->SetValue2(point[0], point[1], &value));
        CKDWORD output[] = {0xffffffffu, 0x13579bdfu};
        EXPECT_FALSE(api->GetValue2(point[0], point[1], output));
        EXPECT_EQ(0u, output[0]);
        EXPECT_EQ(0x13579bdfu, output[1]);
        EXPECT_EQ(0, std::memcmp(snapshot, cells, sizeof(snapshot)));
    }
    RecordProperty("host_cell_size", static_cast<int>(sizeof(CKSquare)));
    RecordProperty("sdk_value_bytes", 4);
}

TEST_F(LayerCellCompletionTest, FormatAndVisibilityPreserveCellsUntilExplicitFitKeepsTheLinearPrefix) {
    RCKGrid grid(Context);
    grid.SetDimensions(2, 2, 2, 2);
    InspectLayer layer(Context, grid.GetID());
    CKLayer *api = &layer;
    CKSquare *original = api->GetSquareArray();
    ASSERT_NE(nullptr, original);
    for (int i = 0; i < 4; ++i)
        original[i].dval = static_cast<CKDWORD>(11 * (i + 1));
    layer.SeedFlags(0xa5a5f0e0u);
    api->SetType(INT_MIN);
    api->SetFormat(9);
    api->SetVisible(-7);
    EXPECT_EQ(INT_MIN, api->GetType());
    EXPECT_EQ(9, api->GetFormat());
    EXPECT_TRUE(api->IsVisible());
    EXPECT_EQ(0xa5a5f0e1u, layer.Flags());
    api->SetVisible(FALSE);
    EXPECT_FALSE(api->IsVisible());
    EXPECT_EQ(0xa5a5f0e0u, layer.Flags());
    grid.SetDimensions(3, 2, 3, 2);
    layer.FitSquareArray();
    EXPECT_EQ(original, api->GetSquareArray());
    EXPECT_EQ(44u, original[3].dval);
    api->SetType(INT_MAX);
    api->SetFormat(0);
    EXPECT_EQ(INT_MAX, api->GetType());
    EXPECT_EQ(0, api->GetFormat());
    EXPECT_EQ(original, api->GetSquareArray());
    layer.FitSquareArray();
    const CKDWORD expected[] = {11, 22, 33, 44, 0, 0};
    for (int i = 0; i < 6; ++i) {
        CKDWORD value = 99;
        ASSERT_TRUE(api->GetValue2(i % 3, i / 3, &value));
        EXPECT_EQ(expected[i], value);
    }
    // Fitting retains row-major ordinal values, rather than treating the
    // old and new widths as an implicit geometric resampling operation.
    grid.SetDimensions(3, 1, 3, 1);
    layer.FitSquareArray();
    grid.SetDimensions(2, 2, 2, 2);
    layer.FitSquareArray();
    const CKDWORD resized[] = {11, 22, 33, 0};
    for (int i = 0; i < 4; ++i) {
        CKDWORD value = 99;
        api->GetValue(i % 2, i / 2, &value);
        EXPECT_EQ(resized[i], value);
    }
    EXPECT_EQ(0xa5a5f0e0u, layer.Flags());
}

TEST_F(LayerCellCompletionTest, HostByteLimitAndRepresentableHugeAllocationHaveDistinctCheckedResults) {
    RCKGrid grid(Context);
    grid.SetDimensions(2, 1, 2, 1);
    RCKLayer layer(Context, nullptr, grid.GetID());
    CKSquare *original = layer.GetSquareArray();
    ASSERT_NE(nullptr, original);
    original[0].dval = 0x12345678u;
    original[1].dval = 0xfedcba98u;
    const int count = 1 << 30;
    ASSERT_LT(count, INT_MAX);
    HugeCellArrayCalls = 0;
    HugeCellArrayBytes = 0;
    grid.SetDimensions(count, 1, 1, 1);
    RejectHugeCellArray = true;
    layer.FitSquareArray();
    RejectHugeCellArray = false;
    EXPECT_EQ(original, layer.GetSquareArray());
    EXPECT_EQ(sizeof(size_t) == 4 ? 0 : 1, HugeCellArrayCalls);
    if (sizeof(size_t) == 8)
        EXPECT_EQ(0x200000000ULL, static_cast<unsigned long long>(HugeCellArrayBytes));
    grid.SetDimensions(2, 1, 2, 1);
    CKStateChunk *chunk = GridDimensionsChunk(Context, layer, count);
    ASSERT_NE(nullptr, chunk);
    CKFile file(Context);
    HugeCellArrayCalls = 0;
    HugeCellArrayBytes = 0;
    RejectHugeCellArray = true;
    const CKERROR error = grid.Load(chunk, &file);
    RejectHugeCellArray = false;
    DeleteCKStateChunk(chunk);
    // Win32 rejects the byte count before allocation even though the
    // signed cell product fits. Win64 can represent it; only the exact
    // huge allocation is rejected by this fixture, without requesting8GB.
    EXPECT_EQ(sizeof(size_t) == 4 ? CKERR_INVALIDPARAMETER : CKERR_OUTOFMEMORY, error);
    EXPECT_EQ(sizeof(size_t) == 4 ? 0 : 1, HugeCellArrayCalls);
    if (sizeof(size_t) == 8)
        EXPECT_EQ(0x200000000ULL, static_cast<unsigned long long>(HugeCellArrayBytes));
    EXPECT_EQ(original, layer.GetSquareArray());
    EXPECT_EQ(0x12345678u, original[0].dval);
    EXPECT_EQ(0xfedcba98u, original[1].dval);
    grid.SetDimensions(2, 1, 2, 1);
    layer.FitSquareArray();
    CKDWORD last = 0;
    ASSERT_TRUE(layer.GetValue2(1, 0, &last));
    EXPECT_EQ(0xfedcba98u, last);
    EXPECT_EQ(original, layer.GetSquareArray());
    RecordProperty("cell_count", count);
    RecordProperty("host_cell_size", static_cast<int>(sizeof(CKSquare)));
    RecordProperty("checked_error", error);
}
