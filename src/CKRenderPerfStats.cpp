#include "CKRenderPerfStats.h"
#include "CKDebugLogger.h"

#include <cstdlib>
#include <cstring>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

static CKRenderPerfStats g_RenderPerfStats = {};

bool CKRenderPerfStatsEnabled() {
    static const bool enabled = []() {
        const char *value = std::getenv("CK2_3D_DEBUG_RENDER_STATS");
        return value && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

double CKRenderPerfNow() {
#if defined(_WIN32)
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart;
#else
    return 0.0;
#endif
}

double CKRenderPerfElapsedUs(double start) {
#if defined(_WIN32)
    static double frequency = []() {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        return freq.QuadPart > 0 ? (double)freq.QuadPart : 1.0;
    }();
    return (CKRenderPerfNow() - start) * 1000000.0 / frequency;
#else
    (void)start;
    return 0.0;
#endif
}

void CKRenderPerfBeginFrame(CKDWORD entities3D, CKDWORD entities2D, CKDWORD cameras, CKDWORD lights) {
    if (!CKRenderPerfStatsEnabled())
        return;
    const CKDWORD frameIndex = g_RenderPerfStats.FrameIndex;
    memset(&g_RenderPerfStats, 0, sizeof(g_RenderPerfStats));
    g_RenderPerfStats.FrameIndex = frameIndex;
    g_RenderPerfStats.Entities3D = entities3D;
    g_RenderPerfStats.Entities2D = entities2D;
    g_RenderPerfStats.Cameras = cameras;
    g_RenderPerfStats.Lights = lights;
}

void CKRenderPerfAddSection(CKRenderPerfSection section, double us) {
    if (!CKRenderPerfStatsEnabled())
        return;
    if ((int)section < 0 || section >= CKRPS_SECTION_COUNT)
        return;
    g_RenderPerfStats.SectionsUs[section] += us;
}

CKRenderPerfStats &CKRenderPerfCurrent() {
    return g_RenderPerfStats;
}

void CKRenderPerfLogAndReset() {
    if (!CKRenderPerfStatsEnabled())
        return;

    CK_LOG_FMT("RenderStats",
               "frame=%u entities3D=%u entities2D=%u cameras=%u lights=%u meshRender=%u meshDefault=%u groups=%u channelPasses=%u materialSet=%u drawPrimitive=%u drawVB=%u primEntries=%u swEntries=%u hwEntries=%u channelEntries=%u channelMeshes=%u alphaGroups=%u vbChecks=%u vbReady=%u groupIndices=%u channelIndices=%u frameSetupUs=%.1f beginFrameUs=%.1f defaultStatesUs=%.1f bg2DUs=%.1f setupLightsUs=%.1f preCallbacksUs=%.1f opaqueTraversalUs=%.1f sprite3DUs=%.1f postCallbacksUs=%.1f transparentSortRenderUs=%.1f fg2DUs=%.1f postSpriteCallbacksUs=%.1f meshRenderUs=%.1f meshDefaultUs=%.1f meshGroupUs=%.1f meshChannelsUs=%.1f materialSetUs=%.1f drawPrimitiveWrapperUs=%.1f",
               g_RenderPerfStats.FrameIndex,
               g_RenderPerfStats.Entities3D,
               g_RenderPerfStats.Entities2D,
               g_RenderPerfStats.Cameras,
               g_RenderPerfStats.Lights,
               g_RenderPerfStats.MeshRenderCalls,
               g_RenderPerfStats.MeshDefaultCalls,
               g_RenderPerfStats.MeshGroupCalls,
               g_RenderPerfStats.MeshChannelPasses,
               g_RenderPerfStats.MaterialSetCalls,
               g_RenderPerfStats.DrawPrimitiveCalls,
               g_RenderPerfStats.DrawVertexBufferCalls,
               g_RenderPerfStats.PrimitiveEntries,
               g_RenderPerfStats.SoftwarePrimitiveEntries,
               g_RenderPerfStats.HardwarePrimitiveEntries,
               g_RenderPerfStats.ChannelPrimitiveEntries,
               g_RenderPerfStats.RenderChannelMeshes,
               g_RenderPerfStats.AlphaGroups,
               g_RenderPerfStats.VertexBufferChecks,
               g_RenderPerfStats.VertexBufferReady,
               g_RenderPerfStats.TotalGroupIndices,
               g_RenderPerfStats.TotalChannelIndices,
               g_RenderPerfStats.SectionsUs[CKRPS_FRAME_SETUP],
               g_RenderPerfStats.SectionsUs[CKRPS_BEGIN_FRAME],
               g_RenderPerfStats.SectionsUs[CKRPS_DEFAULT_STATES],
               g_RenderPerfStats.SectionsUs[CKRPS_BACKGROUND_2D],
               g_RenderPerfStats.SectionsUs[CKRPS_SETUP_LIGHTS],
               g_RenderPerfStats.SectionsUs[CKRPS_PRE_CALLBACKS],
               g_RenderPerfStats.SectionsUs[CKRPS_OPAQUE_TRAVERSAL],
               g_RenderPerfStats.SectionsUs[CKRPS_SPRITE3D],
               g_RenderPerfStats.SectionsUs[CKRPS_POST_CALLBACKS],
               g_RenderPerfStats.SectionsUs[CKRPS_TRANSPARENT_SORT_RENDER],
               g_RenderPerfStats.SectionsUs[CKRPS_FOREGROUND_2D],
               g_RenderPerfStats.SectionsUs[CKRPS_POST_SPRITE_CALLBACKS],
               g_RenderPerfStats.MeshRenderUs,
               g_RenderPerfStats.MeshDefaultUs,
               g_RenderPerfStats.MeshGroupUs,
               g_RenderPerfStats.MeshChannelsUs,
               g_RenderPerfStats.MaterialSetUs,
               g_RenderPerfStats.DrawPrimitiveWrapperUs);

    ++g_RenderPerfStats.FrameIndex;
}
