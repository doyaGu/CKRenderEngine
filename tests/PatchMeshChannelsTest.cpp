#include <gtest/gtest.h>
#include <cmath>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKPatchMesh.h"
#include "RCKMaterial.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class ChannelPatch : public RCKPatchMesh {
public:
    explicit ChannelPatch(CKContext *context) : RCKPatchMesh(context) {}
    int DerivedCount() { return RCKMesh::GetChannelCount(); }
    CKMaterial *DerivedMaterial(int index) { return RCKMesh::GetChannelMaterial(index); }
    CKDWORD DerivedFlags(int index) { return RCKMesh::GetChannelFlags(index); }
    VXBLEND_MODE DerivedSource(int index) { return RCKMesh::GetChannelSourceBlend(index); }
    VXBLEND_MODE DerivedDest(int index) { return RCKMesh::GetChannelDestBlend(index); }
    void DropDerived() { while (RCKMesh::GetChannelCount() > 0) RCKMesh::RemoveChannel(0); }
    CKPatchChannel &Authored(int index) { return m_TexturePatches[index]; }
};
class AuthoredLookupMesh : public RCKMesh {
public:
    explicit AuthoredLookupMesh(CKContext *context) : RCKMesh(context) {}
    int GetChannelByMaterial(CKMaterial *) override { return 0; }
};

class PatchMeshChannelsTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKMaterial *First = nullptr, *Second = nullptr, *Third = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKPatchMesh, CKCID_MESH);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        First = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
        Second = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
        Third = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
        ASSERT_NE(nullptr, First); ASSERT_NE(nullptr, Second); ASSERT_NE(nullptr, Third);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Quad(ChannelPatch &mesh) {
        VxVector corners[4] = {VxVector(0,0,1), VxVector(3,0,1), VxVector(3,3,1), VxVector(0,3,1)};
        mesh.SetVertVecCount(4, 12); mesh.SetPatchCount(1); mesh.SetEdgeCount(4);
        CKPatch patch; patch.type = CK_PATCH_QUAD;
        for (int i = 0; i < 4; ++i) {
            mesh.SetVert(i, &corners[i]); patch.v[i] = static_cast<short>(i);
            patch.edge[i] = static_cast<short>(i); patch.interior[i] = static_cast<short>(8 + i);
            patch.vec[2*i] = static_cast<short>(2*i); patch.vec[2*i+1] = static_cast<short>(2*i+1);
            VxVector a = corners[i] + (corners[(i+1)%4] - corners[i]) * (1.0f / 3);
            VxVector b = corners[i] + (corners[(i+1)%4] - corners[i]) * (2.0f / 3);
            mesh.SetVec(2*i, &a); mesh.SetVec(2*i+1, &b);
            CKPatchEdge edge = {static_cast<short>(i), static_cast<short>(2*i), static_cast<short>(2*i+1), static_cast<short>((i+1)%4), 0, -1};
            mesh.SetEdge(i, &edge);
        }
        mesh.SetPatch(0, &patch); mesh.ComputePatchInteriors(0);
        mesh.SetTVCount(4); mesh.SetTVPatchCount(1); CKTVPatch tv = {{0,1,2,3}}; mesh.SetTVPatch(0, &tv);
        mesh.SetTV(0, 0, 0); mesh.SetTV(1, 1, 0); mesh.SetTV(2, 1, 1); mesh.SetTV(3, 0, 1);
        mesh.SetIterationCount(1);
    }
    static void ScaleUV(ChannelPatch &mesh, int channel, float scale) {
        mesh.SetTV(0, 0, 0, channel); mesh.SetTV(1, scale, 0, channel);
        mesh.SetTV(2, scale, scale, channel); mesh.SetTV(3, 0, scale, channel);
    }
    static void SurfaceUV(ChannelPatch &mesh, int channel, float expected) {
        ASSERT_EQ(9, mesh.GetVertexCount()); ASSERT_EQ(8, mesh.GetFaceCount());
        int corner = -1;
        for (int i = 0; i < mesh.GetVertexCount(); ++i) {
            VxVector position; mesh.GetVertexPosition(i, &position);
            if (fabsf(position.x - 3) < 1e-5f && fabsf(position.y - 3) < 1e-5f) corner = i;
        }
        ASSERT_GE(corner, 0); float u = -99, v = -99;
        mesh.GetVertexTextureCoordinates(corner, &u, &v, channel);
        EXPECT_FLOAT_EQ(expected, u); EXPECT_FLOAT_EQ(expected, v);
    }
    static int SavedChannels(ChannelPatch &mesh, CK_ID *materials = nullptr, CKDWORD *flags = nullptr) {
        CKStateChunk *saved = mesh.Save(nullptr, CK_STATESAVE_PATCHMESHONLY);
        EXPECT_NE(nullptr, saved); if (!saved) return -1;
        saved->StartRead(); EXPECT_TRUE(saved->SeekIdentifier(CK_STATESAVE_PATCHMESHDATA3));
        saved->ReadDword(); saved->ReadInt(); saved->ReadInt();
        int bytes = saved->ReadInt(); saved->ReadInt();
        VxVector points[16]; EXPECT_EQ(192, bytes); saved->ReadAndFillBuffer_LEndian(bytes, points);
        const int patches = saved->StartReadSequence();
        for (int i = 0; i < patches; ++i) saved->ReadObjectID();
        short indices[20];
        for (int i = 0; i < patches; ++i) { saved->ReadDword(); saved->ReadDword(); saved->ReadAndFillBuffer_LEndian16(40, indices); }
        bytes = saved->ReadInt(); saved->ReadInt(); short edges[24];
        EXPECT_EQ(48, bytes); saved->ReadAndFillBuffer_LEndian16(bytes, edges);
        const int channels = saved->StartReadSequence();
        for (int i = 0; i < channels; ++i) { const CK_ID id = saved->ReadObjectID(); if (materials) materials[i] = id; }
        for (int i = 0; i < channels; ++i) {
            saved->ReadDword(); saved->ReadDword(); const CKDWORD value = saved->ReadDword(); if (flags) flags[i] = value;
            bytes = saved->ReadInt(); saved->ReadInt(); short tv[4]; EXPECT_LE(bytes, 8); saved->ReadAndFillBuffer_LEndian16(bytes, tv);
            bytes = saved->ReadInt(); saved->ReadInt(); float uv[8]; EXPECT_LE(bytes, 32); saved->ReadAndFillBuffer_LEndian(bytes, uv);
        }
        DeleteCKStateChunk(saved); return channels;
    }
};

TEST_F(PatchMeshChannelsTest, AddCopiesAuthoredTopologyAndUVsWithNativeDefaultsBeforeTessellation) {
    ChannelPatch mesh(Context); Quad(mesh); ASSERT_EQ(0, mesh.AddChannel(First));
    ASSERT_EQ(1, mesh.GetChannelCount()); ASSERT_EQ(1, mesh.GetTVPatchCount(0)); ASSERT_EQ(4, mesh.GetTVCount(0));
    CKTVPatch patch; mesh.GetTVPatch(0, &patch, 0);
    for (int i = 0; i < 4; ++i) EXPECT_EQ(i, patch.tv[i]);
    float u, v; mesh.GetTV(2, &u, &v, 0); EXPECT_FLOAT_EQ(1, u); EXPECT_FLOAT_EQ(1, v);
    EXPECT_EQ(VXBLEND_ZERO, mesh.GetChannelSourceBlend(0)); EXPECT_EQ(VXBLEND_SRCCOLOR, mesh.GetChannelDestBlend(0));
    EXPECT_EQ(VXCHANNEL_ACTIVE, mesh.GetChannelFlags(0)); EXPECT_TRUE(mesh.IsChannelLit(0));
    EXPECT_EQ(0, mesh.AddChannel(First)); EXPECT_EQ(1, mesh.GetChannelCount());
    EXPECT_EQ(1, mesh.DerivedCount()); EXPECT_EQ(First, mesh.DerivedMaterial(0));
    mesh.BuildRenderMesh(); SurfaceUV(mesh, 0, 1);
}

