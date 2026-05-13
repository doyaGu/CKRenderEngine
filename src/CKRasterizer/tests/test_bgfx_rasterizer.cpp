#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include <atomic>
#include <cassert>

#include "CKBgfxRasterizer.h"
#include "CKBgfxInternal.h"

// Pull in bgfx defines for PT mask constants
#include <bgfx/defines.h>

// ============================================================================
// Test infrastructure
// ============================================================================

static int g_TestCount = 0;
static int g_PassCount = 0;
static int g_FailCount = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_TestCount++; \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d): %s\n", msg, __LINE__, #cond); \
        g_FailCount++; \
    } else { \
        g_PassCount++; \
    } \
} while(0)

#define TEST_SECTION(name) printf("\n[%s]\n", name)

// ============================================================================
// Test 1: Fill mode / topology interaction
// ============================================================================

static void TestFillModeTopology()
{
    TEST_SECTION("Fill Mode / Topology Interaction");

    // Case 1: Solid fill + triangle list = no PT bits (default = triangles)
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_SOLID).Topology(VX_TRIANGLELIST);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        TEST_ASSERT(pt == 0, "solid + trianglelist => no PT bits");
    }

    // Case 2: Solid fill + line list = PT_LINES
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_SOLID).Topology(VX_LINELIST);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        TEST_ASSERT(pt == BGFX_STATE_PT_LINES, "solid + linelist => PT_LINES");
    }

    // Case 3: Solid fill + point list = PT_POINTS
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_SOLID).Topology(VX_POINTLIST);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        TEST_ASSERT(pt == BGFX_STATE_PT_POINTS, "solid + pointlist => PT_POINTS");
    }

    // Case 4: Solid fill + tri strip = PT_TRISTRIP
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_SOLID).Topology(VX_TRIANGLESTRIP);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        TEST_ASSERT(pt == BGFX_STATE_PT_TRISTRIP, "solid + tristrip => PT_TRISTRIP");
    }

    // Case 5: Solid fill + line strip = PT_LINESTRIP
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_SOLID).Topology(VX_LINESTRIP);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        TEST_ASSERT(pt == BGFX_STATE_PT_LINESTRIP, "solid + linestrip => PT_LINESTRIP");
    }

    // Case 6: Wireframe fill overrides topology = always PT_LINES
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_WIREFRAME).Topology(VX_TRIANGLELIST);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        TEST_ASSERT(pt == BGFX_STATE_PT_LINES, "wireframe + trianglelist => PT_LINES (overridden)");
    }

    // Case 7: Wireframe fill overrides tri strip too
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_WIREFRAME).Topology(VX_TRIANGLESTRIP);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        TEST_ASSERT(pt == BGFX_STATE_PT_LINES, "wireframe + tristrip => PT_LINES (overridden)");
    }

    // Case 8: Point fill overrides topology = always PT_POINTS
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_POINT).Topology(VX_TRIANGLELIST);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        TEST_ASSERT(pt == BGFX_STATE_PT_POINTS, "point + trianglelist => PT_POINTS (overridden)");
    }

    // Case 9: Point fill overrides line strip
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_POINT).Topology(VX_LINESTRIP);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        TEST_ASSERT(pt == BGFX_STATE_PT_POINTS, "point + linestrip => PT_POINTS (overridden)");
    }

    // Case 10: Verify no spurious bit collisions (old bug: OR of both would set invalid combo)
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_WIREFRAME).Topology(VX_TRIANGLESTRIP);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        // Old code would produce PT_LINES | PT_TRISTRIP = 0x0003... (PT_LINESTRIP)
        // New code produces only PT_LINES
        TEST_ASSERT(pt == BGFX_STATE_PT_LINES, "no spurious bit collision from fill+topology");
        TEST_ASSERT((pt & BGFX_STATE_PT_MASK) == BGFX_STATE_PT_LINES,
                    "masked result is purely PT_LINES");
    }
}

// ============================================================================
// Test 2: Atomic encoder slot acquisition (thread safety)
// ============================================================================

