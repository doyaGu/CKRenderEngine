#include <gtest/gtest.h>

// Exercise the real private helpers and their callers in one production TU.
// No test exports or copies of the submission algorithms are needed.
#include "../src/CKMesh.cpp"
#include "MeshHardwareObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
TEST(MeshSubmissionGuardsTest, GroupEligibilityUsesCachedActivityAndRequiresAPrimitiveContainer) {
    EXPECT_FALSE(IsRenderableMaterialGroup(nullptr));
    CKMaterialGroup group;
    EXPECT_FALSE(IsRenderableMaterialGroup(&group));
    group.m_Primitives[0].m_Indices.Resize(3);
    group.m_Primitives[0].m_Indices[0] = 0;
    group.m_Primitives[0].m_Indices[1] = 1;
    group.m_Primitives[0].m_Indices[2] = 2;
    EXPECT_FALSE(IsRenderableMaterialGroup(&group)); // Data alone does not activate a group.
    group.m_HasValidPrimitives = 7; // Cached flags use nonzero truth, not equality to TRUE.
    EXPECT_TRUE(IsRenderableMaterialGroup(&group));
    group.m_Primitives.Resize(0);
    EXPECT_FALSE(IsRenderableMaterialGroup(&group));
}

TEST(MeshSubmissionGuardsTest, EmptyGroupsFitPositiveVertexRangesWithoutBecomingRenderable) {
    CKMaterialGroup group;
    EXPECT_TRUE(CKMeshGroupIndicesFit(&group, 3));
    EXPECT_FALSE(IsRenderableMaterialGroup(&group));
    group.m_Primitives.Resize(0);
    EXPECT_TRUE(CKMeshGroupIndicesFit(&group, 3));
    EXPECT_FALSE(CKMeshGroupIndicesFit(&group, 0));
    EXPECT_FALSE(CKMeshGroupIndicesFit(nullptr, 3));
}

TEST(MeshSubmissionGuardsTest, FinalIndexOfALaterPrimitiveUsesTheExclusiveVertexBound) {
    CKMaterialGroup group;
    group.m_Primitives.Resize(3);
    group.m_Primitives[0].m_Indices.PushBack(0);
    group.m_Primitives[2].m_Indices.PushBack(1);
    group.m_Primitives[2].m_Indices.PushBack(2);
    EXPECT_TRUE(CKMeshGroupIndicesFit(&group, 3));
    group.m_Primitives[2].m_Indices[1] = 3;
    EXPECT_FALSE(CKMeshGroupIndicesFit(&group, 3));
    EXPECT_EQ(3, group.m_Primitives[2].m_Indices[1]);
    group.m_Primitives[2].m_Indices[1] = 2;
    EXPECT_TRUE(CKMeshGroupIndicesFit(&group, 3));
}

TEST(MeshSubmissionGuardsTest, WordIndicesRemainUnsignedAndCanAddressVertex65535) {
    CKMaterialGroup group;
    group.m_Primitives[0].m_Indices.PushBack(0xffffu);
    EXPECT_TRUE(CKMeshGroupIndicesFit(&group, 0x10000u));
    EXPECT_FALSE(CKMeshGroupIndicesFit(&group, 0xffffu));
    EXPECT_FALSE(CKMeshGroupIndicesFit(&group, 1));
    group.m_Primitives[0].m_Indices[0] = 0;
    EXPECT_TRUE(CKMeshGroupIndicesFit(&group, 1));
}

TEST(MeshSubmissionGuardsTest, RangeCheckingDoesNotImposeTriangleTopologyOrPrimitiveLength) {
    CKMaterialGroup group;
    const VXPRIMITIVETYPE types[] = {VX_POINTLIST, VX_LINELIST, VX_TRIANGLESTRIP};
    for (int i = 0; i < 3; ++i) {
        group.m_Primitives[0].m_Type = types[i];
        group.m_Primitives[0].m_Indices.Resize(i + 1);
        for (int k = 0; k <= i; ++k) group.m_Primitives[0].m_Indices[k] = static_cast<CKWORD>(k);
        EXPECT_TRUE(CKMeshGroupIndicesFit(&group, 3));
    }
}

