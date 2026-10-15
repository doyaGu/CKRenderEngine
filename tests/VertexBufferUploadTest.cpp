#include "TextureTestObserver.h"
#include "MeshHardwareObserver.h"
#include "RCKVertexBuffer.h"

namespace {
// The observer records public upload calls and owns byte storage, without
// executing a rasterizer backend or inferring success from source text.
class UploadObserver : public MeshHardwareObserver {
public:
    CKDWORD Start = 0, Count = 0;
    CKRST_LOCKFLAGS Flags = CKRST_LOCK_DEFAULT;
    void *LockVertexBuffer(CKDWORD handle, CKDWORD start, CKDWORD count,
                           CKRST_LOCKFLAGS flags) override {
        ++VertexLocks; Start = start; Count = count; Flags = flags;
        if (FailVertexLock || handle != VertexHandle) return nullptr;
        EXPECT_LE(start + count, VertexDesc.m_MaxVertexCount);
        const size_t bytes = static_cast<size_t>(start + count) * VertexDesc.m_VertexSize;
        EXPECT_LE(bytes, sizeof(VertexBytes));
        if (bytes > sizeof(VertexBytes)) return nullptr;
        if (flags == CKRST_LOCK_DISCARD) memset(VertexBytes, 0xDD, sizeof(VertexBytes));
        return VertexBytes + start * VertexDesc.m_VertexSize;
    }
    CKBOOL UnlockVertexBuffer(CKDWORD handle) override {
        ++VertexUnlocks; EXPECT_EQ(VertexHandle, handle);
        if (FailVertexUnlock) memset(VertexBytes, 0xDD, sizeof(VertexBytes));
        return !FailVertexUnlock;
    }
    VxVector Position(int vertex) const {
        VxVector result;
        memcpy(&result, VertexBytes + vertex * VertexDesc.m_VertexSize, sizeof(result));
        return result;
    }
};

class UploadBuffer : public RCKVertexBuffer {
public:
    explicit UploadBuffer(CKContext *context) : RCKVertexBuffer(context) {}
    CKBOOL Submit(CKRenderContext *context, CKDWORD start, CKDWORD count,
                  CKRST_LOCKFLAGS flags = CKRST_LOCK_DEFAULT) {
        return Upload(context, start, count, flags);
    }
};

class VertexBufferUploadTest : public TextureTestWorld {
protected:
    UploadObserver First, Second;
    void SetUp() override {
        TextureTestWorld::SetUp();
        Device->m_RasterizerContext = &First;
        OtherDevice->m_RasterizerContext = &Second;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&First, 0));
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&Second, 0));
    }
    void TearDown() override {
        Manager->ForgetRasterizerContext(&First);
        Manager->ForgetRasterizerContext(&Second);
        TextureTestWorld::TearDown();
    }
    void Initialize(UploadBuffer &buffer, CKBOOL dynamic = FALSE) {
        ASSERT_EQ(CK_VB_LOST, buffer.Check(Device, 4, CKRST_DP_TRANSFORM, dynamic));
        VxDrawPrimitiveData *data = buffer.Lock(Device, 0, 4, CK_LOCK_DEFAULT);
        ASSERT_NE(nullptr, data);
        for (int i = 0; i < 4; ++i) {
            const VxVector point(10.0f + i, 20.0f + i, 30.0f + i);
            memcpy(static_cast<CKBYTE *>(data->PositionPtr) + i * data->PositionStride,
                   &point, sizeof(point));
        }
        buffer.Unlock(Device);
    }
    static void Change(UploadBuffer &buffer, CKRenderContext *context, int vertex,
                       float value, CKLOCKFLAGS flags = CK_LOCK_DEFAULT) {
        VxDrawPrimitiveData *data = buffer.Lock(context, vertex, 1, flags);
        ASSERT_NE(nullptr, data);
        const VxVector point(value, value + 1, value + 2);
        memcpy(data->PositionPtr, &point, sizeof(point));
        buffer.Unlock(context);
    }
    static void ExpectPosition(const UploadObserver &observer, int vertex, float x,
                               float y, float z) {
        const VxVector point = observer.Position(vertex);
        EXPECT_FLOAT_EQ(x, point.x); EXPECT_FLOAT_EQ(y, point.y); EXPECT_FLOAT_EQ(z, point.z);
    }
    static void Current(UploadBuffer &buffer, CKRasterizerContext *context, CKBOOL valid) {
        RCKVertexBufferObjectState state = {};
        buffer.GetRasterizerObjectState(state);
        EXPECT_EQ(context, state.Context); EXPECT_EQ(valid, state.Valid);
        if (valid) { EXPECT_NE(0u, state.ObjectIndex); EXPECT_NE(0u, state.ContentVersion); }
    }
};
}

