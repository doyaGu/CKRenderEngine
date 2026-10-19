#include <gtest/gtest.h>
#include "../src/CKMesh.cpp"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

static void (*BuildMeshActualNormals)(CKFace *, CKWORD *, int, VxVertex *, int) = nullptr;
static int BuildMeshNormalCalls = 0, BuildMeshNormalFaceCount = 0, BuildMeshNormalVertexCount = 0;
static VxVertex *BuildMeshNormalVertices = nullptr;
static CKWORD BuildMeshNormalIndices[18] = {};

static void ObserveBuildMeshNormals(CKFace *faces, CKWORD *indices, int faceCount,
                                    VxVertex *vertices, int vertexCount) {
    ++BuildMeshNormalCalls; BuildMeshNormalFaceCount = faceCount;
    BuildMeshNormalVertexCount = vertexCount; BuildMeshNormalVertices = vertices;
    EXPECT_LE(faceCount * 3, 18);
    for (int i = 0; i < faceCount * 3 && i < 18; ++i) BuildMeshNormalIndices[i] = indices[i];
    // Observe the real dispatch boundary and delegate the actual computation.
    BuildMeshActualNormals(faces, indices, faceCount, vertices, vertexCount);
}

static void ExpectBuildMeshVector(const VxVector &value, float x, float y, float z) {
    EXPECT_FLOAT_EQ(x, value.x); EXPECT_FLOAT_EQ(y, value.y); EXPECT_FLOAT_EQ(z, value.z);
}

class BuildMeshProbe : public RCKMesh {
public:
    explicit BuildMeshProbe(CKContext *context) : RCKMesh(context) {}
    void Configure(int count, CKBOOL morph, int step) {
        if (!m_ProgressiveMesh) m_ProgressiveMesh = new CKProgressiveMesh();
        m_ProgressiveMesh->m_VertexCount = count; m_ProgressiveMesh->m_MorphEnabled = morph;
        m_ProgressiveMesh->m_MorphStep = step;
        const CKDWORD parents[] = {0, 0, 0, 2, 3, 4};
        m_ProgressiveMesh->m_Data.Resize(6);
        for (int i = 0; i < 6; ++i) m_ProgressiveMesh->m_Data[i] = parents[i];
        ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
    }
    CKProgressiveMesh *PM() { return m_ProgressiveMesh; }
    int Groups() const { return m_MaterialGroups.Size(); }
    CKMaterialGroup *Group(int i) { return m_MaterialGroups[i]; }
    CKMaterialGroup *FindGroup(CKMaterial *material) {
        for (int i = 0; i < m_MaterialGroups.Size(); ++i)
            if (m_MaterialGroups[i]->m_Material == material) return m_MaterialGroups[i];
        return nullptr;
    }
    void AddEmptyNullGroup() {
        CKMaterialGroup *empty = new CKMaterialGroup(nullptr);
        m_MaterialGroups.Insert(1, empty);
        for (int i = 0; i < m_Faces.Size(); ++i)
            if (m_Faces[i].m_MatIndex >= 1) ++m_Faces[i].m_MatIndex;
    }
    VxVertex *Source() { return m_Vertices.Begin(); }
    VxVertex *Scratch() { return reinterpret_cast<VxVertex *>(m_ProgressiveMesh->m_EdgeCollapseData.Begin()); }
    void SeedFaceNormals() {
        for (int i = 0; i < m_Faces.Size(); ++i) m_Faces[i].m_Normal = VxVector(100.0f+i, 200.0f+i, 300.0f+i);
    }
    void PendingNormals(CKBOOL enable) {
        if (enable) m_Flags |= VXMESH_PM_BUILDNORM;
        else m_Flags &= ~VXMESH_PM_BUILDNORM;
    }
    CKDWORD FaceUpdateMask() const { return m_FaceChannelMask; }
    CKDWORD Validity() const { return m_Valid; }
    void Validity(CKDWORD value) { m_Valid = value; }
    VxMaterialChannel &Channel(int i) { return m_MaterialChannels[i]; }
};

class BuildRenderMeshCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        BuildMeshActualNormals = g_BuildNormalsFunc; g_BuildNormalsFunc = ObserveBuildMeshNormals;
        ResetNormals();
    }
    void TearDown() override {
        g_BuildNormalsFunc = BuildMeshActualNormals;
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static void ResetNormals() {
        BuildMeshNormalCalls = BuildMeshNormalFaceCount = BuildMeshNormalVertexCount = 0;
        BuildMeshNormalVertices = nullptr;
    }
    static void Geometry(BuildMeshProbe &mesh, RCKMaterial *first = nullptr, RCKMaterial *second = nullptr) {
        ASSERT_TRUE(mesh.SetVertexCount(6)); ASSERT_TRUE(mesh.SetFaceCount(4));
        VxVector positions[] = {VxVector(0,0,0),VxVector(2,0,0),VxVector(0,4,0),
            VxVector(0,8,0),VxVector(4,4,2),VxVector(8,2,4)};
        for (int i = 0; i < 6; ++i) {
            VxVector normal = i == 3 ? VxVector(0,2,0) : VxVector(0,0,2);
            mesh.SetVertexPosition(i, &positions[i]); mesh.SetVertexNormal(i, &normal);
            mesh.SetVertexTextureCoordinates(i, .125f*i, .25f*i, -1);
        }
        mesh.SetFaceVertexIndex(0,0,1,5); mesh.SetFaceVertexIndex(1,0,4,5);
        mesh.SetFaceVertexIndex(2,0,1,4); mesh.SetFaceVertexIndex(3,0,1,3);
        mesh.SetFaceMaterial(0,first); mesh.SetFaceMaterial(1,second); mesh.SetFaceMaterial(3,first);
        mesh.SetFlags(mesh.GetFlags() & ~(VXMESH_STRIPIFY | VXMESH_PM_BUILDNORM));
        mesh.CreateRenderGroups(); mesh.SeedFaceNormals(); ResetNormals();
    }
    static void Indices(CKMaterialGroup *group, const CKWORD *expected, int count) {
        ASSERT_NE(nullptr, group); ASSERT_EQ(1, group->m_Primitives.Size());
        EXPECT_EQ(VX_TRIANGLELIST, group->m_Primitives[0].m_Type);
        ASSERT_EQ(count, group->m_Primitives[0].m_Indices.Size());
        for (int i = 0; i < count; ++i) EXPECT_EQ(expected[i], group->m_Primitives[0].m_Indices[i]);
        EXPECT_EQ(count != 0, group->m_HasValidPrimitives != 0);
    }
    static void Published(BuildMeshProbe &mesh, int count) {
        EXPECT_EQ(count, mesh.PM()->m_Reserved);
        EXPECT_NE(0u, mesh.GetObjectFlags() & CK_OBJECT_UPTODATE);
        EXPECT_NE(0u, mesh.GetFlags() & VXMESH_OPTIMIZED);
        EXPECT_NE(0u, mesh.GetFlags() & VXMESH_POS_CHANGED);
        EXPECT_EQ(0u, mesh.FaceUpdateMask());
        for (int i = 0; i < mesh.Groups(); ++i) {
            EXPECT_EQ(count, mesh.Group(i)->m_VertexCount);
            EXPECT_EQ(0u, mesh.Group(i)->m_MinVertexIndex);
            EXPECT_EQ(static_cast<CKDWORD>(count), mesh.Group(i)->m_MaxVertexIndex);
        }
    }
};

TEST_F(BuildRenderMeshCompletionTest, DirtyMeshWithoutPMDoesNotChangeGroupsOrPublishFreshness) {
    BuildMeshProbe mesh(Context); Geometry(mesh); mesh.ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
    mesh.Validity(1); const int groups = mesh.Groups(), count = mesh.Group(0)->m_Primitives[0].m_Indices.Size();
    mesh.BuildRenderMesh(); EXPECT_EQ(groups, mesh.Groups()); EXPECT_EQ(count, mesh.Group(0)->m_Primitives[0].m_Indices.Size());
    EXPECT_EQ(1u, mesh.Validity()); EXPECT_EQ(0u, mesh.GetObjectFlags() & CK_OBJECT_UPTODATE); EXPECT_EQ(0, BuildMeshNormalCalls);
}

