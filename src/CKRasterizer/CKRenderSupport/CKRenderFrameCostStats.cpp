#include "CKRenderFrameCostStats.h"
#include "CKRenderSettings.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

#include <cstdio>
#include <cstring>
#include <ctime>

struct CKRenderFrameCostStatsState {
    CKBOOL Initialized;
    CKBOOL Enabled;
    CKBOOL Collecting;
    CKBOOL Completed;
    CKBOOL SummaryWritten;
    CKBOOL OutputEnabled;
    CKBOOL AcceptPlayerTiming;
    CKDWORD WarmupFrames;
    CKDWORD SampleFrames;
    CKDWORD FrameIndex;
    CKRenderFrameCostStatsSnapshot Totals;
};

static CKRenderFrameCostStatsState g_FrameCostStats = {};

#ifdef _WIN32
static double CKRenderFrameCostCounterFrequency()
{
    static CKBOOL s_Initialized = FALSE;
    static double s_Frequency = 0.0;
    LARGE_INTEGER freq;
    if (!s_Initialized) {
        s_Initialized = TRUE;
        if (QueryPerformanceFrequency(&freq) && freq.QuadPart > 0)
            s_Frequency = (double)freq.QuadPart;
    }
    return s_Frequency;
}
#endif

static int CKRenderFrameCostStringEquals(const char *a, const char *b)
{
    if (!a || !b)
        return a == b;
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb + ('a' - 'A'));
        if (ca != cb)
            return 0;
        ++a;
        ++b;
    }
    return *a == *b;
}

double CKRenderFrameCostNow()
{
#ifdef _WIN32
    LARGE_INTEGER value;
    if (CKRenderFrameCostCounterFrequency() > 0.0 &&
        QueryPerformanceCounter(&value)) {
        return (double)value.QuadPart;
    }
#endif
    return (double)clock();
}

double CKRenderFrameCostElapsedUs(double start)
{
#ifdef _WIN32
    double freq = CKRenderFrameCostCounterFrequency();
    if (freq > 0.0)
        return (CKRenderFrameCostNow() - start) * 1000000.0 / freq;
#endif
    return (CKRenderFrameCostNow() - start) * 1000000.0 / (double)CLOCKS_PER_SEC;
}

static void CKRenderFrameCostStatsInit()
{
    const CKRenderFrameCostStatsConfig &config = CKRenderDiagnosticsSettings().FrameCostStats;
    memset(&g_FrameCostStats, 0, sizeof(g_FrameCostStats));
    g_FrameCostStats.Initialized = TRUE;
    g_FrameCostStats.Enabled = config.Enabled ? TRUE : FALSE;
    g_FrameCostStats.OutputEnabled = TRUE;
    g_FrameCostStats.WarmupFrames = (CKDWORD)config.WarmupFrames;
    g_FrameCostStats.SampleFrames = (CKDWORD)config.SampleFrames;
    if (g_FrameCostStats.SampleFrames == 0)
        g_FrameCostStats.SampleFrames = 600;
    g_FrameCostStats.Totals.Enabled = g_FrameCostStats.Enabled;
    g_FrameCostStats.Totals.WarmupFrames = g_FrameCostStats.WarmupFrames;
    g_FrameCostStats.Totals.SampleFrames = g_FrameCostStats.SampleFrames;
    if (CKRenderFrameCostStringEquals(config.Output, "none"))
        g_FrameCostStats.OutputEnabled = FALSE;
}

static void CKRenderFrameCostStatsEnsureInit()
{
    if (!g_FrameCostStats.Initialized)
        CKRenderFrameCostStatsInit();
}

CKBOOL CKRenderFrameCostStatsEnabled()
{
    CKRenderFrameCostStatsEnsureInit();
    return g_FrameCostStats.Enabled;
}

CKBOOL CKRenderFrameCostStatsIsCollecting()
{
    CKRenderFrameCostStatsEnsureInit();
    return g_FrameCostStats.Enabled &&
           g_FrameCostStats.Collecting &&
           !g_FrameCostStats.Completed;
}

CKBOOL CKRenderFrameCostStatsWantsPlayerTiming()
{
    CKRenderFrameCostStatsEnsureInit();
    return g_FrameCostStats.Enabled && !g_FrameCostStats.Completed;
}