TEST_F(VertexBufferUploadTest, NewObjectPacksCompleteCpuSnapshotAndCurrentVersionAvoidsTransfer) {
    UploadBuffer buffer(Context); Initialize(buffer);
    ASSERT_EQ(1, First.VertexCreates); ASSERT_EQ(1, First.VertexLocks);
    EXPECT_EQ(0u, First.Start); EXPECT_EQ(4u, First.Count);
    EXPECT_EQ(12u, First.VertexDesc.m_VertexSize);
    for (int i = 0; i < 4; ++i) ExpectPosition(First, i, 10.0f+i, 20.0f+i, 30.0f+i);
    Current(buffer, &First, TRUE);
    ASSERT_TRUE(buffer.Submit(Device, 0, 4)); EXPECT_EQ(1, First.VertexLocks);
    EXPECT_EQ(CK_VB_OK, buffer.Check(Device, 4, CKRST_DP_TRANSFORM, FALSE));
    EXPECT_EQ(1, First.VertexLocks);
}

TEST_F(VertexBufferUploadTest, ConsecutiveSameContextUpdateKeepsTheActualPartialRangeAndHint) {
    UploadBuffer buffer(Context); Initialize(buffer, TRUE);
    Change(buffer, Device, 2, 70.0f, CK_LOCK_NOOVERWRITE);
    EXPECT_EQ(2u, First.Start); EXPECT_EQ(1u, First.Count);
    EXPECT_EQ(CKRST_LOCK_NOOVERWRITE, First.Flags);
    ExpectPosition(First, 0, 10, 20, 30); ExpectPosition(First, 2, 70, 71, 72);
    ExpectPosition(First, 3, 13, 23, 33); Current(buffer, &First, TRUE);
}

TEST_F(VertexBufferUploadTest, StaleContextIncludesEditsMadeElsewhereBeforeItsOwnPartialUpdate) {
    UploadBuffer buffer(Context); Initialize(buffer);
    ASSERT_TRUE(buffer.Submit(OtherDevice, 0, 4));
    Change(buffer, Device, 0, 80);
    Change(buffer, OtherDevice, 2, 90);
    ExpectPosition(Second, 0, 80, 81, 82); ExpectPosition(Second, 2, 90, 91, 92);
    ExpectPosition(Second, 1, 11, 21, 31); ExpectPosition(Second, 3, 13, 23, 33);
    EXPECT_EQ(0u, Second.Start); EXPECT_EQ(4u, Second.Count);
    EXPECT_EQ(CKRST_LOCK_DEFAULT, Second.Flags); Current(buffer, &Second, TRUE);
    ASSERT_TRUE(buffer.Submit(Device, 0, 4)); ExpectPosition(First, 2, 90, 91, 92);
}

TEST_F(VertexBufferUploadTest, StaleNoOverwriteHintCannotSkipEarlierEditsFromOtherContext) {
    UploadBuffer buffer(Context); Initialize(buffer, TRUE);
    ASSERT_TRUE(buffer.Submit(OtherDevice, 0, 4));
    Change(buffer, Device, 1, 80);
    Change(buffer, OtherDevice, 3, 90, CK_LOCK_NOOVERWRITE);
    ExpectPosition(Second, 1, 80, 81, 82); ExpectPosition(Second, 3, 90, 91, 92);
    EXPECT_EQ(0u, Second.Start); EXPECT_EQ(4u, Second.Count);
    EXPECT_EQ(CKRST_LOCK_DEFAULT, Second.Flags);
}

