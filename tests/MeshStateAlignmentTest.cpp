#include <gtest/gtest.h>
#include <cstdint>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"

extern void SetProcessorSpecific_FunctionsPtr();
extern CKBOOL g_UpdateTransparency;

namespace {
class MeshStateProbe : public RCKMesh {
public:
    explicit MeshStateProbe(CKContext *c) : RCKMesh(c) {}
    static void RecordLayout(RCKMesh &mesh) {
        const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(&mesh);
        std::size_t previous = sizeof(CKMesh);
#define FIELD(f) { auto member = &MeshStateProbe::f; \
        const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&(mesh.*member)); \
        const std::size_t offset = address - start; EXPECT_GE(offset, previous); \
        EXPECT_LE(offset + sizeof(mesh.*member), sizeof(RCKMesh)); \
        EXPECT_EQ(0u, address % alignof(decltype(mesh.*member))); \
        ::testing::Test::RecordProperty(#f "_offset", static_cast<int>(offset)); \
        ::testing::Test::RecordProperty(#f "_size", static_cast<int>(sizeof(mesh.*member))); \
        previous = offset + sizeof(mesh.*member); }
        FIELD(m_Flags); FIELD(m_BaryCenter); FIELD(m_Radius); FIELD(m_LocalBox);
        FIELD(m_Faces); FIELD(m_FaceVertexIndices); FIELD(m_LineIndices); FIELD(m_Vertices);
        FIELD(m_VertexWeights); FIELD(m_VertexColors); FIELD(m_DrawFlags); FIELD(m_FaceChannelMask);
        FIELD(m_MaterialChannels); FIELD(m_ActiveTextureChannels); FIELD(m_MaterialGroups); FIELD(m_Valid);
        FIELD(m_VertexBufferReady); FIELD(m_IndexBufferReady); FIELD(m_RasterizerContext);
        FIELD(m_VertexBuffer); FIELD(m_IndexBuffer); FIELD(m_IndexBufferIndexCount); FIELD(m_VertexBufferDpFlags);
        FIELD(m_VertexBufferVertexFormat); FIELD(m_VertexBufferStride); FIELD(m_VertexBufferVertexCount);
        FIELD(m_VertexBufferWrapAware); FIELD(m_ProgressiveMesh); FIELD(m_RenderCallbacks); FIELD(m_SubMeshCallbacks);
#undef FIELD
        ::testing::Test::RecordProperty("SDLSize", static_cast<int>(sizeof(RCKMesh)));
        ::testing::Test::RecordProperty("SDLAlignment", static_cast<int>(alignof(RCKMesh)));
        ::testing::Test::RecordProperty("SDLBaseSize", static_cast<int>(sizeof(CKMesh)));
        ::testing::Test::RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
        ::testing::Test::RecordProperty("m_ClassID_value", static_cast<int>(m_ClassID));
        ::testing::Test::RecordProperty("m_ClassID_size", static_cast<int>(sizeof(m_ClassID)));
    }
};
class MeshStateAlignmentTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); g_UpdateTransparency = FALSE;
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
};
}
TEST_F(MeshStateAlignmentTest, FactoryDefaultsAndPortableLayout) {
    CKMesh *api = RCKMesh::CreateInstance(Context); ASSERT_NE(nullptr, api);
    RCKMesh *mesh = static_cast<RCKMesh *>(api); const CK_ID id = api->GetID();
    EXPECT_EQ(CKCID_MESH, api->GetClassID()); EXPECT_EQ(32, RCKMesh::m_ClassID);
    EXPECT_EQ(10u, api->GetFlags()); EXPECT_TRUE(api->IsVisible());
    EXPECT_EQ(0, api->GetVertexCount()); EXPECT_EQ(0, api->GetFaceCount()); EXPECT_EQ(0, api->GetLineCount());
    EXPECT_EQ(0, api->GetChannelCount()); EXPECT_EQ(0, api->GetVertexWeightsCount());
    EXPECT_EQ(0, api->GetMaterialCount()); EXPECT_EQ(VX_LITMESH, api->GetLitMode());
    EXPECT_EQ(VXTEXTUREWRAP_NONE, api->GetWrapMode()); EXPECT_FALSE(api->IsPM());
    MeshStateProbe::RecordLayout(*mesh); delete static_cast<CKObject *>(api);
    EXPECT_EQ(nullptr, Context->GetObject(id));
}
TEST_F(MeshStateAlignmentTest, RegistrationAndDependencyMetadata) {
    CKClassDesc *desc = CKGetClassDesc(CKCID_MESH); ASSERT_NE(nullptr, desc);
    desc->Parameter = CKGUID(); desc->DefaultOptions = 0; RCKMesh::Register();
    EXPECT_STREQ("Mesh", RCKMesh::GetClassName()); EXPECT_STREQ("Mesh", desc->NameFct());
    EXPECT_EQ(CKPGUID_MESH, desc->Parameter); EXPECT_EQ(609440581u, desc->Parameter.d1);
    EXPECT_EQ(1708216873u, desc->Parameter.d2); EXPECT_EQ(CK_GENERALOPTIONS_CANUSECURRENTOBJECT, desc->DefaultOptions);
    for (int mode = -1; mode <= 6; ++mode) {
        EXPECT_EQ(mode == 1 || mode == 2 || mode == 4 ? 1 : 0, RCKMesh::GetDependenciesCount(mode));
        EXPECT_STREQ("Material", RCKMesh::GetDependencies(0, mode));
        EXPECT_EQ(nullptr, RCKMesh::GetDependencies(-1, mode)); EXPECT_EQ(nullptr, RCKMesh::GetDependencies(1, mode));
    }
}
TEST_F(MeshStateAlignmentTest, ShowAndFlagsKeepObjectAndMeshVisibilityConsistent) {
    RCKMesh mesh(Context); mesh.SetFlags(VXMESH_VISIBLE | VXMESH_WRAPV | VXMESH_PRELITMODE);
    mesh.Show(CKHIDE); EXPECT_FALSE(mesh.IsVisible()); EXPECT_EQ(VXMESH_WRAPV | VXMESH_PRELITMODE, mesh.GetFlags());
    mesh.Show(CKSHOW); EXPECT_TRUE(mesh.IsVisible()); EXPECT_EQ(VXMESH_VISIBLE | VXMESH_WRAPV | VXMESH_PRELITMODE, mesh.GetFlags());
    mesh.SetFlags(VXMESH_WRAPU); EXPECT_FALSE(mesh.IsVisible()); EXPECT_EQ(VXMESH_WRAPU, mesh.GetFlags());
    mesh.SetFlags(VXMESH_VISIBLE); EXPECT_TRUE(mesh.IsVisible()); EXPECT_EQ(VXMESH_VISIBLE, mesh.GetFlags());
}
TEST_F(MeshStateAlignmentTest, WrapLightingAndForcedTransparencyPreserveOtherBits) {
    RCKMesh mesh(Context); const CKDWORD initial = VXMESH_VISIBLE | VXMESH_RENDERCHANNELS | VXMESH_POS_CHANGED;
    mesh.SetFlags(initial);
    for (int mode = 0; mode < 4; ++mode) {
        mesh.SetWrapMode(static_cast<VXTEXTURE_WRAPMODE>(mode));
        EXPECT_EQ(mode, mesh.GetWrapMode()); EXPECT_EQ(initial, mesh.GetFlags() & ~(VXMESH_WRAPU | VXMESH_WRAPV));
    }
    mesh.SetLitMode(VX_PRELITMESH); EXPECT_EQ(VX_PRELITMESH, mesh.GetLitMode()); EXPECT_TRUE(mesh.GetFlags() & VXMESH_PRELITMODE);
    mesh.SetLitMode(VX_LITMESH); EXPECT_EQ(VX_LITMESH, mesh.GetLitMode()); EXPECT_FALSE(mesh.GetFlags() & VXMESH_PRELITMODE);
    const CKDWORD before = mesh.GetFlags(); mesh.SetTransparent(TRUE); EXPECT_TRUE(mesh.IsTransparent());
    EXPECT_EQ(before | VXMESH_FORCETRANSPARENCY, mesh.GetFlags()); mesh.SetTransparent(FALSE); EXPECT_EQ(before, mesh.GetFlags());
}
TEST_F(MeshStateAlignmentTest, MaterialTransparencyCacheRefreshesOnUnOptimizeAndGlobalDirty) {
    RCKMesh mesh(Context); RCKMaterial material(Context); ASSERT_TRUE(mesh.SetVertexCount(3)); ASSERT_TRUE(mesh.SetFaceCount(1));
    mesh.SetFaceVertexIndex(0, 0, 1, 2); mesh.SetFaceMaterial(0, &material);
    material.SetDestBlend(VXBLEND_INVSRCALPHA);
    material.EnableAlphaBlend(FALSE); EXPECT_FALSE(mesh.IsTransparent());
    material.EnableAlphaBlend(TRUE); g_UpdateTransparency = TRUE; EXPECT_TRUE(mesh.IsTransparent());
    g_UpdateTransparency = FALSE; material.EnableAlphaBlend(FALSE); mesh.UnOptimize(); EXPECT_FALSE(mesh.IsTransparent());
    mesh.SetTransparent(TRUE); EXPECT_TRUE(mesh.IsTransparent()); mesh.SetTransparent(FALSE); EXPECT_FALSE(mesh.IsTransparent());
}
TEST_F(MeshStateAlignmentTest, EmptyBoundsAreZeroAndNamedConstructionOwnsName) {
    char name[] = "mesh state"; RCKMesh mesh(Context, name); name[0] = 'X'; EXPECT_STREQ("mesh state", mesh.GetName());
    VxVector center(9, 9, 9); mesh.GetBaryCenter(&center); EXPECT_EQ(0.0f, center.x); EXPECT_EQ(0.0f, center.y); EXPECT_EQ(0.0f, center.z);
    EXPECT_EQ(0.0f, mesh.GetRadius()); const VxBbox &box = mesh.GetLocalBox();
    EXPECT_EQ(0.0f, box.Min.x); EXPECT_EQ(0.0f, box.Min.y); EXPECT_EQ(0.0f, box.Min.z);
    EXPECT_EQ(0.0f, box.Max.x); EXPECT_EQ(0.0f, box.Max.y); EXPECT_EQ(0.0f, box.Max.z);
}
TEST_F(MeshStateAlignmentTest, PortableBufferStateTransfersAllFieldsWithoutTakingResourceOwnership) {
    RCKMesh mesh(Context); RCKMeshBufferState first = {}; mesh.GetRasterizerBufferState(first);
    EXPECT_EQ(nullptr, first.Context); EXPECT_EQ(0u, first.VertexBuffer); EXPECT_EQ(0u, first.IndexBuffer);
    EXPECT_EQ(0u, first.VertexBufferReady); EXPECT_FALSE(first.IndexBufferReady);
    RCKMeshBufferState state = {}; state.VertexBufferReady = 17; state.IndexBufferReady = TRUE;
    state.VertexBuffer = 13; state.IndexBuffer = 14; state.IndexBufferIndexCount = 15; state.VertexBufferDpFlags = 16;
    state.VertexBufferVertexFormat = 18; state.VertexBufferStride = 19; state.VertexBufferVertexCount = 20; state.VertexBufferWrapAware = TRUE;
    mesh.SetRasterizerBufferState(state); RCKMeshBufferState read = {}; mesh.GetRasterizerBufferState(read);
    EXPECT_EQ(state.Context, read.Context); EXPECT_EQ(17u, read.VertexBufferReady); EXPECT_TRUE(read.IndexBufferReady);
    EXPECT_EQ(13u, read.VertexBuffer); EXPECT_EQ(14u, read.IndexBuffer); EXPECT_EQ(15u, read.IndexBufferIndexCount);
    EXPECT_EQ(16u, read.VertexBufferDpFlags); EXPECT_EQ(18u, read.VertexBufferVertexFormat); EXPECT_EQ(19u, read.VertexBufferStride);
    EXPECT_EQ(20u, read.VertexBufferVertexCount); EXPECT_TRUE(read.VertexBufferWrapAware); mesh.SetRasterizerBufferState(first);
}
