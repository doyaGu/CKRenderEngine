#include "MeshStriper.h"
#include "TestTriangleMultiset.h"

namespace {

template <typename IndexT>
void BuildTriangleMultisetFromStripSegments(const IndexT *indices, const CKDWORD *stripLens, CKDWORD stripCount, XArray<TestTriCount> &tris) {
    tris.Resize(0);
    if (!indices || !stripLens)
        return;

    const IndexT *cur = indices;
    for (CKDWORD s = 0; s < stripCount; s++) {
        const CKDWORD len = stripLens[(int)s];
        if (len < 3) {
            cur += len;
            continue;
        }

        // Each strip segment is an independent triangle strip.
        for (CKDWORD i = 2; i < len; i++) {
            const int a = (int)cur[(int)(i - 2)];
            const int b = (int)cur[(int)(i - 1)];
            const int c = (int)cur[(int)i];

            // Skip degenerates (parity fix / connectall may introduce duplicates).
            if (a == b || b == c || a == c)
                continue;

            TestAddTriangle(tris, TestMakeTriKey(a, b, c));
        }

        cur += len;
    }
}

XArray<CKWORD> MakeTwoTrianglesSquare() {
    // Two triangles forming a square: (0,1,2) and (2,1,3)
    XArray<CKWORD> tris;
    tris.PushBack(0);
    tris.PushBack(1);
    tris.PushBack(2);
    tris.PushBack(2);
    tris.PushBack(1);
    tris.PushBack(3);
    return tris;
}

XArray<CKWORD> MakeGridTris(int width, int height) {
    XArray<CKWORD> tris;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const int v00 = y * (width + 1) + x;
            const int v10 = y * (width + 1) + (x + 1);
            const int v01 = (y + 1) * (width + 1) + x;
            const int v11 = (y + 1) * (width + 1) + (x + 1);

            tris.PushBack((CKWORD)v00);
            tris.PushBack((CKWORD)v10);
            tris.PushBack((CKWORD)v01);

            tris.PushBack((CKWORD)v01);
            tris.PushBack((CKWORD)v10);
            tris.PushBack((CKWORD)v11);
        }
    }
    return tris;
}

XArray<CKWORD> MakeTwoDisconnectedSquares() {
    // Square A uses vertices 0..3
    // Square B uses vertices 10..13
    XArray<CKWORD> tris;

    // A
    tris.PushBack(0);
    tris.PushBack(1);
    tris.PushBack(2);
    tris.PushBack(2);
    tris.PushBack(1);
    tris.PushBack(3);

    // B
    tris.PushBack(10);
    tris.PushBack(11);
    tris.PushBack(12);
    tris.PushBack(12);
    tris.PushBack(11);
    tris.PushBack(13);

    return tris;
}

XArray<CKWORD> MakeNonManifoldEdge() {
    // 3 triangles share edge (0,1) => MeshAdjacency::Compute should fail.
    XArray<CKWORD> tris;
    tris.PushBack(0);
    tris.PushBack(1);
    tris.PushBack(2);
    tris.PushBack(0);
    tris.PushBack(1);
    tris.PushBack(3);
    tris.PushBack(0);
    tris.PushBack(1);
    tris.PushBack(4);
    return tris;
}

void RunMeshStriperAndCheck(const XArray<CKWORD> &inTris, int triCount, CKDWORD flags) {
    MeshStriper ms;
    TestCheck(ms.Init((CKWORD *)inTris.Begin(), triCount, flags) == TRUE, "MeshStriper::Init failed");

    MeshStriper::Result r{};
    TestCheck(ms.Compute(&r) == TRUE, "MeshStriper::Compute failed");
    TestCheck(r.NbStrips > 0, "NbStrips should be > 0");
    TestCheck(r.StripLengths != nullptr, "StripLengths is null");
    TestCheck(r.StripIndices != nullptr, "StripIndices is null");

    XArray<TestTriCount> expected;
    XArray<TestTriCount> actual;
    TestBuildTriangleMultisetFromTriList(inTris, expected);

    if ((flags & CKMESHSTRIPER_INDEX16) != 0) {
        BuildTriangleMultisetFromStripSegments(
            (const CKWORD *)r.StripIndices,
            r.StripLengths,
            r.NbStrips,
            actual);
        TestCheck(TestSameTriangleMultiset(expected, actual), "Triangle set mismatch (INDEX16)");
    } else {
        BuildTriangleMultisetFromStripSegments(
            (const CKDWORD *)r.StripIndices,
            r.StripLengths,
            r.NbStrips,
            actual);
        TestCheck(TestSameTriangleMultiset(expected, actual), "Triangle set mismatch (INDEX32)");
    }

    if ((flags & CKMESHSTRIPER_CONNECTALL) != 0) {
        TestCheck(r.NbStrips == 1, "CONNECTALL should produce exactly 1 strip");
        TestCheck(r.StripLengths[0] >= 3, "CONNECTALL strip length should be >= 3 for non-empty meshes");
    }
}