TEST_F(VertexBufferUploadTest, FailedLockFollowedByAnotherEditRetriesBothPendingRanges) {
    UploadBuffer buffer(Context); Initialize(buffer);
    First.FailVertexLock = true; Change(buffer, Device, 0, 80);
    Current(buffer, &First, FALSE);
    First.FailVertexLock = false; Change(buffer, Device, 3, 90);
    ExpectPosition(First, 0, 80, 81, 82); ExpectPosition(First, 3, 90, 91, 92);
    EXPECT_EQ(0u, First.Start); EXPECT_EQ(4u, First.Count); Current(buffer, &First, TRUE);
}

TEST_F(VertexBufferUploadTest, FailedUnlockInvalidatesWholeSnapshotBeforeNextPartialEdit) {
    UploadBuffer buffer(Context); Initialize(buffer);
    First.FailVertexUnlock = true; Change(buffer, Device, 1, 80);
    Current(buffer, &First, FALSE);
    First.FailVertexUnlock = false; Change(buffer, Device, 3, 90);
    ExpectPosition(First, 0, 10, 20, 30); ExpectPosition(First, 1, 80, 81, 82);
    ExpectPosition(First, 2, 12, 22, 32); ExpectPosition(First, 3, 90, 91, 92);
    EXPECT_EQ(0u, First.Start); EXPECT_EQ(4u, First.Count); Current(buffer, &First, TRUE);
}

TEST_F(VertexBufferUploadTest, InitialLockFailureRetainsCreatedObjectAndRetriesWholeSnapshot) {
    UploadBuffer buffer(Context); First.FailVertexLock = true; Initialize(buffer);
    Current(buffer, &First, FALSE); EXPECT_EQ(1, First.VertexCreates);
    First.FailVertexLock = false;
    ASSERT_TRUE(buffer.Submit(Device, 2, 1));
    EXPECT_EQ(1, First.VertexCreates); EXPECT_EQ(0u, First.Start); EXPECT_EQ(4u, First.Count);
    for (int i = 0; i < 4; ++i) ExpectPosition(First, i, 10.0f+i, 20.0f+i, 30.0f+i);
}

TEST_F(VertexBufferUploadTest, CreationFailureRetriesWithoutLosingCommittedCpuContents) {
    UploadBuffer buffer(Context); First.FailVertexCreate = true; Initialize(buffer);
    EXPECT_EQ(0u, First.VertexHandle); EXPECT_EQ(0, First.VertexLocks);
    First.FailVertexCreate = false; ASSERT_TRUE(buffer.Submit(Device, 1, 1));
    EXPECT_EQ(2, First.VertexCreates); EXPECT_EQ(0u, First.Start); EXPECT_EQ(4u, First.Count);
    for (int i = 0; i < 4; ++i) ExpectPosition(First, i, 10.0f+i, 20.0f+i, 30.0f+i);
}

TEST_F(VertexBufferUploadTest, DiscardRepacksUnchangedVerticesAlongsideEditedRange) {
    UploadBuffer buffer(Context); Initialize(buffer, TRUE);
    Change(buffer, Device, 1, 80, CK_LOCK_DISCARD);
    EXPECT_EQ(CKRST_LOCK_DISCARD, First.Flags); EXPECT_EQ(0u, First.Start); EXPECT_EQ(4u, First.Count);
    ExpectPosition(First, 0, 10, 20, 30); ExpectPosition(First, 1, 80, 81, 82);
    ExpectPosition(First, 2, 12, 22, 32); ExpectPosition(First, 3, 13, 23, 33);
}

TEST_F(VertexBufferUploadTest, ContextlessCommitRemainsVisibleWhenAnotherRangeIsLaterEdited) {
    UploadBuffer buffer(Context); Initialize(buffer);
    Change(buffer, nullptr, 0, 80);
    Change(buffer, Device, 3, 90);
    ExpectPosition(First, 0, 80, 81, 82); ExpectPosition(First, 3, 90, 91, 92);
    EXPECT_EQ(0u, First.Start); EXPECT_EQ(4u, First.Count);
}

