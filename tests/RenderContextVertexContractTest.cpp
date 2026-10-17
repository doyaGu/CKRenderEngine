#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MaterialRenderObserver.h"
#include <cstdint>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <malloc.h>
#endif

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class VertexContractBoundary : public MaterialRenderObserver {
public:
    int Creates = 0, Locks = 0, Unlocks = 0, Draws = 0;
    VxVector LastPosition[2];
    CKDWORD SeenFlags = 0;

    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *, const void *, CKDWORD *) override {
        ++Creates;
        return FALSE;
    }
    void *LockVertexBuffer(CKDWORD, CKDWORD, CKDWORD, CKRST_LOCKFLAGS) override {
        ++Locks;
        return nullptr;
    }
    CKBOOL UnlockVertexBuffer(CKDWORD) override {
        ++Unlocks;
        return FALSE;
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE, CKWORD *, int, VxDrawPrimitiveData *data) override {
        if (Draws < 2) {
            const CKBYTE *last = static_cast<const CKBYTE *>(data->PositionPtr)
                + (data->VertexCount - 1) * data->PositionStride;
            LastPosition[Draws] = *reinterpret_cast<const VxVector *>(last);
        }
        SeenFlags = data->Flags;
        ++Draws;
        return TRUE;
    }
};

class RenderContextVertexContractTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *Device = nullptr;
    VertexContractBoundary Boundary;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context);
        Device->m_RasterizerContext = &Boundary;
        Device->BeginFrameErrorTracking();
    }
    void TearDown() override {
        Device->m_RasterizerContext = nullptr;
        delete Device;
        CKCloseContext(Context);
        CKShutdown();
    }
};

TEST_F(RenderContextVertexContractTest, RequestedVbufferUsesWritableCpuStreamsAcrossTwoRealSubmissions) {
    VxDrawPrimitiveData *data = Device->GetDrawPrimitiveStructure(
        static_cast<CKRST_DPFLAGS>(CKRST_DP_VBUFFER | CKRST_DP_TRANSFORM), 3);
    ASSERT_NE(nullptr, data);
    ASSERT_NE(nullptr, data->PositionPtr);
    EXPECT_EQ(0u, data->Flags & CKRST_DP_VBUFFER);
    VxVector *last = reinterpret_cast<VxVector *>(static_cast<CKBYTE *>(data->PositionPtr)
        + 2 * data->PositionStride);
    *last = VxVector(3, 5, 7);
    EXPECT_EQ(nullptr, Device->LockCurrentVB(3));
    EXPECT_FALSE(Device->ReleaseCurrentVB());
    ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, data));
    *last = VxVector(11, 13, 17);
    EXPECT_EQ(nullptr, Device->LockCurrentVB(3));
    EXPECT_FALSE(Device->ReleaseCurrentVB());
    ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, data));
    EXPECT_FLOAT_EQ(7, Boundary.LastPosition[0].z);
    EXPECT_FLOAT_EQ(17, Boundary.LastPosition[1].z);
    EXPECT_EQ(2, Boundary.Draws);
    EXPECT_EQ(0u, Boundary.SeenFlags & CKRST_DP_VBUFFER);
    EXPECT_EQ(0, Boundary.Creates);
    EXPECT_EQ(0, Boundary.Locks);
    EXPECT_EQ(0, Boundary.Unlocks);
}

TEST_F(RenderContextVertexContractTest, UnsupportedLockAndReleasePreserveTheExistingCpuAllocationAndIndices) {
    VxDrawPrimitiveData *data = Device->GetDrawPrimitiveStructure(CKRST_DP_TRANSFORM, 4);
    CKWORD *indices = Device->GetDrawPrimitiveIndices(6);
    ASSERT_NE(nullptr, data->PositionPtr);
    ASSERT_NE(nullptr, indices);
    void *positions = data->PositionPtr;
    reinterpret_cast<VxVector *>(static_cast<CKBYTE *>(positions) + 3 * data->PositionStride)->z = 29;
    indices[5] = 3;
    EXPECT_EQ(nullptr, Device->LockCurrentVB(100));
    EXPECT_FALSE(Device->ReleaseCurrentVB());
    data = Device->GetDrawPrimitiveStructure(CKRST_DP_TRANSFORM, 4);
    EXPECT_EQ(positions, data->PositionPtr);
    EXPECT_FLOAT_EQ(29, reinterpret_cast<VxVector *>(static_cast<CKBYTE *>(positions)
        + 3 * data->PositionStride)->z);
    EXPECT_EQ(indices, Device->GetDrawPrimitiveIndices(6));
    EXPECT_EQ(3, indices[5]);
    EXPECT_EQ(0, Boundary.Locks);
    EXPECT_EQ(0, Boundary.Unlocks);
}

