#include "CKRenderFrameCostStats.h"
#include "CKRenderSettings.h"
#include "CKRenderDiagnosticsContext.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdarg>

static void CKRenderFrameCostWrite(FILE *file, const char *format, ...) {
    char message[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (const auto *context = CKRenderCurrentDiagnostics()) {
        const size_t length = strlen(message);
        if (length && message[length - 1] == '\n') message[length - 1] = '\0';
        context->Write(context->Owner, "FrameCostStats", message, true);
    } else if (file) fputs(message, file);
}

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

static CKRenderFrameCostStatsState &FrameCostStats() {
    return CKRenderDiagnosticState<CKRenderFrameCostStatsState>(CK_DIAGNOSTIC_FRAME_COST);
}

#ifdef _WIN32
static double CKRenderFrameCostCounterFrequency()
{
    static const double frequency = [] {
        LARGE_INTEGER value;
        return QueryPerformanceFrequency(&value) && value.QuadPart > 0 ? double(value.QuadPart) : 0.0;
    }();
    return frequency;
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
    memset(&FrameCostStats(), 0, sizeof(FrameCostStats()));
    FrameCostStats().Initialized = TRUE;
    FrameCostStats().Enabled = config.Enabled ? TRUE : FALSE;
    FrameCostStats().OutputEnabled = TRUE;
    FrameCostStats().WarmupFrames = (CKDWORD)config.WarmupFrames;
    FrameCostStats().SampleFrames = (CKDWORD)config.SampleFrames;
    if (FrameCostStats().SampleFrames == 0)
        FrameCostStats().SampleFrames = 600;
    FrameCostStats().Totals.Enabled = FrameCostStats().Enabled;
    FrameCostStats().Totals.WarmupFrames = FrameCostStats().WarmupFrames;
    FrameCostStats().Totals.SampleFrames = FrameCostStats().SampleFrames;
    if (CKRenderFrameCostStringEquals(config.Output, "none"))
        FrameCostStats().OutputEnabled = FALSE;
}

static void CKRenderFrameCostStatsEnsureInit()
{
    if (!FrameCostStats().Initialized)
        CKRenderFrameCostStatsInit();
}

CKBOOL CKRenderFrameCostStatsEnabled()
{
    CKRenderFrameCostStatsEnsureInit();
    return FrameCostStats().Enabled;
}

CKBOOL CKRenderFrameCostStatsIsCollecting()
{
    CKRenderFrameCostStatsEnsureInit();
    return FrameCostStats().Enabled &&
           FrameCostStats().Collecting &&
           !FrameCostStats().Completed;
}

CKBOOL CKRenderFrameCostStatsWantsPlayerTiming()
{
    CKRenderFrameCostStatsEnsureInit();
    return FrameCostStats().Enabled && !FrameCostStats().Completed;
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
    if (FrameCostStats().Totals.SampledFrames == 0)
        return 0.0;
    return value / (double)FrameCostStats().Totals.SampledFrames;
}

static CKDWORD CKRenderFrameCostAverageDword(CKDWORD value)
{
    if (FrameCostStats().Totals.SampledFrames == 0)
        return 0;
    return value / FrameCostStats().Totals.SampledFrames;
}

static void CKRenderFrameCostStatsWriteSummary()
{
    if (!FrameCostStats().OutputEnabled || FrameCostStats().SummaryWritten)
        return;

    FILE *file = CKRenderCurrentDiagnostics() ? nullptr : CKRenderFrameCostStatsOpenLog();
    if (!file && !CKRenderCurrentDiagnostics())
        return;

    const CKRenderFrameCostStatsSnapshot &s = FrameCostStats().Totals;
    CKRenderFrameCostWrite(file,
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
    CKRenderFrameCostWrite(file,
            "[CK2_3D] [FrameCostStats.RenderSections] clearUs=%.3f drawSceneUs=%.3f endFrameUs=%.3f beginFrameUs=%.3f defaultStatesUs=%.3f opaqueUs=%.3f transparentUs=%.3f foreground2DUs=%.3f\n",
            CKRenderFrameCostAverage(s.SectionsUs[CKRFCS_CLEAR]),
            CKRenderFrameCostAverage(s.SectionsUs[CKRFCS_DRAW_SCENE]),
            CKRenderFrameCostAverage(s.SectionsUs[CKRFCS_END_FRAME]),
            CKRenderFrameCostAverage(s.SectionsUs[CKRFCS_BEGIN_FRAME]),
            CKRenderFrameCostAverage(s.SectionsUs[CKRFCS_DEFAULT_STATES]),
            CKRenderFrameCostAverage(s.SectionsUs[CKRFCS_OPAQUE_TRAVERSAL]),
            CKRenderFrameCostAverage(s.SectionsUs[CKRFCS_TRANSPARENT_SORT_RENDER]),
            CKRenderFrameCostAverage(s.SectionsUs[CKRFCS_FOREGROUND_2D]));
    CKRenderFrameCostWrite(file,
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
    CKRenderFrameCostWrite(file,
            "[CK2_3D] [FrameCostStats.Primitive] transientPrepare=%u fanToList=%u transientVB=%u transientIB=%u viewportSet=%u materialSet=%u materialNoOpCandidate=%u\n",
            CKRenderFrameCostAverageDword(s.TransientPrepareCalls),
            CKRenderFrameCostAverageDword(s.TransientFanToListConversions),
            CKRenderFrameCostAverageDword(s.TransientVertexBytes),
            CKRenderFrameCostAverageDword(s.TransientIndexBytes),
            CKRenderFrameCostAverageDword(s.ViewportSetCalls),
            CKRenderFrameCostAverageDword(s.MaterialSetCalls),
            CKRenderFrameCostAverageDword(s.MaterialNoOpCandidates));
    if (file) fclose(file);
    FrameCostStats().SummaryWritten = TRUE;
}

void CKRenderFrameCostStatsBeginRenderFrame(CKDWORD entities3D, CKDWORD entities2D,
                                            CKDWORD cameras, CKDWORD lights)
{
    CKRenderFrameCostStatsEnsureInit();
    if (!FrameCostStats().Enabled || FrameCostStats().Completed)
        return;

    ++FrameCostStats().FrameIndex;
    FrameCostStats().Collecting =
        FrameCostStats().FrameIndex > FrameCostStats().WarmupFrames &&
        FrameCostStats().Totals.SampledFrames < FrameCostStats().SampleFrames;
    FrameCostStats().AcceptPlayerTiming = FrameCostStats().Collecting;

    if (!FrameCostStats().Collecting)
        return;

    FrameCostStats().Totals.Collecting = TRUE;
    FrameCostStats().Totals.Entities3D += entities3D;
    FrameCostStats().Totals.Entities2D += entities2D;
    FrameCostStats().Totals.Cameras += cameras;
    FrameCostStats().Totals.Lights += lights;
}

void CKRenderFrameCostStatsEndRenderFrame()
{
    CKRenderFrameCostStatsEnsureInit();
    if (!FrameCostStats().Enabled || FrameCostStats().Completed)
        return;

    if (FrameCostStats().Collecting) {
        ++FrameCostStats().Totals.SampledFrames;
        FrameCostStats().Collecting = FALSE;
        FrameCostStats().Totals.Collecting = FALSE;
        if (FrameCostStats().Totals.SampledFrames >= FrameCostStats().SampleFrames) {
            FrameCostStats().Completed = TRUE;
            FrameCostStats().Totals.Completed = TRUE;
            if (!FrameCostStats().AcceptPlayerTiming)
                CKRenderFrameCostStatsWriteSummary();
        }
    }
}

void CKRenderFrameCostStatsAddPlayerTiming(double updateUs, double inputUs,
                                           double processUs, double renderUs,
                                           CKBOOL processRan, CKBOOL renderRan)
{
    CKRenderFrameCostStatsEnsureInit();
    if (!FrameCostStats().AcceptPlayerTiming)
        return;

    FrameCostStats().Totals.PlayerUpdateUs += updateUs;
    FrameCostStats().Totals.PlayerInputUs += inputUs;
    FrameCostStats().Totals.PlayerProcessUs += processUs;
    FrameCostStats().Totals.PlayerRenderUs += renderUs;
    ++FrameCostStats().Totals.PlayerUpdateCalls;
    if (processRan)
        ++FrameCostStats().Totals.PlayerProcessCalls;
    if (renderRan)
        ++FrameCostStats().Totals.PlayerRenderCalls;
    FrameCostStats().AcceptPlayerTiming = FALSE;
    if (FrameCostStats().Completed)
        CKRenderFrameCostStatsWriteSummary();
}

void CKRenderFrameCostStatsAddSection(CKRenderFrameCostSection section, double us)
{
    if (!CKRenderFrameCostStatsIsCollecting())
        return;
    if (section < 0 || section >= CKRFCS_SECTION_COUNT)
        return;
    FrameCostStats().Totals.SectionsUs[section] += us;
}

void CKRenderFrameCostStatsAddDrawPrimitive()
{
    if (!CKRenderFrameCostStatsIsCollecting())
        return;
    ++FrameCostStats().Totals.DrawPrimitiveCalls;
}

void CKRenderFrameCostStatsAddViewportSet()
{
    if (!CKRenderFrameCostStatsIsCollecting())
        return;
    ++FrameCostStats().Totals.ViewportSetCalls;
}

void CKRenderFrameCostStatsAddMaterialSet(CKBOOL noOpCandidate,
                                          CKBOOL skipped,
                                          CKBOOL dirtyMiss)
{
    if (!CKRenderFrameCostStatsIsCollecting())
        return;
    ++FrameCostStats().Totals.MaterialSetCalls;
    if (noOpCandidate)
        ++FrameCostStats().Totals.MaterialNoOpCandidates;
    if (skipped)
        ++FrameCostStats().Totals.MaterialNoOpSkips;
    if (dirtyMiss)
        ++FrameCostStats().Totals.MaterialDirtyMisses;
}

void CKRenderFrameCostStatsAddMeshRender()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++FrameCostStats().Totals.MeshRenderCalls;
}

void CKRenderFrameCostStatsAddMeshDefault()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++FrameCostStats().Totals.MeshDefaultCalls;
}

void CKRenderFrameCostStatsAddMeshGroup()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++FrameCostStats().Totals.MeshGroupCalls;
}