TEST_F(BuildRenderMeshCompletionTest, FreshPMDoesNotResetGroupsScratchOrPendingNormals) {
    BuildMeshProbe mesh(Context); Geometry(mesh); mesh.Configure(3, TRUE, 2); mesh.BuildRenderMesh();
    CKMaterialGroup *group = mesh.Group(0); VxVertex *scratch = mesh.Scratch();
    const int count = group->m_Primitives[0].m_Indices.Size();
    mesh.PendingNormals(TRUE); ResetNormals(); mesh.BuildRenderMesh();
    EXPECT_EQ(group, mesh.Group(0)); EXPECT_EQ(scratch, mesh.Scratch());
    EXPECT_EQ(count, group->m_Primitives[0].m_Indices.Size()); EXPECT_EQ(0, BuildMeshNormalCalls);
    EXPECT_NE(0u, mesh.GetFlags() & VXMESH_PM_BUILDNORM);
}

TEST_F(BuildRenderMeshCompletionTest, LoadedLODCountsUseThePublicClampBeforeBuilding) {
    const int requests[] = {-7, 99};
    for (int request : requests) {
        SCOPED_TRACE(request); BuildMeshProbe mesh(Context); Geometry(mesh); mesh.Configure(request, FALSE, 0);
        mesh.BuildRenderMesh(); EXPECT_EQ(request < 0 ? 0 : 6, mesh.GetVerticesRendered());
        Published(mesh, request < 0 ? 0 : 6);
    }
}

TEST_F(BuildRenderMeshCompletionTest, EmptyMeshesPublishZeroVerticesInBothMorphModesAndStepKinds) {
    for (int morph = 0; morph < 2; ++morph) {
        for (int step = 0; step <= 3; step += 3) {
            SCOPED_TRACE(morph);
            SCOPED_TRACE(step);
            BuildMeshProbe mesh(Context); mesh.Configure(0, morph, step); mesh.PendingNormals(TRUE);
            mesh.BuildRenderMesh(); Published(mesh, 0); EXPECT_EQ(0, BuildMeshNormalCalls);
            for (int i = 0; i < mesh.Groups(); ++i) Indices(mesh.Group(i), nullptr, 0);
            EXPECT_NE(0u, mesh.GetFlags() & VXMESH_PM_BUILDNORM);
        }
    }
}

TEST_F(BuildRenderMeshCompletionTest, VerticesWithoutFacesKeepAuthoredScratchAndPendingNormals) {
    BuildMeshProbe mesh(Context); Geometry(mesh); mesh.SetFaceCount(0); mesh.Configure(3, TRUE, 2); mesh.PendingNormals(TRUE);
    mesh.BuildRenderMesh(); Published(mesh, 4); EXPECT_EQ(0, BuildMeshNormalCalls);
    EXPECT_NE(0u, mesh.GetFlags() & VXMESH_PM_BUILDNORM);
    for (int i = 0; i < 6; ++i) {
        ExpectBuildMeshVector(mesh.Scratch()[i].m_Position, mesh.Source()[i].m_Position.x,mesh.Source()[i].m_Position.y,mesh.Source()[i].m_Position.z);
        EXPECT_FLOAT_EQ(.125f*i, mesh.Scratch()[i].m_UV.x);
    }
}

TEST_F(BuildRenderMeshCompletionTest, NonDividingPositiveStepUsesTheSourceCountAnchoredBracket) {
    BuildMeshProbe mesh(Context); Geometry(mesh); mesh.Configure(4, TRUE, 4); mesh.BuildRenderMesh();
    Published(mesh, 6); // total6 -> lower2, upper6, t=1/2
    ExpectBuildMeshVector(mesh.Scratch()[5].m_Position, 4,1,2);
    EXPECT_FLOAT_EQ(.3125f, mesh.Scratch()[5].m_UV.x);
}

