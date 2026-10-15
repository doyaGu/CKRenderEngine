#include "TextureTestObserver.h"
#include "RCKPatchMesh.h"
#include "RCKMaterial.h"

namespace {
class TopologyPatch : public RCKPatchMesh {
public:
    explicit TopologyPatch(CKContext *context) : RCKPatchMesh(context) {}
    CKBOOL TopologyChanged() const { return m_PatchChanged; }
};

class PatchMeshTopologyTest : public TextureTestWorld {
protected:
    void SetUp() override {
        TextureTestWorld::SetUp();
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKPatchMesh, CKCID_MESH);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
    }
    static void VectorIs(const VxVector &actual, const VxVector &expected) {
        EXPECT_NEAR(expected.x, actual.x, .0001f);
        EXPECT_NEAR(expected.y, actual.y, .0001f);
        EXPECT_NEAR(expected.z, actual.z, .0001f);
    }
    static void Describe(CKPatch &patch, int count, const int *vertices, int vectorBase, const int *edges) {
        patch.type = count; patch.Material = 0; patch.SmoothingGroup = 0xFFFFFFFF;
        for (int i = 0; i < 4; ++i) {
            patch.v[i] = i < count ? static_cast<short>(vertices[i]) : -1;
            patch.interior[i] = i < count ? static_cast<short>(vectorBase + count * 2 + i) : -1;
            patch.edge[i] = i < count ? static_cast<short>(edges[i]) : -1;
        }
        for (int i = 0; i < 8; ++i)
            patch.vec[i] = i < count * 2 ? static_cast<short>(vectorBase + i) : -1;
    }
    static void Controls(TopologyPatch &mesh, const CKPatch &patch) {
        for (int i = 0; i < static_cast<int>(patch.type); ++i) {
            VxVector a, b;
            mesh.GetVert(patch.v[i], &a); mesh.GetVert(patch.v[(i + 1) % patch.type], &b);
            VxVector first = a + (b - a) * (1.0f / 3.0f);
            VxVector second = a + (b - a) * (2.0f / 3.0f);
            mesh.SetVec(patch.vec[2 * i], &first); mesh.SetVec(patch.vec[2 * i + 1], &second);
        }
    }
    static void Quad(TopologyPatch &mesh) {
        mesh.SetVertVecCount(4, 12); mesh.SetPatchCount(1); mesh.SetEdgeCount(4);
        const VxVector points[] = {VxVector(0, 0, 0), VxVector(3, 0, 0), VxVector(3, 3, 0), VxVector(0, 3, 0)};
        for (int i = 0; i < 4; ++i) { VxVector point = points[i]; mesh.SetVert(i, &point); }
        const int corners[] = {0, 1, 2, 3}, edges[] = {0, 1, 2, 3};
        CKPatch patch; Describe(patch, 4, corners, 0, edges); Controls(mesh, patch); mesh.SetPatch(0, &patch);
        for (int i = 0; i < 4; ++i) {
            CKPatchEdge edge = {static_cast<short>(i), static_cast<short>(i * 2), static_cast<short>(i * 2 + 1), static_cast<short>((i + 1) % 4), 0, -1};
            mesh.SetEdge(i, &edge);
        }
        mesh.SetTVCount(4, -1); mesh.SetTVPatchCount(1, -1);
        CKTVPatch tv = {{0, 1, 2, 3}}; mesh.SetTVPatch(0, &tv, -1);
        mesh.SetTV(0, 0, 0, -1); mesh.SetTV(1, 1, 0, -1);
        mesh.SetTV(2, 1, 1, -1); mesh.SetTV(3, 0, 1, -1);
    }
    static void Triangle(TopologyPatch &mesh) {
        mesh.SetVertVecCount(3, 9); mesh.SetPatchCount(1); mesh.SetEdgeCount(3);
        VxVector positions[] = {VxVector(0, 0, 0), VxVector(3, 0, 0), VxVector(0, 3, 0)};
        for (int i = 0; i < 3; ++i) mesh.SetVert(i, &positions[i]);
        const int corners[] = {0, 1, 2}, edges[] = {0, 1, 2};
        CKPatch patch; Describe(patch, 3, corners, 0, edges); Controls(mesh, patch); mesh.SetPatch(0, &patch);
        for (int i = 0; i < 3; ++i) {
            CKPatchEdge edge = {static_cast<short>(i), static_cast<short>(i * 2), static_cast<short>(i * 2 + 1), static_cast<short>((i + 1) % 3), 0, -1};
            mesh.SetEdge(i, &edge);
        }
    }
    static void SourceTriangle(RCKMesh &mesh) {
        ASSERT_TRUE(mesh.SetVertexCount(3)); ASSERT_TRUE(mesh.SetFaceCount(1));
        VxVector positions[] = {VxVector(0, 0, 0), VxVector(3, 0, 0), VxVector(0, 3, 0)};
        for (int i = 0; i < 3; ++i) {
            mesh.SetVertexPosition(i, &positions[i]); VxVector normal(0, 0, 1); mesh.SetVertexNormal(i, &normal);
            mesh.SetVertexTextureCoordinates(i, positions[i].x / 3.0f, positions[i].y / 3.0f, -1);
        }
        mesh.SetFaceVertexIndex(0, 0, 1, 2);
    }
    static int At(RCKMesh &mesh, const VxVector &position) {
        for (int i = 0; i < mesh.GetVertexCount(); ++i) {
            VxVector actual; mesh.GetVertexPosition(i, &actual);
            if (fabsf(actual.x - position.x) < .0001f && fabsf(actual.y - position.y) < .0001f && fabsf(actual.z - position.z) < .0001f)
                return i;
        }
        return -1;
    }
    static void UV(RCKMesh &mesh, const VxVector &position, float expectedU, float expectedV, int channel = -1) {
        const int index = At(mesh, position); ASSERT_GE(index, 0);
        float u = -99, v = -99; mesh.GetVertexTextureCoordinates(index, &u, &v, channel);
        EXPECT_NEAR(expectedU, u, .0001f); EXPECT_NEAR(expectedV, v, .0001f);
    }
    static float OrientedArea(RCKMesh &mesh) {
        float area = 0;
        for (int face = 0; face < mesh.GetFaceCount(); ++face) {
            int a, b, c; mesh.GetFaceVertexIndex(face, a, b, c);
            VxVector first, second, third; mesh.GetVertexPosition(a, &first); mesh.GetVertexPosition(b, &second); mesh.GetVertexPosition(c, &third);
            area += ((second.x - first.x) * (third.y - first.y) - (second.y - first.y) * (third.x - first.x)) * .5f;
        }
        return area;
    }
};