TEST_F(PatchMeshChannelsTest, QualifiedMeshAddUsesItsOwnStorageLookupBeforeAllocatingTheActualUVStream) {
    AuthoredLookupMesh mesh(Context); ASSERT_TRUE(mesh.SetVertexCount(2));
    mesh.SetVertexTextureCoordinates(0, .25f, .5f, -1); mesh.SetVertexTextureCoordinates(1, .75f, 1.0f, -1);
    ASSERT_EQ(0, mesh.RCKMesh::AddChannel(First, TRUE)); ASSERT_EQ(1, mesh.RCKMesh::GetChannelCount());
    EXPECT_EQ(First, mesh.RCKMesh::GetChannelMaterial(0));
    CKDWORD stride = 0; void *uvs = mesh.GetTextureCoordinatesPtr(&stride, 0); ASSERT_NE(nullptr, uvs); EXPECT_EQ(sizeof(Vx2DVector), stride);
    float u = 0, v = 0; mesh.GetVertexTextureCoordinates(0, &u, &v, 0); EXPECT_FLOAT_EQ(.25f, u); EXPECT_FLOAT_EQ(.5f, v);
    mesh.GetVertexTextureCoordinates(1, &u, &v, 0); EXPECT_FLOAT_EQ(.75f, u); EXPECT_FLOAT_EQ(1, v);
    EXPECT_EQ(0, mesh.RCKMesh::AddChannel(First, FALSE)); EXPECT_EQ(1, mesh.RCKMesh::GetChannelCount());
}

TEST_F(PatchMeshChannelsTest, AddWithoutCopyPreservesTVIndicesAndOwnsZeroedIndependentCoordinates) {
    ChannelPatch mesh(Context); Quad(mesh); ASSERT_EQ(0, mesh.AddChannel(First, FALSE));
    ASSERT_EQ(1, mesh.GetTVPatchCount(0)); ASSERT_EQ(4, mesh.GetTVCount(0));
    CKTVPatch patch; mesh.GetTVPatch(0, &patch, 0); EXPECT_EQ(2, patch.tv[2]);
    for (int i = 0; i < 4; ++i) { float u, v; mesh.GetTV(i, &u, &v, 0); EXPECT_FLOAT_EQ(0, u); EXPECT_FLOAT_EQ(0, v); }
    ScaleUV(mesh, 0, 4); float u, v; mesh.GetTV(2, &u, &v, -1); EXPECT_FLOAT_EQ(1, u); EXPECT_FLOAT_EQ(1, v);
    mesh.BuildRenderMesh(); SurfaceUV(mesh, 0, 4);
}