TEST(MeshSubmissionGuardsTest, CapacityDecisionsMatchIndependentWideArithmeticIncludingDwordMaximum) {
    const CKDWORD limits[] = {0, 1, 0x10000u, 0x3fffffffu, 0xffffffffu};
    for (int i = 0; i < 5; ++i) {
        const CKDWORD totals[] = {0, limits[i] / 2, limits[i]};
        const CKDWORD amounts[] = {0, 1, limits[i], 0xffffffffu};
        for (int t = 0; t < 3; ++t) {
            for (int a = 0; a < 4; ++a) {
                const unsigned long long sum = static_cast<unsigned long long>(totals[t]) + amounts[a];
                const bool fits = sum <= limits[i];
                CKDWORD total = totals[t];
                EXPECT_EQ(fits, CKMeshAccumulateCount(amounts[a], limits[i], total) != FALSE);
                EXPECT_EQ(fits ? static_cast<CKDWORD>(sum) : totals[t], total);
            }
        }
    }
}

TEST(MeshSubmissionGuardsTest, RejectedGroupLeavesTheRemainingBudgetAvailableForASmallerGroup) {
    CKDWORD total = 0;
    ASSERT_TRUE(CKMeshAccumulateCount(32768, 65536, total));
    EXPECT_FALSE(CKMeshAccumulateCount(32769, 65536, total));
    EXPECT_EQ(32768u, total);
    ASSERT_TRUE(CKMeshAccumulateCount(32768, 65536, total));
    EXPECT_EQ(65536u, total);
    EXPECT_FALSE(CKMeshAccumulateCount(1, 65536, total));
    EXPECT_EQ(65536u, total);
    EXPECT_TRUE(CKMeshAccumulateCount(0, 65536, total));
}

TEST(MeshSubmissionGuardsTest, ZeroAndIndexByteBudgetsAcceptTheirExactBoundary) {
    CKDWORD total = 0;
    EXPECT_TRUE(CKMeshAccumulateCount(0, 0, total));
    EXPECT_FALSE(CKMeshAccumulateCount(1, 0, total));
    EXPECT_EQ(0u, total);
    ASSERT_TRUE(CKMeshAccumulateCount(0x3ffffffeu, 0x3fffffffu, total));
    EXPECT_FALSE(CKMeshAccumulateCount(2, 0x3fffffffu, total));
    EXPECT_EQ(0x3ffffffeu, total);
    EXPECT_TRUE(CKMeshAccumulateCount(1, 0x3fffffffu, total));
    EXPECT_EQ(0x3fffffffu, total);
}

