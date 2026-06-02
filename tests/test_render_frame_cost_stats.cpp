#include "CKRenderFrameCostStats.h"
#include "CKRenderSettings.h"
#include "TestTriangleMultiset.h"

static void DefaultsDisabled() {
    CKRenderSettingsClearOverridesForTests();
    CKRenderFrameCostStatsResetForTests();

    TestCheck(!CKRenderFrameCostStatsEnabled(),
              "FrameCostStats must be disabled by default");
    TestCheck(!CKRenderFrameCostStatsIsCollecting(),
              "disabled FrameCostStats must not collect");
}

static void WarmupAndSampleAggregateOnce() {
    CKRenderSettingsClearOverridesForTests();
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "Enabled", "1");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "WarmupFrames", "1");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "SampleFrames", "2");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFrameCostStats, "Output", "none");
    CKRenderFrameCostStatsResetForTests();
    CKRenderFrameCostStatsSetOutputEnabledForTests(FALSE);

    CKRenderFrameCostStatsBeginRenderFrame(10, 20, 1, 2);
    TestCheck(!CKRenderFrameCostStatsIsCollecting(),
              "first frame should be warmup");
    CKRenderFrameCostStatsEndRenderFrame();

    CKRenderFrameCostStatsBeginRenderFrame(11, 21, 1, 2);
    TestCheck(CKRenderFrameCostStatsIsCollecting(),
              "second frame should collect");
    CKRenderFrameCostStatsAddSection(CKRFCS_FOREGROUND_2D, 4.0);
    CKRenderFrameCostStatsAdd2DEntityRender();
    CKRenderFrameCostStatsAdd2DEntityUpdateExtents();
    CKRenderFrameCostStatsAdd2DEntityDraw();
    CKRenderFrameCostStatsAddSpriteDraw();
    CKRenderFrameCostStatsAddDrawPrimitive(TRUE, FALSE);
    CKRenderFrameCostStatsAddDrawPrimitiveSanitize();
    CKRenderFrameCostStatsAddTransientPrepare(TRUE, TRUE, 160, 12, FALSE);
    CKRenderFrameCostStatsAddTransientSpriteBatchFastPath(TRUE, TRUE);
    CKRenderFrameCostStatsAddViewportSet(TRUE);
    CKRenderFrameCostStatsAddMaterialSet(TRUE, FALSE, FALSE);
    CKRenderFrameCostStatsAddPrimitiveSubmit();
    CKRenderFrameCostStatsAddSubmittedDraw();
    CKRenderFrameCostStatsEndRenderFrame();
    CKRenderFrameCostStatsAddPlayerTiming(100.0, 10.0, 20.0, 70.0, TRUE, TRUE);

    CKRenderFrameCostStatsBeginRenderFrame(12, 22, 1, 2);
    TestCheck(CKRenderFrameCostStatsIsCollecting(),
              "third frame should collect");
    CKRenderFrameCostStatsAddSection(CKRFCS_FOREGROUND_2D, 6.0);
    CKRenderFrameCostStatsAdd2DEntityRender();
    CKRenderFrameCostStatsAdd2DEntityUpdateExtents();
    CKRenderFrameCostStatsAddDrawPrimitive(TRUE, FALSE);
    CKRenderFrameCostStatsAddDrawPrimitiveSanitize();
    CKRenderFrameCostStatsAddTransientPrepare(TRUE, FALSE, 160, 12, TRUE);
    CKRenderFrameCostStatsAddTransientSpriteBatchFastPath(TRUE, FALSE);
    CKRenderFrameCostStatsAddViewportSet(FALSE);
    CKRenderFrameCostStatsAddMeshSubmit();
    CKRenderFrameCostStatsAddSubmittedDraw();
    CKRenderFrameCostStatsEndRenderFrame();
    CKRenderFrameCostStatsAddPlayerTiming(120.0, 12.0, 22.0, 78.0, TRUE, TRUE);

    CKRenderFrameCostStatsBeginRenderFrame(13, 23, 1, 2);
    TestCheck(!CKRenderFrameCostStatsIsCollecting(),
              "sample completion should stop collection");

    CKRenderFrameCostStatsSnapshot snapshot;
    CKRenderFrameCostStatsCopySnapshot(&snapshot);
    TestCheck(snapshot.Completed,
              "FrameCostStats should mark sample complete");
    TestCheck(snapshot.SampledFrames == 2,
              "FrameCostStats must sample exactly two frames");
    TestCheck(snapshot.Entities3D == 23 && snapshot.Entities2D == 43,
              "FrameCostStats must aggregate sampled entity counts only");
    TestCheck(snapshot.SectionsUs[CKRFCS_FOREGROUND_2D] == 10.0,
              "FrameCostStats must aggregate section timings");
    TestCheck(snapshot.DrawPrimitiveCalls == 2,
              "FrameCostStats must count draw primitive calls");
    TestCheck(snapshot.TwoDEntityRenderCalls == 2 &&
              snapshot.TwoDEntityUpdateExtentsCalls == 2 &&
              snapshot.TwoDEntityDrawCalls == 1 &&
              snapshot.SpriteDrawCalls == 1,
              "FrameCostStats must count 2D render subpaths");
    TestCheck(snapshot.DrawPrimitiveSanitizeCalls == 2,
              "FrameCostStats must count primitive sanitize calls");
    TestCheck(snapshot.DrawPrimitiveFastPathCandidates == 2 &&
              snapshot.DrawPrimitiveFastPathHits == 0 &&
              snapshot.DrawPrimitiveFastPathFallbacks == 2,
              "FrameCostStats must count sanitized quad candidates as fallbacks");
    TestCheck(snapshot.TransientPrepareCalls == 2 &&
              snapshot.TransientQuadFastPathCandidates == 2 &&
              snapshot.TransientQuadFastPathHits == 1 &&
              snapshot.TransientQuadFastPathFallbacks == 1 &&
              snapshot.TransientSpriteBatchFastPathCandidates == 2 &&
              snapshot.TransientSpriteBatchFastPathHits == 1 &&
              snapshot.TransientSpriteBatchFastPathFallbacks == 1 &&
              snapshot.TransientFanToListConversions == 1,
              "FrameCostStats must count transient quad fast path results");
    TestCheck(snapshot.TransientVertexBytes == 320 &&
              snapshot.TransientIndexBytes == 24,
              "FrameCostStats must aggregate transient buffer bytes");
    TestCheck(snapshot.ViewportSetSkipped == 1,
              "FrameCostStats must count skipped viewport sets");
    TestCheck(snapshot.MaterialSetCalls == 1 &&
              snapshot.MaterialNoOpCandidates == 1,
              "FrameCostStats must count material calls");
    TestCheck(snapshot.SubmittedDraws == 2 &&
              snapshot.PrimitiveSubmits == 1 &&
              snapshot.MeshSubmits == 1,
              "FrameCostStats must split submit counts");
    TestCheck(snapshot.PlayerUpdateCalls == 2 &&
              snapshot.PlayerUpdateUs == 220.0 &&
              snapshot.PlayerRenderUs == 148.0,
              "FrameCostStats must accept Player timing after render frame end");

    CKRenderSettingsClearOverridesForTests();
    CKRenderFrameCostStatsResetForTests();
}

int main() {
    TestFramework tests;
    tests.Run("FrameCostStats defaults disabled", &DefaultsDisabled);
    tests.Run("FrameCostStats warmup and sample aggregation", &WarmupAndSampleAggregateOnce);
    return tests.ExitCode();
}
