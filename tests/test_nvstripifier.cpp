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

#include "NvStripifier.h"

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

std::map<TriKey, int> BuildTriangleMultisetFromStripStream(const XArray<CKWORD> &stripStream) {
    std::map<TriKey, int> tris;

    std::vector<CKWORD> cur;
    cur.reserve(64);

    auto flush = [&]() {
        cur.clear();
    };

    const CKWORD RESTART = (CKWORD)0xFFFF;

    for (int i = 0; i < stripStream.Size(); i++) {
        const CKWORD idx = stripStream[i];
        if (idx == RESTART) {
            flush();
            continue;
        }

        cur.push_back(idx);
        if (cur.size() < 3)
            continue;

        const int a = (int)cur[cur.size() - 3];
        const int b = (int)cur[cur.size() - 2];
        const int c = (int)cur[cur.size() - 1];

        // Skip degenerates.
        if (a == b || b == c || a == c)
            continue;

        tris[TriKey::From(a, b, c)]++;
    }

    return tris;
}

bool FaceSharesTwoVertices(const NvFaceInfo *a, const NvFaceInfo *b) {
    if (!a || !b)
        return false;
    int shared = 0;
    for (int i = 0; i < 3; i++) {
        const int v = a->V[i];
        if (v == b->V[0] || v == b->V[1] || v == b->V[2])
            shared++;
    }
    return shared == 2;
}

void CheckStripFaceAdjacency(const XArray<NvStripInfo *> &strips) {
    for (int s = 0; s < strips.Size(); s++) {
        const NvStripInfo *strip = strips[s];
        if (!strip)
            continue;
        if (strip->Faces.Size() <= 1)
            continue;

        for (int i = 1; i < strip->Faces.Size(); i++) {
            const NvFaceInfo *prev = strip->Faces[i - 1];
            const NvFaceInfo *cur = strip->Faces[i];
            if (!FaceSharesTwoVertices(prev, cur)) {
                std::ostringstream oss;
                oss << "Strip " << s << " has non-adjacent consecutive faces at " << (i - 1) << " and " << i;
                Fail(oss.str());
            }
        }
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
    // grid vertices: (width+1)*(height+1)
    // each quad -> 2 triangles
    XArray<CKWORD> tris;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const int v00 = y * (width + 1) + x;
            const int v10 = y * (width + 1) + (x + 1);
            const int v01 = (y + 1) * (width + 1) + x;
            const int v11 = (y + 1) * (width + 1) + (x + 1);

            // Consistent diagonal.
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

void RunStripifyAndCheck(
    const XArray<CKWORD> &inTris,
    CKWORD vertexCount,
    int minStripLen,
    bool joinStrips) {

    NvStripifier stripifier;
    XArray<NvStripInfo *> strips;

    stripifier.Stripify(inTris, minStripLen, /*cacheSize=*/16, vertexCount, strips);
    CheckStripFaceAdjacency(strips);

    XArray<CKWORD> out;
    CKDWORD outStripCount = 0;
    NvStripifier::CreateStrips(strips, out, joinStrips, outStripCount);

    const auto expected = BuildTriangleMultisetFromTriList(inTris);
    const auto actual = BuildTriangleMultisetFromStripStream(out);

    if (expected != actual) {
        std::ostringstream oss;
        oss << "Triangle sets differ (joinStrips=" << (joinStrips ? "true" : "false") << ")\n";
        oss << "Expected unique tris: " << expected.size() << ", actual: " << actual.size() << "\n";
        Fail(oss.str());
    }

    // Basic stream invariants.
    if (joinStrips) {
        // Joined output should not contain restart markers.
        for (int i = 0; i < out.Size(); i++)
            Check(out[i] != (CKWORD)0xFFFF, "joinStrips=true stream contains 0xFFFF restart");
        Check(outStripCount == (out.Size() > 0 ? 1u : 0u), "joinStrips=true outStripCount unexpected");
    } else {
        // Non-joined output stripCount must be >= 1 when non-empty.
        if (out.Size() > 0)
            Check(outStripCount >= 1u, "joinStrips=false outStripCount should be >= 1 when output non-empty");
    }

    NvStripifier::DestroyStrips(strips);
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

void Test_SingleTriangle_JoinFalse_PreservesTriangles() {
    XArray<CKWORD> in;
    in.PushBack(0);
    in.PushBack(1);
    in.PushBack(2);

    RunStripifyAndCheck(in, /*vertexCount=*/3, /*minStripLen=*/6, /*joinStrips=*/false);
}

void Test_TwoTriangles_MinLenCommit_JoinFalse() {
    XArray<CKWORD> in = MakeTwoTrianglesSquare();
    // minStripLen=8 => internalMin=max(1,8-6)=2, so the 2-face strip should be eligible.
    RunStripifyAndCheck(in, /*vertexCount=*/4, /*minStripLen=*/8, /*joinStrips=*/false);
}

void Test_TwoTriangles_MinLenTooHigh_ProducesTwoStrips() {
    XArray<CKWORD> in = MakeTwoTrianglesSquare();

    NvStripifier stripifier;
    XArray<NvStripInfo *> strips;
    stripifier.Stripify(in, /*minStripLen=*/64, /*cacheSize=*/16, /*vertexCount=*/4, strips);

    XArray<CKWORD> out;
    CKDWORD outStripCount = 0;
    NvStripifier::CreateStrips(strips, out, /*joinStrips=*/false, outStripCount);

    // With a very high min, we expect no committed multi-face strip; both faces should fall back to single-triangle strips.
    Check(outStripCount == 2u, "Expected 2 strips for two triangles with very high minStripLen");

    const auto expected = BuildTriangleMultisetFromTriList(in);
    const auto actual = BuildTriangleMultisetFromStripStream(out);
    Check(expected == actual, "Triangle set mismatch for high minStripLen case");

    NvStripifier::DestroyStrips(strips);
}

void Test_Grid_JoinTrue_PreservesTriangles_NoRestartMarkers() {
    // 2x2 quads => 8 triangles, 3x3 vertices => 9 vertices
    XArray<CKWORD> in = MakeGridTris(/*width=*/2, /*height=*/2);
    RunStripifyAndCheck(in, /*vertexCount=*/9, /*minStripLen=*/8, /*joinStrips=*/true);
}

void Test_Grid_JoinFalse_PreservesTriangles() {
    XArray<CKWORD> in = MakeGridTris(/*width=*/2, /*height=*/2);
    RunStripifyAndCheck(in, /*vertexCount=*/9, /*minStripLen=*/8, /*joinStrips=*/false);
}

} // namespace

int main() {
    TestFramework tf;

    tf.Run("Single triangle join=false", &Test_SingleTriangle_JoinFalse_PreservesTriangles);
    tf.Run("Two triangles min eligible", &Test_TwoTriangles_MinLenCommit_JoinFalse);
    tf.Run("Two triangles min too high", &Test_TwoTriangles_MinLenTooHigh_ProducesTwoStrips);
    tf.Run("2x2 grid join=true", &Test_Grid_JoinTrue_PreservesTriangles_NoRestartMarkers);
    tf.Run("2x2 grid join=false", &Test_Grid_JoinFalse_PreservesTriangles);

    return tf.ExitCode();
}
