#include "TextureTestObserver.h"
#include "RCKVertexBuffer.h"
#include "../src/CKVertexPacking.h"
#include <climits>

namespace {
class StagingProbe : public RCKVertexBuffer {
public:
    explicit StagingProbe(CKContext *context = nullptr) : RCKVertexBuffer(context) {}
    const CKVertexBufferDesc &Description() const { return m_Desc; }
    const VxDrawPrimitiveData &Storage() const { return m_DpData; }
    CKDWORD LockedCount() const { return m_LockedCount; }
    CKDWORD Version() const { return m_ContentVersion; }
    size_t Offset(int index) const {
        const void *fields[] = {&m_ObjectIndex, &m_Desc, &m_Valid, &m_MemoryPool,
            &m_CKContext, &m_DpData, &m_LockedData, &m_FormatFlags, &m_RasterizerContext,
            &m_HardwareValid, &m_ContentVersion, &m_HardwareVersion, &m_LockedStart,
            &m_LockedCount, &m_LockFlags, &m_DirtyStart, &m_DirtyCount};
        return static_cast<const char *>(fields[index]) - reinterpret_cast<const char *>(this);
    }
};
void *Element(void *base, CKDWORD stride, int index) {
    return static_cast<CKBYTE *>(base) + stride * index;
}
float ReadFloat(const CKBYTE *bytes, int offset) {
    float value; memcpy(&value, bytes + offset, sizeof(value)); return value;
}
}

TEST(VertexBufferStagingTest, EmptyObjectAndBorrowedStateHaveDefinedPortableLayout) {
    StagingProbe buffer;
    EXPECT_EQ(nullptr, buffer.Storage().PositionPtr);
    EXPECT_EQ(0u, buffer.Description().m_CurrentVCount);
    EXPECT_EQ(0u, buffer.LockedCount()); EXPECT_EQ(0u, buffer.Version());
    EXPECT_EQ(nullptr, buffer.Lock(nullptr, 0, 1, CK_LOCK_DEFAULT));
    RCKVertexBufferObjectState state = {}; buffer.GetRasterizerObjectState(state);
    EXPECT_EQ(nullptr, state.Context); EXPECT_EQ(0u, state.ObjectIndex);
    EXPECT_EQ(0u, state.FormatFlags); EXPECT_EQ(0u, state.ContentVersion); EXPECT_FALSE(state.Valid);
    RecordProperty("buffer_size", static_cast<int>(sizeof(RCKVertexBuffer)));
    RecordProperty("buffer_alignment", static_cast<int>(alignof(RCKVertexBuffer)));
    RecordProperty("state_size", static_cast<int>(sizeof(RCKVertexBufferObjectState)));
    RecordProperty("state_alignment", static_cast<int>(alignof(RCKVertexBufferObjectState)));
    for (int i = 0; i < 17; ++i) {
        char name[40]; sprintf(name, "buffer_offset_%d", i);
        RecordProperty(name, static_cast<int>(buffer.Offset(i)));
        if (i) EXPECT_GT(buffer.Offset(i), buffer.Offset(i - 1));
    }
    EXPECT_EQ(sizeof(void *), offsetof(RCKVertexBufferObjectState, ObjectIndex));
    EXPECT_EQ(sizeof(void *) + 4, offsetof(RCKVertexBufferObjectState, FormatFlags));
    EXPECT_EQ(sizeof(void *) + 8, offsetof(RCKVertexBufferObjectState, ContentVersion));
    EXPECT_EQ(sizeof(void *) + 12, offsetof(RCKVertexBufferObjectState, Valid));
}

