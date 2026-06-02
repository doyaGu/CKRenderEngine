#ifndef CK_RENDER_FRAME_COST_STATS_H
#define CK_RENDER_FRAME_COST_STATS_H

#include "CKTypes.h"

enum CKRenderFrameCostSection {
    CKRFCS_CLEAR = 0,
    CKRFCS_DRAW_SCENE,
    CKRFCS_END_FRAME,
    CKRFCS_FRAME_SETUP,
    CKRFCS_BEGIN_FRAME,
    CKRFCS_DEFAULT_STATES,
    CKRFCS_BACKGROUND_2D,
    CKRFCS_SETUP_LIGHTS,
    CKRFCS_PRE_CALLBACKS,
    CKRFCS_OPAQUE_TRAVERSAL,
    CKRFCS_SPRITE3D,
    CKRFCS_POST_CALLBACKS,
    CKRFCS_TRANSPARENT_SORT_RENDER,
    CKRFCS_FOREGROUND_2D,
    CKRFCS_POST_SPRITE_CALLBACKS,
    CKRFCS_SECTION_COUNT
};

struct CKRenderFrameCostStatsSnapshot {
    CKBOOL Enabled;
    CKBOOL Collecting;
    CKBOOL Completed;
    CKDWORD WarmupFrames;
    CKDWORD SampleFrames;
    CKDWORD SampledFrames;

    double PlayerUpdateUs;
    double PlayerInputUs;
    double PlayerProcessUs;
    double PlayerRenderUs;
    CKDWORD PlayerUpdateCalls;
    CKDWORD PlayerProcessCalls;
    CKDWORD PlayerRenderCalls;

    double SectionsUs[CKRFCS_SECTION_COUNT];

    CKDWORD Entities3D;
    CKDWORD Entities2D;
    CKDWORD Cameras;
    CKDWORD Lights;
    CKDWORD MeshRenderCalls;
    CKDWORD MeshDefaultCalls;
    CKDWORD MeshGroupCalls;
    CKDWORD TwoDEntityRenderCalls;
    CKDWORD TwoDEntityUpdateExtentsCalls;
    CKDWORD TwoDEntityDrawCalls;
    CKDWORD SpriteDrawCalls;
    CKDWORD MaterialSetCalls;
    CKDWORD MaterialNoOpCandidates;
    CKDWORD MaterialNoOpSkips;
    CKDWORD MaterialDirtyMisses;
    CKDWORD DrawPrimitiveCalls;
    CKDWORD DrawPrimitiveSanitizeCalls;
    CKDWORD DrawPrimitiveFastPathCandidates;
    CKDWORD DrawPrimitiveFastPathHits;
    CKDWORD DrawPrimitiveFastPathFallbacks;
    CKDWORD TransientPrepareCalls;
    CKDWORD TransientQuadFastPathCandidates;
    CKDWORD TransientQuadFastPathHits;
    CKDWORD TransientQuadFastPathFallbacks;
    CKDWORD TransientSpriteBatchFastPathCandidates;
    CKDWORD TransientSpriteBatchFastPathHits;
    CKDWORD TransientSpriteBatchFastPathFallbacks;
    CKDWORD TransientFanToListConversions;
    CKDWORD TransientVertexBytes;
    CKDWORD TransientIndexBytes;
    CKDWORD ViewportSetCalls;
    CKDWORD ViewportSetSkipped;
    CKDWORD PrimitiveSubmits;
    CKDWORD MeshSubmits;
    CKDWORD SubmittedDraws;
};

double CKRenderFrameCostNow();
double CKRenderFrameCostElapsedUs(double start);

CKBOOL CKRenderFrameCostStatsEnabled();
CKBOOL CKRenderFrameCostStatsIsCollecting();
CKBOOL CKRenderFrameCostStatsWantsPlayerTiming();
void CKRenderFrameCostStatsBeginRenderFrame(CKDWORD entities3D, CKDWORD entities2D,
                                            CKDWORD cameras, CKDWORD lights);
void CKRenderFrameCostStatsEndRenderFrame();
void CKRenderFrameCostStatsAddPlayerTiming(double updateUs, double inputUs,
                                           double processUs, double renderUs,
                                           CKBOOL processRan, CKBOOL renderRan);
void CKRenderFrameCostStatsAddSection(CKRenderFrameCostSection section, double us);
void CKRenderFrameCostStatsAddDrawPrimitive(CKBOOL fastCandidate,
                                            CKBOOL fastHit);
void CKRenderFrameCostStatsAddViewportSet(CKBOOL skipped);
void CKRenderFrameCostStatsAddMaterialSet(CKBOOL noOpCandidate,
                                          CKBOOL skipped,
                                          CKBOOL dirtyMiss);
void CKRenderFrameCostStatsAddMeshRender();
void CKRenderFrameCostStatsAddMeshDefault();
void CKRenderFrameCostStatsAddMeshGroup();
void CKRenderFrameCostStatsAdd2DEntityRender();
void CKRenderFrameCostStatsAdd2DEntityUpdateExtents();
void CKRenderFrameCostStatsAdd2DEntityDraw();
void CKRenderFrameCostStatsAddSpriteDraw();
void CKRenderFrameCostStatsAddDrawPrimitiveSanitize();
void CKRenderFrameCostStatsAddTransientPrepare(CKBOOL quadCandidate,
                                               CKBOOL quadHit,
                                               CKDWORD vertexBytes,
                                               CKDWORD indexBytes,
                                               CKBOOL fanToListConversion);
void CKRenderFrameCostStatsAddTransientSpriteBatchFastPath(CKBOOL candidate,
                                                           CKBOOL hit);
void CKRenderFrameCostStatsAddPrimitiveSubmit();
void CKRenderFrameCostStatsAddMeshSubmit();
void CKRenderFrameCostStatsAddSubmittedDraw();

void CKRenderFrameCostStatsCopySnapshot(CKRenderFrameCostStatsSnapshot *snapshot);
void CKRenderFrameCostStatsResetForTests();
void CKRenderFrameCostStatsSetOutputEnabledForTests(CKBOOL enabled);

#endif
