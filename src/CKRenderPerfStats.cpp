#include "CKRenderPerfStats.h"

#if CKRE_ENABLE_RENDER_STATS

#include "CKDebugLogger.h"
#include "CKRenderSettings.h"
#include "CKRenderDiagnosticsContext.h"

#include <cstring>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

static CKRenderPerfStats &RenderPerfStats() {
    return CKRenderDiagnosticState<CKRenderPerfStats>(CK_DIAGNOSTIC_RENDER_PERF);
}
bool CKRenderPerfStatsEnabled() {
    return CKRenderDiagnosticsSettings().RenderStats.Enabled;
}

void CKRenderPerfBeginFrame(CKDWORD entities3D, CKDWORD entities2D, CKDWORD cameras, CKDWORD lights) {
    if (!CKRenderPerfStatsEnabled())
        return;
    const CKDWORD frameIndex = RenderPerfStats().FrameIndex;
    memset(&RenderPerfStats(), 0, sizeof(RenderPerfStats()));
    RenderPerfStats().FrameIndex = frameIndex;
    RenderPerfStats().Entities3D = entities3D;
    RenderPerfStats().Entities2D = entities2D;
    RenderPerfStats().Cameras = cameras;
    RenderPerfStats().Lights = lights;
}

void CKRenderPerfAddSection(CKRenderPerfSection section, double us) {
    if (!CKRenderPerfStatsEnabled())
        return;
    if ((int)section < 0 || section >= CKRPS_SECTION_COUNT)
        return;
    RenderPerfStats().SectionsUs[section] += us;
}

CKRenderPerfStats &CKRenderPerfCurrent() {
    return RenderPerfStats();
}

void CKRenderPerfLogAndReset() {
    if (!CKRenderPerfStatsEnabled())
        return;

    CK_LOG_FMT("RenderStats",
               "frame=%u entities3D=%u entities2D=%u cameras=%u lights=%u meshRender=%u meshDefault=%u groups=%u channelPasses=%u materialSet=%u drawPrimitive=%u drawVB=%u primEntries=%u swEntries=%u hwEntries=%u channelEntries=%u channelMeshes=%u alphaGroups=%u vbChecks=%u vbReady=%u groupIndices=%u channelIndices=%u frameSetupUs=%.1f beginFrameUs=%.1f defaultStatesUs=%.1f bg2DUs=%.1f setupLightsUs=%.1f preCallbacksUs=%.1f opaqueTraversalUs=%.1f sprite3DUs=%.1f postCallbacksUs=%.1f transparentSortRenderUs=%.1f fg2DUs=%.1f postSpriteCallbacksUs=%.1f meshRenderUs=%.1f meshDefaultUs=%.1f meshGroupUs=%.1f meshChannelsUs=%.1f materialSetUs=%.1f drawPrimitiveWrapperUs=%.1f",
               RenderPerfStats().FrameIndex,
               RenderPerfStats().Entities3D,
               RenderPerfStats().Entities2D,
               RenderPerfStats().Cameras,
               RenderPerfStats().Lights,
               RenderPerfStats().MeshRenderCalls,
               RenderPerfStats().MeshDefaultCalls,
               RenderPerfStats().MeshGroupCalls,
               RenderPerfStats().MeshChannelPasses,
               RenderPerfStats().MaterialSetCalls,
               RenderPerfStats().DrawPrimitiveCalls,
               RenderPerfStats().DrawVertexBufferCalls,
               RenderPerfStats().PrimitiveEntries,
               RenderPerfStats().SoftwarePrimitiveEntries,
               RenderPerfStats().HardwarePrimitiveEntries,
               RenderPerfStats().ChannelPrimitiveEntries,
               RenderPerfStats().RenderChannelMeshes,
               RenderPerfStats().AlphaGroups,
               RenderPerfStats().VertexBufferChecks,
               RenderPerfStats().VertexBufferReady,
               RenderPerfStats().TotalGroupIndices,
               RenderPerfStats().TotalChannelIndices,
               RenderPerfStats().SectionsUs[CKRPS_FRAME_SETUP],
               RenderPerfStats().SectionsUs[CKRPS_BEGIN_FRAME],
               RenderPerfStats().SectionsUs[CKRPS_DEFAULT_STATES],
               RenderPerfStats().SectionsUs[CKRPS_BACKGROUND_2D],
               RenderPerfStats().SectionsUs[CKRPS_SETUP_LIGHTS],
               RenderPerfStats().SectionsUs[CKRPS_PRE_CALLBACKS],
               RenderPerfStats().SectionsUs[CKRPS_OPAQUE_TRAVERSAL],
               RenderPerfStats().SectionsUs[CKRPS_SPRITE3D],
               RenderPerfStats().SectionsUs[CKRPS_POST_CALLBACKS],
               RenderPerfStats().SectionsUs[CKRPS_TRANSPARENT_SORT_RENDER],
               RenderPerfStats().SectionsUs[CKRPS_FOREGROUND_2D],
               RenderPerfStats().SectionsUs[CKRPS_POST_SPRITE_CALLBACKS],
               RenderPerfStats().MeshRenderUs,
               RenderPerfStats().MeshDefaultUs,
               RenderPerfStats().MeshGroupUs,
               RenderPerfStats().MeshChannelsUs,
               RenderPerfStats().MaterialSetUs,
               RenderPerfStats().DrawPrimitiveWrapperUs);

    ++RenderPerfStats().FrameIndex;
}

#endif // CKRE_ENABLE_RENDER_STATS