TEST(VertexBufferStagingTest, HighestTextureStageProvidesAllEightWritableIndependentStreams) {
    StagingProbe buffer;
    const CKRST_DPFLAGS flags = static_cast<CKRST_DPFLAGS>(CKRST_DP_TRANSFORM | CKRST_DP_LIGHT |
        CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR | CKRST_DP_TWEEN | CKRST_DP_STAGES7 | CKRST_DP_VBUFFER);
    ASSERT_EQ(CK_VB_LOST, buffer.Check(nullptr, 5, flags, FALSE));
    VxDrawPrimitiveData *data = buffer.Lock(nullptr, 0, 5, CK_LOCK_DEFAULT); ASSERT_NE(nullptr, data);
    EXPECT_EQ(0u, data->Flags & CKRST_DP_VBUFFER); EXPECT_EQ(5, data->VertexCount);
    void *streams[] = {data->PositionPtr, data->NormalPtr, data->TweenPositionPtr, data->TweenNormalPtr,
        data->ColorPtr, data->SpecularColorPtr, data->TexCoordPtr, data->TexCoordPtrs[0],
        data->TexCoordPtrs[1], data->TexCoordPtrs[2], data->TexCoordPtrs[3], data->TexCoordPtrs[4],
        data->TexCoordPtrs[5], data->TexCoordPtrs[6]};
    CKDWORD strides[] = {data->PositionStride, data->NormalStride, data->TweenPositionStride,
        data->TweenNormalStride, data->ColorStride, data->SpecularColorStride, data->TexCoordStride,
        data->TexCoordStrides[0], data->TexCoordStrides[1], data->TexCoordStrides[2],
        data->TexCoordStrides[3], data->TexCoordStrides[4], data->TexCoordStrides[5], data->TexCoordStrides[6]};
    for (int stream = 0; stream < 14; ++stream) {
        ASSERT_NE(nullptr, streams[stream]); EXPECT_EQ(0u, reinterpret_cast<size_t>(streams[stream]) & 15u);
        EXPECT_EQ(stream < 4 ? 12u : stream < 6 ? 4u : 8u, strides[stream]);
        for (int i = 0; i < 5; ++i) memset(Element(streams[stream], strides[stream], i), 16 + stream * 5 + i, strides[stream]);
    }
    buffer.Unlock(nullptr);
    data = buffer.Lock(nullptr, 2, 99, CK_LOCK_NOOVERWRITE); ASSERT_NE(nullptr, data);
    EXPECT_EQ(3, data->VertexCount);
    EXPECT_EQ(Element(streams[0], strides[0], 2), data->PositionPtr);
    EXPECT_EQ(Element(streams[1], strides[1], 2), data->NormalPtr);
    EXPECT_EQ(Element(streams[2], strides[2], 2), data->TweenPositionPtr);
    EXPECT_EQ(Element(streams[3], strides[3], 2), data->TweenNormalPtr);
    EXPECT_EQ(Element(streams[4], strides[4], 2), data->ColorPtr);
    EXPECT_EQ(Element(streams[5], strides[5], 2), data->SpecularColorPtr);
    EXPECT_EQ(Element(streams[6], strides[6], 2), data->TexCoordPtr);
    for (int stage = 1; stage < 8; ++stage)
        EXPECT_EQ(Element(streams[6 + stage], strides[6 + stage], 2), data->TexCoordPtrs[stage - 1]);
    for (int stream = 0; stream < 14; ++stream)
        for (int i = 0; i < 5; ++i)
            for (CKDWORD byte = 0; byte < strides[stream]; ++byte)
                EXPECT_EQ(16 + stream * 5 + i, static_cast<CKBYTE *>(Element(streams[stream], strides[stream], i))[byte]);
    buffer.Unlock(nullptr);
}

TEST(VertexBufferStagingTest, ScreenPositionRetainsRhwAndAbsentChannelsRemainNull) {
    StagingProbe buffer;
    ASSERT_EQ(CK_VB_LOST, buffer.Check(nullptr, 3, CKRST_DP_V, FALSE));
    VxDrawPrimitiveData *data = buffer.Lock(nullptr, 1, 1, CK_LOCK_DEFAULT); ASSERT_NE(nullptr, data);
    EXPECT_EQ(16u, data->PositionStride); EXPECT_EQ(nullptr, data->NormalPtr);
    EXPECT_EQ(nullptr, data->TweenPositionPtr); EXPECT_EQ(nullptr, data->TweenNormalPtr);
    EXPECT_EQ(nullptr, data->ColorPtr); EXPECT_EQ(nullptr, data->SpecularColorPtr);
    EXPECT_EQ(nullptr, data->TexCoordPtr);
    const float position[] = {3, 4, .5f, .25f}; memcpy(data->PositionPtr, position, sizeof(position));
    CKRSTVertexLayout layout; CKRSTGetVertexLayout(data->Flags, nullptr, &layout);
    CKBYTE packed[16] = {}; CKRSTPackVertices(layout, packed, 1, data);
    for (int i = 0; i < 4; ++i) EXPECT_FLOAT_EQ(position[i], ReadFloat(packed, i * 4));
    buffer.Unlock(nullptr);
}

