#include <gtest/gtest.h>
#include <cstdlib>
#include <new>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"

namespace {
struct ReleaseWatch { void *Pointer; int Releases; };
ReleaseWatch Watches[16];
int WatchCount = 0;
int Watch(void *pointer) {
    const int index = WatchCount++;
    Watches[index] = {pointer, 0};
    return index;
}
void Release(void *pointer) {
    for (int i = 0; i < WatchCount; ++i) {
        if (pointer && Watches[i].Pointer == pointer) {
            ++Watches[i].Releases;
            return; // Hold only watched allocations, preventing address reuse.
        }
    }
    std::free(pointer);
}
}
void *operator new(std::size_t bytes) {
    void *pointer = std::malloc(bytes ? bytes : 1);
    if (!pointer) throw std::bad_alloc();
    return pointer;
}
void *operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void *pointer) noexcept { Release(pointer); }
void operator delete[](void *pointer) noexcept { Release(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { Release(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept { Release(pointer); }

extern void SetProcessorSpecific_FunctionsPtr();
namespace {
class ChannelStorageMesh : public RCKMesh {
public:
    explicit ChannelStorageMesh(CKContext *context) : RCKMesh(context) {}
    VxMaterialChannel &Channel(int index) { return m_MaterialChannels[index]; }
};
class MaterialChannelStorageTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        WatchCount = 0;
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
        for (int i = 0; i < WatchCount; ++i) {
            EXPECT_EQ(1, Watches[i].Releases);
            if (Watches[i].Releases) std::free(Watches[i].Pointer);
        }
        WatchCount = 0;
    }
    static void Own(VxMaterialChannel &channel, float u) {
        channel.m_UVs = new Vx2DVector[2];
        channel.m_UVs[0] = Vx2DVector(u, u + 1);
        channel.m_UVs[1] = Vx2DVector(u + 2, u + 3);
        channel.m_FaceIndices = new XArray<CKWORD>;
        channel.m_FaceIndices->PushBack(2);
        channel.m_FaceIndices->PushBack(0);
        channel.m_FaceIndices->PushBack(1);
    }
    static void Geometry(ChannelStorageMesh &mesh) {
        ASSERT_TRUE(mesh.SetVertexCount(4));
        mesh.SetFaceCount(2);
        mesh.SetFaceVertexIndex(0, 0, 1, 2);
        mesh.SetFaceVertexIndex(1, 2, 1, 3);
    }
};
}

TEST_F(MaterialChannelStorageTest, DefaultsAndClearingEmptyStorageKeepIndependentChannelSettings) {
    RCKMaterial material(Context);
    VxMaterialChannel channel, other;
    EXPECT_EQ(nullptr, channel.m_UVs);
    EXPECT_EQ(nullptr, channel.m_FaceIndices);
    EXPECT_EQ(nullptr, channel.m_Material);
    EXPECT_EQ(VXBLEND_ZERO, channel.m_SourceBlend);
    EXPECT_EQ(VXBLEND_SRCCOLOR, channel.m_DestBlend);
    EXPECT_EQ(static_cast<CKDWORD>(VXCHANNEL_ACTIVE), channel.m_Flags);
    channel.m_Material = &material;
    channel.m_SourceBlend = VXBLEND_SRCALPHA;
    channel.m_DestBlend = VXBLEND_INVSRCALPHA;
    channel.m_Flags = VXCHANNEL_ACTIVE | VXCHANNEL_NOTLIT;
    channel.Clear();
    EXPECT_EQ(&material, channel.m_Material);
    EXPECT_EQ(VXBLEND_SRCALPHA, channel.m_SourceBlend);
    EXPECT_EQ(VXBLEND_INVSRCALPHA, channel.m_DestBlend);
    EXPECT_EQ(static_cast<CKDWORD>(VXCHANNEL_ACTIVE | VXCHANNEL_NOTLIT), channel.m_Flags);
    EXPECT_EQ(nullptr, other.m_Material);
    EXPECT_EQ(VXBLEND_ZERO, other.m_SourceBlend);
}

TEST_F(MaterialChannelStorageTest, ClearReleasesOwnedArraysOnceAndRetainsTheBorrowedMaterial) {
    RCKMaterial *material = new RCKMaterial(Context);
    const int materialWatch = Watch(material);
    VxMaterialChannel channel;
    channel.m_Material = material;
    Own(channel, 10);
    const int uv = Watch(channel.m_UVs), indices = Watch(channel.m_FaceIndices);
    channel.Clear();
    EXPECT_EQ(nullptr, channel.m_UVs);
    EXPECT_EQ(nullptr, channel.m_FaceIndices);
    EXPECT_EQ(1, Watches[uv].Releases);
    EXPECT_EQ(1, Watches[indices].Releases);
    EXPECT_EQ(0, Watches[materialWatch].Releases);
    EXPECT_EQ(material, channel.m_Material);
    channel.Clear();
    EXPECT_EQ(1, Watches[uv].Releases);
    EXPECT_EQ(1, Watches[indices].Releases);
    delete material;
    EXPECT_EQ(1, Watches[materialWatch].Releases);
}

TEST_F(MaterialChannelStorageTest, ClearingOneChannelLeavesOtherOwnedPayloadUsable) {
    VxMaterialChannel a, b;
    Own(a, 3); Own(b, 30);
    const int au = Watch(a.m_UVs), af = Watch(a.m_FaceIndices);
    const int bu = Watch(b.m_UVs), bf = Watch(b.m_FaceIndices);
    a.Clear();
    EXPECT_EQ(1, Watches[au].Releases); EXPECT_EQ(1, Watches[af].Releases);
    EXPECT_EQ(0, Watches[bu].Releases); EXPECT_EQ(0, Watches[bf].Releases);
    EXPECT_FLOAT_EQ(30, b.m_UVs[0].x);
    EXPECT_FLOAT_EQ(33, b.m_UVs[1].y);
    ASSERT_EQ(3, b.m_FaceIndices->Size());
    EXPECT_EQ(2, (*b.m_FaceIndices)[0]);
    b.Clear();
}

TEST_F(MaterialChannelStorageTest, AReusedChannelCanOwnNewPayloadAfterIdempotentClear) {
    VxMaterialChannel channel;
    Own(channel, 1);
    const int oldUv = Watch(channel.m_UVs), oldFaces = Watch(channel.m_FaceIndices);
    channel.Clear(); channel.Clear();
    Own(channel, 20);
    const int newUv = Watch(channel.m_UVs), newFaces = Watch(channel.m_FaceIndices);
    EXPECT_FLOAT_EQ(20, channel.m_UVs[0].x);
    EXPECT_FLOAT_EQ(23, channel.m_UVs[1].y);
    channel.Clear();
    EXPECT_EQ(1, Watches[oldUv].Releases); EXPECT_EQ(1, Watches[oldFaces].Releases);
    EXPECT_EQ(1, Watches[newUv].Releases); EXPECT_EQ(1, Watches[newFaces].Releases);
}

TEST_F(MaterialChannelStorageTest, ActualFaceMembershipBuildsAndReleasesOnlyTheSubsetCache) {
    RCKMaterial material(Context);
    ChannelStorageMesh mesh(Context);
    Geometry(mesh);
    ASSERT_EQ(0, mesh.AddChannel(&material, FALSE));
    mesh.SetFaceChannelMask(0, 1); mesh.SetFaceChannelMask(1, 0);
    mesh.UpdateChannelIndices();
    XArray<CKWORD> *subset = mesh.Channel(0).m_FaceIndices;
    ASSERT_NE(nullptr, subset);
    ASSERT_EQ(3, subset->Size());
    EXPECT_EQ(0, (*subset)[0]); EXPECT_EQ(1, (*subset)[1]); EXPECT_EQ(2, (*subset)[2]);
    const int cached = Watch(subset);
    Vx2DVector *uv = mesh.Channel(0).m_UVs;
    mesh.SetFaceChannelMask(1, 1);
    mesh.UpdateChannelIndices();
    EXPECT_EQ(nullptr, mesh.Channel(0).m_FaceIndices);
    EXPECT_EQ(1, Watches[cached].Releases);
    EXPECT_EQ(uv, mesh.Channel(0).m_UVs);
    EXPECT_EQ(&material, mesh.Channel(0).m_Material);
}

TEST_F(MaterialChannelStorageTest, RemovingMiddleChannelReleasesItsPayloadAndPreservesShiftedSurvivor) {
    RCKMaterial a(Context), b(Context), c(Context);
    {
        ChannelStorageMesh mesh(Context); Geometry(mesh);
        ASSERT_EQ(0, mesh.AddChannel(&a, FALSE));
        ASSERT_EQ(1, mesh.AddChannel(&b, FALSE));
        ASSERT_EQ(2, mesh.AddChannel(&c, FALSE));
        mesh.SetVertexTextureCoordinates(2, .25f, .75f, 2);
        mesh.SetChannelSourceBlend(2, VXBLEND_SRCALPHA);
        mesh.SetChannelDestBlend(2, VXBLEND_INVSRCALPHA);
        mesh.SetFaceChannelMask(0, 2); mesh.SetFaceChannelMask(1, 4);
        mesh.UpdateChannelIndices();
        ASSERT_NE(nullptr, mesh.Channel(1).m_FaceIndices);
        ASSERT_NE(nullptr, mesh.Channel(2).m_FaceIndices);
        const int removedUv = Watch(mesh.Channel(1).m_UVs);
        const int removedFaces = Watch(mesh.Channel(1).m_FaceIndices);
        const int survivorUv = Watch(mesh.Channel(2).m_UVs);
        const int survivorFaces = Watch(mesh.Channel(2).m_FaceIndices);
        Vx2DVector *survivor = mesh.Channel(2).m_UVs;
        mesh.RemoveChannel(1);
        ASSERT_EQ(2, mesh.GetChannelCount());
        EXPECT_EQ(1, Watches[removedUv].Releases); EXPECT_EQ(1, Watches[removedFaces].Releases);
        EXPECT_EQ(0, Watches[survivorUv].Releases); EXPECT_EQ(0, Watches[survivorFaces].Releases);
        EXPECT_EQ(&c, mesh.GetChannelMaterial(1)); EXPECT_EQ(survivor, mesh.Channel(1).m_UVs);
        EXPECT_EQ(VXBLEND_SRCALPHA, mesh.GetChannelSourceBlend(1));
        EXPECT_EQ(VXBLEND_INVSRCALPHA, mesh.GetChannelDestBlend(1));
        EXPECT_FLOAT_EQ(.25f, survivor[2].x); EXPECT_FLOAT_EQ(.75f, survivor[2].y);
        EXPECT_EQ(0, mesh.GetFaceChannelMask(0)); EXPECT_EQ(2, mesh.GetFaceChannelMask(1));
        mesh.UpdateChannelIndices();
        ASSERT_EQ(3, mesh.Channel(1).m_FaceIndices->Size());
        EXPECT_EQ(2, (*mesh.Channel(1).m_FaceIndices)[0]);
        EXPECT_EQ(1, (*mesh.Channel(1).m_FaceIndices)[1]);
        EXPECT_EQ(3, (*mesh.Channel(1).m_FaceIndices)[2]);
    }
}

TEST_F(MaterialChannelStorageTest, SameUvTransitionReleasesPrivateStorageWithoutOwningTheBaseStream) {
    RCKMaterial material(Context);
    ChannelStorageMesh mesh(Context); Geometry(mesh);
    mesh.SetVertexTextureCoordinates(2, .5f, .625f, -1);
    ASSERT_EQ(0, mesh.AddChannel(&material, TRUE));
    const int privateUv = Watch(mesh.Channel(0).m_UVs);
    mesh.SetChannelFlags(0, VXCHANNEL_ACTIVE | VXCHANNEL_SAMEUV);
    EXPECT_EQ(1, Watches[privateUv].Releases);
    EXPECT_EQ(nullptr, mesh.Channel(0).m_UVs);
    CKDWORD baseStride = 0, sameStride = 0;
    EXPECT_EQ(mesh.GetTextureCoordinatesPtr(&baseStride, -1),
              mesh.GetTextureCoordinatesPtr(&sameStride, 0));
    EXPECT_EQ(baseStride, sameStride);
    mesh.SetChannelFlags(0, VXCHANNEL_ACTIVE);
    ASSERT_NE(nullptr, mesh.Channel(0).m_UVs);
    const int replacementUv = Watch(mesh.Channel(0).m_UVs);
    EXPECT_FLOAT_EQ(0, mesh.Channel(0).m_UVs[2].x);
    mesh.RemoveChannel(0);
    EXPECT_EQ(1, Watches[replacementUv].Releases);
    float u = 0, v = 0;
    mesh.GetVertexTextureCoordinates(2, &u, &v, -1);
    EXPECT_FLOAT_EQ(.5f, u); EXPECT_FLOAT_EQ(.625f, v);
}