static void TestEncoderSlotAtomic()
{
    TEST_SECTION("Atomic Encoder Slot Acquisition");

    // Simulate the CAS pattern used by BeginEncoder
    static const int NUM_SLOTS = CKRST_MAX_ENCODERS;
    std::atomic<CKBOOL> slots[NUM_SLOTS];
    for (int i = 0; i < NUM_SLOTS; ++i)
        slots[i].store(FALSE, std::memory_order_relaxed);

    static const int NUM_THREADS = 16;
    std::atomic<int> acquired{0};
    std::atomic<int> collisions{0};
    std::atomic<bool> go{false};

    auto worker = [&]() {
        while (!go.load(std::memory_order_acquire)) {}

        for (int i = 0; i < NUM_SLOTS; ++i)
        {
            CKBOOL expected = FALSE;
            if (slots[i].compare_exchange_strong(
                    expected, TRUE,
                    std::memory_order_acq_rel, std::memory_order_relaxed))
            {
                acquired.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        collisions.fetch_add(1, std::memory_order_relaxed);
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; ++t)
        threads.emplace_back(worker);

    go.store(true, std::memory_order_release);

    for (auto &t : threads)
        t.join();

    int totalAcquired = acquired.load();
    int totalCollisions = collisions.load();

    TEST_ASSERT(totalAcquired + totalCollisions == NUM_THREADS,
                "all threads completed (acquired + rejected = total)");
    TEST_ASSERT(totalAcquired <= NUM_SLOTS,
                "no more slots acquired than available");
    TEST_ASSERT(totalCollisions == NUM_THREADS - NUM_SLOTS || totalAcquired == NUM_SLOTS,
                "exactly NUM_SLOTS threads acquired when threads > slots");

    // Verify no slot was double-acquired
    int activeCount = 0;
    for (int i = 0; i < NUM_SLOTS; ++i)
        if (slots[i].load()) activeCount++;
    TEST_ASSERT(activeCount == totalAcquired, "no double-acquisition of any slot");

    printf("  (threads=%d, slots=%d, acquired=%d, rejected=%d)\n",
           NUM_THREADS, NUM_SLOTS, totalAcquired, totalCollisions);
}

// ============================================================================
// Test 3: Atomic transient buffer counter (thread safety)
// ============================================================================

static void TestTransientCounterAtomic()
{
    TEST_SECTION("Atomic Transient Buffer Counter");

    static const CKDWORD MAX_POOL = 256;
    std::atomic<CKDWORD> counter{0};

    static const int NUM_THREADS = 32;
    static const int ALLOCS_PER_THREAD = 20;
    std::atomic<int> successCount{0};
    std::atomic<int> failCount{0};
    std::atomic<bool> go{false};

    auto worker = [&]() {
        while (!go.load(std::memory_order_acquire)) {}

        for (int i = 0; i < ALLOCS_PER_THREAD; ++i)
        {
            CKDWORD slot = counter.fetch_add(1, std::memory_order_acq_rel);
            if (slot >= MAX_POOL)
            {
                counter.fetch_sub(1, std::memory_order_relaxed);
                failCount.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                successCount.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; ++t)
        threads.emplace_back(worker);

    go.store(true, std::memory_order_release);

    for (auto &t : threads)
        t.join();

    int totalSuccess = successCount.load();
    int totalFail = failCount.load();
    CKDWORD finalCounter = counter.load();

    TEST_ASSERT(totalSuccess + totalFail == NUM_THREADS * ALLOCS_PER_THREAD,
                "all alloc attempts accounted for");
    TEST_ASSERT((CKDWORD)totalSuccess <= MAX_POOL,
                "never exceeded max pool size");
    TEST_ASSERT(finalCounter == (CKDWORD)totalSuccess,
                "final counter matches successful allocations");
    TEST_ASSERT(finalCounter <= MAX_POOL,
                "final counter within pool bounds");

    printf("  (threads=%d, allocs_each=%d, total_attempted=%d, success=%d, rejected=%d, final=%u)\n",
           NUM_THREADS, ALLOCS_PER_THREAD, NUM_THREADS * ALLOCS_PER_THREAD,
           totalSuccess, totalFail, (unsigned)finalCounter);
}

// ============================================================================
// Test 4: CKDrawStateBuilder bit layout correctness
// ============================================================================

static void TestDrawStateBuilderLayout()
{
    TEST_SECTION("CKDrawStateBuilder Bit Layout");

    // Verify fill mode bits are at Lo[13:12]
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_WIREFRAME);
        CKDrawState s = b.Build();
        CKDWORD fillBits = (s.Lo >> 12) & 0x3;
        TEST_ASSERT(fillBits == 1, "VXFILL_WIREFRAME maps to 1 in bits [13:12]");
    }
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_POINT);
        CKDrawState s = b.Build();
        CKDWORD fillBits = (s.Lo >> 12) & 0x3;
        TEST_ASSERT(fillBits == 2, "VXFILL_POINT maps to 2 in bits [13:12]");
    }
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_SOLID);
        CKDrawState s = b.Build();
        CKDWORD fillBits = (s.Lo >> 12) & 0x3;
        TEST_ASSERT(fillBits == 0, "VXFILL_SOLID maps to 0 in bits [13:12]");
    }

    // Verify topology bits are at Mid[8:6]
    {
        CKDrawStateBuilder b;
        b.Topology(VX_POINTLIST);
        CKDrawState s = b.Build();
        CKDWORD ptBits = (s.Mid >> 6) & 0x7;
        TEST_ASSERT(ptBits == VX_POINTLIST, "VX_POINTLIST stored correctly");
    }
    {
        CKDrawStateBuilder b;
        b.Topology(VX_LINELIST);
        CKDrawState s = b.Build();
        CKDWORD ptBits = (s.Mid >> 6) & 0x7;
        TEST_ASSERT(ptBits == VX_LINELIST, "VX_LINELIST stored correctly");
    }
    {
        CKDrawStateBuilder b;
        b.Topology(VX_TRIANGLESTRIP);
        CKDrawState s = b.Build();
        CKDWORD ptBits = (s.Mid >> 6) & 0x7;
        TEST_ASSERT(ptBits == VX_TRIANGLESTRIP, "VX_TRIANGLESTRIP stored correctly");
    }

    // Verify fill mode doesn't clobber other Lo bits
    {
        CKDrawStateBuilder b;
        b.Depth(TRUE, TRUE, VXCMP_LESSEQUAL).Fill(VXFILL_WIREFRAME);
        CKDrawState s = b.Build();
        TEST_ASSERT(s.Lo & CKRST_STATE_DEPTH_TEST, "depth test preserved after fill set");
        TEST_ASSERT(s.Lo & CKRST_STATE_DEPTH_WRITE, "depth write preserved after fill set");
        CKDWORD fillBits = (s.Lo >> 12) & 0x3;
        TEST_ASSERT(fillBits == 1, "fill mode set correctly alongside other bits");
    }
}