void CKRenderFrameCostStatsAdd2DEntityRender()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++FrameCostStats().Totals.TwoDEntityRenderCalls;
}

void CKRenderFrameCostStatsAdd2DEntityUpdateExtents()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++FrameCostStats().Totals.TwoDEntityUpdateExtentsCalls;
}

void CKRenderFrameCostStatsAdd2DEntityDraw()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++FrameCostStats().Totals.TwoDEntityDrawCalls;
}

void CKRenderFrameCostStatsAddSpriteDraw()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++FrameCostStats().Totals.SpriteDrawCalls;
}

void CKRenderFrameCostStatsAddDrawPrimitiveSanitize()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++FrameCostStats().Totals.DrawPrimitiveSanitizeCalls;
}

void CKRenderFrameCostStatsAddTransientPrepare(CKDWORD vertexBytes,
                                               CKDWORD indexBytes,
                                               CKBOOL fanToListConversion)
{
    if (!CKRenderFrameCostStatsIsCollecting())
        return;
    ++FrameCostStats().Totals.TransientPrepareCalls;
    if (fanToListConversion)
        ++FrameCostStats().Totals.TransientFanToListConversions;
    FrameCostStats().Totals.TransientVertexBytes += vertexBytes;
    FrameCostStats().Totals.TransientIndexBytes += indexBytes;
}

