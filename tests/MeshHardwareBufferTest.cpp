#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MeshHardwareObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();
namespace {
class MeshHardwareProbe : public RCKMesh {
public:
    explicit MeshHardwareProbe(CKContext *context) : RCKMesh(context) {}
    CKMaterialGroup *Group(int index = 0) { return m_MaterialGroups[index]; }
    void ReadyFlags() { m_Flags &= ~(VXMESH_POS_CHANGED | VXMESH_NORMAL_CHANGED | VXMESH_UV_CHANGED | VXMESH_COLOR_CHANGED); }
    void AttachMorph() { m_ProgressiveMesh = new CKProgressiveMesh; m_ProgressiveMesh->m_MorphEnabled = TRUE; }
    void ActiveChannel(int index) { m_ActiveTextureChannels.PushBack(index); }
    void Remap(CKMaterialGroup *group) {
        CKVBuffer *buffer = new CKVBuffer(3);
        buffer->m_VertexRemap[0] = 2; buffer->m_VertexRemap[1] = 0; buffer->m_VertexRemap[2] = 1;
        group->m_RemapData = reinterpret_cast<CKUINTPTR>(buffer); group->m_VertexCount = 3;
    }
};
class MeshHardwareBufferTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    MeshHardwareObserver State;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context); Device = new RCKRenderContext(Context);
        Device->DetachAll(); Device->m_RasterizerContext = &State;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    VxDrawPrimitiveData Triangle(MeshHardwareProbe &mesh, bool wrap = false) {
        mesh.SetVertexCount(3); mesh.SetFaceCount(1); mesh.SetFaceVertexIndex(0, 0, 1, 2);
        mesh.SetFlags(VXMESH_VISIBLE | VXMESH_RENDERCHANNELS | (wrap ? VXMESH_WRAPU : 0));
        const VxVector points[] = {VxVector(2, 3, 4), VxVector(5, 6, 7), VxVector(8, 9, 10)};
        for (int i = 0; i < 3; ++i) {
            mesh.SetVertexPosition(i, const_cast<VxVector *>(&points[i]));
            mesh.SetVertexTextureCoordinates(i, i == 0 ? .9f : .1f, i * .1f, -1);
        }
        CKMaterialGroup *group = mesh.Group(); group->m_HasValidPrimitives = TRUE;
        group->m_Primitives.Resize(1); CKPrimitiveEntry &prim = group->m_Primitives[0];
        prim.m_Type = VX_TRIANGLELIST; prim.m_IndexBufferOffset = -1; prim.m_Indices.Resize(3);
        prim.m_Indices[0] = 0; prim.m_Indices[1] = 1; prim.m_Indices[2] = 2;
        VxDrawPrimitiveData data = {}; data.VertexCount = 3;
        data.PositionPtr = mesh.GetPositionsPtr(&data.PositionStride);
        data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_STAGES0;
        data.TexCoordPtr = mesh.GetTextureCoordinatesPtr(&data.TexCoordStride, -1);
        return data;
    }
    bool Upload(MeshHardwareProbe &mesh, VxDrawPrimitiveData &data) {
        const CKBOOL result = mesh.CheckHWVertexBuffer(Device, &State, &data);
        Manager->StoreMeshBuffers(&mesh); return result != FALSE;
    }
    float VertexFloat(int vertex, int component) {
        float result = 0; std::memcpy(&result, State.VertexBytes + vertex * State.VertexDesc.m_VertexSize + component * sizeof(float), sizeof(float));
        return result;
    }
    RCKMeshBufferState Buffers(MeshHardwareProbe &mesh) {
        RCKMeshBufferState value = {}; mesh.GetRasterizerBufferState(value); return value;
    }
};
}

