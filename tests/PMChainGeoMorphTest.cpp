#include <gtest/gtest.h>
#include "../src/CKMesh.cpp"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

static const CKDWORD PMChainParents[] = {0, 0, 1, 1, 2, 3, 4, 2, 6, 5, 8, 10};

// Literal quotient maps for the branching tree above, independent of the
// traversal implementation. Row zero is the native empty-LOD result.
static const int PMChainExpected[13][12] = {
    {0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0},
    {0,1,1,1,1,1,1,1,1,1,1,1},
    {0,1,2,1,2,1,2,2,2,1,2,2},
    {0,1,2,3,2,3,2,2,2,3,2,2},
    {0,1,2,3,4,3,4,2,4,3,4,4},
    {0,1,2,3,4,5,4,2,4,5,4,4},
    {0,1,2,3,4,5,6,2,6,5,6,6},
    {0,1,2,3,4,5,6,7,6,5,6,6},
    {0,1,2,3,4,5,6,7,8,5,8,8},
    {0,1,2,3,4,5,6,7,8,9,8,8},
    {0,1,2,3,4,5,6,7,8,9,10,10},
    {0,1,2,3,4,5,6,7,8,9,10,11}
};

static void SetPMChain(CKProgressiveMesh &pm) {
    pm.m_Data.Resize(12);
    for (int i = 0; i < 12; ++i) pm.m_Data[i] = PMChainParents[i];
}

static void ExpectPMVector(const VxVector &actual, float x, float y, float z) {
    EXPECT_FLOAT_EQ(x, actual.x); EXPECT_FLOAT_EQ(y, actual.y); EXPECT_FLOAT_EQ(z, actual.z);
}

static void ExpectPMVertex(const VxVertex &actual, float x, float y, float z,
                           float nx, float ny, float nz, float u, float v) {
    ExpectPMVector(actual.m_Position, x, y, z);
    ExpectPMVector(actual.m_Normal, nx, ny, nz);
    EXPECT_FLOAT_EQ(u, actual.m_UV.x); EXPECT_FLOAT_EQ(v, actual.m_UV.y);
}

static VxVertex PMValueFrom() {
    VxVertex value;
    value.m_Position = VxVector(2, -4, 8);
    value.m_Normal = VxVector(0, 4, 0);
    value.m_UV = Vx2DVector(-2, 6);
    return value;
}

static VxVertex PMValueTo() {
    VxVertex value;
    value.m_Position = VxVector(10, 12, -8);
    value.m_Normal = VxVector(0, 0, 8);
    value.m_UV = Vx2DVector(6, -2);
    return value;
}

class PMChainMesh : public RCKMesh {
public:
    explicit PMChainMesh(CKContext *context) : RCKMesh(context) {}
    void Configure(int count, CKBOOL morph, int step) {
        if (!m_ProgressiveMesh) m_ProgressiveMesh = new CKProgressiveMesh();
        SetPMChain(*m_ProgressiveMesh);
        m_ProgressiveMesh->m_VertexCount = count;
        m_ProgressiveMesh->m_MorphEnabled = morph;
        m_ProgressiveMesh->m_MorphStep = step;
        ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
    }
    CKProgressiveMesh &PM() { return *m_ProgressiveMesh; }
    const VxVertex &Source(int i) const { return m_Vertices[i]; }
    const VxVertex &Scratch(int i) const {
        return reinterpret_cast<const VxVertex *>(m_ProgressiveMesh->m_EdgeCollapseData.Begin())[i];
    }
    CKMaterialGroup *Group(CKMaterial *material) {
        for (int i = 0; i < m_MaterialGroups.Size(); ++i)
            if (m_MaterialGroups[i]->m_Material == material) return m_MaterialGroups[i];
        return nullptr;
    }
};

