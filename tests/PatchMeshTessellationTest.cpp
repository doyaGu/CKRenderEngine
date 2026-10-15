#include "TextureTestObserver.h"
#include "RCKPatchMesh.h"
#include "RCKMaterial.h"
#include <climits>

namespace {
class TessellatedPatch : public RCKPatchMesh {
public:
    explicit TessellatedPatch(CKContext *context) : RCKPatchMesh(context) {}
    void EvaluateTriangle(CKPatch *patch, float a, float b, float c, VxVector *out) { EvaluateTriPatch(patch, a, b, c, out); }
    void EvaluateQuad(CKPatch *patch, float u, float v, VxVector *out) { EvaluateQuadPatch(patch, u, v, out); }
    void ExtraChannel(CKMaterial *material, CKDWORD flags = VXCHANNEL_ACTIVE) {
        m_TexturePatches.Resize(2);
        m_TexturePatches[1].Material = material->GetID();
        m_TexturePatches[1].Flags = VXBLEND_ONE;
        m_TexturePatches[1].Type = VXBLEND_ZERO;
        m_TexturePatches[1].SubType = flags;
        m_PatchChanged = TRUE; m_PatchFlags &= ~CK_PATCHMESH_UPTODATE;
    }
    void Dirty() { m_PatchChanged = TRUE; m_PatchFlags &= ~CK_PATCHMESH_UPTODATE; }
    CKBOOL SharedUV(int edge) const { return DoPatchesShareUVOnEdge(edge); }
    CKBOOL CornerUV(int channel, int patch, int corner, float &u, float &v) const { return GetCornerTextureCoordinate(channel, patch, corner, u, v); }
    void WriteUV(void *pointer, CKDWORD stride, int index, float u, float v) const { WriteTextureCoordinate(pointer, stride, index, u, v); }
};

void VecIs(const VxVector &actual, const VxVector &expected, float tolerance = 0.0001f) {
    EXPECT_NEAR(expected.x, actual.x, tolerance); EXPECT_NEAR(expected.y, actual.y, tolerance);
    EXPECT_NEAR(expected.z, actual.z, tolerance);
}
VxVector Position(RCKMesh &mesh, int vertex) { VxVector result; mesh.GetVertexPosition(vertex, &result); return result; }
VxVector Normal(RCKMesh &mesh, int vertex) { VxVector result; mesh.GetVertexNormal(vertex, &result); return result; }
void UVIs(RCKMesh &mesh, int vertex, float u, float v, int channel = -1) {
    float actualU = -1000, actualV = -1000; mesh.GetVertexTextureCoordinates(vertex, &actualU, &actualV, channel);
    EXPECT_NEAR(u, actualU, 0.0001f); EXPECT_NEAR(v, actualV, 0.0001f);
}

// The oracle uses de Casteljau interpolation, independently of the runtime's
// Bernstein coefficient evaluation and its cached lattice index functions.
VxVector CubicReference(const VxVector *control, float t) {
    VxVector temp[4]; for (int i = 0; i < 4; ++i) temp[i] = control[i];
    for (int degree = 3; degree > 0; --degree)
        for (int i = 0; i < degree; ++i) temp[i] = temp[i] * (1 - t) + temp[i + 1] * t;
    return temp[0];
}
VxVector QuadReference(const VxVector control[4][4], float u, float v) {
    VxVector rows[4]; for (int i = 0; i < 4; ++i) rows[i] = CubicReference(control[i], u);
    return CubicReference(rows, v);
}
VxVector TriReference(const VxVector control[5][5], float a, float b, float c) {
    VxVector temp[5][5];
    for (int i = 0; i <= 4; ++i) for (int j = 0; j <= 4 - i; ++j) temp[i][j] = control[i][j];
    for (int degree = 4; degree > 0; --degree)
        for (int i = 0; i < degree; ++i)
            for (int j = 0; j < degree - i; ++j)
                temp[i][j] = temp[i][j] * a + temp[i + 1][j] * b + temp[i][j + 1] * c;
    return temp[0][0];
}

void InitPatch(CKPatch &patch, int type, int vectorBase, const int *corners, const int *edges) {
    patch.type = type; patch.SmoothingGroup = 1; patch.Material = 0;
    for (int i = 0; i < 4; ++i) {
        patch.v[i] = static_cast<short>(i < type ? corners[i] : -1);
        patch.edge[i] = static_cast<short>(i < type ? edges[i] : -1);
        patch.interior[i] = static_cast<short>(i < type ? vectorBase + 2 * type + i : -1);
    }
    for (int i = 0; i < 8; ++i) patch.vec[i] = static_cast<short>(i < 2 * type ? vectorBase + i : -1);
}
void EdgeControls(TessellatedPatch &mesh, const CKPatch &patch) {
    for (int i = 0; i < static_cast<int>(patch.type); ++i) {
        VxVector first, second; mesh.GetVert(patch.v[i], &first); mesh.GetVert(patch.v[(i + 1) % patch.type], &second);
        VxVector control = first * (2.0f / 3.0f) + second * (1.0f / 3.0f); mesh.SetVec(patch.vec[2 * i], &control);
        control = first * (1.0f / 3.0f) + second * (2.0f / 3.0f); mesh.SetVec(patch.vec[2 * i + 1], &control);
    }
}
void Single(TessellatedPatch &mesh, int type, int steps, CKMaterial *material = nullptr) {
    mesh.SetVertVecCount(type, type * 3); mesh.SetPatchCount(1); mesh.SetEdgeCount(type);
    VxVector positions[4] = {VxVector(0, 0, 0), VxVector(3, 0, 0), VxVector(type == 4 ? 3.0f : 0.0f, 3, 0), VxVector(0, 3, 0)};
    for (int i = 0; i < type; ++i) mesh.SetVert(i, &positions[i]);
    const int corners[] = {0, 1, 2, 3}, edges[] = {0, 1, 2, 3}; CKPatch patch;
    InitPatch(patch, type, 0, corners, edges); mesh.SetPatch(0, &patch); EdgeControls(mesh, patch);
    for (int i = 0; i < type; ++i) {
        CKPatchEdge edge = {static_cast<short>(i), static_cast<short>(2 * i), static_cast<short>(2 * i + 1),
                            static_cast<short>((i + 1) % type), 0, -1}; mesh.SetEdge(i, &edge);
    }
    mesh.SetTVPatchCount(1); mesh.SetTVCount(type);
    CKTVPatch tv = {{0, 1, 2, 3}}; mesh.SetTVPatch(0, &tv);
    for (int i = 0; i < type; ++i) mesh.SetTV(i, positions[i].x / 3, positions[i].y / 3);
    if (material) mesh.SetPatchMaterial(0, material);
    mesh.SetIterationCount(steps - 1);
}
void Adjacent(TessellatedPatch &mesh, int steps, bool seam, bool smooth, bool uvs = true) {
    mesh.SetVertVecCount(6, 24); mesh.SetPatchCount(2); mesh.SetEdgeCount(7);
    VxVector positions[] = {VxVector(0, 0, 0), VxVector(1, 0, 0), VxVector(1, 1, 0), VxVector(0, 1, 0),
                            VxVector(2, 0, 1), VxVector(2, 1, 1)};
    for (int i = 0; i < 6; ++i) mesh.SetVert(i, &positions[i]);
    const int corners0[] = {0, 1, 2, 3}, corners1[] = {1, 4, 5, 2};
    const int edges0[] = {0, 1, 2, 3}, edges1[] = {4, 5, 6, 1}; CKPatch first, second;
    InitPatch(first, 4, 0, corners0, edges0); InitPatch(second, 4, 12, corners1, edges1);
    second.SmoothingGroup = smooth ? 1 : 2;
    mesh.SetPatch(0, &first); mesh.SetPatch(1, &second); EdgeControls(mesh, first); EdgeControls(mesh, second);
    CKPatchEdge edges[] = {{0, 0, 1, 1, 0, -1}, {1, 2, 3, 2, 0, 1}, {2, 4, 5, 3, 0, -1}, {3, 6, 7, 0, 0, -1},
                           {1, 12, 13, 4, 1, -1}, {4, 14, 15, 5, 1, -1}, {5, 16, 17, 2, 1, -1}};
    for (int i = 0; i < 7; ++i) mesh.SetEdge(i, &edges[i]);
    if (uvs) {
        mesh.SetTVPatchCount(2); mesh.SetTVCount(seam ? 8 : 6);
        CKTVPatch firstTV = {{0, 1, 2, 3}}, secondTV = {{1, 4, 5, 2}};
        if (seam) { secondTV.tv[0] = 6; secondTV.tv[3] = 7; }
        mesh.SetTVPatch(0, &firstTV); mesh.SetTVPatch(1, &secondTV);
        for (int i = 0; i < 6; ++i) mesh.SetTV(i, positions[i].x, positions[i].y);
        if (seam) { mesh.SetTV(6, 10, 0); mesh.SetTV(7, 10, 1); }
    }
    mesh.SetIterationCount(steps - 1);
}
int VerticesAt(RCKMesh &mesh, const VxVector &position, int *indices) {
    int found = 0;
    for (int i = 0; i < mesh.GetVertexCount(); ++i) {
        VxVector actual = Position(mesh, i);
        if (fabsf(actual.x - position.x) < 0.0001f && fabsf(actual.y - position.y) < 0.0001f && fabsf(actual.z - position.z) < 0.0001f)
            indices[found++] = i;
    }
    return found;
}
class PatchMeshTessellationTest : public TextureTestWorld {};

TEST_F(PatchMeshTessellationTest, QuadEvaluatorMatchesIndependentDeCasteljauForAsymmetricControlNet) {
    TessellatedPatch mesh(Context); Single(mesh, 4, 3); CKPatch &patch = mesh.GetPatches()[0];
    VxVector controls[4][4];
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
        controls[y][x] = VxVector(float(x * x + y), float(y * y - x), float(x * 2 - y * y + x * y));
    mesh.SetVert(0, &controls[0][0]); mesh.SetVert(1, &controls[0][3]); mesh.SetVert(2, &controls[3][3]); mesh.SetVert(3, &controls[3][0]);
    const int xy[12][2] = {{1,0},{2,0},{3,1},{3,2},{2,3},{1,3},{0,2},{0,1},{1,1},{2,1},{2,2},{1,2}};
    for (int i = 0; i < 12; ++i) mesh.SetVec(i, &controls[xy[i][1]][xy[i][0]]);
    const float samples[] = {0, 0.125f, 0.37f, 0.75f, 1};
    for (float u : samples) for (float v : samples) {
        VxVector actual; mesh.EvaluateQuad(&patch, u, v, &actual); VecIs(actual, QuadReference(controls, u, v));
    }
}

TEST_F(PatchMeshTessellationTest, TriEvaluatorMatchesIndependentQuarticDeCasteljauIncludingInteriorTerms) {
    TessellatedPatch mesh(Context); Single(mesh, 3, 4); CKPatch &patch = mesh.GetPatches()[0];
    patch.auxs = new VxVector[9]; VxVector controls[5][5];
    for (int b = 0; b <= 4; ++b) for (int c = 0; c <= 4 - b; ++c)
        controls[b][c] = VxVector(float(b * b + c), float(c * 3 - b), float(b * c + 2 * b - c));
    mesh.SetVert(0, &controls[0][0]); mesh.SetVert(1, &controls[4][0]); mesh.SetVert(2, &controls[0][4]);
    for (int i = 0; i < 3; ++i) {
        patch.auxs[i] = controls[i + 1][0]; patch.auxs[3 + i] = controls[3 - i][i + 1];
        patch.auxs[6 + i] = controls[0][3 - i];
    }
    mesh.SetVec(patch.interior[0], &controls[1][1]); mesh.SetVec(patch.interior[1], &controls[2][1]); mesh.SetVec(patch.interior[2], &controls[1][2]);
    for (int b = 0; b <= 5; ++b) for (int c = 0; c <= 5 - b; ++c) {
        float bv = b / 5.0f, cv = c / 5.0f, av = 1 - bv - cv; VxVector actual;
        mesh.EvaluateTriangle(&patch, av, bv, cv, &actual); VecIs(actual, TriReference(controls, av, bv, cv));
    }
}

TEST_F(PatchMeshTessellationTest, TriangleLatticeHasCompleteNondegenerateFacesAndNativeWindingAtEveryLevel) {
    for (int steps = 1; steps <= 6; ++steps) {
        TessellatedPatch mesh(Context); Single(mesh, 3, steps); mesh.BuildRenderMesh();
        ASSERT_EQ(steps * steps, mesh.GetFaceCount());
        ASSERT_EQ((steps + 1) * (steps + 2) / 2, mesh.GetVertexCount());
        double area = 0;
        for (int face = 0; face < mesh.GetFaceCount(); ++face) {
            int a, b, c; mesh.GetFaceVertexIndex(face, a, b, c);
            ASSERT_GE(a, 0); ASSERT_GE(b, 0); ASSERT_GE(c, 0);
            ASSERT_LT(a, mesh.GetVertexCount()); ASSERT_LT(b, mesh.GetVertexCount()); ASSERT_LT(c, mesh.GetVertexCount());
            VxVector pa = Position(mesh, a), pb = Position(mesh, b), pc = Position(mesh, c);
            const float signedArea = (pb.x - pa.x) * (pc.y - pa.y) - (pb.y - pa.y) * (pc.x - pa.x);
            EXPECT_LT(signedArea, -0.00001f); area -= signedArea * 0.5;
        }
        EXPECT_NEAR(4.5, area, 0.0001);
        // Native Build emits reverse control-corner winding; its face-normal
        // function is normalize((p1-p0) cross (p2-p0)), so this plane faces -Z.
        for (int vertex = 0; vertex < mesh.GetVertexCount(); ++vertex) VecIs(Normal(mesh, vertex), VxVector(0, 0, -1));
    }
}

TEST_F(PatchMeshTessellationTest, QuadLatticeSamplesPositionsBilinearUVAndNativeFacingNormals) {
    TessellatedPatch mesh(Context); Single(mesh, 4, 4); mesh.BuildRenderMesh();
    ASSERT_EQ(25, mesh.GetVertexCount()); ASSERT_EQ(32, mesh.GetFaceCount());
    for (int vertex = 0; vertex < mesh.GetVertexCount(); ++vertex) {
        VxVector actual = Position(mesh, vertex); EXPECT_FLOAT_EQ(0, actual.z);
        UVIs(mesh, vertex, actual.x / 3, actual.y / 3); VecIs(Normal(mesh, vertex), VxVector(0, 0, -1));
    }
    for (int y = 0; y <= 4; ++y) for (int x = 0; x <= 4; ++x) {
        int matches[4]; ASSERT_EQ(1, VerticesAt(mesh, VxVector(x * .75f, y * .75f, 0), matches));
    }
    const VxBbox &box = mesh.GetLocalBox(); VecIs(box.Min, VxVector(0, 0, 0)); VecIs(box.Max, VxVector(3, 3, 0));
}

TEST_F(PatchMeshTessellationTest, TriangleGridUVsIdentifyTheIndependentQuarticSamples) {
    TessellatedPatch mesh(Context); Single(mesh, 3, 5); mesh.BuildRenderMesh();
    CKPatch &patch = mesh.GetPatches()[0]; VxVector controls[5][5];
    controls[0][0] = VxVector(0, 0, 0); controls[4][0] = VxVector(3, 0, 0); controls[0][4] = VxVector(0, 3, 0);
    for (int i = 0; i < 3; ++i) { controls[i + 1][0] = patch.auxs[i]; controls[3 - i][i + 1] = patch.auxs[3 + i]; controls[0][3 - i] = patch.auxs[6 + i]; }
    mesh.GetVec(patch.interior[0], &controls[1][1]); mesh.GetVec(patch.interior[1], &controls[2][1]); mesh.GetVec(patch.interior[2], &controls[1][2]);
    bool visited[6][6] = {};
    for (int vertex = 0; vertex < mesh.GetVertexCount(); ++vertex) {
        float b, c; mesh.GetVertexTextureCoordinates(vertex, &b, &c, -1); int bi = int(b * 5 + .5f), ci = int(c * 5 + .5f);
        ASSERT_GE(bi, 0); ASSERT_GE(ci, 0); ASSERT_LE(bi + ci, 5); ASSERT_FALSE(visited[bi][ci]); visited[bi][ci] = true;
        VecIs(Position(mesh, vertex), TriReference(controls, 1 - b - c, b, c));
    }
    for (int b = 0; b <= 5; ++b) for (int c = 0; c <= 5 - b; ++c) EXPECT_TRUE(visited[b][c]);
}

TEST_F(PatchMeshTessellationTest, NoTexturePayloadDoesNotCreateArtificialHardGeometryEdges) {
    TessellatedPatch mesh(Context); Adjacent(mesh, 2, false, true, false); mesh.BuildRenderMesh();
    int matches[4]; ASSERT_EQ(1, VerticesAt(mesh, VxVector(1, .5f, 0), matches));
    VxVector expected(0.70710678f, 0, -1.70710678f); expected.Normalize(); VecIs(Normal(mesh, matches[0]), expected);
}

TEST_F(PatchMeshTessellationTest, BaseUVSeamDuplicatesCoordinatesButPreservesSmoothingGroupNormals) {
    TessellatedPatch mesh(Context); Adjacent(mesh, 2, true, true); mesh.BuildRenderMesh();
    int matches[4]; ASSERT_EQ(2, VerticesAt(mesh, VxVector(1, .5f, 0), matches));
    VxVector expected(0.70710678f, 0, -1.70710678f); expected.Normalize();
    for (int i = 0; i < 2; ++i) VecIs(Normal(mesh, matches[i]), expected);
    float firstU, firstV, secondU, secondV; mesh.GetVertexTextureCoordinates(matches[0], &firstU, &firstV, -1); mesh.GetVertexTextureCoordinates(matches[1], &secondU, &secondV, -1);
    EXPECT_FLOAT_EQ(.5f, firstV); EXPECT_FLOAT_EQ(.5f, secondV); EXPECT_TRUE((firstU == 1 && secondU == 10) || (firstU == 10 && secondU == 1));
}

TEST_F(PatchMeshTessellationTest, DisjointSmoothingGroupsPreserveSharpNormalsAtSharedGeometry) {
    TessellatedPatch mesh(Context); Adjacent(mesh, 2, false, false); mesh.BuildRenderMesh();
    int matches[4]; ASSERT_EQ(2, VerticesAt(mesh, VxVector(1, .5f, 0), matches));
    const VxVector first = Normal(mesh, matches[0]), second = Normal(mesh, matches[1]);
    EXPECT_TRUE(fabsf(first.x) < .0001f || fabsf(second.x) < .0001f);
    EXPECT_TRUE(fabsf(first.x - .70710678f) < .0001f || fabsf(second.x - .70710678f) < .0001f);
}

TEST_F(PatchMeshTessellationTest, ExtraChannelUVSeamKeepsBothPatchPayloadsAndSmoothNormals) {
    RCKMaterial material(Context); TessellatedPatch mesh(Context); Adjacent(mesh, 2, false, true);
    mesh.ExtraChannel(reinterpret_cast<CKMaterial *>(&material)); mesh.SetTVPatchCount(2, 0); mesh.SetTVCount(8, 0);
    CKTVPatch firstTV = {{0,1,2,3}}, secondTV = {{4,5,6,7}}; mesh.SetTVPatch(0, &firstTV, 0); mesh.SetTVPatch(1, &secondTV, 0);
    for (int i = 0; i < 4; ++i) { mesh.SetTV(i, i == 1 || i == 2 ? 1.0f : 0.0f, i >= 2 ? 1.0f : 0.0f, 0); mesh.SetTV(i + 4, i == 0 || i == 3 ? 10.0f : 20.0f, i >= 2 ? 1.0f : 0.0f, 0); }
    mesh.BuildRenderMesh(); ASSERT_EQ(1, mesh.GetChannelCount());
    int matches[4]; ASSERT_EQ(2, VerticesAt(mesh, VxVector(1, .5f, 0), matches));
    float u0,v0,u1,v1; mesh.GetVertexTextureCoordinates(matches[0], &u0, &v0, 0); mesh.GetVertexTextureCoordinates(matches[1], &u1, &v1, 0);
    EXPECT_TRUE((u0 == 1 && u1 == 10) || (u0 == 10 && u1 == 1)); EXPECT_FLOAT_EQ(.5f,v0); EXPECT_FLOAT_EQ(.5f,v1);
    UVIs(mesh,matches[0],1,.5f); UVIs(mesh,matches[1],1,.5f);
    VecIs(Normal(mesh,matches[0]),Normal(mesh,matches[1]));
}

TEST_F(PatchMeshTessellationTest, FacesRetainPatchMaterialsAcrossTessellatedBoundaries) {
    RCKMaterial first(Context), second(Context); TessellatedPatch mesh(Context); Adjacent(mesh, 3, false, true);
    mesh.SetPatchMaterial(0,reinterpret_cast<CKMaterial *>(&first)); mesh.SetPatchMaterial(1,reinterpret_cast<CKMaterial *>(&second)); mesh.BuildRenderMesh();
    ASSERT_EQ(36,mesh.GetFaceCount());
    for(int i=0;i<36;++i) EXPECT_EQ(i<18?reinterpret_cast<CKMaterial *>(&first):reinterpret_cast<CKMaterial *>(&second),mesh.GetFaceMaterial(i));
}

TEST_F(PatchMeshTessellationTest, MixedTriangleAndQuadShareAnOrientedCurvedEdgeWithoutCracks) {
    RCKMaterial left(Context), right(Context); TessellatedPatch mesh(Context);
    mesh.SetVertVecCount(5,21); mesh.SetPatchCount(2); mesh.SetEdgeCount(6);
    VxVector vertices[]={VxVector(0,0,0),VxVector(1,0,0),VxVector(1,1,0),VxVector(0,1,0),VxVector(2,0,0)};
    for(int i=0;i<5;++i) mesh.SetVert(i,&vertices[i]);
    const int quadCorners[]={0,1,2,3}, triCorners[]={1,4,2}, quadEdges[]={0,1,2,3}, triEdges[]={4,5,1};
    CKPatch quad,tri; InitPatch(quad,4,0,quadCorners,quadEdges); InitPatch(tri,3,12,triCorners,triEdges);
    mesh.SetPatch(0,&quad); mesh.SetPatch(1,&tri); EdgeControls(mesh,quad); EdgeControls(mesh,tri);
    CKPatchEdge edges[]={{0,0,1,1,0,-1},{1,2,3,2,0,1},{2,4,5,3,0,-1},{3,6,7,0,0,-1},{1,12,13,4,1,-1},{4,14,15,2,1,-1}};
    for(int i=0;i<6;++i) mesh.SetEdge(i,&edges[i]);
    // Both patch boundaries are the same cubic with opposite local winding.
    VxVector control0(1,.2f,.75f),control1(1,.8f,-.25f);
    mesh.SetVec(2,&control0); mesh.SetVec(3,&control1); mesh.SetVec(16,&control1); mesh.SetVec(17,&control0);
    mesh.SetTVPatchCount(2); mesh.SetTVCount(5); CKTVPatch qTV={{0,1,2,3}},tTV={{1,4,2,-1}};
    mesh.SetTVPatch(0,&qTV); mesh.SetTVPatch(1,&tTV);
    for(int i=0;i<5;++i) mesh.SetTV(i,vertices[i].x,vertices[i].y);
    mesh.SetPatchMaterial(0,reinterpret_cast<CKMaterial *>(&left)); mesh.SetPatchMaterial(1,reinterpret_cast<CKMaterial *>(&right));
    mesh.SetIterationCount(3); mesh.BuildRenderMesh(); ASSERT_EQ(48,mesh.GetFaceCount());
    VxVector edgeControl[]={vertices[1],control0,control1,vertices[2]};
    for(int sample=1;sample<4;++sample) {
        int matches[4]; ASSERT_EQ(1,VerticesAt(mesh,CubicReference(edgeControl,sample*.25f),matches));
        UVIs(mesh,matches[0],1,sample*.25f); int firstUses=0,secondUses=0;
        for(int face=0;face<48;++face) {
            int a,b,c; mesh.GetFaceVertexIndex(face,a,b,c);
            if(a==matches[0] || b==matches[0] || c==matches[0]) { if(face<32) ++firstUses; else ++secondUses; }
        }
        EXPECT_GT(firstUses,0); EXPECT_GT(secondUses,0);
    }
    for(int face=0;face<48;++face) EXPECT_EQ(face<32?reinterpret_cast<CKMaterial *>(&left):reinterpret_cast<CKMaterial *>(&right),mesh.GetFaceMaterial(face));
}

TEST_F(PatchMeshTessellationTest, ReversedEdgeRecordsPreserveEverySamplePositionAndUV) {
    TessellatedPatch mesh(Context); Single(mesh,4,4);
    for(int i=0;i<4;++i) {
        CKPatchEdge edge; mesh.GetEdge(i,&edge); const short first=edge.v1,vec=edge.vec12;
        edge.v1=edge.v2; edge.v2=first; edge.vec12=edge.vec21; edge.vec21=vec; mesh.SetEdge(i,&edge);
    }
    mesh.BuildRenderMesh();
    for(int y=0;y<=4;++y) for(int x=0;x<=4;++x) {
        int matches[4]; ASSERT_EQ(1,VerticesAt(mesh,VxVector(x*.75f,y*.75f,0),matches)); UVIs(mesh,matches[0],x*.25f,y*.25f);
    }
    double area=0;
    for(int face=0;face<mesh.GetFaceCount();++face) {
        int a,b,c; mesh.GetFaceVertexIndex(face,a,b,c); const VxVector pa=Position(mesh,a),pb=Position(mesh,b),pc=Position(mesh,c);
        const float twice=(pb.x-pa.x)*(pc.y-pa.y)-(pb.y-pa.y)*(pc.x-pa.x); EXPECT_LT(twice,0); area-=twice*.5;
    }
    EXPECT_NEAR(9,area,.0001);
}

TEST_F(PatchMeshTessellationTest, InvalidCornerUVLookupAndStridedWritesPreserveUnrelatedPayload) {
    TessellatedPatch mesh(Context); Single(mesh,4,1); float u=-7,v=-9;
    EXPECT_FALSE(mesh.CornerUV(3,0,0,u,v)); EXPECT_FLOAT_EQ(-7,u); EXPECT_FLOAT_EQ(-9,v);
    EXPECT_FALSE(mesh.CornerUV(0,-1,0,u,v)); EXPECT_FALSE(mesh.CornerUV(0,0,4,u,v));
    mesh.GetTVPatches()[0].tv[1]=-1; EXPECT_FALSE(mesh.CornerUV(0,0,1,u,v));
    struct UVRecord { float Before,U,V,After; } records[3]={{1,2,3,4},{5,6,7,8},{9,10,11,12}};
    mesh.WriteUV(&records[0].U,sizeof(UVRecord),1,.25f,.75f);
    EXPECT_FLOAT_EQ(1,records[0].Before); EXPECT_FLOAT_EQ(2,records[0].U); EXPECT_FLOAT_EQ(3,records[0].V); EXPECT_FLOAT_EQ(4,records[0].After);
    EXPECT_FLOAT_EQ(5,records[1].Before); EXPECT_FLOAT_EQ(.25f,records[1].U); EXPECT_FLOAT_EQ(.75f,records[1].V); EXPECT_FLOAT_EQ(8,records[1].After);
    EXPECT_FLOAT_EQ(9,records[2].Before); EXPECT_FLOAT_EQ(10,records[2].U); EXPECT_FLOAT_EQ(11,records[2].V); EXPECT_FLOAT_EQ(12,records[2].After);
}

TEST_F(PatchMeshTessellationTest, AuthoredControlEditAndIterationChangeRefreshPositionsAndNormals) {
    TessellatedPatch mesh(Context); Single(mesh,4,2); mesh.BuildRenderMesh();
    VxVector raised(3,3,3); mesh.SetVert(2,&raised); mesh.SetIterationCount(2); mesh.BuildRenderMesh();
    EXPECT_EQ(16,mesh.GetVertexCount()); EXPECT_EQ(18,mesh.GetFaceCount()); VecIs(Position(mesh,2),raised);
    const VxBbox &box=mesh.GetLocalBox(); EXPECT_NEAR(3,box.Max.z,.0001f);
    EXPECT_LT(Normal(mesh,2).z,0); EXPECT_GT(Normal(mesh,2).z,-1);
}

TEST_F(PatchMeshTessellationTest, EmptyTopologyClearsPreviouslyBuiltRenderMesh) {
    TessellatedPatch mesh(Context); Single(mesh,4,2); mesh.BuildRenderMesh(); ASSERT_EQ(8,mesh.GetFaceCount());
    mesh.SetPatchCount(0); mesh.Dirty(); mesh.BuildRenderMesh(); EXPECT_EQ(0,mesh.GetVertexCount()); EXPECT_EQ(0,mesh.GetFaceCount());
}

TEST_F(PatchMeshTessellationTest, HugeIterationRequestReducesToAnActualRepresentableLattice) {
    TessellatedPatch mesh(Context); Single(mesh,4,2); mesh.SetIterationCount(INT_MAX); mesh.BuildRenderMesh();
    const int steps=mesh.GetIterationCount()+1;
    ASSERT_EQ(253,steps); EXPECT_EQ(64516,mesh.GetVertexCount()); EXPECT_EQ(2*253*253,mesh.GetFaceCount());
    int a,b,c; mesh.GetFaceVertexIndex(mesh.GetFaceCount()-1,a,b,c); EXPECT_LT(a,mesh.GetVertexCount()); EXPECT_LT(b,mesh.GetVertexCount()); EXPECT_LT(c,mesh.GetVertexCount());
    VecIs(Position(mesh,2),VxVector(3,3,0));
}
} // namespace