TEST_F(PatchMeshTopologyTest, ControlStorageAliasesAccessorsAndFeedsActualMesh) {
    TopologyPatch mesh(Context); Quad(mesh);
    EXPECT_EQ(4, mesh.GetVertCount()); EXPECT_EQ(12, mesh.GetVecCount());
    EXPECT_EQ(mesh.GetVerts() + 4, mesh.GetVecs());
    VxVector point; mesh.GetVert(2, &point); VectorIs(point, VxVector(3, 3, 0));
    mesh.GetVec(0, &point); VectorIs(point, VxVector(1, 0, 0));
    mesh.SetIterationCount(2); mesh.BuildRenderMesh();
    EXPECT_GE(At(mesh, VxVector(1, 0, 0)), 0); EXPECT_NEAR(-9.0f, OrientedArea(mesh), .0001f);
    CKPatch patch; mesh.GetPatch(0, &patch); EXPECT_EQ(4u, patch.type); EXPECT_EQ(0, patch.v[0]);
    CKPatchEdge edge; mesh.GetEdge(1, &edge); EXPECT_EQ(1, edge.v1); EXPECT_EQ(2, edge.v2);
    EXPECT_EQ(mesh.GetPatches()->v[2], patch.v[2]); EXPECT_EQ(mesh.GetEdges()[1].vec21, edge.vec21);
}

