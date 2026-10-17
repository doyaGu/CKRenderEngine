#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKMaterial.h"
#include "RCKMesh.h"
#include "RCKRenderManager.h"

#include <initializer_list>
#include <memory>

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class StorageMesh : public RCKMesh {
public:
    explicit StorageMesh(CKContext *context) : RCKMesh(context) {}
    int GroupCount() const { return m_MaterialGroups.Size(); }
    CKMaterialGroup *Group(int index) { return m_MaterialGroups[index]; }
};

void ExpectWords(const XArray<CKWORD> &words, std::initializer_list<CKWORD> expected) {
    ASSERT_EQ(static_cast<int>(expected.size()), words.Size());
    int i = 0;
    for (CKWORD value : expected) EXPECT_EQ(value, words[i++]);
}

void ExpectReadyPrimitive(CKMaterialGroup &group) {
    ASSERT_EQ(1, group.m_Primitives.Size());
    EXPECT_EQ(VX_TRIANGLELIST, group.m_Primitives[0].m_Type);
    EXPECT_EQ(-1, group.m_Primitives[0].m_IndexBufferOffset);
    EXPECT_EQ(0, group.m_Primitives[0].m_Indices.Size());
}

class MaterialGroupStorageTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        Manager->m_VertexCache.Value = 0;
    }
    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    RCKMaterial *Material(const char *name) {
        return static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL, const_cast<char *>(name)));
    }
    void Geometry(StorageMesh &mesh, CKMaterial *a, CKMaterial *b) {
        mesh.SetVertexCount(6);
        mesh.SetFaceCount(2);
        for (int i = 0; i < 6; ++i) {
            VxVector position(static_cast<float>(i), static_cast<float>(i * i), 0);
            mesh.SetVertexPosition(i, &position);
            mesh.SetVertexTextureCoordinates(i, static_cast<float>(i) / 8, static_cast<float>(i) / 4, -1);
        }
        mesh.SetFaceVertexIndex(0, 5, 1, 4);
        mesh.SetFaceVertexIndex(1, 4, 2, 5);
        mesh.SetFaceMaterial(0, a);
        mesh.SetFaceMaterial(1, b);
        mesh.SetFlags(mesh.GetFlags() & ~VXMESH_STRIPIFY);
    }
};

TEST_F(MaterialGroupStorageTest, ValueInitializedPrimitiveHasDefinedTriangleAndUnboundBufferDefaults) {
    CKPrimitiveEntry entry = CKPrimitiveEntry();
    EXPECT_EQ(VX_TRIANGLELIST, entry.m_Type);
    EXPECT_EQ(-1, entry.m_IndexBufferOffset);
    EXPECT_EQ(0, entry.m_Indices.Size());
    entry.m_Indices.PushBack(40000);
    entry.m_Indices.PushBack(2);
    entry.m_Indices.PushBack(7);
    ExpectWords(entry.m_Indices, {40000, 2, 7});
}

TEST_F(MaterialGroupStorageTest, PrimitiveCopiesAndClassArrayGrowthOwnIndependentWordStreams) {
    CKPrimitiveEntry source = CKPrimitiveEntry();
    source.m_Type = VX_TRIANGLELIST;
    source.m_IndexBufferOffset = 13;
    for (CKWORD index : {5, 1, 4, 4, 2, 5}) source.m_Indices.PushBack(index);
    CKPrimitiveEntry copy = source;
    ASSERT_NE(source.m_Indices.Begin(), copy.m_Indices.Begin());
    XClassArray<CKPrimitiveEntry> entries;
    entries.PushBack(source);
    entries.Reserve(12); // Exercises the real element copy/destruction path.
    entries.Resize(2);
    entries[1] = copy;
    source.m_Indices[0] = 42;
    copy.m_Indices[1] = 43;
    for (int i = 0; i < entries.Size(); ++i) {
        EXPECT_EQ(VX_TRIANGLELIST, entries[i].m_Type);
        EXPECT_EQ(13, entries[i].m_IndexBufferOffset);
        ExpectWords(entries[i].m_Indices, {5, 1, 4, 4, 2, 5});
    }
    entries[0].m_Indices[0] = 11;
    EXPECT_EQ(5, entries[1].m_Indices[0]);
    EXPECT_EQ(42, source.m_Indices[0]);
    EXPECT_EQ(5, copy.m_Indices[0]);
}