TEST_F(PatchMeshChannelsTest, AuthoredMaterialFlagsAndBlendsSurviveSaveReloadAndGeometryRebuild) {
    ChannelPatch mesh(Context), loaded(Context); Quad(mesh); ASSERT_EQ(0, mesh.AddChannel(First)); mesh.BuildRenderMesh();
    mesh.SetChannelMaterial(0, Second); mesh.SetChannelSourceBlend(0, VXBLEND_SRCALPHA); mesh.SetChannelDestBlend(0, VXBLEND_INVSRCCOLOR);
    mesh.SetChannelFlags(0, VXCHANNEL_ACTIVE | VXCHANNEL_NOTLIT);
    EXPECT_EQ(Second, mesh.DerivedMaterial(0)); EXPECT_EQ(VXBLEND_SRCALPHA, mesh.DerivedSource(0)); EXPECT_EQ(VXBLEND_INVSRCCOLOR, mesh.DerivedDest(0));
    CK_ID materials[2] = {}; CKDWORD flags[2] = {}; ASSERT_EQ(2, SavedChannels(mesh, materials, flags));
    EXPECT_EQ(Second->GetID(), materials[1]); EXPECT_EQ(VXCHANNEL_ACTIVE | VXCHANNEL_NOTLIT, flags[1]);
    CKStateChunk *saved = mesh.Save(nullptr, CK_STATESAVE_PATCHMESHONLY); ASSERT_NE(nullptr, saved);
    ASSERT_EQ(CK_OK, loaded.Load(saved, nullptr)); DeleteCKStateChunk(saved);
    EXPECT_EQ(Second, loaded.GetChannelMaterial(0)); EXPECT_EQ(VXBLEND_SRCALPHA, loaded.GetChannelSourceBlend(0));
    EXPECT_EQ(VXBLEND_INVSRCCOLOR, loaded.GetChannelDestBlend(0)); EXPECT_FALSE(loaded.IsChannelLit(0)); SurfaceUV(loaded, 0, 1);
    mesh.CleanRenderMesh(); mesh.BuildRenderMesh(); EXPECT_EQ(Second, mesh.DerivedMaterial(0)); EXPECT_EQ(flags[1], mesh.DerivedFlags(0)); SurfaceUV(mesh, 0, 1);
}

TEST_F(PatchMeshChannelsTest, ActivationAndLightingModifyOnlyTheirOwnBitsAndPersistToDerivedState) {
    ChannelPatch mesh(Context); Quad(mesh); ASSERT_EQ(0, mesh.AddChannel(First)); ASSERT_EQ(1, mesh.AddChannel(Second));
    mesh.SetChannelFlags(0, VXCHANNEL_ACTIVE | VXCHANNEL_SAMEUV | VXCHANNEL_NOTLIT);
    mesh.ActivateChannel(0, FALSE); EXPECT_FALSE(mesh.IsChannelActive(0)); EXPECT_EQ(VXCHANNEL_SAMEUV | VXCHANNEL_NOTLIT, mesh.GetChannelFlags(0));
    mesh.LitChannel(0, TRUE); EXPECT_TRUE(mesh.IsChannelLit(0)); EXPECT_EQ(VXCHANNEL_SAMEUV, mesh.GetChannelFlags(0));
    mesh.ActivateAllChannels(FALSE); EXPECT_FALSE(mesh.IsChannelActive(0)); EXPECT_FALSE(mesh.IsChannelActive(1));
    mesh.ActivateAllChannels(TRUE); EXPECT_TRUE(mesh.IsChannelActive(0)); EXPECT_TRUE(mesh.IsChannelActive(1));
    mesh.BuildRenderMesh(); EXPECT_EQ(VXCHANNEL_ACTIVE | VXCHANNEL_SAMEUV, mesh.DerivedFlags(0)); EXPECT_TRUE(mesh.IsChannelLit(0));
}

TEST_F(PatchMeshChannelsTest, SameUVFlagKeepsBaseRenderAliasWhileRebuildRestoresIndependentAuthoredCoordinates) {
    ChannelPatch mesh(Context); Quad(mesh); ASSERT_EQ(0, mesh.AddChannel(First)); ScaleUV(mesh, 0, 2); mesh.BuildRenderMesh();
    mesh.SetChannelFlags(0, VXCHANNEL_ACTIVE | VXCHANNEL_SAMEUV);
    CKDWORD baseStride = 0, extraStride = 0; void *base = mesh.GetTextureCoordinatesPtr(&baseStride, -1);
    EXPECT_EQ(base, mesh.GetTextureCoordinatesPtr(&extraStride, 0)); EXPECT_EQ(baseStride, extraStride);
    mesh.CleanRenderMesh(); mesh.BuildRenderMesh(); SurfaceUV(mesh, -1, 1); SurfaceUV(mesh, 0, 2);
    base = mesh.GetTextureCoordinatesPtr(&baseStride, -1);
    EXPECT_EQ(base, mesh.GetTextureCoordinatesPtr(&extraStride, 0));
    mesh.SetChannelFlags(0, VXCHANNEL_ACTIVE); mesh.CleanRenderMesh(); mesh.BuildRenderMesh(); SurfaceUV(mesh, 0, 2);
}

