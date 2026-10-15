#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class MeshGeometryProbe : public RCKMesh {
public:
    explicit MeshGeometryProbe(CKContext *context) : RCKMesh(context) {}
    void CacheFlags(CKDWORD flags) { m_Flags |= flags; m_Valid = TRUE; }
    CKBOOL GeometryValid() const { return m_Valid; }
    void AttachProgressiveMap() {
        m_ProgressiveMesh = new CKProgressiveMesh();
        m_ProgressiveMesh->m_VertexCount = m_Vertices.Size();
        m_ProgressiveMesh->m_Data.Resize(m_Vertices.Size());
        for (int i = 0; i < m_Vertices.Size(); ++i) m_ProgressiveMesh->m_Data[i] = 0;
    }
};
class MeshGeometryAlignmentTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Vec(const VxVector &v, float x, float y, float z) {
        EXPECT_NEAR(x, v.x, 0.0001f); EXPECT_NEAR(y, v.y, 0.0001f); EXPECT_NEAR(z, v.z, 0.0001f);
    }
    static void Position(RCKMesh &mesh, int index, float x, float y, float z) {
        VxVector v(x, y, z); mesh.SetVertexPosition(index, &v);
    }
    static void ExpectPosition(RCKMesh &mesh, int index, float x, float y, float z) {
        VxVector v; mesh.GetVertexPosition(index, &v); Vec(v, x, y, z);
    }
    static void ExpectUV(RCKMesh &mesh, int index, int channel, float x, float y) {
        float u = -9.0f, v = -9.0f; mesh.GetVertexTextureCoordinates(index, &u, &v, channel);
        EXPECT_FLOAT_EQ(x, u); EXPECT_FLOAT_EQ(y, v);
    }
    static void Payload(RCKMesh &mesh, int count) {
        ASSERT_TRUE(mesh.SetVertexCount(count)); mesh.SetVertexWeightsCount(count);
        for (int i = 0; i < count; ++i) {
            Position(mesh, i, static_cast<float>(i), static_cast<float>(i % 2), 0.0f);
            VxVector n(0.0f, 0.0f, 1.0f); mesh.SetVertexNormal(i, &n);
            mesh.SetVertexColor(i, 0xff123400u + i); mesh.SetVertexSpecularColor(i, 0x10203000u + i);
            mesh.SetVertexTextureCoordinates(i, i + 0.25f, i + 0.75f, -1);
            mesh.SetVertexWeight(i, i + 0.5f);
        }
    }
    static void ExpectedPayload(RCKMesh &mesh, int output, int input) {
        ExpectPosition(mesh, output, static_cast<float>(input), static_cast<float>(input % 2), 0.0f);
        EXPECT_EQ(0xff123400u + input, mesh.GetVertexColor(output));
        EXPECT_EQ(0x10203000u + input, mesh.GetVertexSpecularColor(output));
        ExpectUV(mesh, output, -1, input + 0.25f, input + 0.75f);
        EXPECT_FLOAT_EQ(input + 0.5f, mesh.GetVertexWeight(output));
    }
};
}

