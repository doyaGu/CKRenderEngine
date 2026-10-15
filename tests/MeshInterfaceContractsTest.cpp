#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"

extern void SetProcessorSpecific_FunctionsPtr();
extern CKBOOL g_UpdateTransparency;
namespace {
class MeshInterfaceContractsTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        g_UpdateTransparency = FALSE;
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Triangle(RCKMesh &mesh, int faces = 1) {
        ASSERT_TRUE(mesh.SetVertexCount(3)); ASSERT_TRUE(mesh.SetFaceCount(faces));
        VxVector p[] = {VxVector(0,0,0), VxVector(2,0,0), VxVector(0,3,0)};
        VxVector n(0,0,1);
        for (int i=0;i<3;++i) { mesh.SetVertexPosition(i,&p[i]); mesh.SetVertexNormal(i,&n); }
        for (int i=0;i<faces;++i) mesh.SetFaceVertexIndex(i,0,1,2);
    }
    static void Vec(const VxVector &p,float x,float y,float z) {
        EXPECT_NEAR(x,p.x,1e-5f); EXPECT_NEAR(y,p.y,1e-5f); EXPECT_NEAR(z,p.z,1e-5f);
    }
};
}
TEST_F(MeshInterfaceContractsTest, EmptySaveFlagsDoNotRequireAnExistingVertex) {
    RCKMesh mesh(Context); EXPECT_EQ(15u,mesh.GetSaveFlags());
}
TEST_F(MeshInterfaceContractsTest, ConstantPayloadAndReconstructibleNormalsUseIndependentCompactBits) {
    RCKMesh mesh(Context); Triangle(mesh); EXPECT_EQ(15u,mesh.GetSaveFlags());
}
TEST_F(MeshInterfaceContractsTest, DistinctDiffuseSpecularAndUVEachDisableOnlyTheirOwnCompressionBit) {
    RCKMesh mesh(Context); Triangle(mesh);
    mesh.SetVertexColor(1,0xff112233); EXPECT_EQ(14u,mesh.GetSaveFlags());
    mesh.SetVertexColor(1,0xffffffff); mesh.SetVertexSpecularColor(2,0x11223344); EXPECT_EQ(13u,mesh.GetSaveFlags());
    mesh.SetVertexSpecularColor(2,0); mesh.SetVertexTextureCoordinates(2,.5f,.75f,-1); EXPECT_EQ(7u,mesh.GetSaveFlags());
}
TEST_F(MeshInterfaceContractsTest, NonunitStoredNormalIsNotDiscardedAsReconstructible) {
    RCKMesh mesh(Context); Triangle(mesh); VxVector n(0,0,2); mesh.SetVertexNormal(0,&n); EXPECT_EQ(11u,mesh.GetSaveFlags());
}
TEST_F(MeshInterfaceContractsTest, MeanNormalErrorBelowThresholdRetainsReconstructionBit) {
    RCKMesh mesh(Context); Triangle(mesh); VxVector n(0,0,1.002f); mesh.SetVertexNormal(0,&n); EXPECT_EQ(15u,mesh.GetSaveFlags());
}
TEST_F(MeshInterfaceContractsTest, MeanNormalErrorAboveThresholdRequiresStoredNormals) {
    RCKMesh mesh(Context); Triangle(mesh); VxVector n(0,0,1.006f); mesh.SetVertexNormal(0,&n); EXPECT_EQ(11u,mesh.GetSaveFlags());
}
TEST_F(MeshInterfaceContractsTest, ProceduralPositionAndUVFlagsSuppressContentComparison) {
    RCKMesh mesh(Context); Triangle(mesh); mesh.SetVertexTextureCoordinates(1,.5f,.75f,-1);
    VxVector n(1,0,0); mesh.SetVertexNormal(0,&n);
    mesh.SetFlags(mesh.GetFlags() | VXMESH_PROCEDURALPOS | VXMESH_PROCEDURALUV); EXPECT_EQ(31u,mesh.GetSaveFlags());
}
TEST_F(MeshInterfaceContractsTest, GeneratedNormalsAreReconstructibleButOrdinaryUnfacedVerticesAreNot) {
    RCKMesh mesh(Context); mesh.SetVertexCount(3); EXPECT_EQ(11u,mesh.GetSaveFlags());
    mesh.SetFlags(mesh.GetFlags() | VXMESH_GENNORMALS); EXPECT_EQ(15u,mesh.GetSaveFlags());
}
TEST_F(MeshInterfaceContractsTest, FaceCornerReferenceAndRawIndexStreamAddressTheAuthoredVertex) {
    RCKMesh mesh(Context); Triangle(mesh); int a=-1,b=-1,c=-1; mesh.GetFaceVertexIndex(0,a,b,c);
    EXPECT_EQ(0,a); EXPECT_EQ(1,b); EXPECT_EQ(2,c); ASSERT_NE(nullptr,mesh.GetFacesIndices());
    EXPECT_EQ(2,mesh.GetFacesIndices()[2]); Vec(mesh.GetFaceVertex(0,1),2,0,0);
    mesh.GetFaceVertex(0,1).x=5; mesh.VertexMove(); VxVector p; mesh.GetVertexPosition(1,&p); Vec(p,5,0,0);
    EXPECT_FLOAT_EQ(5,mesh.GetLocalBox().Max.x);
}
TEST_F(MeshInterfaceContractsTest, FaceNormalPointerStrideSeparatesActualFaceNormals) {
    RCKMesh mesh(Context); Triangle(mesh,2); mesh.SetFaceVertexIndex(1,0,2,1); mesh.BuildFaceNormals();
    CKDWORD stride=0; CKBYTE *normals=mesh.GetFaceNormalsPtr(&stride); ASSERT_NE(nullptr,normals);
    EXPECT_EQ(sizeof(CKFace),stride); Vec(*reinterpret_cast<VxVector *>(normals),0,0,1);
    Vec(*reinterpret_cast<VxVector *>(normals+stride),0,0,-1);
}
TEST_F(MeshInterfaceContractsTest, EmptyFaceNormalsHaveNoDataAndStillReportStride) {
    RCKMesh mesh(Context); CKDWORD stride=0; EXPECT_EQ(nullptr,mesh.GetFaceNormalsPtr(&stride)); EXPECT_EQ(sizeof(CKFace),stride);
}
TEST_F(MeshInterfaceContractsTest, BatchMaterialAssignmentChangesOnlySubmittedFaces) {
    RCKMesh mesh(Context); RCKMaterial first(Context),second(Context); Triangle(mesh,3);
    mesh.ApplyGlobalMaterial(&first); int faces[]={2,0}; mesh.SetFaceMaterialEx(faces,2,&second);
    EXPECT_EQ(&second,mesh.GetFaceMaterial(0)); EXPECT_EQ(&first,mesh.GetFaceMaterial(1)); EXPECT_EQ(&second,mesh.GetFaceMaterial(2));
}
TEST_F(MeshInterfaceContractsTest, MaterialEnumerationSkipsDefaultGroupAndReportsAssignedMaterials) {
    RCKMesh mesh(Context); RCKMaterial first(Context),second(Context); Triangle(mesh,2);
    mesh.SetFaceMaterial(0,&first); mesh.SetFaceMaterial(1,&second); EXPECT_EQ(2,mesh.GetMaterialCount());
    EXPECT_EQ(&first,mesh.GetMaterial(0)); EXPECT_EQ(&second,mesh.GetMaterial(1)); EXPECT_EQ(nullptr,mesh.GetMaterial(2));
}
TEST_F(MeshInterfaceContractsTest, ReplacingNonnullMaterialInvalidatesCachedTransparency) {
    RCKMesh mesh(Context); RCKMaterial first(Context),second(Context); Triangle(mesh);
    second.SetDestBlend(VXBLEND_INVSRCALPHA); second.EnableAlphaBlend(TRUE);
    mesh.SetFaceMaterial(0,&first); g_UpdateTransparency=FALSE; EXPECT_FALSE(mesh.IsTransparent());
    mesh.ReplaceMaterial(&first,&second); EXPECT_EQ(&second,mesh.GetFaceMaterial(0)); EXPECT_TRUE(mesh.IsTransparent());
}
TEST_F(MeshInterfaceContractsTest, ReplacingMaterialWithNullPreservesOtherGroupFaceOrdinals) {
    RCKMesh mesh(Context); RCKMaterial first(Context),second(Context); Triangle(mesh,2);
    mesh.SetFaceMaterial(0,&first); mesh.SetFaceMaterial(1,&second); mesh.ReplaceMaterial(&first,nullptr);
    EXPECT_EQ(nullptr,mesh.GetFaceMaterial(0)); EXPECT_EQ(&second,mesh.GetFaceMaterial(1));
    EXPECT_EQ(-1,mesh.GetMaterialGroupIndex(&first,FALSE)); EXPECT_GE(mesh.GetMaterialGroupIndex(&second,FALSE),1);
}
TEST_F(MeshInterfaceContractsTest, ReplacingDefaultMaterialChangesOnlyDefaultFaces) {
    RCKMesh mesh(Context); RCKMaterial first(Context),second(Context); Triangle(mesh,2);
    mesh.SetFaceMaterial(1,&first); mesh.ReplaceMaterial(nullptr,&second);
    EXPECT_EQ(&second,mesh.GetFaceMaterial(0)); EXPECT_EQ(&first,mesh.GetFaceMaterial(1));
}
TEST_F(MeshInterfaceContractsTest, ReplacingSameOrAbsentMaterialPreservesFaceTopologyAndMaterials) {
    RCKMesh mesh(Context); RCKMaterial first(Context),second(Context); Triangle(mesh); mesh.SetFaceMaterial(0,&first);
    mesh.ReplaceMaterial(&first,&first); mesh.ReplaceMaterial(&second,nullptr);
    EXPECT_EQ(&first,mesh.GetFaceMaterial(0)); EXPECT_EQ(3,mesh.GetVertexCount()); EXPECT_EQ(1,mesh.GetFaceCount());
}
TEST_F(MeshInterfaceContractsTest, PlainAndProgressiveMeshPresenceTracksDestroyPM) {
    RCKMesh mesh(Context); EXPECT_FALSE(mesh.IsPM()); EXPECT_EQ(CK_OK,mesh.CreatePM());
    EXPECT_TRUE(mesh.IsPM());
    // The owned empty PM is explicitly removed; creation algorithms are
    // reviewed separately from this pointer-presence contract.
    mesh.DestroyPM(); EXPECT_FALSE(mesh.IsPM());
}
