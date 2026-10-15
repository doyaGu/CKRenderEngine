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






















} // namespace