static CKDWORD ExtractStencilRef(uint32_t stencil)
{
    return (stencil & BGFX_STENCIL_FUNC_REF_MASK) >> BGFX_STENCIL_FUNC_REF_SHIFT;
}

static CKDWORD ExtractStencilRMask(uint32_t stencil)
{
    return (stencil & BGFX_STENCIL_FUNC_RMASK_MASK) >> BGFX_STENCIL_FUNC_RMASK_SHIFT;
}

static void TestBgfxStencilWriteMaskEncoding()
{
    TEST_SECTION("Bgfx Stencil Write Mask Encoding");

    CKDrawState enabled = CKDrawStateBuilder()
        .Stencil(TRUE, VXCMP_ALWAYS,
                 VXSTENCILOP_REPLACE, VXSTENCILOP_INCRSAT, VXSTENCILOP_DECRSAT)
        .Build();

    uint32_t full = CKBgfxBuildFrontStencil(enabled, 0xAB, 0xFF, 0xFF);
    TEST_ASSERT(full != BGFX_STENCIL_NONE, "enabled stencil produces bgfx stencil state");
    TEST_ASSERT(ExtractStencilRef(full) == 0xAB, "full write mask keeps low 8-bit ref");
    TEST_ASSERT(ExtractStencilRMask(full) == 0xFF, "read mask is encoded separately");

    uint32_t disabled = CKBgfxBuildFrontStencil(CKDrawState(), 0xAB, 0xFF, 0xFF);
    TEST_ASSERT(disabled == BGFX_STENCIL_NONE, "disabled stencil clears bgfx stencil state");

    CKDrawState disabledWithBackOps = CKDrawStateBuilder()
        .Stencil(FALSE, VXCMP_ALWAYS,
                 VXSTENCILOP_REPLACE, VXSTENCILOP_INCRSAT, VXSTENCILOP_DECRSAT)
        .StencilBack(VXCMP_ALWAYS,
                     VXSTENCILOP_REPLACE, VXSTENCILOP_INCRSAT, VXSTENCILOP_DECRSAT)
        .Build();
    TEST_ASSERT(CKBgfxBuildFrontStencil(disabledWithBackOps, 0xAB, 0xFF, 0xFF) == BGFX_STENCIL_NONE,
                "disabled stencil clears front state even if ops are present");
    TEST_ASSERT(CKBgfxBuildBackStencil(disabledWithBackOps, 0xAB, 0xFF, 0xFF) == BGFX_STENCIL_NONE,
                "disabled stencil clears back state even if back ops are present");

    uint32_t noWrite = CKBgfxBuildFrontStencil(enabled, 0xAB, 0xFF, 0x00);
    TEST_ASSERT((noWrite & BGFX_STENCIL_OP_FAIL_S_MASK) == BGFX_STENCIL_OP_FAIL_S_KEEP,
                "writeMask zero forces stencil fail op to KEEP");
    TEST_ASSERT((noWrite & BGFX_STENCIL_OP_FAIL_Z_MASK) == BGFX_STENCIL_OP_FAIL_Z_KEEP,
                "writeMask zero forces depth fail op to KEEP");
    TEST_ASSERT((noWrite & BGFX_STENCIL_OP_PASS_Z_MASK) == BGFX_STENCIL_OP_PASS_Z_KEEP,
                "writeMask zero forces pass op to KEEP");

    uint32_t masked = CKBgfxBuildFrontStencil(enabled, 0xAB, 0x0F, 0x0F);
    TEST_ASSERT(ExtractStencilRef(masked) == 0x0B,
                "matching read/write mask constrains ref to writable bits");
    TEST_ASSERT(ExtractStencilRMask(masked) == 0x0F,
                "matching read/write mask keeps compare mask");

    uint32_t partial = CKBgfxBuildFrontStencil(enabled, 0xAB, 0xF0, 0x0F);
    TEST_ASSERT(ExtractStencilRef(partial) == 0xAB,
                "partial write mask with distinct read mask keeps compare ref");
    TEST_ASSERT(ExtractStencilRMask(partial) == 0xF0,
                "partial write mask with distinct read mask keeps read mask");
}