TEST_F(BuildRenderMeshCompletionTest, LowestNonDividingBracketUsesZeroInsteadOfANegativeLowerCount) {
    BuildMeshProbe mesh(Context); Geometry(mesh); mesh.Configure(1, TRUE, 4); mesh.BuildRenderMesh();
    Published(mesh, 4); ExpectBuildMeshVector(mesh.Scratch()[3].m_Position, 0,2,0);
    const CKWORD expected[] = {0,1,3,0,1,3,0,1,3}; Indices(mesh.Group(0), expected, 9);
}

TEST_F(BuildRenderMeshCompletionTest, HugePositivePublicStepKeepsFullLODGeometryAndFiniteScratch) {
    BuildMeshProbe mesh(Context); Geometry(mesh); mesh.Configure(6, TRUE, 2);
    mesh.SetPMGeoMorphStep(INT_MAX); EXPECT_EQ(INT_MAX, mesh.GetPMGeoMorphStep()); mesh.BuildRenderMesh();
    Published(mesh, 6);
    const CKWORD expected[] = {0,1,5,0,4,5,0,1,4,0,1,3}; Indices(mesh.Group(0), expected, 12);
    for (int i = 0; i < 6; ++i)
        ExpectBuildMeshVector(mesh.Scratch()[i].m_Position, mesh.Source()[i].m_Position.x,mesh.Source()[i].m_Position.y,mesh.Source()[i].m_Position.z);
}

TEST_F(BuildRenderMeshCompletionTest, PublicStepChangesInvalidateAndRebuildThePreparedVertexDomain) {
    BuildMeshProbe mesh(Context); Geometry(mesh); mesh.Configure(4, TRUE, 0); mesh.BuildRenderMesh(); Published(mesh, 6);
    mesh.SetPMGeoMorphStep(2); EXPECT_EQ(0u, mesh.GetObjectFlags() & CK_OBJECT_UPTODATE);
    mesh.BuildRenderMesh(); Published(mesh, 6); ExpectBuildMeshVector(mesh.Scratch()[5].m_Position, 0,8,0);
    mesh.SetPMGeoMorphStep(4); mesh.BuildRenderMesh(); Published(mesh, 6);
    ExpectBuildMeshVector(mesh.Scratch()[5].m_Position, 4,1,2);
}

TEST_F(BuildRenderMeshCompletionTest, NonMorphNormalsUseCompactSurvivorsAndPreserveOmittedFaceIdentity) {
    BuildMeshProbe mesh(Context); Geometry(mesh); mesh.Configure(4, FALSE, 0); mesh.PendingNormals(TRUE);
    mesh.BuildRenderMesh(); ASSERT_EQ(1, BuildMeshNormalCalls); EXPECT_EQ(3, BuildMeshNormalFaceCount);
    EXPECT_EQ(4, BuildMeshNormalVertexCount); EXPECT_EQ(mesh.Source(), BuildMeshNormalVertices);
    const CKWORD expected[] = {0,1,3,0,1,3,0,1,3};
    for (int i = 0; i < 9; ++i) EXPECT_EQ(expected[i], BuildMeshNormalIndices[i]);
    ExpectBuildMeshVector(mesh.GetFaceNormal(1),101,201,301);
    ExpectBuildMeshVector(mesh.GetFaceNormal(0),0,0,1); ExpectBuildMeshVector(mesh.GetFaceNormal(2),0,0,1);
    ExpectBuildMeshVector(mesh.Source()[3].m_Normal,0,0,1); EXPECT_EQ(0u, mesh.GetFlags() & VXMESH_PM_BUILDNORM);
}

TEST_F(BuildRenderMeshCompletionTest, MorphNormalsUseScratchWithoutChangingAuthoredVertexNormals) {
    BuildMeshProbe mesh(Context); Geometry(mesh); mesh.Configure(3, TRUE, 2); mesh.PendingNormals(TRUE);
    mesh.BuildRenderMesh(); ASSERT_EQ(1, BuildMeshNormalCalls); EXPECT_EQ(3, BuildMeshNormalFaceCount);
    EXPECT_EQ(4, BuildMeshNormalVertexCount); EXPECT_EQ(mesh.Scratch(), BuildMeshNormalVertices);
    ExpectBuildMeshVector(mesh.Scratch()[3].m_Normal,0,0,1); ExpectBuildMeshVector(mesh.Source()[3].m_Normal,0,2,0);
    ExpectBuildMeshVector(mesh.GetFaceNormal(1),101,201,301); EXPECT_EQ(0u, mesh.GetFlags() & VXMESH_PM_BUILDNORM);
}