TEST_F(PatchMeshTopologyTest, ControlVertexEditsRebuildPositionAndNormals) {
    TopologyPatch mesh(Context); Quad(mesh); mesh.BuildRenderMesh();
    VxVector normal; mesh.GetVertexNormal(0, &normal); EXPECT_NEAR(1.0f, fabsf(normal.z), .0001f);
    VxVector moved(0, 3, 3); mesh.SetVert(3, &moved); mesh.BuildRenderMesh();
    EXPECT_GE(At(mesh, moved), 0); mesh.GetVertexNormal(0, &normal); EXPECT_GT(fabsf(normal.x) + fabsf(normal.y), .1f);
}

TEST_F(PatchMeshTopologyTest, EdgeControlEditsRefreshCurvedSamplesAndNormals) {
    TopologyPatch mesh(Context); Quad(mesh); mesh.SetIterationCount(1); mesh.BuildRenderMesh();
    EXPECT_GE(At(mesh, VxVector(1.5f, 0, 0)), 0);
    VxVector raised(1, 0, 4); mesh.SetVec(0, &raised); mesh.BuildRenderMesh();
    const int index = At(mesh, VxVector(1.5f, 0, 1.5f)); ASSERT_GE(index, 0);
    VxVector normal; mesh.GetVertexNormal(index, &normal); EXPECT_GT(fabsf(normal.x) + fabsf(normal.y), .1f);
}

TEST_F(PatchMeshTopologyTest, ReplacingControlAllocationInvalidatesAnExistingMesh) {
    TopologyPatch mesh(Context); Quad(mesh); mesh.BuildRenderMesh();
    Quad(mesh); VxVector changed(9, 0, 0); mesh.SetVert(1, &changed); mesh.BuildRenderMesh();
    EXPECT_GE(At(mesh, changed), 0); EXPECT_LT(At(mesh, VxVector(3, 0, 0)), 0);
    mesh.SetVertVecCount(0, 0); mesh.BuildRenderMesh(); EXPECT_EQ(0, mesh.GetVertexCount()); EXPECT_EQ(0, mesh.GetFaceCount());
}

TEST_F(PatchMeshTopologyTest, EdgeOrientationEditsRebuildTheIndexedBoundary) {
    TopologyPatch mesh(Context); Quad(mesh); mesh.SetIterationCount(2); mesh.BuildRenderMesh();
    VxVector sample; mesh.GetVertexPosition(4, &sample); VectorIs(sample, VxVector(1, 0, 0));
    CKPatchEdge edge; mesh.GetEdge(0, &edge); edge.v1 = 1; edge.v2 = 0; edge.vec12 = 1; edge.vec21 = 0; mesh.SetEdge(0, &edge);
    mesh.BuildRenderMesh(); mesh.GetVertexPosition(4, &sample); VectorIs(sample, VxVector(2, 0, 0));
    EXPECT_NEAR(-9.0f, OrientedArea(mesh), .0001f);
}

TEST_F(PatchMeshTopologyTest, RemovingEdgesAndPatchesClearsStaleDerivedGeometry) {
    TopologyPatch mesh(Context); Quad(mesh); mesh.BuildRenderMesh(); ASSERT_GT(mesh.GetFaceCount(), 0);
    mesh.SetEdgeCount(0); mesh.BuildRenderMesh(); EXPECT_EQ(0, mesh.GetFaceCount()); EXPECT_EQ(0, mesh.GetVertexCount());
    Quad(mesh); mesh.BuildRenderMesh(); ASSERT_GT(mesh.GetFaceCount(), 0);
    mesh.SetPatchCount(0); mesh.BuildRenderMesh(); EXPECT_EQ(0, mesh.GetFaceCount()); EXPECT_EQ(0, mesh.GetVertexCount());
    EXPECT_EQ(0, mesh.GetPatchCount()); EXPECT_EQ(4, mesh.GetEdgeCount());
}