static void TestTextureVolumeDescriptorDefaults()
{
    TEST_SECTION("Texture Volume Descriptor Defaults");

    CKTextureDesc desc;
    TEST_ASSERT(desc.Depth == 1, "texture desc defaults to a single 2D slice");

    desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_VOLUMEMAP;
    desc.Depth = 4;
    TEST_ASSERT((desc.Flags & CKRST_TEXTURE_VOLUMEMAP) != 0 && desc.Depth == 4,
                "volume texture desc carries explicit depth");
}

static void TestSamplerCompareFlags()
{
    TEST_SECTION("Sampler Compare Flags");

    CKSamplerDesc sampler = {};
    sampler.CompareFunc = CKRST_COMPARE_NONE;
    TEST_ASSERT((CKBgfxSamplerFlags(&sampler) & BGFX_SAMPLER_COMPARE_LEQUAL) == 0,
                "compare NONE emits no compare flags");

    sampler.CompareFunc = CKRST_COMPARE_LEQUAL;
    TEST_ASSERT((CKBgfxSamplerFlags(&sampler) & BGFX_SAMPLER_COMPARE_LEQUAL) != 0,
                "compare LEQUAL emits bgfx compare flag");
}

// ============================================================================
// Test 5: Rasterizer start/close lifecycle
// ============================================================================