TEST_F(VertexBufferUploadTest, NewContextGetsEveryCommittedEditWithoutChangingOtherObjectIdentity) {
    UploadBuffer buffer(Context); Initialize(buffer);
    RCKVertexBufferObjectState before = {}; buffer.GetRasterizerObjectState(before);
    Change(buffer, Device, 0, 80); Change(buffer, Device, 2, 90);
    ASSERT_TRUE(buffer.Submit(OtherDevice, 3, 1));
    ExpectPosition(Second, 0, 80, 81, 82); ExpectPosition(Second, 2, 90, 91, 92);
    EXPECT_EQ(0u, Second.Start); EXPECT_EQ(4u, Second.Count);
    ASSERT_TRUE(Manager->SelectVertexBufferObject(&buffer, &First));
    RCKVertexBufferObjectState after = {}; buffer.GetRasterizerObjectState(after);
    EXPECT_EQ(before.Context, after.Context); EXPECT_EQ(before.ObjectIndex, after.ObjectIndex);
    EXPECT_EQ(1, First.VertexCreates); EXPECT_EQ(1, Second.VertexCreates);
    ASSERT_TRUE(buffer.Submit(Device, 0, 4)); ExpectPosition(First, 2, 90, 91, 92);
}

TEST_F(VertexBufferUploadTest, InvalidTransferArgumentsNeverLockOrAdvanceHardwareState) {
    UploadBuffer buffer(Context); Initialize(buffer);
    EXPECT_FALSE(buffer.Submit(nullptr, 0, 4));
    EXPECT_FALSE(buffer.Submit(Device, 0, 0));
    EXPECT_FALSE(buffer.Submit(Device, 4, 1));
    EXPECT_FALSE(buffer.Submit(Device, 3, 2));
    EXPECT_EQ(1, First.VertexLocks); Current(buffer, &First, TRUE);
    ExpectPosition(First, 3, 13, 23, 33);
}

TEST_F(VertexBufferUploadTest, UploadInterleavesIndependentPositionNormalColorAndTwoUvStreams) {
    UploadBuffer buffer(Context);
    const CKRST_DPFLAGS flags = static_cast<CKRST_DPFLAGS>(CKRST_DP_TRANSFORM |
        CKRST_DP_LIGHT | CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR | CKRST_DP_STAGES1);
    ASSERT_EQ(CK_VB_LOST, buffer.Check(Device, 2, flags, FALSE));
    VxDrawPrimitiveData *data = buffer.Lock(Device, 0, 2, CK_LOCK_DEFAULT);
    ASSERT_NE(nullptr, data);
    struct Packed {
        float Position[3], Normal[3];
        CKDWORD Diffuse, Specular;
        float Uv0[2], Uv1[2];
    };
    const Packed expected[] = {
        {{1,2,3}, {0,.5f,1}, 0xFF123456u, 0xAB789012u, {.125f,.25f}, {.5f,.75f}},
        {{4,5,6}, {1,.25f,0}, 0xCC543210u, 0xDE135790u, {.375f,.625f}, {.875f,1}}
    };
    static_assert(sizeof(Packed) == 48, "Canonical public interleaved layout");
    for (int i = 0; i < 2; ++i) {
        memcpy(static_cast<CKBYTE *>(data->PositionPtr) + i * data->PositionStride, expected[i].Position, 12);
        memcpy(static_cast<CKBYTE *>(data->NormalPtr) + i * data->NormalStride, expected[i].Normal, 12);
        memcpy(static_cast<CKBYTE *>(data->ColorPtr) + i * data->ColorStride, &expected[i].Diffuse, 4);
        memcpy(static_cast<CKBYTE *>(data->SpecularColorPtr) + i * data->SpecularColorStride, &expected[i].Specular, 4);
        memcpy(static_cast<CKBYTE *>(data->TexCoordPtr) + i * data->TexCoordStride, expected[i].Uv0, 8);
        memcpy(static_cast<CKBYTE *>(data->TexCoordPtrs[0]) + i * data->TexCoordStrides[0], expected[i].Uv1, 8);
    }
    buffer.Unlock(Device);
    ASSERT_EQ(sizeof(Packed), First.VertexDesc.m_VertexSize);
    EXPECT_EQ(0, memcmp(expected, First.VertexBytes, sizeof(expected)));
    ASSERT_TRUE(buffer.Submit(OtherDevice, 1, 1));
    EXPECT_EQ(0, memcmp(expected, Second.VertexBytes, sizeof(expected)));
}