void CKRenderFrameCostStatsAddPrimitiveSubmit()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++FrameCostStats().Totals.PrimitiveSubmits;
}

void CKRenderFrameCostStatsAddMeshSubmit()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++FrameCostStats().Totals.MeshSubmits;
}

void CKRenderFrameCostStatsAddSubmittedDraw()
{
    if (CKRenderFrameCostStatsIsCollecting())
        ++FrameCostStats().Totals.SubmittedDraws;
}

void CKRenderFrameCostStatsCopySnapshot(CKRenderFrameCostStatsSnapshot *snapshot)
{
    if (!snapshot)
        return;
    CKRenderFrameCostStatsEnsureInit();
    *snapshot = FrameCostStats().Totals;
    snapshot->Enabled = FrameCostStats().Enabled;
    snapshot->Collecting = FrameCostStats().Collecting;
    snapshot->Completed = FrameCostStats().Completed;
    snapshot->WarmupFrames = FrameCostStats().WarmupFrames;
    snapshot->SampleFrames = FrameCostStats().SampleFrames;
}

void CKRenderFrameCostStatsResetForTests()
{
    memset(&FrameCostStats(), 0, sizeof(FrameCostStats()));
}

void CKRenderFrameCostStatsSetOutputEnabledForTests(CKBOOL enabled)
{
    CKRenderFrameCostStatsEnsureInit();
    FrameCostStats().OutputEnabled = enabled;
}