TEST(VertexBufferStagingTest, FourAndFiveWeightsAndPaletteArePackedWithoutClippingOrOverlap) {
    for (int count = 4; count <= 5; ++count) {
        StagingProbe buffer;
        const CKRST_DPFLAGS flags = static_cast<CKRST_DPFLAGS>(CKRST_DP_TRANSFORM |
            CKRST_DP_WEIGHT(count) | CKRST_DP_MATRIXPAL | CKRST_DP_DIFFUSE);
        ASSERT_EQ(CK_VB_LOST, buffer.Check(nullptr, 3, flags, FALSE));
        VxDrawPrimitiveData *data = buffer.Lock(nullptr, 0, 3, CK_LOCK_DEFAULT); ASSERT_NE(nullptr, data);
        const CKDWORD positionSize = 12 + count * 4 + 4;
        ASSERT_EQ(positionSize, data->PositionStride);
        for (int vertex = 0; vertex < 3; ++vertex) {
            float values[8] = {}; values[0] = 1.0f + vertex; values[1] = 2.0f; values[2] = 3.0f;
            for (int weight = 0; weight < count; ++weight) values[3 + weight] = .05f * (weight + 1 + vertex);
            CKBYTE *position = static_cast<CKBYTE *>(Element(data->PositionPtr, data->PositionStride, vertex));
            memcpy(position, values, 12 + count * 4);
            const CKDWORD palette = 0x03020100u + vertex; memcpy(position + 12 + count * 4, &palette, 4);
            const CKDWORD color = 0xff123400u + vertex; memcpy(Element(data->ColorPtr, data->ColorStride, vertex), &color, 4);
        }
        CKRSTVertexLayout layout; CKRSTGetVertexLayout(flags, nullptr, &layout); CKBYTE packed[160] = {};
        CKRSTPackVertices(layout, packed, 3, data);
        for (int vertex = 0; vertex < 3; ++vertex) {
            const CKBYTE *position = packed + layout.Stride * vertex;
            EXPECT_FLOAT_EQ(1.0f + vertex, ReadFloat(position, layout.PositionOffset));
            for (int weight = 0; weight < count; ++weight)
                EXPECT_FLOAT_EQ(.05f * (weight + 1 + vertex), ReadFloat(position, layout.WeightOffset + weight * 4));
            CKDWORD palette; memcpy(&palette, position + layout.BlendIndexOffset, 4); EXPECT_EQ(0x03020100u + vertex, palette);
            CKDWORD color; memcpy(&color, position + layout.DiffuseOffset, 4); EXPECT_EQ(0xff123400u + vertex, color);
        }
        buffer.Unlock(nullptr);
    }
}

