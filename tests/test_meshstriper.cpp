#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "MeshStriper.h"

namespace {

struct TriKey {
    std::array<int, 3> v;

    static TriKey From(int a, int b, int c) {
        TriKey t{{a, b, c}};
        std::sort(t.v.begin(), t.v.end());
        return t;
    }

    bool operator<(const TriKey &o) const {
        return v < o.v;
    }

    bool operator==(const TriKey &o) const {
        return v == o.v;
    }
};

[[noreturn]] void Fail(const std::string &msg) {
    throw std::runtime_error(msg);
}

void Check(bool cond, const std::string &msg) {
    if (!cond)
        Fail(msg);
}

std::map<TriKey, int> BuildTriangleMultisetFromTriList(const XArray<CKWORD> &triIndices) {
    std::map<TriKey, int> tris;
    Check((triIndices.Size() % 3) == 0, "Input triangle index list must be multiple of 3");

    for (int i = 0; i < triIndices.Size(); i += 3) {
        const int a = (int)triIndices[i + 0];
        const int b = (int)triIndices[i + 1];
        const int c = (int)triIndices[i + 2];
        tris[TriKey::From(a, b, c)]++;
    }

    return tris;
}

template <typename IndexT>
std::map<TriKey, int> BuildTriangleMultisetFromStripSegments(const IndexT *indices, const CKDWORD *stripLens, CKDWORD stripCount) {
    std::map<TriKey, int> tris;
    if (!indices || !stripLens)
        return tris;

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

            tris[TriKey::From(a, b, c)]++;
        }

        cur += len;
    }

    return tris;
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
    Check(ms.Init((CKWORD *)inTris.Begin(), triCount, flags) == TRUE, "MeshStriper::Init failed");

    MeshStriper::Result r{};
    Check(ms.Compute(&r) == TRUE, "MeshStriper::Compute failed");
    Check(r.NbStrips > 0, "NbStrips should be > 0");
    Check(r.StripLengths != nullptr, "StripLengths is null");
    Check(r.StripIndices != nullptr, "StripIndices is null");

    const auto expected = BuildTriangleMultisetFromTriList(inTris);

    if ((flags & CKMESHSTRIPER_INDEX16) != 0) {
        const auto actual = BuildTriangleMultisetFromStripSegments(
            (const CKWORD *)r.StripIndices,
            r.StripLengths,
            r.NbStrips);
        Check(expected == actual, "Triangle set mismatch (INDEX16)");
    } else {
        const auto actual = BuildTriangleMultisetFromStripSegments(
            (const CKDWORD *)r.StripIndices,
            r.StripLengths,
            r.NbStrips);
        Check(expected == actual, "Triangle set mismatch (INDEX32)");
    }

    if ((flags & CKMESHSTRIPER_CONNECTALL) != 0) {
        Check(r.NbStrips == 1, "CONNECTALL should produce exactly 1 strip");
        Check(r.StripLengths[0] >= 3, "CONNECTALL strip length should be >= 3 for non-empty meshes");
    }
}

struct TestFramework {
    int total = 0;
    int passed = 0;

    void Run(const std::string &name, void (*fn)()) {
        total++;
        std::cout << "Running test: " << name << "... ";
        try {
            fn();
            passed++;
            std::cout << "PASSED\n";
        } catch (const std::exception &e) {
            std::cout << "FAILED: " << e.what() << "\n";
        } catch (...) {
            std::cout << "FAILED: unknown exception\n";
        }
    }

    int ExitCode() const {
        std::cout << "\n=== Test Summary ===\n";
        std::cout << "Total tests: " << total << "\n";
        std::cout << "Passed: " << passed << "\n";
        std::cout << "Failed: " << (total - passed) << "\n";
        return (passed == total) ? 0 : 1;
    }
};

// -------------------- Tests --------------------

void Test_InitRejectsInvalidInput() {
    MeshStriper ms;
    Check(ms.Init(nullptr, 1, 0) == FALSE, "Init should fail with null triList");

    XArray<CKWORD> empty;
    Check(ms.Init((CKWORD *)empty.Begin(), 0, 0) == FALSE, "Init should fail with triCount<=0");
}

void Test_ComputeRejectsWithoutInit() {
    MeshStriper ms;
    MeshStriper::Result r{};
    Check(ms.Compute(&r) == FALSE, "Compute should fail without Init");
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
    Check(ms.Init((CKWORD *)in.Begin(), /*triCount=*/3, CKMESHSTRIPER_INDEX16) == FALSE, "Init should fail for non-manifold edge input");
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