static void TestBgfxRasterizerLifecycle()
{
    TEST_SECTION("CKBgfxRasterizer Start/Close Lifecycle");

    CKBgfxRasterizer rasterizer;
    TEST_ASSERT(rasterizer.GetDriverCount() == 0, "new rasterizer has no drivers");
    TEST_ASSERT(rasterizer.Start(NULL) == TRUE, "start succeeds without creating a context");
    TEST_ASSERT(rasterizer.GetDriverCount() == 1, "start creates one bgfx driver");
    TEST_ASSERT(rasterizer.Start(NULL) == TRUE, "repeated start is idempotent");
    TEST_ASSERT(rasterizer.GetDriverCount() == 1, "repeated start does not add another driver");

    CKRasterizerDriver *driver = rasterizer.GetDriver(0);
    TEST_ASSERT(driver != NULL, "driver exists after start");
    TEST_ASSERT(driver->m_Owner == &rasterizer, "driver owner points to rasterizer");
    TEST_ASSERT(driver->m_Hardware == TRUE, "bgfx driver is marked hardware");

    rasterizer.Close();
    TEST_ASSERT(rasterizer.GetDriverCount() == 0, "close removes driver");
    rasterizer.Close();
    TEST_ASSERT(rasterizer.GetDriverCount() == 0, "repeated close is safe");
}

// ============================================================================
// Test 6: Stress test - multi-threaded slot acquisition with release/reuse
// ============================================================================

static void TestEncoderSlotReuse()
{
    TEST_SECTION("Encoder Slot Reuse Under Contention");

    static const int NUM_SLOTS = CKRST_MAX_ENCODERS;
    std::atomic<CKBOOL> slots[NUM_SLOTS];
    for (int i = 0; i < NUM_SLOTS; ++i)
        slots[i].store(FALSE, std::memory_order_relaxed);

    static const int NUM_THREADS = 8;
    static const int ITERATIONS = 1000;
    std::atomic<int> totalAcquired{0};
    std::atomic<bool> go{false};

    auto worker = [&]() {
        while (!go.load(std::memory_order_acquire)) {}

        for (int iter = 0; iter < ITERATIONS; ++iter)
        {
            int acquired = -1;
            for (int i = 0; i < NUM_SLOTS; ++i)
            {
                CKBOOL expected = FALSE;
                if (slots[i].compare_exchange_strong(
                        expected, TRUE,
                        std::memory_order_acq_rel, std::memory_order_relaxed))
                {
                    acquired = i;
                    break;
                }
            }

            if (acquired >= 0)
            {
                totalAcquired.fetch_add(1, std::memory_order_relaxed);
                // Simulate some work
                for (volatile int x = 0; x < 10; ++x) {}
                // Release
                slots[acquired].store(FALSE, std::memory_order_release);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; ++t)
        threads.emplace_back(worker);

    go.store(true, std::memory_order_release);

    for (auto &t : threads)
        t.join();

    int total = totalAcquired.load();
    TEST_ASSERT(total > 0, "some acquisitions succeeded");
    TEST_ASSERT(total <= NUM_THREADS * ITERATIONS, "no more than max possible acquisitions");

    // All slots should be released
    int finalActive = 0;
    for (int i = 0; i < NUM_SLOTS; ++i)
        if (slots[i].load()) finalActive++;
    TEST_ASSERT(finalActive == 0, "all slots released after completion");

    printf("  (threads=%d, iterations_each=%d, total_acquired=%d)\n",
           NUM_THREADS, ITERATIONS, total);
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("=== CKBgfxRasterizer Unit Tests ===\n");

    TestFillModeTopology();
    TestDrawStateBuilderLayout();
    TestBgfxStencilWriteMaskEncoding();
    TestTextureVolumeDescriptorDefaults();
    TestSamplerCompareFlags();
    TestEncoderSlotAtomic();
    TestTransientCounterAtomic();
    TestBgfxRasterizerLifecycle();
    TestEncoderSlotReuse();

    printf("\n=== Results: %d passed, %d failed, %d total ===\n",
           g_PassCount, g_FailCount, g_TestCount);

    return g_FailCount == 0 ? 0 : 1;
}