TEST(VertexBufferStagingTest, PaletteHintWithoutBlendWeightsDoesNotChangeOrdinaryPosition) {
    StagingProbe buffer;
    const CKRST_DPFLAGS flags = static_cast<CKRST_DPFLAGS>(CKRST_DP_TRANSFORM | CKRST_DP_MATRIXPAL);
    ASSERT_EQ(CK_VB_LOST, buffer.Check(nullptr, 2, flags, FALSE));
    VxDrawPrimitiveData *data = buffer.Lock(nullptr, 0, 2, CK_LOCK_DEFAULT); ASSERT_NE(nullptr, data);
    EXPECT_EQ(12u, data->PositionStride);
    const VxVector point(3, 5, 7); memcpy(data->PositionPtr, &point, sizeof(point));
    CKRSTVertexLayout layout; CKRSTGetVertexLayout(flags, nullptr, &layout);
    EXPECT_EQ(0, layout.WeightCount); EXPECT_EQ(-1, layout.BlendIndexOffset);
    CKBYTE packed[24] = {}; CKRSTPackVertices(layout, packed, 1, data);
    EXPECT_FLOAT_EQ(3, ReadFloat(packed, 0)); EXPECT_FLOAT_EQ(5, ReadFloat(packed, 4));
    EXPECT_FLOAT_EQ(7, ReadFloat(packed, 8)); buffer.Unlock(nullptr);
}

TEST(VertexBufferStagingTest, CheckDuringWriteLockDoesNotReplaceOrPublishTheBorrowedStorage) {
    StagingProbe buffer;
    ASSERT_EQ(CK_VB_LOST, buffer.Check(nullptr, 3, CKRST_DP_TRANSFORM, FALSE));
    VxDrawPrimitiveData *data = buffer.Lock(nullptr, 1, 2, CK_LOCK_DEFAULT); ASSERT_NE(nullptr, data);
    void *position = data->PositionPtr; const VxVector original(4, 5, 6); memcpy(position, &original, sizeof(original));
    EXPECT_EQ(CK_VB_FAILED, buffer.Check(nullptr, 3, CKRST_DP_TRANSFORM, FALSE));
    EXPECT_EQ(CK_VB_FAILED, buffer.Check(nullptr, 5, CKRST_DP_TR_VC, TRUE));
    EXPECT_EQ(3u, buffer.Description().m_MaxVertexCount); EXPECT_EQ(2u, buffer.LockedCount());
    EXPECT_EQ(0u, buffer.Version());
    // Rejected Check must preserve the borrowed memory, so the writer can finish normally.
    if (buffer.Description().m_MaxVertexCount == 3) EXPECT_EQ(0, memcmp(position, &original, sizeof(original)));
    buffer.Unlock(nullptr); EXPECT_EQ(0u, buffer.LockedCount());
    EXPECT_EQ(CK_VB_LOST, buffer.Check(nullptr, 5, CKRST_DP_TR_VC, TRUE));
    data = buffer.Lock(nullptr, 0, 5, CK_LOCK_DEFAULT); ASSERT_NE(nullptr, data);
    EXPECT_NE(nullptr, data->ColorPtr); EXPECT_EQ(5, data->VertexCount); buffer.Unlock(nullptr);
}

TEST(VertexBufferStagingTest, LockClipsOnlyTheEndAndRejectsNestedZeroOrUnsupportedRequests) {
    StagingProbe buffer; ASSERT_EQ(CK_VB_LOST, buffer.Check(nullptr, 4, CKRST_DP_TRANSFORM, FALSE));
    EXPECT_EQ(nullptr, buffer.Lock(nullptr, 4, 1, CK_LOCK_DEFAULT));
    EXPECT_EQ(nullptr, buffer.Lock(nullptr, 0, 0, CK_LOCK_DEFAULT));
    EXPECT_EQ(nullptr, buffer.Lock(nullptr, 0, 1, static_cast<CKLOCKFLAGS>(3)));
    VxDrawPrimitiveData *data = buffer.Lock(nullptr, 3, UINT_MAX, CK_LOCK_DISCARD); ASSERT_NE(nullptr, data);
    EXPECT_EQ(1, data->VertexCount); EXPECT_EQ(4u, buffer.Description().m_CurrentVCount);
    EXPECT_EQ(nullptr, buffer.Lock(nullptr, 0, 1, CK_LOCK_DEFAULT));
    buffer.Unlock(nullptr); buffer.Unlock(nullptr); EXPECT_EQ(1u, buffer.Version());
    data = buffer.Lock(nullptr, 0, 1, CK_LOCK_NOOVERWRITE); ASSERT_NE(nullptr, data); buffer.Unlock(nullptr);
    EXPECT_EQ(2u, buffer.Version());
}