TEST_F(BuildRenderMeshCompletionTest, NoNormalRequestPreservesAuthoredNormalsAndSkipsTheBuilder) {
    BuildMeshProbe mesh(Context); Geometry(mesh); mesh.Configure(4, FALSE, 0); mesh.BuildRenderMesh();
    EXPECT_EQ(0, BuildMeshNormalCalls); ExpectBuildMeshVector(mesh.Source()[3].m_Normal,0,2,0);
    ExpectBuildMeshVector(mesh.GetFaceNormal(0),100,200,300);
}

TEST_F(BuildRenderMeshCompletionTest, FullyCollapsedTopologyKeepsNormalWorkPendingUntilGeometryReturns) {
    BuildMeshProbe mesh(Context); Geometry(mesh); mesh.Configure(0, FALSE, 0); mesh.PendingNormals(TRUE);
    mesh.BuildRenderMesh(); Published(mesh, 0); Indices(mesh.Group(0), nullptr, 0);
    EXPECT_EQ(0, BuildMeshNormalCalls); EXPECT_NE(0u, mesh.GetFlags() & VXMESH_PM_BUILDNORM);
    mesh.SetVerticesRendered(6); mesh.BuildRenderMesh(); EXPECT_EQ(1, BuildMeshNormalCalls);
    EXPECT_EQ(4, BuildMeshNormalFaceCount); EXPECT_EQ(0u, mesh.GetFlags() & VXMESH_PM_BUILDNORM);
}

TEST_F(BuildRenderMeshCompletionTest, RepeatedLODChangesResetPrimitivesInsteadOfAppendingStaleIndices) {
    BuildMeshProbe mesh(Context); RCKMaterial first(Context), second(Context); Geometry(mesh,&first,&second);
    mesh.Configure(4, FALSE, 0); mesh.BuildRenderMesh();
    const CKWORD reduced[] = {0,1,3,0,1,3}; Indices(mesh.FindGroup(&first), reduced, 6);
    Indices(mesh.FindGroup(&second), nullptr, 0);
    mesh.SetVerticesRendered(6); mesh.BuildRenderMesh(); const CKWORD full[] = {0,1,5,0,1,3};
    Indices(mesh.FindGroup(&first), full, 6); const CKWORD secondFull[] = {0,4,5}; Indices(mesh.FindGroup(&second),secondFull,3);
    mesh.SetVerticesRendered(4); mesh.BuildRenderMesh(); Indices(mesh.FindGroup(&first), reduced, 6);
    ASSERT_EQ(2, mesh.FindGroup(&first)->m_FaceIndices.Size());
    EXPECT_EQ(0, mesh.FindGroup(&first)->m_FaceIndices[0]); EXPECT_EQ(3, mesh.FindGroup(&first)->m_FaceIndices[1]);
}

TEST_F(BuildRenderMeshCompletionTest, ChannelsKeepAuthoredMasksButRebuildMembershipInCurrentLODSpace) {
    BuildMeshProbe mesh(Context); RCKMaterial first(Context), second(Context); Geometry(mesh);
    ASSERT_EQ(0, mesh.AddChannel(&first)); ASSERT_EQ(1, mesh.AddChannel(&second));
    const CKWORD masks[] = {1,3,2,0}; for (int i = 0; i < 4; ++i) mesh.SetFaceChannelMask(i,masks[i]);
    mesh.Configure(4,FALSE,0); mesh.BuildRenderMesh();
    for (int channel = 0; channel < 2; ++channel) {
        ASSERT_NE(nullptr, mesh.Channel(channel).m_FaceIndices); ASSERT_EQ(3, mesh.Channel(channel).m_FaceIndices->Size());
        EXPECT_EQ(0, (*mesh.Channel(channel).m_FaceIndices)[0]); EXPECT_EQ(1, (*mesh.Channel(channel).m_FaceIndices)[1]);
        EXPECT_EQ(3, (*mesh.Channel(channel).m_FaceIndices)[2]);
    }
    mesh.SetVerticesRendered(6); mesh.BuildRenderMesh();
    const CKWORD expected[2][6] = {{0,1,5,0,4,5},{0,4,5,0,1,4}};
    for (int channel = 0; channel < 2; ++channel) {
        ASSERT_EQ(6, mesh.Channel(channel).m_FaceIndices->Size());
        for (int i = 0; i < 6; ++i) EXPECT_EQ(expected[channel][i], (*mesh.Channel(channel).m_FaceIndices)[i]);
    }
    for (int i = 0; i < 4; ++i) EXPECT_EQ(masks[i], mesh.GetFaceChannelMask(i));
    EXPECT_EQ(0u, mesh.FaceUpdateMask());
}