TEST_F(PatchMeshChannelsTest, RemoveIndexShiftsAuthoredAndDerivedChannelsAndPreservesDefaultUVs) {
    ChannelPatch mesh(Context); Quad(mesh); ASSERT_EQ(0, mesh.AddChannel(First)); ASSERT_EQ(1, mesh.AddChannel(Second));
    ScaleUV(mesh, 0, 2); ScaleUV(mesh, 1, 3); mesh.BuildRenderMesh();
    mesh.RemoveChannel(0); ASSERT_EQ(1, mesh.GetChannelCount()); EXPECT_EQ(Second, mesh.GetChannelMaterial(0));
    EXPECT_EQ(-1, mesh.GetChannelByMaterial(First)); EXPECT_EQ(0, mesh.GetChannelByMaterial(Second));
    EXPECT_EQ(4, mesh.GetTVCount(-1)); EXPECT_EQ(1, mesh.GetTVPatchCount(-1));
    mesh.BuildRenderMesh(); ASSERT_EQ(1, mesh.DerivedCount()); EXPECT_EQ(Second, mesh.DerivedMaterial(0)); SurfaceUV(mesh, 0, 3); SurfaceUV(mesh, -1, 1);
}

TEST_F(PatchMeshChannelsTest, RemoveByMaterialRemovesEveryExtraMatchWithoutDeletingDefaultChannel) {
    ChannelPatch mesh(Context); Quad(mesh); mesh.Authored(0).Material = First->GetID();
    ASSERT_EQ(0, mesh.AddChannel(First)); ASSERT_EQ(1, mesh.AddChannel(Second)); mesh.SetChannelMaterial(1, First);
    mesh.RemoveChannelByMaterial(First); EXPECT_EQ(0, mesh.GetChannelCount()); EXPECT_EQ(0, mesh.DerivedCount());
    EXPECT_EQ(4, mesh.GetTVCount(-1)); EXPECT_EQ(1, mesh.GetTVPatchCount(-1));
    CK_ID materials[1] = {}; ASSERT_EQ(1, SavedChannels(mesh, materials)); EXPECT_EQ(First->GetID(), materials[0]);
    EXPECT_EQ(0, mesh.AddChannel(Third)); mesh.BuildRenderMesh(); EXPECT_EQ(Third, mesh.DerivedMaterial(0)); SurfaceUV(mesh, 0, 1);
}

TEST_F(PatchMeshChannelsTest, NullPlaceholderKeepsItsOrdinalAndDoesNotHideLaterRenderableChannels) {
    ChannelPatch mesh(Context); Quad(mesh); ASSERT_EQ(0, mesh.AddChannel(nullptr)); ASSERT_EQ(1, mesh.AddChannel(Second));
    EXPECT_EQ(0, mesh.AddChannel(nullptr)); EXPECT_EQ(0, mesh.GetChannelByMaterial(nullptr)); EXPECT_EQ(nullptr, mesh.GetChannelMaterial(0));
    ScaleUV(mesh, 0, 2); ScaleUV(mesh, 1, 3); mesh.BuildRenderMesh(); ASSERT_EQ(2, mesh.DerivedCount());
    EXPECT_EQ(nullptr, mesh.DerivedMaterial(0)); EXPECT_EQ(Second, mesh.DerivedMaterial(1)); SurfaceUV(mesh, 1, 3);
    mesh.SetChannelMaterial(0, First); EXPECT_EQ(First, mesh.DerivedMaterial(0)); SurfaceUV(mesh, 0, 2);
}