TEST(VertexBufferStagingTest, ImpossibleCountOrByteSizeFailsBeforeMutatingExistingData) {
    StagingProbe buffer; ASSERT_EQ(CK_VB_LOST, buffer.Check(nullptr, 2, CKRST_DP_TRANSFORM, FALSE));
    const void *position = buffer.Storage().PositionPtr;
    EXPECT_EQ(CK_VB_FAILED, buffer.Check(nullptr, 0, CKRST_DP_TRANSFORM, FALSE));
    EXPECT_EQ(CK_VB_FAILED, buffer.Check(nullptr, UINT_MAX / 12u + 1u, CKRST_DP_TRANSFORM, FALSE));
    EXPECT_EQ(CK_VB_FAILED, buffer.Check(nullptr, static_cast<CKDWORD>(INT_MAX) + 1u, CKRST_DP_TRANSFORM, FALSE));
    EXPECT_EQ(2u, buffer.Description().m_MaxVertexCount); EXPECT_EQ(position, buffer.Storage().PositionPtr);
}

TEST(VertexBufferStagingTest, ReconfigurationClearsChannelsAndKeepsUnusedCapacityOnCompatibleCheck) {
    StagingProbe buffer;
    ASSERT_EQ(CK_VB_LOST, buffer.Check(nullptr, 4, CKRST_DP_TR_CL_VCST, FALSE));
    const void *oldPosition = buffer.Storage().PositionPtr;
    EXPECT_EQ(CK_VB_LOST, buffer.Check(nullptr, 2, CKRST_DP_TR_CL_VCST, FALSE));
    EXPECT_EQ(4u, buffer.Description().m_MaxVertexCount); EXPECT_EQ(oldPosition, buffer.Storage().PositionPtr);
    ASSERT_EQ(CK_VB_LOST, buffer.Check(nullptr, 2, CKRST_DP_V, TRUE));
    EXPECT_EQ(2u, buffer.Description().m_MaxVertexCount); EXPECT_NE(0u, buffer.Description().m_Flags & CKRST_VB_DYNAMIC);
    EXPECT_EQ(nullptr, buffer.Storage().ColorPtr); EXPECT_EQ(nullptr, buffer.Storage().SpecularColorPtr);
    EXPECT_EQ(nullptr, buffer.Storage().TexCoordPtr); EXPECT_EQ(0u, buffer.Description().m_CurrentVCount);
    VxDrawPrimitiveData *data = buffer.Lock(nullptr, 0, 2, CK_LOCK_DEFAULT); ASSERT_NE(nullptr, data);
    EXPECT_EQ(16u, data->PositionStride); buffer.Unlock(nullptr);
}

class VertexBufferLifecycleTest : public TextureTestWorld {};
TEST_F(VertexBufferLifecycleTest, ManagerOwnedDestroyUnlinksExactlyTheSelectedBuffer) {
    const int previousCount = Manager->m_VertexBuffers.Size();
    CKVertexBuffer *first = Manager->CreateVertexBuffer(); CKVertexBuffer *second = Manager->CreateVertexBuffer();
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    EXPECT_EQ(previousCount + 2, Manager->m_VertexBuffers.Size());
    ASSERT_EQ(CK_VB_LOST, first->Check(nullptr, 2, CKRST_DP_TRANSFORM, FALSE));
    ASSERT_EQ(CK_VB_LOST, second->Check(nullptr, 2, CKRST_DP_TRANSFORM, FALSE));
    first->Destroy();
    EXPECT_EQ(previousCount + 1, Manager->m_VertexBuffers.Size());
    VxDrawPrimitiveData *data = second->Lock(nullptr, 0, 2, CK_LOCK_DEFAULT); ASSERT_NE(nullptr, data);
    VxVector point(1, 2, 3); memcpy(data->PositionPtr, &point, sizeof(point)); second->Unlock(nullptr);
    Manager->DestroyVertexBuffer(second);
    EXPECT_EQ(previousCount, Manager->m_VertexBuffers.Size());
}