TEST_F(BuildRenderMeshCompletionTest, PublishedGroupsHaveGlobalDomainsAndDirtyGeometryPayload) {
    BuildMeshProbe mesh(Context); RCKMaterial material(Context); Geometry(mesh,&material);
    mesh.Validity(1); mesh.Configure(3,TRUE,2); mesh.BuildRenderMesh(); Published(mesh,4);
    EXPECT_EQ(0u, mesh.Validity()); EXPECT_EQ(0u, mesh.GetFlags() & VXMESH_BOUNDINGUPTODATE);
}

TEST_F(BuildRenderMeshCompletionTest, EmptyNullGroupCleanupKeepsTheDefaultAndRemapsLaterMaterials) {
    BuildMeshProbe mesh(Context); RCKMaterial first(Context), second(Context); Geometry(mesh,&first,&second);
    mesh.AddEmptyNullGroup(); ASSERT_EQ(4, mesh.Groups()); mesh.Configure(4,FALSE,0); mesh.BuildRenderMesh();
    ASSERT_EQ(3, mesh.Groups()); EXPECT_EQ(nullptr, mesh.Group(0)->m_Material);
    EXPECT_EQ(&first, mesh.GetFaceMaterial(0)); EXPECT_EQ(&second, mesh.GetFaceMaterial(1)); EXPECT_EQ(&first, mesh.GetFaceMaterial(3));
    EXPECT_EQ(&first, mesh.Group(1)->m_Material); EXPECT_EQ(&second, mesh.Group(2)->m_Material);
}

TEST_F(BuildRenderMeshCompletionTest, EmptyNonNullMaterialGroupSurvivesForAFutureLOD) {
    BuildMeshProbe mesh(Context); RCKMaterial first(Context), second(Context); Geometry(mesh,&first,&second);
    mesh.Configure(4,FALSE,0); mesh.BuildRenderMesh(); CKMaterialGroup *retained = mesh.FindGroup(&second);
    ASSERT_NE(nullptr, retained); Indices(retained,nullptr,0);
    mesh.SetVerticesRendered(6); mesh.BuildRenderMesh(); EXPECT_EQ(retained, mesh.FindGroup(&second));
    const CKWORD expected[] = {0,4,5}; Indices(retained,expected,3);
}

TEST_F(BuildRenderMeshCompletionTest, AuthoredRemapStorageIsRetiredBeforeGlobalPMPrimitivesArePublished) {
    BuildMeshProbe mesh(Context); RCKMaterial first(Context), second(Context); Geometry(mesh,&first,&second);
    int remapped = 0; for (int i = 0; i < mesh.Groups(); ++i) remapped += mesh.Group(i)->m_RemapData != 0;
    ASSERT_GT(remapped,0); mesh.Configure(4,FALSE,0); mesh.BuildRenderMesh();
    for (int i = 0; i < mesh.Groups(); ++i) {
        EXPECT_EQ(static_cast<CKUINTPTR>(0), mesh.Group(i)->m_RemapData); EXPECT_EQ(-1, mesh.Group(i)->m_Primitives[0].m_IndexBufferOffset);
    }
    const CKWORD expected[] = {0,1,3,0,1,3}; Indices(mesh.FindGroup(&first),expected,6);
}