TEST_F(PatchMeshChannelsTest, EqualMaterialsPreserveDistinctAuthoredAndTessellatedUVOrdinals) {
    ChannelPatch mesh(Context); Quad(mesh); ASSERT_EQ(0, mesh.AddChannel(First)); ASSERT_EQ(1, mesh.AddChannel(Second));
    ScaleUV(mesh, 0, 2); ScaleUV(mesh, 1, 3); mesh.SetChannelMaterial(1, First); mesh.BuildRenderMesh();
    ASSERT_EQ(2, mesh.DerivedCount()); EXPECT_EQ(First, mesh.DerivedMaterial(0)); EXPECT_EQ(First, mesh.DerivedMaterial(1));
    EXPECT_EQ(0, mesh.GetChannelByMaterial(First)); SurfaceUV(mesh, 0, 2); SurfaceUV(mesh, 1, 3);
    CK_ID materials[3] = {}; ASSERT_EQ(3, SavedChannels(mesh, materials)); EXPECT_EQ(materials[1], materials[2]);
}

TEST_F(PatchMeshChannelsTest, MeshInterfaceQueriesUseAuthoredStateWhenDerivedStreamsAreAbsent) {
    ChannelPatch mesh(Context); Quad(mesh); ASSERT_EQ(0, mesh.AddChannel(First)); mesh.SetChannelFlags(0, VXCHANNEL_NOTLIT);
    mesh.SetChannelSourceBlend(0, VXBLEND_SRCALPHA); mesh.DropDerived(); CKMesh *sdk = &mesh;
    ASSERT_EQ(1, sdk->GetChannelCount()); EXPECT_EQ(First, sdk->GetChannelMaterial(0)); EXPECT_EQ(0, sdk->GetChannelByMaterial(First));
    EXPECT_FALSE(sdk->IsChannelActive(0)); EXPECT_FALSE(sdk->IsChannelLit(0)); EXPECT_EQ(VXBLEND_SRCALPHA, sdk->GetChannelSourceBlend(0));
    sdk->ActivateChannel(0); sdk->LitChannel(0); EXPECT_EQ(VXCHANNEL_ACTIVE, sdk->GetChannelFlags(0));
    mesh.BuildRenderMesh(); EXPECT_EQ(VXCHANNEL_ACTIVE, mesh.DerivedFlags(0)); SurfaceUV(mesh, 0, 1);
}

TEST_F(PatchMeshChannelsTest, UnbuiltSourceCopyOwnsAuthoredChannelsAndFeedsMeshAfterSourceDestruction) {
    ChannelPatch copy(Context);
    {
        ChannelPatch source(Context); Quad(source); ASSERT_EQ(0, source.AddChannel(First)); ScaleUV(source, 0, 2);
        source.SetChannelSourceBlend(0, VXBLEND_SRCALPHA); source.DropDerived(); ASSERT_EQ(0, source.DerivedCount());
        CKDependencies options; options.Resize(80); for (int i = 0; i < 80; ++i) options[i] = 0; options.m_Flags = CK_DEPENDENCIES_CUSTOM;
        CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
        ASSERT_EQ(CK_OK, copy.Copy(source, dependencies)); dependencies.StopDependencies();
        ScaleUV(source, 0, 7);
    }
    ASSERT_EQ(1, copy.GetChannelCount()); EXPECT_EQ(First, copy.GetChannelMaterial(0)); EXPECT_EQ(VXBLEND_SRCALPHA, copy.GetChannelSourceBlend(0));
    copy.BuildRenderMesh(); EXPECT_EQ(First, copy.DerivedMaterial(0)); SurfaceUV(copy, 0, 2);
}