// -------------------- Tests --------------------

void Test_InitRejectsInvalidInput() {
    MeshStriper ms;
    TestCheck(ms.Init(nullptr, 1, 0) == FALSE, "Init should fail with null triList");

    XArray<CKWORD> empty;
    TestCheck(ms.Init((CKWORD *)empty.Begin(), 0, 0) == FALSE, "Init should fail with triCount<=0");
}

void Test_ComputeRejectsWithoutInit() {
    MeshStriper ms;
    MeshStriper::Result r{};
    TestCheck(ms.Compute(&r) == FALSE, "Compute should fail without Init");
}

void Test_SingleTriangle_Index16_NoConnect() {
    XArray<CKWORD> in;
    in.PushBack(0);
    in.PushBack(1);
    in.PushBack(2);

    RunMeshStriperAndCheck(in, /*triCount=*/1, CKMESHSTRIPER_INDEX16);
}

void Test_TwoTriangles_AllFlags_Index16() {
    XArray<CKWORD> in = MakeTwoTrianglesSquare();

    const CKDWORD flags = CKMESHSTRIPER_INDEX16 | CKMESHSTRIPER_PARITYFIX | CKMESHSTRIPER_SORTSEEDS | CKMESHSTRIPER_CONNECTALL;
    RunMeshStriperAndCheck(in, /*triCount=*/2, flags);
}

void Test_Grid2x2_Index16_NoConnect() {
    XArray<CKWORD> in = MakeGridTris(/*width=*/2, /*height=*/2);
    const CKDWORD flags = CKMESHSTRIPER_INDEX16 | CKMESHSTRIPER_SORTSEEDS;
    RunMeshStriperAndCheck(in, /*triCount=*/8, flags);
}

void Test_Grid2x2_Index32_ConnectAll_ParityFix() {
    XArray<CKWORD> in = MakeGridTris(/*width=*/2, /*height=*/2);
    const CKDWORD flags = CKMESHSTRIPER_PARITYFIX | CKMESHSTRIPER_SORTSEEDS | CKMESHSTRIPER_CONNECTALL;
    RunMeshStriperAndCheck(in, /*triCount=*/8, flags);
}

void Test_DisconnectedSquares_ConnectAll() {
    XArray<CKWORD> in = MakeTwoDisconnectedSquares();
    const CKDWORD flags = CKMESHSTRIPER_INDEX16 | CKMESHSTRIPER_CONNECTALL | CKMESHSTRIPER_PARITYFIX;
    RunMeshStriperAndCheck(in, /*triCount=*/4, flags);
}

void Test_NonManifoldEdge_InitFails() {
    XArray<CKWORD> in = MakeNonManifoldEdge();
    MeshStriper ms;
    TestCheck(ms.Init((CKWORD *)in.Begin(), /*triCount=*/3, CKMESHSTRIPER_INDEX16) == FALSE, "Init should fail for non-manifold edge input");
}

} // namespace

int main() {
    TestFramework tf;

    tf.Run("Init rejects invalid input", &Test_InitRejectsInvalidInput);
    tf.Run("Compute rejects without Init", &Test_ComputeRejectsWithoutInit);
    tf.Run("Single triangle INDEX16 no connect", &Test_SingleTriangle_Index16_NoConnect);
    tf.Run("Two triangles all flags INDEX16", &Test_TwoTriangles_AllFlags_Index16);
    tf.Run("2x2 grid INDEX16 no connect", &Test_Grid2x2_Index16_NoConnect);
    tf.Run("2x2 grid INDEX32 connectall parityfix", &Test_Grid2x2_Index32_ConnectAll_ParityFix);
    tf.Run("Disconnected squares connectall", &Test_DisconnectedSquares_ConnectAll);
    tf.Run("Non-manifold edge init fails", &Test_NonManifoldEdge_InitFails);

    return tf.ExitCode();
}