TEST_F(PatchMeshTopologyTest, PatchMaterialReplacementIsVisibleOnRebuild) {
    RCKMaterial first(Context), second(Context); TopologyPatch mesh(Context); Quad(mesh);
    mesh.SetPatchMaterial(0, &first); EXPECT_EQ(&first, mesh.GetPatchMaterial(0)); mesh.BuildRenderMesh();
    for (int i = 0; i < mesh.GetFaceCount(); ++i) EXPECT_EQ(&first, mesh.GetFaceMaterial(i));
    CKPatch patch; mesh.GetPatch(0, &patch); patch.Material = second.GetID(); mesh.SetPatch(0, &patch); mesh.BuildRenderMesh();
    EXPECT_EQ(&second, mesh.GetPatchMaterial(0));
    for (int i = 0; i < mesh.GetFaceCount(); ++i) EXPECT_EQ(&second, mesh.GetFaceMaterial(i));
    mesh.SetPatchMaterial(0, nullptr); mesh.BuildRenderMesh(); EXPECT_EQ(nullptr, mesh.GetFaceMaterial(0));
}

TEST_F(PatchMeshTopologyTest, SmoothingEditRebuildsSharedCornerNormals) {
    TopologyPatch mesh(Context); mesh.SetVertVecCount(6, 24); mesh.SetPatchCount(2); mesh.SetEdgeCount(7);
    VxVector points[] = {VxVector(0,0,0), VxVector(3,0,0), VxVector(3,3,0), VxVector(0,3,0), VxVector(3,0,3), VxVector(3,3,3)};
    for (int i = 0; i < 6; ++i) mesh.SetVert(i, &points[i]);
    const int cornersA[] = {0,1,2,3}, cornersB[] = {1,4,5,2}, edgesA[] = {0,1,2,3}, edgesB[] = {4,5,6,1};
    CKPatch first, second; Describe(first,4,cornersA,0,edgesA); Describe(second,4,cornersB,12,edgesB);
    Controls(mesh,first); Controls(mesh,second); mesh.SetPatch(0,&first); mesh.SetPatch(1,&second);
    CKPatchEdge edges[] = {{0,0,1,1,0,-1},{1,2,3,2,0,1},{2,4,5,3,0,-1},{3,6,7,0,0,-1},{1,12,13,4,1,-1},{4,14,15,5,1,-1},{5,16,17,2,1,-1}};
    for (int i = 0; i < 7; ++i) mesh.SetEdge(i,&edges[i]);
    mesh.SetTVCount(6,-1); mesh.SetTVPatchCount(2,-1);
    CKTVPatch uvA={{0,1,2,3}}, uvB={{1,4,5,2}}; mesh.SetTVPatch(0,&uvA,-1); mesh.SetTVPatch(1,&uvB,-1);
    for (int i = 0; i < 6; ++i) mesh.SetTV(i, points[i].x, points[i].y + points[i].z,-1);
    mesh.BuildRenderMesh(); const int shared=At(mesh,points[1]); ASSERT_GE(shared,0);
    VxVector smooth; mesh.GetVertexNormal(shared,&smooth); EXPECT_GT(fabsf(smooth.x),.2f); EXPECT_GT(fabsf(smooth.z),.2f);
    mesh.SetPatchSM(1,0); EXPECT_EQ(0u,mesh.GetPatchSM(1)); mesh.BuildRenderMesh();
    int matches=0;
    for(int i=0;i<mesh.GetVertexCount();++i){
        VxVector position, normal; mesh.GetVertexPosition(i,&position); mesh.GetVertexNormal(i,&normal);
        if (fabsf(position.x-3)<.0001f && fabsf(position.y)<.0001f && fabsf(position.z)<.0001f) {
            ++matches; EXPECT_LT(fabsf(normal.x*normal.z),.001f);
        }
    }
    EXPECT_GE(matches,2);
}