TEST_F(PatchMeshChannelsTest, SixteenChannelMaskLimitRejectsBeforeChangingAuthoredPayload) {
    ChannelPatch mesh(Context); Quad(mesh);
    for (int i = 0; i < 16; ++i) {
        RCKMaterial *material = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL)); ASSERT_NE(nullptr, material);
        ASSERT_EQ(i, mesh.AddChannel(material));
    }
    ASSERT_EQ(16, mesh.GetChannelCount()); EXPECT_EQ(-1, mesh.AddChannel(Third)); EXPECT_EQ(16, mesh.GetChannelCount());
    EXPECT_EQ(17, SavedChannels(mesh)); mesh.BuildRenderMesh(); EXPECT_EQ(16, mesh.DerivedCount()); SurfaceUV(mesh, 15, 1);
}

TEST_F(PatchMeshChannelsTest, InvalidExtraChannelIndicesLeaveAuthoredWireAndLiveConsumerUnchanged) {
    ChannelPatch mesh(Context); Quad(mesh); ASSERT_EQ(0, mesh.AddChannel(First)); mesh.BuildRenderMesh();
    const int invalid[] = {-1, 1, 100};
    for (int i = 0; i < 3; ++i) {
        const int index = invalid[i]; mesh.RemoveChannel(index); mesh.ActivateChannel(index, FALSE); mesh.LitChannel(index, FALSE);
        mesh.SetChannelFlags(index, 0); mesh.SetChannelMaterial(index, Second);
        mesh.SetChannelSourceBlend(index, VXBLEND_ONE); mesh.SetChannelDestBlend(index, VXBLEND_ONE);
        EXPECT_EQ(nullptr, mesh.GetChannelMaterial(index)); EXPECT_EQ(0u, mesh.GetChannelFlags(index));
        EXPECT_FALSE(mesh.IsChannelActive(index)); EXPECT_FALSE(mesh.IsChannelLit(index));
        EXPECT_EQ(VXBLEND_ZERO, mesh.GetChannelSourceBlend(index)); EXPECT_EQ(VXBLEND_ZERO, mesh.GetChannelDestBlend(index));
    }
    CK_ID materials[2] = {}; ASSERT_EQ(2, SavedChannels(mesh, materials)); EXPECT_EQ(First->GetID(), materials[1]);
    EXPECT_EQ(First, mesh.DerivedMaterial(0)); SurfaceUV(mesh, 0, 1);
}

TEST_F(PatchMeshChannelsTest, MaterialConvenienceOverloadUsesThePatchRemovalVirtualSlot) {
    ChannelPatch mesh(Context); Quad(mesh); ASSERT_EQ(0, mesh.AddChannel(First)); ASSERT_EQ(1, mesh.AddChannel(Second));
    mesh.RemoveChannel(First); ASSERT_EQ(1, mesh.GetChannelCount()); EXPECT_EQ(Second, mesh.GetChannelMaterial(0));
    EXPECT_EQ(2, SavedChannels(mesh)); mesh.BuildRenderMesh(); SurfaceUV(mesh, 0, 1);
}

TEST_F(PatchMeshChannelsTest, RepeatedRemoveAddAndRebuildKeepsAuthoredUVAndBlendState) {
    ChannelPatch mesh(Context); Quad(mesh); ASSERT_EQ(0, mesh.AddChannel(First));
    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(1, mesh.AddChannel(Second)); ScaleUV(mesh, 1, 2.0f + i); mesh.SetChannelDestBlend(1, VXBLEND_INVSRCCOLOR);
        mesh.BuildRenderMesh(); SurfaceUV(mesh, 1, 2.0f + i); EXPECT_EQ(VXBLEND_INVSRCCOLOR, mesh.DerivedDest(1));
        mesh.RemoveChannel(0); ASSERT_EQ(1, mesh.GetChannelCount()); mesh.BuildRenderMesh(); SurfaceUV(mesh, 0, 2.0f + i);
        mesh.SetChannelMaterial(0, First);
    }
    EXPECT_EQ(First, mesh.GetChannelMaterial(0)); EXPECT_EQ(2, SavedChannels(mesh));
}
} // namespace