class PMChainGeoMorphTest : public ::testing::Test {
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
    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static void Geometry(PMChainMesh &mesh, RCKMaterial *material = nullptr) {
        ASSERT_TRUE(mesh.SetVertexCount(12)); ASSERT_TRUE(mesh.SetFaceCount(4));
        for (int i = 0; i < 12; ++i) {
            VxVector position(static_cast<float>(i * i), 8.0f * i, 3.0f - i);
            VxVector normal(1.0f + i, 2.0f * i, 4.0f - i);
            mesh.SetVertexPosition(i, &position); mesh.SetVertexNormal(i, &normal);
            mesh.SetVertexTextureCoordinates(i, .25f * i, -.5f * i, -1);
        }
        // Surviving faces precede the collapsed tail, matching native PM order.
        mesh.SetFaceVertexIndex(0, 0, 1, 11); mesh.SetFaceVertexIndex(1, 1, 7, 9);
        mesh.SetFaceVertexIndex(2, 10, 8, 6); mesh.SetFaceVertexIndex(3, 2, 9, 5);
        mesh.SetFaceMaterial(0, material); mesh.SetFaceMaterial(2, material);
        mesh.SetFlags(mesh.GetFlags() & ~(VXMESH_STRIPIFY | VXMESH_PM_BUILDNORM));
        mesh.CreateRenderGroups();
    }
    static void ExpectGroup(PMChainMesh &mesh, CKMaterial *material, const CKWORD *indices,
                             int indexCount, const int *ordinals, int faceCount) {
        CKMaterialGroup *group = mesh.Group(material); ASSERT_NE(nullptr, group);
        ASSERT_GE(group->m_Primitives.Size(), 1);
        ASSERT_EQ(indexCount, group->m_Primitives[0].m_Indices.Size());
        ASSERT_EQ(faceCount, group->m_FaceIndices.Size());
        for (int i = 0; i < indexCount; ++i) EXPECT_EQ(indices[i], group->m_Primitives[0].m_Indices[i]);
        for (int i = 0; i < faceCount; ++i) EXPECT_EQ(ordinals[i], group->m_FaceIndices[i]);
        EXPECT_EQ(mesh.PM().m_Reserved, group->m_VertexCount);
    }
};

TEST_F(PMChainGeoMorphTest, CurrentCutoffMatchesEveryLiteralBranchingTreeQuotient) {
    CKProgressiveMesh pm; SetPMChain(pm);
    for (int count = 0; count <= 12; ++count) {
        SCOPED_TRACE(count); pm.m_VertexCount = count;
        for (int vertex = 0; vertex < 12; ++vertex) {
            SCOPED_TRACE(vertex);
            EXPECT_EQ(PMChainExpected[count][vertex], PMGetParentVertex(&pm, vertex));
        }
    }
}

TEST_F(PMChainGeoMorphTest, ExplicitCutoffIgnoresStoredLODAndMatchesTheLiteralQuotients) {
    CKProgressiveMesh pm; SetPMChain(pm);
    for (int count = 0; count <= 12; ++count) {
        SCOPED_TRACE(count); pm.m_VertexCount = 12 - count;
        for (int vertex = 0; vertex < 12; ++vertex) {
            SCOPED_TRACE(vertex);
            EXPECT_EQ(PMChainExpected[count][vertex], PMGetParentVertexEx(&pm, vertex, count));
        }
    }
}

TEST_F(PMChainGeoMorphTest, BothTraversalsReachTheLastRetainedVertexThroughALongChain) {
    CKProgressiveMesh pm; pm.m_Data.Resize(1024); pm.m_Data[0] = 0;
    for (int i = 1; i < 1024; ++i) pm.m_Data[i] = i - 1;
    const int cutoffs[] = {1, 7, 511, 1024};
    for (int count : cutoffs) {
        SCOPED_TRACE(count); pm.m_VertexCount = count;
        for (int i = 0; i < 1024; ++i) {
            const int expected = i < count ? i : count - 1;
            EXPECT_EQ(expected, PMGetParentVertex(&pm, i));
            EXPECT_EQ(expected, PMGetParentVertexEx(&pm, i, count));
        }
    }
}

TEST_F(PMChainGeoMorphTest, NonpositiveCountsReturnZeroWithoutReadingAParentTable) {
    CKProgressiveMesh pm;
    const int counts[] = {0, -1, -7};
    for (int count : counts) {
        pm.m_VertexCount = count;
        EXPECT_EQ(0, PMGetParentVertex(&pm, 65535));
        EXPECT_EQ(0, PMGetParentVertexEx(&pm, 65535, count));
    }
}

TEST_F(PMChainGeoMorphTest, RetainedIndicesDoNotRequireParentEntries) {
    CKProgressiveMesh pm; pm.m_VertexCount = 5;
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(i, PMGetParentVertex(&pm, i));
        EXPECT_EQ(i, PMGetParentVertexEx(&pm, i, 5));
    }
}

TEST_F(PMChainGeoMorphTest, VectorInterpolationMatchesAnalyticEndpointsInteriorAndExtrapolation) {
    VxVector from(2, -4, 8), to(10, 12, -8), result;
    const float factors[] = {0, .25f, .5f, 1, -.5f, 1.5f};
    const VxVector expected[] = {VxVector(2,-4,8), VxVector(4,0,4), VxVector(6,4,0),
        VxVector(10,12,-8), VxVector(-2,-12,16), VxVector(14,20,-16)};
    for (int i = 0; i < 6; ++i) {
        SCOPED_TRACE(factors[i]); VxVectorLerp(&result, factors[i], &from, &to);
        ExpectPMVector(result, expected[i].x, expected[i].y, expected[i].z);
    }
    ExpectPMVector(from, 2, -4, 8); ExpectPMVector(to, 10, 12, -8);
}