TEST_F(MaterialGroupStorageTest, ConstructedGroupsProvideTheirFirstDrawableSlotAndOnlyBorrowMaterial) {
    RCKMaterial *material = Material("borrowed");
    ASSERT_NE(nullptr, material);
    const CK_ID id = material->GetID();
    StorageMesh mesh(Context);
    {
        CKMaterialGroup empty;
        CKMaterialGroup bound(material);
        ExpectReadyPrimitive(empty);
        ExpectReadyPrimitive(bound);
        ASSERT_EQ(1, empty.m_Primitives.Size());
        ASSERT_EQ(1, bound.m_Primitives.Size());
        EXPECT_EQ(nullptr, empty.m_Material);
        EXPECT_EQ(material, bound.m_Material);
        EXPECT_EQ(0, bound.m_HasValidPrimitives);
        EXPECT_EQ(0x10000u, bound.m_MinVertexIndex);
        EXPECT_EQ(0u, bound.m_MaxVertexIndex);
        EXPECT_EQ(0u, bound.m_BaseVertex);
        EXPECT_EQ(0u, bound.m_VertexCount);
        EXPECT_EQ(0u, bound.m_RemapData);
        EXPECT_EQ(0, bound.m_FaceIndices.Size());
        bound.m_Primitives[0].m_Indices.PushBack(2);
        bound.m_Primitives[0].m_Indices.PushBack(5);
        bound.m_Primitives[0].m_Indices.PushBack(7);
        mesh.UpdateHasValidPrimitives(&bound);
        EXPECT_EQ(1u, bound.m_HasValidPrimitives);
        bound.m_Primitives[0].m_Indices.Resize(0);
        mesh.UpdateHasValidPrimitives(&bound);
        EXPECT_EQ(0u, bound.m_HasValidPrimitives);
    }
    EXPECT_EQ(material, Context->GetObject(id));
    EXPECT_STREQ("borrowed", material->GetName());
}

TEST_F(MaterialGroupStorageTest, MeshLookupUsesMaterialIdentityAndCreatesOnlyWhenRequested) {
    StorageMesh mesh(Context);
    RCKMaterial *a = Material("same-name");
    RCKMaterial *b = Material("same-name");
    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, b);
    ASSERT_NE(a, b);
    ASSERT_EQ(1, mesh.GroupCount());
    EXPECT_EQ(0, mesh.GetMaterialGroupIndex(nullptr, FALSE));
    EXPECT_EQ(-1, mesh.GetMaterialGroupIndex(a, FALSE));
    EXPECT_EQ(1, mesh.GroupCount());
    EXPECT_EQ(1, mesh.GetMaterialGroupIndex(a, TRUE));
    EXPECT_EQ(1, mesh.GetMaterialGroupIndex(a, TRUE));
    EXPECT_EQ(2, mesh.GroupCount());
    EXPECT_EQ(2, mesh.CreateNewMaterialGroup(a)); // Explicit creation permits another slot.
    EXPECT_EQ(1, mesh.GetMaterialGroupIndex(a, FALSE)); // Lookup returns the first identity.
    EXPECT_EQ(-1, mesh.GetMaterialGroupIndex(b, FALSE));
    EXPECT_EQ(3, mesh.GetMaterialGroupIndex(b, TRUE));
    EXPECT_EQ(4, mesh.GroupCount());
    for (int i = 0; i < mesh.GroupCount(); ++i) ExpectReadyPrimitive(*mesh.Group(i));
}