class GuardMesh : public RCKMesh {
public:
    explicit GuardMesh(CKContext *context) : RCKMesh(context) {}
    CKMaterialGroup *Group(int index = 0) { return m_MaterialGroups[index]; }
    CKMaterialGroup *ExtraGroup() { return m_MaterialGroups[CreateNewMaterialGroup(nullptr)]; }
    void Remap(CKMaterialGroup *group, int count) {
        CKVBuffer *buffer = new CKVBuffer(count);
        for (int i = 0; i < count; ++i) buffer->m_VertexRemap[i] = count - i - 1;
        group->m_RemapData = reinterpret_cast<CKUINTPTR>(buffer);
        group->m_VertexCount = count;
    }
};
class GuardHardwareObserver : public MeshHardwareObserver {
public:
    CKDWORD RequestedVertexCount = 0;
    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *desc, const void *data, CKDWORD *handle) override {
        RequestedVertexCount = desc->m_MaxVertexCount;
        return MeshHardwareObserver::CreateVertexBuffer(desc, data, handle);
    }
};
class MeshSubmissionGuardWorldTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    GuardHardwareObserver State;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context);
        Device->DetachAll(); Device->m_RasterizerContext = &State;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static void TriangleIndices(CKMaterialGroup *group) {
        group->m_HasValidPrimitives = TRUE;
        group->m_Primitives.Resize(1);
        CKPrimitiveEntry &primitive = group->m_Primitives[0];
        primitive.m_Type = VX_TRIANGLELIST;
        primitive.m_IndexBufferOffset = -1;
        primitive.m_Indices.Resize(3);
        for (int i = 0; i < 3; ++i) primitive.m_Indices[i] = static_cast<CKWORD>(i);
    }
    VxDrawPrimitiveData Geometry(GuardMesh &mesh, int count = 3) {
        mesh.SetVertexCount(count); mesh.SetFaceCount(1); mesh.SetFaceVertexIndex(0, 0, 1, 2);
        mesh.SetFlags(VXMESH_VISIBLE | VXMESH_RENDERCHANNELS);
        for (int i = 0; i < count; ++i) {
            VxVector position(2.0f + i, 3.0f + i, 4.0f + i);
            mesh.SetVertexPosition(i, &position);
        }
        TriangleIndices(mesh.Group());
        mesh.Group()->m_VertexCount = count;
        VxDrawPrimitiveData data = {};
        data.VertexCount = count;
        data.Flags = CKRST_DP_TRANSFORM;
        data.PositionPtr = mesh.GetPositionsPtr(&data.PositionStride);
        return data;
    }
    CKBOOL Upload(GuardMesh &mesh, VxDrawPrimitiveData &data) {
        const CKBOOL result = mesh.CheckHWVertexBuffer(Device, &State, &data);
        Manager->StoreMeshBuffers(&mesh);
        return result;
    }
    static void ExpectNotReady(GuardMesh &mesh) {
        RCKMeshBufferState state = {};
        mesh.GetRasterizerBufferState(state);
        EXPECT_EQ(0u, state.VertexBufferReady);
        EXPECT_FALSE(state.IndexBufferReady);
    }
};

TEST_F(MeshSubmissionGuardWorldTest, InactiveGroupsNeverAllocateOrPolluteAnActiveDirectUpload) {
    GuardMesh mesh(Context);
    VxDrawPrimitiveData data = Geometry(mesh);
    mesh.Group()->m_HasValidPrimitives = FALSE;
    EXPECT_FALSE(Upload(mesh, data));
    EXPECT_EQ(0, State.VertexCreates);
    CKMaterialGroup *inactive = mesh.ExtraGroup();
    TriangleIndices(inactive);
    inactive->m_Primitives[0].m_Indices[2] = 0xffffu;
    inactive->m_HasValidPrimitives = FALSE;
    mesh.Group()->m_HasValidPrimitives = TRUE;
    ASSERT_TRUE(Upload(mesh, data));
    EXPECT_EQ(3u, State.RequestedVertexCount);
    EXPECT_EQ(3u, State.LockedIndices);
    EXPECT_EQ(2, State.IndexWords[2]);
}

TEST_F(MeshSubmissionGuardWorldTest, DirectOutOfRangeIndexStopsBeforeAllocationAndCanBeRepaired) {
    GuardMesh mesh(Context);
    VxDrawPrimitiveData data = Geometry(mesh);
    mesh.Group()->m_Primitives[0].m_Indices[2] = 3;
    EXPECT_FALSE(Upload(mesh, data));
    EXPECT_EQ(0, State.VertexCreates); EXPECT_EQ(0, State.IndexCreates);
    ExpectNotReady(mesh);
    mesh.Group()->m_Primitives[0].m_Indices[2] = 2;
    ASSERT_TRUE(Upload(mesh, data));
    EXPECT_EQ(1, State.VertexCreates); EXPECT_EQ(1, State.VertexUnlocks);
    EXPECT_EQ(2, State.IndexWords[2]);
}

TEST_F(MeshSubmissionGuardWorldTest, RemappedIndicesUseTheLocalCountRatherThanThePhysicalMeshCount) {
    GuardMesh mesh(Context);
    VxDrawPrimitiveData data = Geometry(mesh, 6);
    mesh.Remap(mesh.Group(), 3);
    mesh.Group()->m_Primitives[0].m_Indices[2] = 3; // In the mesh, outside this group's local storage.
    EXPECT_FALSE(Upload(mesh, data));
    EXPECT_EQ(0, State.VertexCreates); ExpectNotReady(mesh);
    mesh.Group()->m_Primitives[0].m_Indices[2] = 2;
    ASSERT_TRUE(Upload(mesh, data));
    EXPECT_EQ(3u, State.RequestedVertexCount);
    float firstX = 0;
    memcpy(&firstX, State.VertexBytes, 4);
    EXPECT_FLOAT_EQ(4.0f, firstX); // Local vertex0 comes from physical vertex2.
    EXPECT_EQ(2, State.IndexWords[2]);
}