TEST_F(MeshGeometryAlignmentTest, VertexGrowthKeepsPayloadAndInitializesNewVertices) {
    RCKMesh mesh(Context); Payload(mesh, 2); ASSERT_TRUE(mesh.SetVertexCount(5));
    ExpectedPayload(mesh, 0, 0); ExpectedPayload(mesh, 1, 1);
    for (int i = 2; i < 5; ++i) {
        ExpectPosition(mesh, i, 0, 0, 0); EXPECT_EQ(0xffffffffu, mesh.GetVertexColor(i));
        EXPECT_EQ(0u, mesh.GetVertexSpecularColor(i)); ExpectUV(mesh, i, -1, 0, 0);
        EXPECT_FLOAT_EQ(0, mesh.GetVertexWeight(i));
    }
}
TEST_F(MeshGeometryAlignmentTest, VertexShrinkDropsOnlyPrimitivesUsingRemovedVertices) {
    RCKMesh mesh(Context); Payload(mesh, 5); ASSERT_TRUE(mesh.SetFaceCount(3));
    mesh.SetFaceVertexIndex(0, 0, 1, 2); mesh.SetFaceVertexIndex(1, 2, 3, 4); mesh.SetFaceVertexIndex(2, 2, 1, 0);
    mesh.SetFaceChannelMask(0, 3); mesh.SetFaceChannelMask(2, 5);
    ASSERT_TRUE(mesh.SetLineCount(3)); mesh.SetLine(0, 0, 2); mesh.SetLine(1, 3, 4); mesh.SetLine(2, 1, 2);
    ASSERT_TRUE(mesh.SetVertexCount(3)); EXPECT_EQ(2, mesh.GetFaceCount()); EXPECT_EQ(2, mesh.GetLineCount());
    EXPECT_EQ(3, mesh.GetFaceChannelMask(0)); EXPECT_EQ(5, mesh.GetFaceChannelMask(1));
    int a, b; mesh.GetLine(1, &a, &b); EXPECT_EQ(1, a); EXPECT_EQ(2, b);
}
TEST_F(MeshGeometryAlignmentTest, IndependentChannelResizePreservesExistingUVsAndZerosNewOnes) {
    RCKMesh mesh(Context); RCKMaterial material(Context); Payload(mesh, 2);
    ASSERT_EQ(0, mesh.AddChannel(&material, FALSE)); mesh.SetVertexTextureCoordinates(1, 7, 8, 0);
    ASSERT_TRUE(mesh.SetVertexCount(5)); ExpectUV(mesh, 1, 0, 7, 8); ExpectUV(mesh, 4, 0, 0, 0);
    ASSERT_TRUE(mesh.SetVertexCount(2)); ExpectUV(mesh, 1, 0, 7, 8);
}
TEST_F(MeshGeometryAlignmentTest, ExplicitWeightsGrowWithOneAndClearToAbsent) {
    RCKMesh mesh(Context); mesh.SetVertexWeightsCount(2); mesh.SetVertexWeight(1, 0.375f);
    mesh.SetVertexWeightsCount(4); EXPECT_FLOAT_EQ(0.375f, mesh.GetVertexWeight(1));
    EXPECT_FLOAT_EQ(1.0f, mesh.GetVertexWeight(2)); EXPECT_FLOAT_EQ(1.0f, mesh.GetVertexWeight(3));
    mesh.SetVertexWeightsCount(0); EXPECT_EQ(0, mesh.GetVertexWeightsCount()); EXPECT_EQ(nullptr, mesh.GetVertexWeightsPtr());
}
TEST_F(MeshGeometryAlignmentTest, BoundingVolumesFollowMovedVertices) {
    RCKMesh mesh(Context); ASSERT_TRUE(mesh.SetVertexCount(2)); Position(mesh, 0, -2, 0, 0); Position(mesh, 1, 2, 0, 0);
    EXPECT_FLOAT_EQ(2, mesh.GetRadius()); Position(mesh, 1, 6, 0, 0);
    VxVector center; mesh.GetBaryCenter(&center); Vec(center, 2, 0, 0);
    EXPECT_FLOAT_EQ(4, mesh.GetRadius()); Vec(mesh.GetLocalBox().Min, -2, 0, 0); Vec(mesh.GetLocalBox().Max, 6, 0, 0);
}
TEST_F(MeshGeometryAlignmentTest, CleanWithoutVerticesInvalidatesCachedBounds) {
    RCKMesh mesh(Context); ASSERT_TRUE(mesh.SetVertexCount(2)); Position(mesh, 0, -3, 0, 0); Position(mesh, 1, 3, 0, 0);
    EXPECT_FLOAT_EQ(3, mesh.GetRadius()); mesh.Clean(FALSE);
    EXPECT_EQ(0, mesh.GetVertexCount()); EXPECT_FLOAT_EQ(0, mesh.GetRadius());
    Vec(mesh.GetLocalBox().Min, 0, 0, 0); Vec(mesh.GetLocalBox().Max, 0, 0, 0);
    VxVector center; mesh.GetBaryCenter(&center); Vec(center, 0, 0, 0);
}
TEST_F(MeshGeometryAlignmentTest, CleanKeepingVerticesRetainsPayloadAndClearsPrimitiveState) {
    RCKMesh mesh(Context); RCKMaterial material(Context); Payload(mesh, 3);
    ASSERT_TRUE(mesh.SetFaceCount(1)); mesh.SetFaceVertexIndex(0, 0, 1, 2); mesh.SetLineCount(1); mesh.SetLine(0, 0, 1);
    ASSERT_EQ(0, mesh.AddChannel(&material, TRUE)); mesh.Clean(TRUE);
    EXPECT_EQ(3, mesh.GetVertexCount()); EXPECT_EQ(0, mesh.GetFaceCount()); EXPECT_EQ(0, mesh.GetLineCount());
    EXPECT_EQ(0, mesh.GetChannelCount()); ExpectedPayload(mesh, 2, 2);
}
TEST_F(MeshGeometryAlignmentTest, DissociationDuplicatesCompleteVertexPayload) {
    RCKMesh mesh(Context); Payload(mesh, 4); mesh.SetFaceCount(2);
    mesh.SetFaceVertexIndex(0, 0, 1, 2); mesh.SetFaceVertexIndex(1, 0, 2, 3); mesh.DissociateAllFaces();
    ASSERT_EQ(6, mesh.GetVertexCount()); ASSERT_EQ(2, mesh.GetFaceCount());
    const int source[] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i) { EXPECT_EQ(i, mesh.GetFacesIndices()[i]); ExpectedPayload(mesh, i, source[i]); }
}
TEST_F(MeshGeometryAlignmentTest, DissociationPreservesHighSourceIndicesWhenOutputShrinks) {
    RCKMesh mesh(Context); Payload(mesh, 8); mesh.SetFaceCount(1); mesh.SetFaceVertexIndex(0, 5, 6, 7);
    mesh.DissociateAllFaces(); ASSERT_EQ(1, mesh.GetFaceCount()); ASSERT_EQ(3, mesh.GetVertexCount());
    for (int i = 0; i < 3; ++i) { EXPECT_EQ(i, mesh.GetFacesIndices()[i]); ExpectedPayload(mesh, i, i + 5); }
}
TEST_F(MeshGeometryAlignmentTest, DissociationDuplicatesIndependentChannelCoordinates) {
    RCKMesh mesh(Context); RCKMaterial material(Context); Payload(mesh, 4); mesh.AddChannel(&material, FALSE);
    for (int i = 0; i < 4; ++i) mesh.SetVertexTextureCoordinates(i, i + 10.25f, i + 20.5f, 0);
    mesh.SetFaceCount(2); mesh.SetFaceVertexIndex(0, 0, 1, 2); mesh.SetFaceVertexIndex(1, 0, 2, 3); mesh.DissociateAllFaces();
    const int source[] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i) ExpectUV(mesh, i, 0, source[i] + 10.25f, source[i] + 20.5f);
}
TEST_F(MeshGeometryAlignmentTest, DissociationRetainsFaceMaterialAndChannelMasks) {
    RCKMesh mesh(Context); RCKMaterial material(Context); Payload(mesh, 4); mesh.SetFaceCount(2);
    mesh.SetFaceVertexIndex(0, 0, 1, 2); mesh.SetFaceVertexIndex(1, 0, 2, 3);
    mesh.SetFaceMaterial(1, &material); mesh.SetFaceChannelMask(0, 2); mesh.SetFaceChannelMask(1, 9); mesh.DissociateAllFaces();
    EXPECT_EQ(2, mesh.GetFaceChannelMask(0)); EXPECT_EQ(9, mesh.GetFaceChannelMask(1)); EXPECT_EQ(&material, mesh.GetFaceMaterial(1));
}
TEST_F(MeshGeometryAlignmentTest, ConsolidateRemovesAllUnusedVerticesAcrossMultipleHoles) {
    RCKMesh mesh(Context); Payload(mesh, 9); mesh.SetFaceCount(1); mesh.SetFaceVertexIndex(0, 5, 7, 8); mesh.BuildFaceNormals();
    mesh.Consolidate(); ASSERT_EQ(3, mesh.GetVertexCount()); ASSERT_EQ(1, mesh.GetFaceCount());
    const int source[] = {5, 7, 8};
    for (int i = 0; i < 3; ++i) {
        const int index = mesh.GetFacesIndices()[i]; ASSERT_LT(index, 3);
        ExpectPosition(mesh, index, source[i], source[i] % 2, 0);
    }
}
TEST_F(MeshGeometryAlignmentTest, ConsolidateRemapsWeightsColorsAndIndependentUVsTogether) {
    RCKMesh mesh(Context); RCKMaterial material(Context); Payload(mesh, 6); mesh.AddChannel(&material, FALSE);
    for (int i = 0; i < 6; ++i) mesh.SetVertexTextureCoordinates(i, i + 10, i + 20, 0);
    mesh.SetFaceCount(1); mesh.SetFaceVertexIndex(0, 3, 4, 5); mesh.BuildFaceNormals(); mesh.Consolidate();
    ASSERT_EQ(3, mesh.GetVertexCount()); ASSERT_EQ(3, mesh.GetVertexWeightsCount());
    const int source[] = {3, 4, 5};
    for (int i = 0; i < 3; ++i) {
        const int index = mesh.GetFacesIndices()[i]; ExpectedPayload(mesh, index, source[i]);
        ExpectUV(mesh, index, 0, source[i] + 10, source[i] + 20);
    }
}
TEST_F(MeshGeometryAlignmentTest, ConsolidatePreservesLineOnlyVerticesAndRemapsLineIndices) {
    RCKMesh mesh(Context); Payload(mesh, 7); mesh.SetLineCount(1); mesh.SetLine(0, 4, 6);
    mesh.Consolidate(); ASSERT_EQ(2, mesh.GetVertexCount()); ASSERT_EQ(1, mesh.GetLineCount());
    int a, b; mesh.GetLine(0, &a, &b); ASSERT_GE(a, 0); ASSERT_LT(a, 2); ASSERT_GE(b, 0); ASSERT_LT(b, 2);
    ExpectPosition(mesh, a, 4, 0, 0); ExpectPosition(mesh, b, 6, 0, 0);
}
TEST_F(MeshGeometryAlignmentTest, ConsolidateRefreshesBoundsAfterRemovingFarIsolatedVertex) {
    RCKMesh mesh(Context); mesh.SetVertexCount(4); Position(mesh, 0, 0, 0, 0); Position(mesh, 1, 2, 0, 0);
    Position(mesh, 2, 0, 2, 0); Position(mesh, 3, 100, 0, 0); mesh.SetFaceCount(1); mesh.SetFaceVertexIndex(0, 0, 1, 2);
    mesh.BuildFaceNormals(); EXPECT_FLOAT_EQ(100, mesh.GetLocalBox().Max.x); mesh.Consolidate();
    EXPECT_FLOAT_EQ(2, mesh.GetLocalBox().Max.x); VxVector center; mesh.GetBaryCenter(&center); Vec(center, 2.0f / 3.0f, 2.0f / 3.0f, 0);
}
TEST_F(MeshGeometryAlignmentTest, ConsolidateRemovesDegenerateFaceWithoutLosingRetainedFaceAttributes) {
    RCKMesh mesh(Context); RCKMaterial material(Context); mesh.SetVertexCount(4);
    Position(mesh, 0, 0, 0, 0); Position(mesh, 1, 1, 0, 0); Position(mesh, 2, 0, 1, 0); Position(mesh, 3, 5, 5, 0);
    mesh.SetFaceCount(2); mesh.SetFaceVertexIndex(0, 0, 0, 3); mesh.SetFaceVertexIndex(1, 0, 1, 2);
    mesh.SetFaceMaterial(1, &material); mesh.SetFaceChannelMask(1, 7); mesh.BuildFaceNormals(); mesh.Consolidate();
    ASSERT_EQ(1, mesh.GetFaceCount()); EXPECT_EQ(&material, mesh.GetFaceMaterial(0)); EXPECT_EQ(7, mesh.GetFaceChannelMask(0));
}
TEST_F(MeshGeometryAlignmentTest, InverseWindingSwapsCornersAndRebuildsNormals) {
    RCKMesh mesh(Context); mesh.SetVertexCount(3); Position(mesh, 0, 0, 0, 0); Position(mesh, 1, 1, 0, 0); Position(mesh, 2, 0, 1, 0);
    mesh.SetFaceCount(1); mesh.SetFaceVertexIndex(0, 0, 1, 2); mesh.BuildNormals(); mesh.InverseWinding();
    EXPECT_EQ(0, mesh.GetFacesIndices()[0]); EXPECT_EQ(2, mesh.GetFacesIndices()[1]); EXPECT_EQ(1, mesh.GetFacesIndices()[2]);
    Vec(mesh.GetFaceNormal(0), 0, 0, -1); VxVector normal; mesh.GetVertexNormal(1, &normal); Vec(normal, 0, 0, -1);
}
TEST_F(MeshGeometryAlignmentTest, TranslationAndPivotScalingTransformPositions) {
    RCKMesh mesh(Context); mesh.SetVertexCount(1); Position(mesh, 0, 1, 2, 3);
    VxVector translation(3, 4, 5); mesh.TranslateVertices(&translation); ExpectPosition(mesh, 0, 4, 6, 8);
    VxVector scale(2, 3, 4), pivot(1, 1, 1); mesh.ScaleVertices(&scale, &pivot); ExpectPosition(mesh, 0, 7, 16, 29);
}
TEST_F(MeshGeometryAlignmentTest, AxisRotationUsesModifierVerticesAndInvalidatesBounds) {
    RCKMesh mesh(Context); mesh.SetVertexCount(1); Position(mesh, 0, 1, 0, 0); mesh.GetLocalBox();
    VxVector axis(0, 0, 1); mesh.RotateVertices(&axis, 1.5707963267948966f);
    // Engine row-vector axis convention rotates +X toward -Y for a positive Z angle.
    ExpectPosition(mesh, 0, 0, -1, 0); Vec(mesh.GetLocalBox().Max, 0, -1, 0);
}
TEST_F(MeshGeometryAlignmentTest, ChannelCreationCopiesBaseUVAndRejectsDuplicateMaterial) {
    RCKMesh mesh(Context); RCKMaterial material(Context); Payload(mesh, 3);
    EXPECT_EQ(0, mesh.AddChannel(&material, TRUE)); EXPECT_EQ(0, mesh.AddChannel(&material, FALSE)); EXPECT_EQ(1, mesh.GetChannelCount());
    ExpectUV(mesh, 2, 0, 2.25f, 2.75f); EXPECT_EQ(&material, mesh.GetChannelMaterial(0)); EXPECT_EQ(0, mesh.GetChannelByMaterial(&material));
}
TEST_F(MeshGeometryAlignmentTest, ChannelRemovalShiftsFaceMembershipAndKeepsRemainingUVs) {
    RCKMesh mesh(Context); RCKMaterial a(Context), b(Context), c(Context); Payload(mesh, 3); mesh.SetFaceCount(1);
    mesh.AddChannel(&a, FALSE); mesh.AddChannel(&b, FALSE); mesh.AddChannel(&c, FALSE);
    mesh.SetVertexTextureCoordinates(2, 7, 8, 2); mesh.SetFaceChannelMask(0, 5); mesh.RemoveChannel(1);
    EXPECT_EQ(2, mesh.GetChannelCount()); EXPECT_EQ(3, mesh.GetFaceChannelMask(0)); EXPECT_EQ(&c, mesh.GetChannelMaterial(1));
    ExpectUV(mesh, 2, 1, 7, 8); mesh.RemoveChannel(&a); EXPECT_EQ(1, mesh.GetChannelCount()); EXPECT_EQ(1, mesh.GetFaceChannelMask(0));
}
TEST_F(MeshGeometryAlignmentTest, ChannelModesBlendAndMaterialRemainIndependent) {
    RCKMesh mesh(Context); RCKMaterial a(Context), b(Context); Payload(mesh, 1); mesh.AddChannel(&a, FALSE);
    mesh.ActivateChannel(0, FALSE); EXPECT_FALSE(mesh.IsChannelActive(0)); mesh.ActivateAllChannels(TRUE); EXPECT_TRUE(mesh.IsChannelActive(0));
    mesh.LitChannel(0, FALSE); EXPECT_FALSE(mesh.IsChannelLit(0)); mesh.LitChannel(0, TRUE); EXPECT_TRUE(mesh.IsChannelLit(0));
    mesh.SetChannelSourceBlend(0, VXBLEND_SRCALPHA); mesh.SetChannelDestBlend(0, VXBLEND_INVSRCALPHA); mesh.SetChannelMaterial(0, &b);
    EXPECT_EQ(VXBLEND_SRCALPHA, mesh.GetChannelSourceBlend(0)); EXPECT_EQ(VXBLEND_INVSRCALPHA, mesh.GetChannelDestBlend(0)); EXPECT_EQ(&b, mesh.GetChannelMaterial(0));
}
TEST_F(MeshGeometryAlignmentTest, SameUVChannelPointerAliasesBaseUVAndPrivateModeStartsZeroed) {
    RCKMesh mesh(Context); RCKMaterial material(Context); Payload(mesh, 2); mesh.AddChannel(&material, FALSE);
    mesh.SetChannelFlags(0, mesh.GetChannelFlags(0) | VXCHANNEL_SAMEUV); CKDWORD strideA, strideB;
    EXPECT_EQ(mesh.GetTextureCoordinatesPtr(&strideA, -1), mesh.GetTextureCoordinatesPtr(&strideB, 0)); EXPECT_EQ(strideA, strideB);
    mesh.SetChannelFlags(0, mesh.GetChannelFlags(0) & ~VXCHANNEL_SAMEUV); ExpectUV(mesh, 1, 0, 0, 0);
}
TEST_F(MeshGeometryAlignmentTest, ChannelMaskUpdatesAndGlobalMaterialPreserveGeometry) {
    RCKMesh mesh(Context); RCKMaterial material(Context); Payload(mesh, 3); mesh.SetFaceCount(1); mesh.SetFaceVertexIndex(0, 0, 1, 2);
    mesh.SetFaceChannelMask(0, 5); mesh.ChangeFaceChannelMask(0, 2, 4); EXPECT_EQ(3, mesh.GetFaceChannelMask(0));
    mesh.ApplyGlobalMaterial(&material); EXPECT_EQ(&material, mesh.GetFaceMaterial(0)); ExpectedPayload(mesh, 2, 2);
}
TEST_F(MeshGeometryAlignmentTest, LineStripBuildsConsecutiveEdgesAndGrowthZerosNewEntries) {
    RCKMesh mesh(Context); mesh.SetVertexCount(6); mesh.SetLineCount(3); mesh.CreateLineStrip(0, 3, 2);
    for (int i = 0; i < 3; ++i) { int a, b; mesh.GetLine(i, &a, &b); EXPECT_EQ(i + 2, a); EXPECT_EQ(i + 3, b); }
    mesh.SetLineCount(4); int a, b; mesh.GetLine(3, &a, &b); EXPECT_EQ(0, a); EXPECT_EQ(0, b);
}
TEST_F(MeshGeometryAlignmentTest, ModifierAndRawPointersExposeDocumentedStrideAndPayload) {
    RCKMesh mesh(Context); Payload(mesh, 2); CKDWORD stride = 0;
    EXPECT_EQ(mesh.GetPositionsPtr(&stride), mesh.GetModifierVertices(&stride)); EXPECT_EQ(sizeof(VxVertex), stride);
    EXPECT_EQ(2, mesh.GetModifierVertexCount()); EXPECT_EQ(2, mesh.GetModifierUVCount(-1));
    EXPECT_EQ(mesh.GetTextureCoordinatesPtr(&stride, -1), mesh.GetModifierUVs(&stride, -1));
    VxVector *normal = reinterpret_cast<VxVector *>(mesh.GetNormalsPtr(&stride)); ASSERT_NE(nullptr, normal); Vec(*normal, 0, 0, 1);
    EXPECT_EQ(sizeof(VxVertex), stride); CKDWORD *color = reinterpret_cast<CKDWORD *>(mesh.GetColorsPtr(&stride));
    ASSERT_NE(nullptr, color); EXPECT_EQ(0xff123400u, color[0]); EXPECT_EQ(sizeof(VxColors), stride);
    CKDWORD *specular = reinterpret_cast<CKDWORD *>(mesh.GetSpecularColorsPtr(&stride)); ASSERT_NE(nullptr, specular); EXPECT_EQ(0x10203000u, specular[0]);
}
TEST_F(MeshGeometryAlignmentTest, VertexNotificationsInvalidateOnlyTheirDocumentedCaches) {
    MeshGeometryProbe mesh(Context); Payload(mesh, 1);
    mesh.CacheFlags(VXMESH_BOUNDINGUPTODATE | VXMESH_PROCEDURALPOS); mesh.VertexMove(); EXPECT_FALSE(mesh.GeometryValid());
    EXPECT_EQ(0u, mesh.GetFlags() & (VXMESH_BOUNDINGUPTODATE | VXMESH_PROCEDURALPOS)); EXPECT_NE(0u, mesh.GetFlags() & VXMESH_POS_CHANGED);
    mesh.CacheFlags(VXMESH_PROCEDURALUV); mesh.ModifierUVMove(); EXPECT_EQ(0u, mesh.GetFlags() & VXMESH_PROCEDURALUV);
    mesh.CacheFlags(VXMESH_GENNORMALS); mesh.NormalChanged(); EXPECT_EQ(0u, mesh.GetFlags() & VXMESH_GENNORMALS);
    mesh.CacheFlags(VXMESH_OPTIMIZED | VXMESH_TRANSPARENCYUPTODATE); mesh.UnOptimize();
    EXPECT_EQ(0u, mesh.GetFlags() & (VXMESH_OPTIMIZED | VXMESH_TRANSPARENCYUPTODATE));
}
TEST_F(MeshGeometryAlignmentTest, ConsolidateHandlesPartialExplicitWeightStreamWithoutInventingWeights) {
    RCKMesh mesh(Context); Payload(mesh, 8); mesh.SetVertexWeightsCount(2); mesh.SetVertexWeight(0, 0.25f);
    mesh.SetFaceCount(1); mesh.SetFaceVertexIndex(0, 4, 6, 7); mesh.BuildFaceNormals(); mesh.Consolidate();
    ASSERT_EQ(3, mesh.GetVertexCount()); ASSERT_EQ(3, mesh.GetVertexWeightsCount());
    for (int i = 0; i < 3; ++i) EXPECT_FLOAT_EQ(0, mesh.GetVertexWeight(i));
}
TEST_F(MeshGeometryAlignmentTest, ConsolidateDiscardsProgressiveMapAfterVertexRenumbering) {
    MeshGeometryProbe mesh(Context); Payload(mesh, 6); mesh.SetFaceCount(1); mesh.SetFaceVertexIndex(0, 3, 4, 5);
    mesh.BuildFaceNormals(); mesh.AttachProgressiveMap(); ASSERT_TRUE(mesh.IsPM()); mesh.Consolidate();
    EXPECT_FALSE(mesh.IsPM()); EXPECT_EQ(3, mesh.GetVertexCount());
}
TEST_F(MeshGeometryAlignmentTest, DissociationDiscardsProgressiveMapAfterVertexDuplication) {
    MeshGeometryProbe mesh(Context); Payload(mesh, 4); mesh.SetFaceCount(2);
    mesh.SetFaceVertexIndex(0, 0, 1, 2); mesh.SetFaceVertexIndex(1, 0, 2, 3); mesh.AttachProgressiveMap();
    ASSERT_TRUE(mesh.IsPM()); mesh.DissociateAllFaces(); EXPECT_FALSE(mesh.IsPM()); EXPECT_EQ(6, mesh.GetVertexCount());
}
TEST_F(MeshGeometryAlignmentTest, NonuniformScalingKeepsNormalsPerpendicularToTransformedSurface) {
    RCKMesh mesh(Context); mesh.SetVertexCount(3); Position(mesh, 0, 0, 0, 0); Position(mesh, 1, 1, 0, 1); Position(mesh, 2, 0, 1, 0);
    mesh.SetFaceCount(1); mesh.SetFaceVertexIndex(0, 0, 1, 2); mesh.BuildNormals(); mesh.ScaleVertices3f(2, 1, 1);
    VxVector normal; mesh.GetVertexNormal(0, &normal);
    EXPECT_NEAR(0, 2.0f * normal.x + normal.z, 0.0001f); EXPECT_NEAR(0, normal.y, 0.0001f);
    EXPECT_NEAR(1.0f, normal.SquareMagnitude(), 0.0001f);
    const VxVector &face = mesh.GetFaceNormal(0);
    EXPECT_NEAR(face.x, normal.x, 0.0001f); EXPECT_NEAR(face.z, normal.z, 0.0001f);
}
TEST_F(MeshGeometryAlignmentTest, ReflectedScalingKeepsVertexNormalsConsistentWithTriangleWinding) {
    RCKMesh mesh(Context); mesh.SetVertexCount(3); Position(mesh, 0, 0, 0, 0); Position(mesh, 1, 1, 0, 0); Position(mesh, 2, 0, 1, 0);
    mesh.SetFaceCount(1); mesh.SetFaceVertexIndex(0, 0, 1, 2); mesh.BuildNormals(); mesh.ScaleVertices3f(-1, 1, 1);
    Vec(mesh.GetFaceNormal(0), 0, 0, -1); VxVector normal; mesh.GetVertexNormal(0, &normal); Vec(normal, 0, 0, -1);
}
TEST_F(MeshGeometryAlignmentTest, RemoveChannelByMaterialRemovesEveryDuplicateAndShiftsMembership) {
    RCKMesh mesh(Context); RCKMaterial a(Context), b(Context), c(Context); Payload(mesh, 3); mesh.SetFaceCount(2);
    mesh.AddChannel(&a, FALSE); mesh.AddChannel(&b, FALSE); mesh.AddChannel(&c, FALSE); mesh.SetChannelMaterial(2, &a);
    mesh.SetVertexTextureCoordinates(1, 7, 8, 1); mesh.SetFaceChannelMask(0, 5); mesh.SetFaceChannelMask(1, 3);
    mesh.RemoveChannelByMaterial(&a); ASSERT_EQ(1, mesh.GetChannelCount()); EXPECT_EQ(&b, mesh.GetChannelMaterial(0));
    EXPECT_EQ(-1, mesh.GetChannelByMaterial(&a)); EXPECT_EQ(0, mesh.GetFaceChannelMask(0)); EXPECT_EQ(1, mesh.GetFaceChannelMask(1));
    ExpectUV(mesh, 1, 0, 7, 8);
}
TEST_F(MeshGeometryAlignmentTest, VertexGrowthInitializesFromActualPartialWeightCount) {
    RCKMesh mesh(Context); ASSERT_TRUE(mesh.SetVertexCount(8)); mesh.SetVertexWeightsCount(2);
    mesh.SetVertexWeight(0, 0.25f); mesh.SetVertexWeight(1, 0.75f); ASSERT_TRUE(mesh.SetVertexCount(10));
    ASSERT_EQ(10, mesh.GetVertexWeightsCount()); EXPECT_FLOAT_EQ(0.25f, mesh.GetVertexWeight(0)); EXPECT_FLOAT_EQ(0.75f, mesh.GetVertexWeight(1));
    for (int i = 2; i < 10; ++i) EXPECT_FLOAT_EQ(0, mesh.GetVertexWeight(i));
}