static FILE *CKRenderFrameCostStatsOpenLog()
{
    char path[260] = {0};
    if (!CKRenderDebugSettings().GetString("LogPath", path, (CKDWORD)sizeof(path)))
        strcpy(path, "CK2_3D_Debug.log");
    return fopen(path, "ab");
}

static double CKRenderFrameCostAverage(double value)
{
    if (g_FrameCostStats.Totals.SampledFrames == 0)
        return 0.0;
    return value / (double)g_FrameCostStats.Totals.SampledFrames;
}

static CKDWORD CKRenderFrameCostAverageDword(CKDWORD value)
{
    if (g_FrameCostStats.Totals.SampledFrames == 0)
        return 0;
    return value / g_FrameCostStats.Totals.SampledFrames;
}

static void CKRenderFrameCostStatsWriteSummary()
{
    if (!g_FrameCostStats.OutputEnabled || g_FrameCostStats.SummaryWritten)
        return;

    FILE *file = CKRenderFrameCostStatsOpenLog();
    if (!file)
        return;

    const CKRenderFrameCostStatsSnapshot &s = g_FrameCostStats.Totals;
    fprintf(file,
            "[CK2_3D] [FrameCostStats.Core] frames=%u warmup=%u sample=%u updateUs=%.3f inputUs=%.3f processUs=%.3f renderUs=%.3f processCalls=%u renderCalls=%u\n",
            s.SampledFrames,
            s.WarmupFrames,
            s.SampleFrames,
            CKRenderFrameCostAverage(s.PlayerUpdateUs),
            CKRenderFrameCostAverage(s.PlayerInputUs),
            CKRenderFrameCostAverage(s.PlayerProcessUs),
            CKRenderFrameCostAverage(s.PlayerRenderUs),
            s.PlayerProcessCalls,
            s.PlayerRenderCalls);
    fprintf(file,
            "[CK2_3D] [FrameCostStats.RenderSections] clearUs=%.3f drawSceneUs=%.3f endFrameUs=%.3f beginFrameUs=%.3f defaultStatesUs=%.3f opaqueUs=%.3f transparentUs=%.3f foreground2DUs=%.3f\n",
            CKRenderFrameCostAverage(s.SectionsUs[CKRFCS_CLEAR]),
            CKRenderFrameCostAverage(s.SectionsUs[CKRFCS_DRAW_SCENE]),
            CKRenderFrameCostAverage(s.SectionsUs[CKRFCS_END_FRAME]),
            CKRenderFrameCostAverage(s.SectionsUs[CKRFCS_BEGIN_FRAME]),
            CKRenderFrameCostAverage(s.SectionsUs[CKRFCS_DEFAULT_STATES]),
            CKRenderFrameCostAverage(s.SectionsUs[CKRFCS_OPAQUE_TRAVERSAL]),
            CKRenderFrameCostAverage(s.SectionsUs[CKRFCS_TRANSPARENT_SORT_RENDER]),
            CKRenderFrameCostAverage(s.SectionsUs[CKRFCS_FOREGROUND_2D]));
    fprintf(file,
            "[CK2_3D] [FrameCostStats.DrawCounts] entities3D=%u entities2D=%u meshRender=%u meshGroup=%u 2dRender=%u 2dExtents=%u 2dDraw=%u spriteDraw=%u drawPrimitive=%u sanitize=%u submitted=%u primitiveSubmit=%u meshSubmit=%u\n",
            CKRenderFrameCostAverageDword(s.Entities3D),
            CKRenderFrameCostAverageDword(s.Entities2D),
            CKRenderFrameCostAverageDword(s.MeshRenderCalls),
            CKRenderFrameCostAverageDword(s.MeshGroupCalls),
            CKRenderFrameCostAverageDword(s.TwoDEntityRenderCalls),
            CKRenderFrameCostAverageDword(s.TwoDEntityUpdateExtentsCalls),
            CKRenderFrameCostAverageDword(s.TwoDEntityDrawCalls),
            CKRenderFrameCostAverageDword(s.SpriteDrawCalls),
            CKRenderFrameCostAverageDword(s.DrawPrimitiveCalls),
            CKRenderFrameCostAverageDword(s.DrawPrimitiveSanitizeCalls),
            CKRenderFrameCostAverageDword(s.SubmittedDraws),
            CKRenderFrameCostAverageDword(s.PrimitiveSubmits),
            CKRenderFrameCostAverageDword(s.MeshSubmits));
    fprintf(file,
            "[CK2_3D] [FrameCostStats.Primitive] transientPrepare=%u fanToList=%u transientVB=%u transientIB=%u viewportSet=%u materialSet=%u materialNoOpCandidate=%u\n",
            CKRenderFrameCostAverageDword(s.TransientPrepareCalls),
            CKRenderFrameCostAverageDword(s.TransientFanToListConversions),
            CKRenderFrameCostAverageDword(s.TransientVertexBytes),
            CKRenderFrameCostAverageDword(s.TransientIndexBytes),
            CKRenderFrameCostAverageDword(s.ViewportSetCalls),
            CKRenderFrameCostAverageDword(s.MaterialSetCalls),
            CKRenderFrameCostAverageDword(s.MaterialNoOpCandidates));
    fclose(file);
    g_FrameCostStats.SummaryWritten = TRUE;
}