TEST_F(MaterialGroupStorageTest, ResetConsumesRealBuiltGroupsAndInvalidatesRangesRemapAndHardwareOffsets) {
    RCKMaterial *a = Material("a");
    RCKMaterial *b = Material("b");
    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, b);
    StorageMesh mesh(Context);
    Geometry(mesh, a, b);
    mesh.CreateRenderGroups();
    const int index = mesh.GetMaterialGroupIndex(a, FALSE);
    ASSERT_GE(index, 0);
    CKMaterialGroup *group = mesh.Group(index);
    ASSERT_NE(nullptr, mesh.GetVBuffer(group));
    ASSERT_EQ(1u, group->m_HasValidPrimitives);
    ASSERT_GT(group->m_Primitives.Size(), 0);
    group->m_Primitives[0].m_IndexBufferOffset = 17; // A previously bound hardware slice.
    mesh.ResetMaterialGroup(group, 2);
    ExpectReadyPrimitive(*group);
    EXPECT_EQ(a, group->m_Material);
    EXPECT_EQ(0u, group->m_HasValidPrimitives);
    EXPECT_EQ(0x10000u, group->m_MinVertexIndex);
    EXPECT_EQ(0u, group->m_MaxVertexIndex);
    EXPECT_EQ(0u, group->m_BaseVertex);
    EXPECT_EQ(0u, group->m_VertexCount);
    EXPECT_EQ(nullptr, mesh.GetVBuffer(group));
    EXPECT_EQ(0u, group->m_RemapData);
    EXPECT_EQ(0, group->m_FaceIndices.Size());
    group->m_Primitives[0].m_Indices.PushBack(0);
    group->m_Primitives[0].m_Indices.PushBack(1);
    group->m_Primitives[0].m_Indices.PushBack(2);
    mesh.UpdateHasValidPrimitives(group);
    EXPECT_EQ(1u, group->m_HasValidPrimitives);
    mesh.ResetMaterialGroup(group, 0);
    ExpectReadyPrimitive(*group);
}

TEST_F(MaterialGroupStorageTest, MultipleMaterialOutputsUseDenseLocalIndicesAndDisjointBaseRanges) {
    RCKMaterial *a = Material("a");
    RCKMaterial *b = Material("b");
    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, b);
    StorageMesh mesh(Context);
    Geometry(mesh, a, b);
    mesh.CreateRenderGroups();
    const int ai = mesh.GetMaterialGroupIndex(a, FALSE);
    const int bi = mesh.GetMaterialGroupIndex(b, FALSE);
    ASSERT_GE(ai, 0);
    ASSERT_GE(bi, 0);
    CKMaterialGroup *ga = mesh.Group(ai);
    CKMaterialGroup *gb = mesh.Group(bi);
    ASSERT_NE(nullptr, mesh.GetVBuffer(ga));
    ASSERT_NE(nullptr, mesh.GetVBuffer(gb));
    ASSERT_EQ(1, ga->m_Primitives.Size());
    ASSERT_EQ(1, gb->m_Primitives.Size());
    ExpectWords(ga->m_Primitives[0].m_Indices, {0, 1, 2});
    ExpectWords(gb->m_Primitives[0].m_Indices, {0, 1, 2});
    ExpectWords(ga->m_FaceIndices, {0});
    ExpectWords(gb->m_FaceIndices, {1});
    for (CKMaterialGroup *group : {ga, gb}) {
        EXPECT_EQ(VX_TRIANGLELIST, group->m_Primitives[0].m_Type);
        EXPECT_EQ(-1, group->m_Primitives[0].m_IndexBufferOffset);
        EXPECT_EQ(1u, group->m_HasValidPrimitives);
        EXPECT_EQ(0u, group->m_MinVertexIndex);
        EXPECT_EQ(3u, group->m_MaxVertexIndex); // Exclusive local upper bound.
        EXPECT_EQ(3u, group->m_VertexCount);
    }
    EXPECT_EQ(0u, ga->m_BaseVertex);
    EXPECT_EQ(3u, gb->m_BaseVertex);
    CKVBuffer *va = mesh.GetVBuffer(ga);
    CKVBuffer *vb = mesh.GetVBuffer(gb);
    ASSERT_EQ(3, va->m_VertexRemap.Size());
    ASSERT_EQ(3, vb->m_VertexRemap.Size());
    EXPECT_EQ(5, va->m_VertexRemap[0]);
    EXPECT_EQ(1, va->m_VertexRemap[1]);
    EXPECT_EQ(4, va->m_VertexRemap[2]);
    EXPECT_EQ(4, vb->m_VertexRemap[0]);
    EXPECT_EQ(2, vb->m_VertexRemap[1]);
    EXPECT_EQ(5, vb->m_VertexRemap[2]);
}