TEST_F(MeshHardwareBufferTest, WrapFlagsMapOnlyTheTwoUVWrappingBits) {
    EXPECT_EQ(0u, RCKMesh::TextureWrapModeFromMeshFlags(VXMESH_VISIBLE | VXMESH_RENDERCHANNELS));
    EXPECT_EQ(static_cast<CKDWORD>(VXWRAP_U), RCKMesh::TextureWrapModeFromMeshFlags(VXMESH_WRAPU));
    EXPECT_EQ(static_cast<CKDWORD>(VXWRAP_V), RCKMesh::TextureWrapModeFromMeshFlags(VXMESH_WRAPV));
    EXPECT_EQ(static_cast<CKDWORD>(VXWRAP_U | VXWRAP_V), RCKMesh::TextureWrapModeFromMeshFlags(VXMESH_WRAPU | VXMESH_WRAPV));
    EXPECT_FALSE(RCKMesh::RequiresWrapAwareHardwareVertexBuffer(VXMESH_VISIBLE));
    EXPECT_TRUE(RCKMesh::RequiresWrapAwareHardwareVertexBuffer(VXMESH_WRAPU));
    EXPECT_TRUE(RCKMesh::RequiresWrapAwareHardwareVertexBuffer(VXMESH_WRAPV));
}
TEST_F(MeshHardwareBufferTest, ResetInvalidatesVertexMetadataWithoutDroppingOwnedHandles) {
    MeshHardwareProbe mesh(Context); RCKMeshBufferState value = {};
    value.Context = &State; value.VertexBuffer = 71; value.IndexBuffer = 72;
    value.VertexBufferReady = 1; value.IndexBufferReady = TRUE; value.VertexBufferWrapAware = TRUE;
    value.VertexBufferDpFlags = 9; value.VertexBufferVertexFormat = 8; value.VertexBufferStride = 7; value.VertexBufferVertexCount = 6;
    mesh.SetRasterizerBufferState(value); mesh.ResetHardwareVertexBufferState(); const auto result = Buffers(mesh);
    EXPECT_EQ(0u, result.VertexBufferReady); EXPECT_FALSE(result.VertexBufferWrapAware);
    EXPECT_EQ(0u, result.VertexBufferDpFlags); EXPECT_EQ(0u, result.VertexBufferVertexFormat);
    EXPECT_EQ(0u, result.VertexBufferStride); EXPECT_EQ(0u, result.VertexBufferVertexCount);
    EXPECT_EQ(71u, result.VertexBuffer); EXPECT_EQ(72u, result.IndexBuffer); EXPECT_TRUE(result.IndexBufferReady);
    // These are metadata-only fake handles; do not ask destruction to delete them.
    value = {}; mesh.SetRasterizerBufferState(value);
}
TEST_F(MeshHardwareBufferTest, DirectGeometryUploadPacksPositionsUVsAndAbsoluteIndices) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); ASSERT_TRUE(Upload(mesh, data));
    EXPECT_EQ(3u, State.VertexDesc.m_MaxVertexCount); EXPECT_EQ(20u, State.VertexDesc.m_VertexSize);
    EXPECT_FLOAT_EQ(2, VertexFloat(0, 0)); EXPECT_FLOAT_EQ(6, VertexFloat(1, 1)); EXPECT_FLOAT_EQ(10, VertexFloat(2, 2));
    EXPECT_FLOAT_EQ(.9f, VertexFloat(0, 3)); EXPECT_FLOAT_EQ(.1f, VertexFloat(1, 3));
    EXPECT_EQ(0, State.IndexWords[0]); EXPECT_EQ(1, State.IndexWords[1]); EXPECT_EQ(2, State.IndexWords[2]);
    EXPECT_EQ(1, State.VertexUnlocks); EXPECT_EQ(1, State.IndexUnlocks);
}
TEST_F(MeshHardwareBufferTest, UnchangedResidentBuffersReuseUploadedBytes) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); ASSERT_TRUE(Upload(mesh, data)); mesh.ReadyFlags();
    ASSERT_TRUE(Upload(mesh, data)); EXPECT_EQ(1, State.VertexCreates); EXPECT_EQ(1, State.VertexLocks);
    EXPECT_EQ(1, State.IndexCreates); EXPECT_EQ(1, State.IndexLocks);
}
TEST_F(MeshHardwareBufferTest, LostResidentVertexBufferIsRecreatedAndUploaded) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); ASSERT_TRUE(Upload(mesh, data)); mesh.ReadyFlags();
    State.VertexHandle = 0; ASSERT_TRUE(Upload(mesh, data));
    EXPECT_EQ(2, State.VertexCreates); EXPECT_EQ(2, State.VertexLocks); EXPECT_NE(0u, Buffers(mesh).VertexBuffer);
    EXPECT_FLOAT_EQ(2, VertexFloat(0, 0)); EXPECT_GT(State.VertexQueries, 0);
}
TEST_F(MeshHardwareBufferTest, LostResidentIndexBufferIsRecreatedAndUploaded) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); ASSERT_TRUE(Upload(mesh, data)); mesh.ReadyFlags();
    State.IndexHandle = 0; ASSERT_TRUE(Upload(mesh, data));
    EXPECT_EQ(2, State.IndexCreates); EXPECT_EQ(2, State.IndexLocks); EXPECT_EQ(2, State.IndexWords[2]); EXPECT_GT(State.IndexQueries, 0);
}
TEST_F(MeshHardwareBufferTest, ActualVertexFormatChangeReplacesCachedFormat) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); ASSERT_TRUE(Upload(mesh, data)); mesh.ReadyFlags();
    State.VertexDesc.m_VertexFormat = CKRST_DP_TRANSFORM;
    ASSERT_TRUE(Upload(mesh, data)); EXPECT_EQ(2, State.VertexCreates); EXPECT_EQ(1, State.VertexDeletes);
    EXPECT_EQ(20u, State.VertexDesc.m_VertexSize); EXPECT_FLOAT_EQ(.9f, VertexFloat(0, 3));
}
TEST_F(MeshHardwareBufferTest, ActualSmallerIndexCapacityIsReplacedBeforeUpload) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); ASSERT_TRUE(Upload(mesh, data)); mesh.ReadyFlags();
    State.IndexDesc.m_MaxIndexCount = 1;
    ASSERT_TRUE(Upload(mesh, data)); EXPECT_EQ(2, State.IndexCreates); EXPECT_EQ(1, State.IndexDeletes);
    EXPECT_EQ(3u, State.IndexDesc.m_MaxIndexCount);
}
TEST_F(MeshHardwareBufferTest, VertexCreateFailureReturnsSoftwareFallbackWithoutReadyState) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); State.FailVertexCreate = true;
    EXPECT_FALSE(Upload(mesh, data)); EXPECT_EQ(0u, Buffers(mesh).VertexBufferReady); EXPECT_EQ(0, State.VertexLocks);
    State.FailVertexCreate = false; EXPECT_TRUE(Upload(mesh, data));
}
TEST_F(MeshHardwareBufferTest, VertexLockFailureDeletesCreatedBufferAndRetryUploads) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); State.FailVertexLock = true;
    EXPECT_FALSE(Upload(mesh, data)); EXPECT_EQ(1, State.VertexDeletes); EXPECT_EQ(0u, Buffers(mesh).VertexBufferReady);
    State.FailVertexLock = false; EXPECT_TRUE(Upload(mesh, data)); EXPECT_FLOAT_EQ(2, VertexFloat(0, 0));
}
TEST_F(MeshHardwareBufferTest, VertexUnlockFailureCannotAdvertiseUploadedBufferReady) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); State.FailVertexUnlock = true;
    EXPECT_FALSE(Upload(mesh, data)); EXPECT_EQ(0u, Buffers(mesh).VertexBufferReady); EXPECT_EQ(1, State.VertexDeletes);
    State.FailVertexUnlock = false; EXPECT_TRUE(Upload(mesh, data));
}
TEST_F(MeshHardwareBufferTest, IndexCreateFailureReturnsFallbackAndRetainsRetryableVertexBuffer) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); State.FailIndexCreate = true;
    EXPECT_FALSE(Upload(mesh, data)); EXPECT_FALSE(Buffers(mesh).IndexBufferReady); EXPECT_EQ(-1, mesh.Group()->m_Primitives[0].m_IndexBufferOffset);
    State.FailIndexCreate = false; EXPECT_TRUE(Upload(mesh, data)); EXPECT_EQ(2, State.IndexWords[2]);
}
TEST_F(MeshHardwareBufferTest, IndexLockFailureMarksOffsetsUnusableUntilSuccessfulRetry) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); State.FailIndexLock = true;
    EXPECT_FALSE(Upload(mesh, data)); EXPECT_FALSE(Buffers(mesh).IndexBufferReady); EXPECT_EQ(-1, mesh.Group()->m_Primitives[0].m_IndexBufferOffset);
    State.FailIndexLock = false; EXPECT_TRUE(Upload(mesh, data)); EXPECT_EQ(0, mesh.Group()->m_Primitives[0].m_IndexBufferOffset);
}
TEST_F(MeshHardwareBufferTest, IndexUnlockFailureDoesNotPublishPrimitiveOffsets) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); State.FailIndexUnlock = true;
    EXPECT_FALSE(Upload(mesh, data)); EXPECT_FALSE(Buffers(mesh).IndexBufferReady); EXPECT_EQ(-1, mesh.Group()->m_Primitives[0].m_IndexBufferOffset);
    State.FailIndexUnlock = false; EXPECT_TRUE(Upload(mesh, data));
}
TEST_F(MeshHardwareBufferTest, WrapAwareUploadDuplicatesTriangleCornersAndAdjustsSeam) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh, true); ASSERT_TRUE(Upload(mesh, data));
    EXPECT_TRUE(Buffers(mesh).VertexBufferWrapAware); EXPECT_EQ(3u, State.VertexDesc.m_MaxVertexCount);
    EXPECT_FLOAT_EQ(.9f, VertexFloat(0, 3)); EXPECT_FLOAT_EQ(1.1f, VertexFloat(1, 3)); EXPECT_FLOAT_EQ(1.1f, VertexFloat(2, 3));
    EXPECT_EQ(0, State.IndexWords[0]); EXPECT_EQ(1, State.IndexWords[1]); EXPECT_EQ(2, State.IndexWords[2]);
}
TEST_F(MeshHardwareBufferTest, WrapAwareLostIndexBufferCannotUseCachedReadyVertexBuffer) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh, true); ASSERT_TRUE(Upload(mesh, data)); mesh.ReadyFlags();
    State.IndexHandle = 0; ASSERT_TRUE(Upload(mesh, data));
    EXPECT_EQ(2, State.IndexCreates); EXPECT_NE(0u, State.IndexHandle); EXPECT_EQ(2, State.IndexWords[2]);
}
TEST_F(MeshHardwareBufferTest, WrapAwareIndexChangeRebuildsExpandedCornerOrder) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh, true); ASSERT_TRUE(Upload(mesh, data)); mesh.ReadyFlags();
    auto &prim = mesh.Group()->m_Primitives[0]; prim.m_Indices[0] = 2; prim.m_Indices[2] = 0; prim.m_IndexBufferOffset = -1;
    ASSERT_TRUE(Upload(mesh, data)); EXPECT_FLOAT_EQ(8, VertexFloat(0, 0)); EXPECT_FLOAT_EQ(2, VertexFloat(2, 0));
}
TEST_F(MeshHardwareBufferTest, RemappedGroupsPackLocalGeometryAndRemappedExtraTextureCoordinates) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); RCKMaterial material(Context);
    ASSERT_EQ(0, mesh.AddChannel(&material, FALSE)); mesh.ActiveChannel(0);
    for (int i = 0; i < 3; ++i) mesh.SetVertexTextureCoordinates(i, .2f * (i + 1), .3f * (i + 1), 0);
    data.Flags |= CKRST_DP_STAGES1;
    data.TexCoordPtrs[0] = mesh.GetTextureCoordinatesPtr(&data.TexCoordStrides[0], 0);
    mesh.Remap(mesh.Group()); ASSERT_TRUE(Upload(mesh, data));
    EXPECT_EQ(28u, State.VertexDesc.m_VertexSize);
    EXPECT_FLOAT_EQ(8, VertexFloat(0, 0)); EXPECT_FLOAT_EQ(2, VertexFloat(1, 0));
    EXPECT_FLOAT_EQ(.6f, VertexFloat(0, 5)); EXPECT_FLOAT_EQ(.2f, VertexFloat(1, 5)); EXPECT_FLOAT_EQ(.4f, VertexFloat(2, 5));
}
TEST_F(MeshHardwareBufferTest, WrapAwareRemappedGroupsUseLocalExtraTextureCoordinates) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh, true); RCKMaterial material(Context);
    ASSERT_EQ(0, mesh.AddChannel(&material, FALSE)); mesh.ActiveChannel(0);
    for (int i = 0; i < 3; ++i) mesh.SetVertexTextureCoordinates(i, .2f * (i + 1), .3f * (i + 1), 0);
    data.Flags |= CKRST_DP_STAGES1;
    data.TexCoordPtrs[0] = mesh.GetTextureCoordinatesPtr(&data.TexCoordStrides[0], 0);
    mesh.Remap(mesh.Group()); ASSERT_TRUE(Upload(mesh, data));
    EXPECT_FLOAT_EQ(8, VertexFloat(0, 0)); EXPECT_FLOAT_EQ(2, VertexFloat(1, 0));
    EXPECT_FLOAT_EQ(.6f, VertexFloat(0, 5)); EXPECT_FLOAT_EQ(.2f, VertexFloat(1, 5)); EXPECT_FLOAT_EQ(.4f, VertexFloat(2, 5));
}
TEST_F(MeshHardwareBufferTest, MixedDirectAndRemappedGroupsUploadDistinctRangesAndBaseIndices) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); RCKMaterial material(Context);
    const int groupIndex = mesh.CreateNewMaterialGroup(&material); CKMaterialGroup *group = mesh.Group(groupIndex);
    group->m_HasValidPrimitives = TRUE; group->m_Primitives.Resize(1);
    auto &prim = group->m_Primitives[0]; prim.m_Type = VX_TRIANGLELIST; prim.m_IndexBufferOffset = -1;
    prim.m_Indices.Resize(3); prim.m_Indices[0] = 0; prim.m_Indices[1] = 1; prim.m_Indices[2] = 2;
    mesh.Remap(group); ASSERT_TRUE(Upload(mesh, data));
    EXPECT_EQ(6u, State.VertexDesc.m_MaxVertexCount); EXPECT_EQ(3u, group->m_BaseVertex);
    EXPECT_FLOAT_EQ(2, VertexFloat(0, 0)); EXPECT_FLOAT_EQ(8, VertexFloat(3, 0));
    for (int i = 0; i < 6; ++i) EXPECT_EQ(i, State.IndexWords[i]);
    EXPECT_EQ(3, group->m_Primitives[0].m_IndexBufferOffset);
    // Growing the direct range moves the second group's base even though its
    // local indices and previously published primitive offset are unchanged.
    mesh.SetVertexCount(4); data.VertexCount = 4;
    data.PositionPtr = mesh.GetPositionsPtr(&data.PositionStride);
    data.TexCoordPtr = mesh.GetTextureCoordinatesPtr(&data.TexCoordStride, -1);
    ASSERT_TRUE(Upload(mesh, data)); EXPECT_EQ(4u, group->m_BaseVertex);
    EXPECT_EQ(4, State.IndexWords[3]); EXPECT_EQ(5, State.IndexWords[4]); EXPECT_EQ(6, State.IndexWords[5]);
}
TEST_F(MeshHardwareBufferTest, MorphScratchStaysOnSoftwarePathWithoutUploadingOriginalGeometry) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); mesh.AttachMorph();
    VxVector scratch[] = {VxVector(20, 30, 40), VxVector(50, 60, 70), VxVector(80, 90, 100)};
    data.PositionPtr = scratch; data.PositionStride = sizeof(VxVector);
    EXPECT_FALSE(Upload(mesh, data)); EXPECT_EQ(0, State.VertexCreates); EXPECT_EQ(0, State.VertexLocks);
}
TEST_F(MeshHardwareBufferTest, PrimitiveBaseVertexIsAddedToEveryUploadedIndex) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); RCKMeshBufferState value = {};
    value.Context = &State; value.VertexBufferVertexCount = 9; mesh.SetRasterizerBufferState(value);
    mesh.Group()->m_BaseVertex = 4; ASSERT_TRUE(mesh.CheckHWIndexBuffer(&State));
    EXPECT_EQ(4, State.IndexWords[0]); EXPECT_EQ(5, State.IndexWords[1]); EXPECT_EQ(6, State.IndexWords[2]);
}
TEST_F(MeshHardwareBufferTest, AbsoluteIndexBeyondSixteenBitRangeUsesSoftwareFallback) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); RCKMeshBufferState value = {};
    value.Context = &State; value.VertexBufferVertexCount = 0x10000; mesh.SetRasterizerBufferState(value);
    mesh.Group()->m_BaseVertex = 0xffff; EXPECT_FALSE(mesh.CheckHWIndexBuffer(&State)); EXPECT_EQ(0, State.IndexCreates);
}
TEST_F(MeshHardwareBufferTest, VertexFactoryReusesActualCompatibleCapacity) {
    MeshHardwareProbe mesh(Context); CKVertexBufferDesc desc; desc.m_VertexFormat = CKRST_DP_TRANSFORM;
    desc.m_VertexSize = 12; desc.m_MaxVertexCount = 9; CKDWORD handle = 0;
    ASSERT_TRUE(State.CreateVertexBuffer(&desc, nullptr, &handle));
    RCKMeshBufferState value = {}; value.Context = &State; value.VertexBuffer = handle;
    mesh.SetRasterizerBufferState(value);
    EXPECT_TRUE(mesh.CreateHardwareVertexBuffer(&State, CKRST_DP_TRANSFORM, 12, 3));
    EXPECT_EQ(1, State.VertexCreates); EXPECT_EQ(0, State.VertexDeletes);
}
TEST_F(MeshHardwareBufferTest, FailedVertexReplacementPreservesOwnedHandleForRetry) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); ASSERT_TRUE(Upload(mesh, data));
    State.FailVertexDelete = true; EXPECT_FALSE(mesh.CreateHardwareVertexBuffer(&State, CKRST_DP_TRANSFORM, 12, 4));
    EXPECT_NE(0u, Buffers(mesh).VertexBuffer); EXPECT_EQ(0u, Buffers(mesh).VertexBufferReady);
    State.FailVertexDelete = false; EXPECT_TRUE(mesh.CreateHardwareVertexBuffer(&State, CKRST_DP_TRANSFORM, 12, 4));
}
TEST_F(MeshHardwareBufferTest, VertexCountAboveWordIndexRangeDoesNotAllocate) {
    MeshHardwareProbe mesh(Context); EXPECT_FALSE(mesh.CreateHardwareVertexBuffer(&State, CKRST_DP_TRANSFORM, 12, 0x10001));
    EXPECT_EQ(0, State.VertexCreates); EXPECT_EQ(0u, Buffers(mesh).VertexBufferReady);
}
TEST_F(MeshHardwareBufferTest, InvalidationDeletesBothResourceKindsAndClearsSelectedState) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); ASSERT_TRUE(Upload(mesh, data)); mesh.InvalidateHardwareBuffers();
    EXPECT_EQ(1, State.VertexDeletes); EXPECT_EQ(1, State.IndexDeletes);
    const auto result = Buffers(mesh); EXPECT_EQ(0u, result.VertexBuffer); EXPECT_EQ(0u, result.IndexBuffer); EXPECT_EQ(nullptr, result.Context);
}
TEST_F(MeshHardwareBufferTest, InvalidationPreservesFailedDeletionAndCanRetryIt) {
    MeshHardwareProbe mesh(Context); auto data = Triangle(mesh); ASSERT_TRUE(Upload(mesh, data)); State.FailVertexDelete = true;
    mesh.InvalidateHardwareBuffers(); const auto partial = Buffers(mesh);
    EXPECT_NE(0u, partial.VertexBuffer); EXPECT_EQ(0u, partial.VertexBufferReady); EXPECT_EQ(0u, partial.IndexBuffer);
    State.FailVertexDelete = false; mesh.InvalidateHardwareBuffers(); EXPECT_EQ(0u, Buffers(mesh).VertexBuffer);
    EXPECT_EQ(2, State.VertexDeletes); EXPECT_EQ(1, State.IndexDeletes);
}