void CKRenderFrameCostStatsBeginRenderFrame(CKDWORD entities3D, CKDWORD entities2D,
                                            CKDWORD cameras, CKDWORD lights)
{
    CKRenderFrameCostStatsEnsureInit();
    if (!g_FrameCostStats.Enabled || g_FrameCostStats.Completed)
        return;

    ++g_FrameCostStats.FrameIndex;
    g_FrameCostStats.Collecting =
        g_FrameCostStats.FrameIndex > g_FrameCostStats.WarmupFrames &&
        g_FrameCostStats.Totals.SampledFrames < g_FrameCostStats.SampleFrames;
    g_FrameCostStats.AcceptPlayerTiming = g_FrameCostStats.Collecting;

    if (!g_FrameCostStats.Collecting)
        return;

    g_FrameCostStats.Totals.Collecting = TRUE;
    g_FrameCostStats.Totals.Entities3D += entities3D;
    g_FrameCostStats.Totals.Entities2D += entities2D;
    g_FrameCostStats.Totals.Cameras += cameras;
    g_FrameCostStats.Totals.Lights += lights;
}

void CKRenderFrameCostStatsEndRenderFrame()
{
    CKRenderFrameCostStatsEnsureInit();
    if (!g_FrameCostStats.Enabled || g_FrameCostStats.Completed)
        return;

    if (g_FrameCostStats.Collecting) {
        ++g_FrameCostStats.Totals.SampledFrames;
        g_FrameCostStats.Collecting = FALSE;
        g_FrameCostStats.Totals.Collecting = FALSE;
        if (g_FrameCostStats.Totals.SampledFrames >= g_FrameCostStats.SampleFrames) {
            g_FrameCostStats.Completed = TRUE;
            g_FrameCostStats.Totals.Completed = TRUE;
            if (!g_FrameCostStats.AcceptPlayerTiming)
                CKRenderFrameCostStatsWriteSummary();
        }
    }
}

void CKRenderFrameCostStatsAddPlayerTiming(double updateUs, double inputUs,
                                           double processUs, double renderUs,
                                           CKBOOL processRan, CKBOOL renderRan)
{
    CKRenderFrameCostStatsEnsureInit();
    if (!g_FrameCostStats.AcceptPlayerTiming)
        return;

    g_FrameCostStats.Totals.PlayerUpdateUs += updateUs;
    g_FrameCostStats.Totals.PlayerInputUs += inputUs;
    g_FrameCostStats.Totals.PlayerProcessUs += processUs;
    g_FrameCostStats.Totals.PlayerRenderUs += renderUs;
    ++g_FrameCostStats.Totals.PlayerUpdateCalls;
    if (processRan)
        ++g_FrameCostStats.Totals.PlayerProcessCalls;
    if (renderRan)
        ++g_FrameCostStats.Totals.PlayerRenderCalls;
    g_FrameCostStats.AcceptPlayerTiming = FALSE;
    if (g_FrameCostStats.Completed)
        CKRenderFrameCostStatsWriteSummary();
}

void CKRenderFrameCostStatsAddSection(CKRenderFrameCostSection section, double us)
{
    if (!CKRenderFrameCostStatsIsCollecting())
        return;
    if (section < 0 || section >= CKRFCS_SECTION_COUNT)
        return;
    g_FrameCostStats.Totals.SectionsUs[section] += us;
}