TEST_F(RenderContextVertexContractTest, HeadlessContextReportsNoCurrentHardwareBufferWithoutAcquiringCpuStorage) {
    Device->m_RasterizerContext = nullptr;
    EXPECT_EQ(nullptr, Device->LockCurrentVB(3));
    EXPECT_FALSE(Device->ReleaseCurrentVB());
    VxDrawPrimitiveData *data = Device->GetDrawPrimitiveStructure(
        static_cast<CKRST_DPFLAGS>(CKRST_DP_TRANSFORM | CKRST_DP_VBUFFER), 3);
    ASSERT_NE(nullptr, data->PositionPtr);
    EXPECT_EQ(0u, data->Flags & CKRST_DP_VBUFFER);
    reinterpret_cast<VxVector *>(data->PositionPtr)->x = 37;
    EXPECT_EQ(nullptr, Device->LockCurrentVB(3));
    EXPECT_FALSE(Device->ReleaseCurrentVB());
    EXPECT_FLOAT_EQ(37, reinterpret_cast<VxVector *>(data->PositionPtr)->x);
}

#if defined(_WIN32)
// Observe real CRT heap allocations without intercepting allocation functions
// or reading the allocator's private aligned-allocation headers. All heap walks
// and lifetime transitions below run before GoogleTest allocates diagnostics.
struct ScratchHeapObservation {
    void *Pointers[15] = {};
    void *Blocks[15] = {};
    bool Found[15] = {};
    bool Complete = false;
};

void CollectScratchPointers(UserDrawPrimitiveDataClass &storage, ScratchHeapObservation &result) {
    result.Pointers[0] = storage.PositionPtr;
    result.Pointers[1] = storage.NormalPtr;
    result.Pointers[2] = storage.TweenPositionPtr;
    result.Pointers[3] = storage.TweenNormalPtr;
    result.Pointers[4] = storage.ColorPtr;
    result.Pointers[5] = storage.SpecularColorPtr;
    result.Pointers[6] = storage.TexCoordPtr;
    for (int i = 0; i < 7; ++i)
        result.Pointers[7 + i] = storage.TexCoordPtrs[i];
    result.Pointers[14] = storage.m_Indices;
}

void ObserveScratchHeap(ScratchHeapObservation &result) {
    HANDLE heap = reinterpret_cast<HANDLE>(_get_heap_handle());
    if (!HeapLock(heap))
        return;
    PROCESS_HEAP_ENTRY entry = {};
    while (HeapWalk(heap, &entry)) {
        if (!(entry.wFlags & PROCESS_HEAP_ENTRY_BUSY))
            continue;
        const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(entry.lpData);
        for (int i = 0; i < 15; ++i) {
            const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(result.Pointers[i]);
            if (address >= start && address - start < entry.cbData) {
                result.Blocks[i] = entry.lpData;
                result.Found[i] = true;
            }
        }
    }
    const DWORD error = GetLastError();
    HeapUnlock(heap);
    result.Complete = error == ERROR_NO_MORE_ITEMS;
}

void ExpectLiveScratch(const ScratchHeapObservation &result) {
    ASSERT_TRUE(result.Complete);
    for (int i = 0; i < 15; ++i) {
        ASSERT_NE(nullptr, result.Pointers[i]) << i;
        EXPECT_TRUE(result.Found[i]) << i;
        for (int j = 0; j < i; ++j)
            EXPECT_NE(result.Blocks[j], result.Blocks[i]) << i << "," << j;
    }
}