TEST_F(PMChainGeoMorphTest, VectorInterpolationCanWriteEitherInput) {
    VxVector from(2, -4, 8), to(10, 12, -8);
    VxVectorLerp(&from, .25f, &from, &to); ExpectPMVector(from, 4, 0, 4); ExpectPMVector(to, 10, 12, -8);
    from = VxVector(2, -4, 8);
    VxVectorLerp(&to, .25f, &from, &to); ExpectPMVector(to, 4, 0, 4); ExpectPMVector(from, 2, -4, 8);
}

TEST_F(PMChainGeoMorphTest, VertexInterpolationPreservesAllFieldsWithoutNormalizationOrClamping) {
    VxVertex from = PMValueFrom(), to = PMValueTo(), result;
    PMInterpolateGeoMorphVertex(&result, .25f, &from, &to);
    ExpectPMVertex(result, 4,0,4, 0,3,2, 0,4);
    PMInterpolateGeoMorphVertex(&result, -.5f, &from, &to);
    ExpectPMVertex(result, -2,-12,16, 0,6,-4, -6,10);
    PMInterpolateGeoMorphVertex(&result, 1.5f, &from, &to);
    ExpectPMVertex(result, 14,20,-16, 0,-2,12, 10,-6);
    PMInterpolateGeoMorphVertex(&result, 0, &from, &to);
    ExpectPMVertex(result, 2,-4,8, 0,4,0, -2,6);
    PMInterpolateGeoMorphVertex(&result, 1, &from, &to);
    ExpectPMVertex(result, 10,12,-8, 0,0,8, 6,-2);
}

TEST_F(PMChainGeoMorphTest, VertexInterpolationSupportsAliasingAndKeepsTheOtherInput) {
    VxVertex from = PMValueFrom(), to = PMValueTo();
    PMInterpolateGeoMorphVertex(&from, .25f, &from, &to);
    ExpectPMVertex(from, 4,0,4, 0,3,2, 0,4); ExpectPMVertex(to, 10,12,-8, 0,0,8, 6,-2);
    from = PMValueFrom(); PMInterpolateGeoMorphVertex(&to, .25f, &from, &to);
    ExpectPMVertex(to, 4,0,4, 0,3,2, 0,4); ExpectPMVertex(from, 2,-4,8, 0,4,0, -2,6);
}

TEST_F(PMChainGeoMorphTest, NonMorphGroupsUseMultiHopParentsAndRejectTheCollapsedTail) {
    PMChainMesh mesh(Context); RCKMaterial material(Context); Geometry(mesh, &material);
    mesh.Configure(6, FALSE, 4); mesh.BuildRenderMesh(); EXPECT_EQ(6, mesh.PM().m_Reserved);
    const CKWORD first[] = {0,1,4}, second[] = {1,2,5}; const int firstFace[] = {0}, secondFace[] = {1};
    ExpectGroup(mesh, &material, first, 3, firstFace, 1); ExpectGroup(mesh, nullptr, second, 3, secondFace, 1);
    ExpectPMVertex(mesh.Source(11), 121,88,-8, 12,22,-7, 2.75f,-5.5f);
    mesh.SetVerticesRendered(4); mesh.BuildRenderMesh();
    const CKWORD nextFirst[] = {0,1,2}, nextSecond[] = {1,2,3};
    ExpectGroup(mesh, &material, nextFirst, 3, firstFace, 1); ExpectGroup(mesh, nullptr, nextSecond, 3, secondFace, 1);
}

TEST_F(PMChainGeoMorphTest, AdditiveMorphUsesBothParentCutoffsAndAnUnsimplifiedFraction) {
    PMChainMesh mesh(Context); RCKMaterial material(Context); Geometry(mesh, &material);
    mesh.Configure(7, TRUE, 4); mesh.BuildRenderMesh(); EXPECT_EQ(8, mesh.PM().m_Reserved);
    const CKWORD first[] = {0,1,6}, second[] = {1,7,5}; const int firstFace[] = {0}, secondFace[] = {1};
    ExpectGroup(mesh, &material, first, 3, firstFace, 1); ExpectGroup(mesh, nullptr, second, 3, secondFace, 1);
    // lower=4, upper=8, t=3/4: original11 resolves to lower2 and upper6.
    ExpectPMVertex(mesh.Scratch(6), 28,40,-2, 6,10,-1, 1.25f,-2.5f);
    ExpectPMVertex(mesh.Scratch(7), 37.75f,46,-2.75f, 6.75f,11.5f,-1.75f, 1.4375f,-2.875f);
    ExpectPMVertex(mesh.Scratch(5), 21,36,-1.5f, 5.5f,9,-.5f, 1.125f,-2.25f);
    ExpectPMVertex(mesh.Source(6), 36,48,-3, 7,12,-2, 1.5f,-3);
}