void CKRenderFrameCostStatsAddDrawPrimitive()
{
    if (!CKRenderFrameCostStatsIsCollecting())
        return;
    ++g_FrameCostStats.Totals.DrawPrimitiveCalls;
}

void CKRenderFrameCostStatsAddViewportSet()
{
    if (!CKRenderFrameCostStatsIsCollecting())
        return;
    ++g_FrameCostStats.Totals.ViewportSetCalls;
}

void CKRenderFrameCostStatsAddMaterialSet(CKBOOL noOpCandidate,
                                          CKBOOL skipped,
                                          CKBOOL dirtyMiss)
{
    if (!CKRenderFrameCostStatsIsCollecting())
        return;
    ++g_FrameCostStats.Totals.MaterialSetCalls;
    if (noOpCandidate)
        ++g_FrameCostStats.Totals.MaterialNoOpCandidates;
    if (skipped)
        ++g_FrameCostStats.Totals.MaterialNoOpSkips;
    if (dirtyMiss)
        ++g_FrameCostStats.Totals.MaterialDirtyMisses;
}

void CKRenderFrameCostStatsAddMeshRender()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++g_FrameCostStats.Totals.MeshRenderCalls;
}

void CKRenderFrameCostStatsAddMeshDefault()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++g_FrameCostStats.Totals.MeshDefaultCalls;
}

void CKRenderFrameCostStatsAddMeshGroup()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++g_FrameCostStats.Totals.MeshGroupCalls;
}

void CKRenderFrameCostStatsAdd2DEntityRender()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++g_FrameCostStats.Totals.TwoDEntityRenderCalls;
}

void CKRenderFrameCostStatsAdd2DEntityUpdateExtents()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++g_FrameCostStats.Totals.TwoDEntityUpdateExtentsCalls;
}

void CKRenderFrameCostStatsAdd2DEntityDraw()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++g_FrameCostStats.Totals.TwoDEntityDrawCalls;
}

void CKRenderFrameCostStatsAddSpriteDraw()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++g_FrameCostStats.Totals.SpriteDrawCalls;
}

void CKRenderFrameCostStatsAddDrawPrimitiveSanitize()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++g_FrameCostStats.Totals.DrawPrimitiveSanitizeCalls;
}

void CKRenderFrameCostStatsAddTransientPrepare(CKDWORD vertexBytes,
                                               CKDWORD indexBytes,
                                               CKBOOL fanToListConversion)
{
    if (!CKRenderFrameCostStatsIsCollecting())
        return;
    ++g_FrameCostStats.Totals.TransientPrepareCalls;
    if (fanToListConversion)
        ++g_FrameCostStats.Totals.TransientFanToListConversions;
    g_FrameCostStats.Totals.TransientVertexBytes += vertexBytes;
    g_FrameCostStats.Totals.TransientIndexBytes += indexBytes;
}

void CKRenderFrameCostStatsAddPrimitiveSubmit()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++g_FrameCostStats.Totals.PrimitiveSubmits;
}

void CKRenderFrameCostStatsAddMeshSubmit()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++g_FrameCostStats.Totals.MeshSubmits;
}

void CKRenderFrameCostStatsAddSubmittedDraw()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++g_FrameCostStats.Totals.SubmittedDraws;
}

void CKRenderFrameCostStatsCopySnapshot(CKRenderFrameCostStatsSnapshot *snapshot)
{
    if (!snapshot)
        return;
    CKRenderFrameCostStatsEnsureInit();
    *snapshot = g_FrameCostStats.Totals;
    snapshot->Enabled = g_FrameCostStats.Enabled;
    snapshot->Collecting = g_FrameCostStats.Collecting;
    snapshot->Completed = g_FrameCostStats.Completed;
    snapshot->WarmupFrames = g_FrameCostStats.WarmupFrames;
    snapshot->SampleFrames = g_FrameCostStats.SampleFrames;
}

void CKRenderFrameCostStatsResetForTests()
{
    memset(&g_FrameCostStats, 0, sizeof(g_FrameCostStats));
}

void CKRenderFrameCostStatsSetOutputEnabledForTests(CKBOOL enabled)
{
    CKRenderFrameCostStatsEnsureInit();
    g_FrameCostStats.OutputEnabled = enabled;
}