TEST(RenderContextVertexStorageTest, DestructionReclaimsEveryAlignedStreamAndTheIndexArray) {
    ScratchHeapObservation live, destroyed;
    {
        UserDrawPrimitiveDataClass storage;
        storage.GetStructure(static_cast<CKRST_DPFLAGS>(CKRST_DP_TRANSFORM | CKRST_DP_LIGHT
            | CKRST_DP_TWEEN | CKRST_DP_STAGES7 | CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR), 4096);
        storage.GetIndices(16384)[16383] = 4095;
        CollectScratchPointers(storage, live);
        ObserveScratchHeap(live);
        destroyed = live;
        for (int i = 0; i < 15; ++i) destroyed.Found[i] = false;
    }
    ObserveScratchHeap(destroyed);
    ExpectLiveScratch(live);
    ASSERT_TRUE(destroyed.Complete);
    for (int i = 0; i < 15; ++i)
        EXPECT_FALSE(destroyed.Found[i]) << "allocation " << i << " survived destruction";
}

TEST(RenderContextVertexStorageTest, ClearReclaimsStreamsWhileRetainingWritableIndexStorageUntilDestruction) {
    ScratchHeapObservation live, cleared, destroyed;
    CKWORD valueAfterClear = 0;
    {
        UserDrawPrimitiveDataClass storage;
        storage.GetStructure(CKRST_DP_TRANSFORM, 4096);
        storage.GetIndices(16384)[16383] = 2903;
        CollectScratchPointers(storage, live);
        ObserveScratchHeap(live);
        cleared = live;
        for (int i = 0; i < 15; ++i) cleared.Found[i] = false;
        storage.ClearStructure();
        storage.ClearStructure();
        valueAfterClear = storage.GetIndices(1)[16383];
        ObserveScratchHeap(cleared);
        destroyed = live;
        for (int i = 0; i < 15; ++i) destroyed.Found[i] = false;
    }
    ObserveScratchHeap(destroyed);
    ExpectLiveScratch(live);
    ASSERT_TRUE(cleared.Complete);
    EXPECT_EQ(2903, valueAfterClear);
    for (int i = 0; i < 14; ++i)
        EXPECT_FALSE(cleared.Found[i]) << "stream " << i << " survived clear";
    EXPECT_TRUE(cleared.Found[14]);
    ASSERT_TRUE(destroyed.Complete);
    EXPECT_FALSE(destroyed.Found[14]);
}

TEST(RenderContextVertexStorageTest, GrowingStorageReleasesSupersededStreamsAndBothIndexAllocations) {
    ScratchHeapObservation initial, grown, afterGrow, initialDestroyed, grownDestroyed;
    {
        UserDrawPrimitiveDataClass storage;
        storage.GetStructure(CKRST_DP_TRANSFORM, 4096);
        storage.GetIndices(16384);
        CollectScratchPointers(storage, initial);
        ObserveScratchHeap(initial);
        storage.GetStructure(CKRST_DP_TRANSFORM, 8192);
        storage.GetIndices(32768)[32767] = 8191;
        CollectScratchPointers(storage, grown);
        ObserveScratchHeap(grown);
        afterGrow = initial;
        for (int i = 0; i < 15; ++i) afterGrow.Found[i] = false;
        ObserveScratchHeap(afterGrow);
        initialDestroyed = initial;
        grownDestroyed = grown;
        for (int i = 0; i < 15; ++i) {
            initialDestroyed.Found[i] = false;
            grownDestroyed.Found[i] = false;
        }
    }
    ObserveScratchHeap(initialDestroyed);
    ObserveScratchHeap(grownDestroyed);
    ExpectLiveScratch(initial);
    ExpectLiveScratch(grown);
    ASSERT_TRUE(afterGrow.Complete);
    for (int i = 0; i < 15; ++i) {
        if (afterGrow.Found[i]) {
            bool reusedByCurrentStorage = false;
            for (int j = 0; j < 15; ++j)
                reusedByCurrentStorage |= afterGrow.Blocks[i] == grown.Blocks[j];
            EXPECT_TRUE(reusedByCurrentStorage) << "superseded allocation " << i << " leaked";
        }
    }
    ASSERT_TRUE(initialDestroyed.Complete);
    ASSERT_TRUE(grownDestroyed.Complete);
    for (int i = 0; i < 15; ++i) {
        EXPECT_FALSE(initialDestroyed.Found[i]) << i;
        EXPECT_FALSE(grownDestroyed.Found[i]) << i;
    }
}
#endif
} // namespace
