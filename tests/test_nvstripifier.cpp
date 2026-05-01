#include "NvStripifier.h"
#include "TestTriangleMultiset.h"

#include <cstdlib>
#include <new>

static bool g_TrackAllocations = false;
static int g_AllocationBalance = 0;

void *operator new(std::size_t size) {
    void *p = std::malloc(size ? size : 1);
    if (!p)
        throw std::bad_alloc();
    if (g_TrackAllocations)
        ++g_AllocationBalance;
    return p;
}

void *operator new[](std::size_t size) {
    void *p = std::malloc(size ? size : 1);
    if (!p)
        throw std::bad_alloc();
    if (g_TrackAllocations)
        ++g_AllocationBalance;
    return p;
}

void operator delete(void *p) noexcept {
    if (g_TrackAllocations && p)
        --g_AllocationBalance;
    std::free(p);
}

void operator delete[](void *p) noexcept {
    if (g_TrackAllocations && p)
        --g_AllocationBalance;
    std::free(p);
}

void operator delete(void *p, std::size_t) noexcept {
    if (g_TrackAllocations && p)
        --g_AllocationBalance;
    std::free(p);
}

void operator delete[](void *p, std::size_t) noexcept {
    if (g_TrackAllocations && p)
        --g_AllocationBalance;
    std::free(p);
}

namespace {

class AllocationTrackingScope {
public:
    AllocationTrackingScope() {
        g_AllocationBalance = 0;
        g_TrackAllocations = true;
    }

    ~AllocationTrackingScope() {
        g_TrackAllocations = false;
    }
};

void BuildTriangleMultisetFromStripStream(const XArray<CKWORD> &stripStream, XArray<TestTriCount> &tris) {
    tris.Resize(0);
    XArray<CKWORD> cur;
    cur.Reserve(64);
    const CKWORD RESTART = (CKWORD)0xFFFF;

    for (int i = 0; i < stripStream.Size(); i++) {
        const CKWORD idx = stripStream[i];
        if (idx == RESTART) {
            cur.Resize(0);
            continue;
        }

        cur.PushBack(idx);
        if (cur.Size() < 3)
            continue;

        const int a = (int)cur[cur.Size() - 3];
        const int b = (int)cur[cur.Size() - 2];
        const int c = (int)cur[cur.Size() - 1];

        // Skip degenerates.
        if (a == b || b == c || a == c)
            continue;

        TestAddTriangle(tris, TestMakeTriKey(a, b, c));
    }
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
                TestFail("Strip has non-adjacent consecutive faces");
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

void StripifyAndDestroyForLeakCheck() {
    XArray<CKWORD> in = MakeGridTris(/*width=*/2, /*height=*/2);
    NvStripifier stripifier;
    XArray<NvStripInfo *> strips;
    stripifier.Stripify(in, /*minStripLen=*/8, /*cacheSize=*/16, /*vertexCount=*/9, strips);
    NvStripifier::DestroyStrips(strips);
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

    XArray<TestTriCount> expected;
    XArray<TestTriCount> actual;
    TestBuildTriangleMultisetFromTriList(inTris, expected);
    BuildTriangleMultisetFromStripStream(out, actual);

    TestCheck(TestSameTriangleMultiset(expected, actual), "Triangle sets differ");

    // Basic stream invariants.
    if (joinStrips) {
        // Joined output should not contain restart markers.
        for (int i = 0; i < out.Size(); i++)
            TestCheck(out[i] != (CKWORD)0xFFFF, "joinStrips=true stream contains 0xFFFF restart");
        TestCheck(outStripCount == (out.Size() > 0 ? 1u : 0u), "joinStrips=true outStripCount unexpected");
    } else {
        // Non-joined output stripCount must be >= 1 when non-empty.
        if (out.Size() > 0)
            TestCheck(outStripCount >= 1u, "joinStrips=false outStripCount should be >= 1 when output non-empty");
    }

    NvStripifier::DestroyStrips(strips);
}

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
    TestCheck(outStripCount == 2u, "Expected 2 strips for two triangles with very high minStripLen");

    XArray<TestTriCount> expected;
    XArray<TestTriCount> actual;
    TestBuildTriangleMultisetFromTriList(in, expected);
    BuildTriangleMultisetFromStripStream(out, actual);
    TestCheck(TestSameTriangleMultiset(expected, actual), "Triangle set mismatch for high minStripLen case");

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

void Test_DestroyStrips_ReleasesStripifyAllocations() {
    // Warm static scratch arrays so the tracked pass only counts per-call allocations.
    StripifyAndDestroyForLeakCheck();

    {
        AllocationTrackingScope scope;
        StripifyAndDestroyForLeakCheck();
    }

    TestCheck(g_AllocationBalance == 0, "Stripify/DestroyStrips should release per-call allocations");
}

} // namespace

int main() {
    TestFramework tf;

    tf.Run("Single triangle join=false", &Test_SingleTriangle_JoinFalse_PreservesTriangles);
    tf.Run("Two triangles min eligible", &Test_TwoTriangles_MinLenCommit_JoinFalse);
    tf.Run("Two triangles min too high", &Test_TwoTriangles_MinLenTooHigh_ProducesTwoStrips);
    tf.Run("2x2 grid join=true", &Test_Grid_JoinTrue_PreservesTriangles_NoRestartMarkers);
    tf.Run("2x2 grid join=false", &Test_Grid_JoinFalse_PreservesTriangles);
    tf.Run("DestroyStrips releases stripify allocations", &Test_DestroyStrips_ReleasesStripifyAllocations);

    return tf.ExitCode();
}