TEST_F(MeshSubmissionGuardWorldTest, MixedStorageRejects65537VerticesAndAcceptsTheExactWordRangeBudget) {
    GuardMesh mesh(Context);
    VxDrawPrimitiveData data = Geometry(mesh, 65536); // Real CPU storage, including vertex65535.
    mesh.Group()->m_Primitives[0].m_Indices[2] = 0xffffu;
    CKMaterialGroup *remapped = mesh.ExtraGroup();
    TriangleIndices(remapped);
    for (int i = 0; i < 3; ++i) remapped->m_Primitives[0].m_Indices[i] = 0;
    mesh.Remap(remapped, 1);
    EXPECT_FALSE(Upload(mesh, data));
    EXPECT_EQ(0, State.VertexCreates); EXPECT_EQ(0, State.VertexLocks);
    ExpectNotReady(mesh);
    remapped->m_HasValidPrimitives = FALSE;
    State.FailVertexCreate = true; // Observe acceptance without allocating a large test GPU buffer.
    EXPECT_FALSE(Upload(mesh, data));
    EXPECT_EQ(1, State.VertexCreates);
    EXPECT_EQ(65536u, State.RequestedVertexCount);
    EXPECT_EQ(0, State.VertexLocks);
}

TEST_F(MeshSubmissionGuardWorldTest, SeveralDirectGroupsShareOneSourceVertexBudget) {
    GuardMesh mesh(Context);
    VxDrawPrimitiveData data = Geometry(mesh);
    CKMaterialGroup *second = mesh.ExtraGroup();
    TriangleIndices(second);
    second->m_Primitives[0].m_Indices[0] = 2;
    second->m_Primitives[0].m_Indices[2] = 0;
    ASSERT_TRUE(Upload(mesh, data));
    EXPECT_EQ(3u, State.RequestedVertexCount);
    EXPECT_EQ(3u, State.LockedVertices);
    EXPECT_EQ(6u, State.LockedIndices);
    EXPECT_EQ(0u, mesh.Group()->m_BaseVertex); EXPECT_EQ(0u, second->m_BaseVertex);
    const CKWORD expected[] = {0, 1, 2, 2, 1, 0};
    for (int i = 0; i < 6; ++i) EXPECT_EQ(expected[i], State.IndexWords[i]);
}

TEST_F(MeshSubmissionGuardWorldTest, WrappedTrianglesBudgetTheirExpandedVerticesBeforeAllocation) {
    GuardMesh mesh(Context);
    VxDrawPrimitiveData data = Geometry(mesh);
    mesh.SetFlags(mesh.GetFlags() | VXMESH_WRAPU);
    data.Flags |= CKRST_DP_STAGES0;
    data.TexCoordPtr = mesh.GetTextureCoordinatesPtr(&data.TexCoordStride, -1);
    CKPrimitiveEntry &primitive = mesh.Group()->m_Primitives[0];
    primitive.m_Indices.Resize(65538);
    for (int i = 0; i < 65538; ++i) primitive.m_Indices[i] = static_cast<CKWORD>(i % 3);
    EXPECT_FALSE(Upload(mesh, data));
    EXPECT_EQ(0, State.VertexCreates); EXPECT_EQ(0, State.VertexLocks);
    ExpectNotReady(mesh);
    primitive.m_Indices.Resize(65535);
    State.FailVertexCreate = true;
    EXPECT_FALSE(Upload(mesh, data));
    EXPECT_EQ(1, State.VertexCreates);
    EXPECT_EQ(65535u, State.RequestedVertexCount);
    EXPECT_EQ(0, State.VertexLocks);
}
} // namespace