TEST_F(MaterialGroupStorageTest, MeshBufferAndGroupDeletionKeepBorrowedMaterialsAlive) {
    RCKMaterial *a = Material("a");
    RCKMaterial *b = Material("b");
    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, b);
    const CK_ID aid = a->GetID();
    const CK_ID bid = b->GetID();
    StorageMesh mesh(Context);
    Geometry(mesh, a, b);
    mesh.CreateRenderGroups();
    CKMaterialGroup *group = mesh.Group(mesh.GetMaterialGroupIndex(a, FALSE));
    ASSERT_NE(nullptr, mesh.GetVBuffer(group));
    mesh.DeleteVBuffer(group);
    EXPECT_EQ(nullptr, mesh.GetVBuffer(group));
    EXPECT_EQ(0u, group->m_RemapData);
    EXPECT_EQ(a, group->m_Material);
    ASSERT_EQ(1, group->m_Primitives.Size());
    ExpectWords(group->m_Primitives[0].m_Indices, {0, 1, 2});
    mesh.DeleteVBuffer(group); // The real empty ownership slot is repeatable.
    mesh.DeleteRenderGroup();
    EXPECT_EQ(0, mesh.GroupCount());
    EXPECT_EQ(a, Context->GetObject(aid));
    EXPECT_EQ(b, Context->GetObject(bid));
    EXPECT_STREQ("a", a->GetName());
    EXPECT_STREQ("b", b->GetName());
}

TEST_F(MaterialGroupStorageTest, PublicMeshCopyOwnsItsRemapAndRebuildsAfterTheSourceIsDestroyed) {
    RCKMaterial *a = Material("a");
    RCKMaterial *b = Material("b");
    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, b);
    std::unique_ptr<StorageMesh> source(new StorageMesh(Context));
    Geometry(*source, a, b);
    source->CreateRenderGroups();
    StorageMesh copy(Context);
    CKDependenciesContext dependencies(Context);
    ASSERT_EQ(CK_OK, copy.Copy(*source, dependencies));
    int srcIndex = source->GetMaterialGroupIndex(a, FALSE);
    int dstIndex = copy.GetMaterialGroupIndex(a, FALSE);
    ASSERT_GE(srcIndex, 0);
    ASSERT_GE(dstIndex, 0);
    CKVBuffer *original = source->GetVBuffer(source->Group(srcIndex));
    CKVBuffer *cloned = copy.GetVBuffer(copy.Group(dstIndex));
    ASSERT_NE(nullptr, original);
    ASSERT_NE(nullptr, cloned);
    ASSERT_NE(original, cloned);
    ASSERT_EQ(3, original->m_Vertices.Size());
    ASSERT_EQ(3, cloned->m_Vertices.Size());
    EXPECT_NE(original->m_Vertices.Begin(), cloned->m_Vertices.Begin());
    EXPECT_NE(original->m_VertexRemap.Begin(), cloned->m_VertexRemap.Begin());
    original->m_Vertices[0].m_Position.x = 999;
    EXPECT_FLOAT_EQ(5, cloned->m_Vertices[0].m_Position.x);
    source.reset();
    EXPECT_FLOAT_EQ(5, cloned->m_Vertices[0].m_Position.x);
    EXPECT_EQ(a, copy.GetFaceMaterial(0));
    EXPECT_EQ(b, copy.GetFaceMaterial(1));
    copy.CreateRenderGroups(); // SDL invalidates caches and rebuilds from semantic topology.
    dstIndex = copy.GetMaterialGroupIndex(a, FALSE);
    ASSERT_GE(dstIndex, 0);
    CKMaterialGroup *group = copy.Group(dstIndex);
    ASSERT_EQ(1, group->m_Primitives.Size());
    ExpectWords(group->m_Primitives[0].m_Indices, {0, 1, 2});
    ExpectWords(group->m_FaceIndices, {0});
    ASSERT_NE(nullptr, copy.GetVBuffer(group));
    EXPECT_FLOAT_EQ(5, copy.GetVBuffer(group)->m_Vertices[0].m_Position.x);
}
}