TEST_F(PatchMeshTopologyTest, TextureValueAndTexturePatchEditsReachGeneratedVertices) {
    TopologyPatch mesh(Context); Quad(mesh); mesh.SetIterationCount(1); mesh.BuildRenderMesh();
    UV(mesh,VxVector(0,0,0),0,0); mesh.SetTV(0,.25f,.75f,-1); mesh.BuildRenderMesh(); UV(mesh,VxVector(0,0,0),.25f,.75f);
    CKTVPatch data; mesh.GetTVPatch(0,&data,-1); EXPECT_EQ(0,data.tv[0]); EXPECT_EQ(0,mesh.GetTVPatches(-1)[0].tv[0]);
    data.tv[0]=2; mesh.SetTVPatch(0,&data,-1); mesh.BuildRenderMesh(); UV(mesh,VxVector(0,0,0),1,1);
    EXPECT_FLOAT_EQ(.25f,mesh.GetTVs(-1)[0].u); float u,v; mesh.GetTV(0,&u,&v,-1); EXPECT_FLOAT_EQ(.25f,u); EXPECT_FLOAT_EQ(.75f,v);
}

TEST_F(PatchMeshTopologyTest, RemovingTextureStorageOrMappingsClearsGeneratedUv) {
    TopologyPatch mesh(Context); Quad(mesh); mesh.BuildRenderMesh(); UV(mesh,VxVector(3,3,0),1,1);
    mesh.SetTVCount(0,-1); mesh.BuildRenderMesh(); UV(mesh,VxVector(3,3,0),0,0); EXPECT_EQ(0,mesh.GetTVCount(-1));
    Quad(mesh); mesh.BuildRenderMesh(); UV(mesh,VxVector(3,3,0),1,1);
    mesh.SetTVPatchCount(0,-1); mesh.BuildRenderMesh(); UV(mesh,VxVector(3,3,0),0,0); EXPECT_EQ(0,mesh.GetTVPatchCount(-1));
}

TEST_F(PatchMeshTopologyTest, IterationAndCleanRebuildTheSameNativeFacingSurface) {
    TopologyPatch mesh(Context); Quad(mesh); mesh.SetIterationCount(-7); EXPECT_EQ(0,mesh.GetIterationCount());
    mesh.BuildRenderMesh(); EXPECT_EQ(2,mesh.GetFaceCount()); mesh.SetIterationCount(2); mesh.BuildRenderMesh(); EXPECT_EQ(18,mesh.GetFaceCount());
    mesh.CleanRenderMesh(); EXPECT_EQ(0,mesh.GetFaceCount()); EXPECT_EQ(0,mesh.GetVertexCount());
    EXPECT_EQ(4,mesh.GetVertCount()); EXPECT_EQ(1,mesh.GetPatchCount()); mesh.BuildRenderMesh(); EXPECT_EQ(18,mesh.GetFaceCount());
    EXPECT_NEAR(-9.0f,OrientedArea(mesh),.0001f);
    const CKDWORD flags=mesh.GetPatchFlags(); EXPECT_NE(0u,flags & CK_PATCHMESH_UPTODATE);
    mesh.GetVecs()[0].z=4;
    mesh.SetPatchFlags((flags & ~CK_PATCHMESH_UPTODATE) | CK_PATCHMESH_BUILDNORMALS);
    mesh.BuildRenderMesh(); EXPECT_GE(At(mesh,VxVector(1,0,16.0f/9.0f)),0);
}