TEST_F(PMChainGeoMorphTest, ExactAdditiveBoundaryUsesTheNextBracketAtFractionZero) {
    PMChainMesh mesh(Context); Geometry(mesh); mesh.Configure(8, TRUE, 4); mesh.BuildRenderMesh();
    EXPECT_EQ(12, mesh.PM().m_Reserved);
    // lower=8, upper=12, t=0: original11 follows 11->10->8->6 at lower LOD.
    ExpectPMVertex(mesh.Scratch(11), 36,48,-3, 7,12,-2, 1.5f,-3);
    ExpectPMVertex(mesh.Scratch(10), 36,48,-3, 7,12,-2, 1.5f,-3);
}

TEST_F(PMChainGeoMorphTest, ExactDichotomyBoundaryEndsThePreviousBracketAtFractionOne) {
    PMChainMesh mesh(Context); Geometry(mesh); mesh.Configure(6, TRUE, 0); mesh.BuildRenderMesh();
    EXPECT_EQ(6, mesh.PM().m_Reserved);
    // lower=3, upper=6, t=1; the target value remains authored and unnormalized.
    ExpectPMVertex(mesh.Scratch(4), 16,32,-1, 5,8,0, 1,-2);
    ExpectPMVertex(mesh.Scratch(5), 25,40,-2, 6,10,-1, 1.25f,-2.5f);
}

TEST_F(PMChainGeoMorphTest, DichotomyInteriorKeepsAHalfwayMorphInsteadOfSnappingTheLOD) {
    PMChainMesh mesh(Context); Geometry(mesh); mesh.Configure(9, TRUE, 0); mesh.BuildRenderMesh();
    EXPECT_EQ(12, mesh.PM().m_Reserved);
    // lower=6, upper=12, t=1/2: original11 resolves to lower4 and upper11.
    ExpectPMVertex(mesh.Scratch(11), 68.5f,60,-4.5f, 8.5f,15,-3.5f, 1.875f,-3.75f);
}

TEST_F(PMChainGeoMorphTest, SharedTargetPayloadIsStableAcrossFacesAndMaterialGroups) {
    PMChainMesh mesh(Context); RCKMaterial material(Context); Geometry(mesh, &material);
    mesh.SetFaceVertexIndex(1, 2, 1, 11); mesh.Configure(7, TRUE, 4); mesh.BuildRenderMesh();
    const CKWORD first[] = {0,1,6}, second[] = {2,1,6}; const int firstFace[] = {0}, secondFace[] = {1};
    ExpectGroup(mesh, &material, first, 3, firstFace, 1); ExpectGroup(mesh, nullptr, second, 3, secondFace, 1);
    ExpectPMVertex(mesh.Scratch(6), 28,40,-2, 6,10,-1, 1.25f,-2.5f);
}

TEST_F(PMChainGeoMorphTest, ExplicitRebuildUsesAuthoredPayloadInsteadOfThePreviousScratch) {
    PMChainMesh mesh(Context); Geometry(mesh); mesh.Configure(7, TRUE, 4); mesh.BuildRenderMesh();
    ExpectPMVertex(mesh.Scratch(6), 28,40,-2, 6,10,-1, 1.25f,-2.5f);
    mesh.Configure(7, TRUE, 4); mesh.BuildRenderMesh();
    ExpectPMVertex(mesh.Scratch(6), 28,40,-2, 6,10,-1, 1.25f,-2.5f);
    VxVector changed(100, 50, -10); mesh.SetVertexPosition(6, &changed);
    mesh.Configure(7, TRUE, 4); mesh.BuildRenderMesh();
    ExpectPMVertex(mesh.Scratch(6), 76,41.5f,-7.25f, 6,10,-1, 1.25f,-2.5f);
    ExpectPMVector(mesh.Source(6).m_Position, 100,50,-10);
}

TEST_F(PMChainGeoMorphTest, FullLODIsAnIdentityForEveryAuthoredField) {
    PMChainMesh mesh(Context); Geometry(mesh); mesh.Configure(12, TRUE, 0); mesh.BuildRenderMesh();
    EXPECT_EQ(12, mesh.PM().m_Reserved);
    for (int i = 0; i < 12; ++i) {
        SCOPED_TRACE(i);
        ExpectPMVertex(mesh.Scratch(i), static_cast<float>(i*i),8.0f*i,3.0f-i, 1.0f+i,2.0f*i,4.0f-i, .25f*i,-.5f*i);
    }
}