TEST_F(PatchMeshTopologyTest, TriangleAuxiliaryAndInteriorControlsMatchIndependentValues) {
    TopologyPatch mesh(Context); Triangle(mesh); mesh.ComputePatchInteriors(0);
    CKPatch *patch=mesh.GetPatches(); ASSERT_NE(nullptr,patch->auxs);
    VectorIs(patch->auxs[0],VxVector(.75f,0,0)); VectorIs(patch->auxs[1],VxVector(1.5f,0,0)); VectorIs(patch->auxs[2],VxVector(2.25f,0,0));
    VectorIs(mesh.GetVecs()[6],VxVector(1,1,0)); VectorIs(mesh.GetVecs()[7],VxVector(1,1,0)); VectorIs(mesh.GetVecs()[8],VxVector(1,1,0));
    VxVector changed(1,0,4); mesh.SetVec(0,&changed); mesh.ComputePatchAux(0); VectorIs(patch->auxs[0],VxVector(.75f,0,3));
    mesh.ComputePatchInteriors(0); VectorIs(mesh.GetVecs()[6],VxVector(1,1,4)); mesh.SetIterationCount(2); mesh.BuildRenderMesh();
    EXPECT_GE(At(mesh,VxVector(1,0,16.0f/9.0f)),0);
}

TEST_F(PatchMeshTopologyTest, QuadInteriorControlsAreRecomputedFromIncidentTangents) {
    TopologyPatch mesh(Context); Quad(mesh); mesh.ComputePatchInteriors(0);
    const VxVector expected[]={VxVector(1,1,0),VxVector(2,1,0),VxVector(2,2,0),VxVector(1,2,0)};
    for(int i=0;i<4;++i) VectorIs(mesh.GetVecs()[8+i],expected[i]);
    mesh.SetIterationCount(1); mesh.BuildRenderMesh(); mesh.GetVecs()[0].z=4; mesh.ComputePatchInteriors(0); mesh.BuildRenderMesh();
    EXPECT_GE(At(mesh,VxVector(1.5f,0,1.5f)),0);
}

TEST_F(PatchMeshTopologyTest, GrowingPatchStorageDropsDerivedAuxWithoutAliasingItsOwner) {
    TopologyPatch mesh(Context); Triangle(mesh); mesh.ComputePatchAux(0); ASSERT_NE(nullptr,mesh.GetPatches()[0].auxs);
    mesh.SetPatchCount(3); EXPECT_EQ(3,mesh.GetPatchCount()); EXPECT_EQ(nullptr,mesh.GetPatches()[0].auxs);
    mesh.ComputePatchAux(0); VectorIs(mesh.GetPatches()[0].auxs[0],VxVector(.75f,0,0));
    mesh.SetPatchCount(1); mesh.BuildRenderMesh(); EXPECT_EQ(1,mesh.GetFaceCount()); EXPECT_NEAR(-4.5f,OrientedArea(mesh),.0001f);
}







TEST_F(PatchMeshTopologyTest, InvalidConversionTargetsAndMissingTextureReadsHaveDefinedResults) {
    TopologyPatch mesh(Context); EXPECT_EQ(CKERR_INVALIDPARAMETER,mesh.FromMesh(nullptr)); EXPECT_EQ(CKERR_INVALIDPARAMETER,mesh.ToMesh(nullptr,0));
    float u=3,v=4; mesh.GetTV(0,&u,&v,7); EXPECT_FLOAT_EQ(0,u); EXPECT_FLOAT_EQ(0,v);
    EXPECT_EQ(0,mesh.GetTVCount(7)); EXPECT_EQ(0,mesh.GetTVPatchCount(7)); EXPECT_EQ(nullptr,mesh.GetTVs(7)); EXPECT_EQ(nullptr,mesh.GetTVPatches(7));
    EXPECT_EQ(0u,mesh.GetPatchSM(0)); EXPECT_EQ(nullptr,mesh.GetPatchMaterial(0));
    RCKMesh output(Context); EXPECT_EQ(CK_OK,mesh.ToMesh(&output,0)); EXPECT_EQ(0,output.GetFaceCount());
}


}
