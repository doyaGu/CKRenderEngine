#ifndef CK_RENDER_FRAME_COST_STATS_H
#define CK_RENDER_FRAME_COST_STATS_H

#include "CKRenderConfig.h"
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

#if CKRE_ENABLE_FRAME_COST_STATS

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

#define CK_FRAME_COST_NOW() CKRenderFrameCostNow()
#define CK_FRAME_COST_ELAPSED_US(Start) CKRenderFrameCostElapsedUs(Start)
#define CK_FRAME_COST_IS_COLLECTING() CKRenderFrameCostStatsIsCollecting()
#define CK_FRAME_COST_BEGIN_RENDER_FRAME(Entities3D, Entities2D, Cameras, Lights) \
    CKRenderFrameCostStatsBeginRenderFrame((Entities3D), (Entities2D), (Cameras), (Lights))
#define CK_FRAME_COST_END_RENDER_FRAME() CKRenderFrameCostStatsEndRenderFrame()
#define CK_FRAME_COST_ADD_SECTION(Section, Us) CKRenderFrameCostStatsAddSection((Section), (Us))
#define CK_FRAME_COST_ADD_DRAW_PRIMITIVE(FastCandidate, FastHit) \
    CKRenderFrameCostStatsAddDrawPrimitive((FastCandidate), (FastHit))
#define CK_FRAME_COST_ADD_VIEWPORT_SET(Skipped) CKRenderFrameCostStatsAddViewportSet((Skipped))
#define CK_FRAME_COST_ADD_MATERIAL_SET(NoOpCandidate, Skipped, DirtyMiss) \
    CKRenderFrameCostStatsAddMaterialSet((NoOpCandidate), (Skipped), (DirtyMiss))
#define CK_FRAME_COST_ADD_MESH_RENDER() CKRenderFrameCostStatsAddMeshRender()
#define CK_FRAME_COST_ADD_MESH_DEFAULT() CKRenderFrameCostStatsAddMeshDefault()
#define CK_FRAME_COST_ADD_MESH_GROUP() CKRenderFrameCostStatsAddMeshGroup()
#define CK_FRAME_COST_ADD_2D_ENTITY_RENDER() CKRenderFrameCostStatsAdd2DEntityRender()
#define CK_FRAME_COST_ADD_2D_ENTITY_UPDATE_EXTENTS() CKRenderFrameCostStatsAdd2DEntityUpdateExtents()
#define CK_FRAME_COST_ADD_2D_ENTITY_DRAW() CKRenderFrameCostStatsAdd2DEntityDraw()
#define CK_FRAME_COST_ADD_SPRITE_DRAW() CKRenderFrameCostStatsAddSpriteDraw()
#define CK_FRAME_COST_ADD_DRAW_PRIMITIVE_SANITIZE() CKRenderFrameCostStatsAddDrawPrimitiveSanitize()
#define CK_FRAME_COST_ADD_TRANSIENT_PREPARE(QuadCandidate, QuadHit, VertexBytes, IndexBytes, FanToListConversion) \
    CKRenderFrameCostStatsAddTransientPrepare((QuadCandidate), (QuadHit), (VertexBytes), (IndexBytes), (FanToListConversion))
#define CK_FRAME_COST_ADD_TRANSIENT_SPRITE_BATCH_FAST_PATH(Candidate, Hit) \
    CKRenderFrameCostStatsAddTransientSpriteBatchFastPath((Candidate), (Hit))
#define CK_FRAME_COST_ADD_PRIMITIVE_SUBMIT() CKRenderFrameCostStatsAddPrimitiveSubmit()
#define CK_FRAME_COST_ADD_MESH_SUBMIT() CKRenderFrameCostStatsAddMeshSubmit()
#define CK_FRAME_COST_ADD_SUBMITTED_DRAW() CKRenderFrameCostStatsAddSubmittedDraw()
#define CK_FRAME_COST_DECLARE_COLLECTING(Name) const CKBOOL Name = CK_FRAME_COST_IS_COLLECTING()
#define CK_FRAME_COST_DECLARE_SECTION_START(Name, Collecting) double Name = (Collecting) ? CK_FRAME_COST_NOW() : 0.0
#define CK_FRAME_COST_RESTART_SECTION(Name, Collecting) \
    do { (Name) = (Collecting) ? CK_FRAME_COST_NOW() : 0.0; } while (0)
#define CK_FRAME_COST_ADD_SECTION_FROM_START(Section, Start) \
    do { if ((Start) > 0.0) CK_FRAME_COST_ADD_SECTION((Section), CK_FRAME_COST_ELAPSED_US(Start)); } while (0)

#else

inline double CKRenderFrameCostNow() { return 0.0; }
inline double CKRenderFrameCostElapsedUs(double) { return 0.0; }

inline CKBOOL CKRenderFrameCostStatsEnabled() { return FALSE; }
inline CKBOOL CKRenderFrameCostStatsIsCollecting() { return FALSE; }
inline CKBOOL CKRenderFrameCostStatsWantsPlayerTiming() { return FALSE; }
inline void CKRenderFrameCostStatsBeginRenderFrame(CKDWORD, CKDWORD, CKDWORD, CKDWORD) {}
inline void CKRenderFrameCostStatsEndRenderFrame() {}
inline void CKRenderFrameCostStatsAddPlayerTiming(double, double, double, double, CKBOOL, CKBOOL) {}
inline void CKRenderFrameCostStatsAddSection(CKRenderFrameCostSection, double) {}
inline void CKRenderFrameCostStatsAddDrawPrimitive(CKBOOL, CKBOOL) {}
inline void CKRenderFrameCostStatsAddViewportSet(CKBOOL) {}
inline void CKRenderFrameCostStatsAddMaterialSet(CKBOOL, CKBOOL, CKBOOL) {}
inline void CKRenderFrameCostStatsAddMeshRender() {}
inline void CKRenderFrameCostStatsAddMeshDefault() {}
inline void CKRenderFrameCostStatsAddMeshGroup() {}
inline void CKRenderFrameCostStatsAdd2DEntityRender() {}
inline void CKRenderFrameCostStatsAdd2DEntityUpdateExtents() {}
inline void CKRenderFrameCostStatsAdd2DEntityDraw() {}
inline void CKRenderFrameCostStatsAddSpriteDraw() {}
inline void CKRenderFrameCostStatsAddDrawPrimitiveSanitize() {}
inline void CKRenderFrameCostStatsAddTransientPrepare(CKBOOL, CKBOOL, CKDWORD, CKDWORD, CKBOOL) {}
inline void CKRenderFrameCostStatsAddTransientSpriteBatchFastPath(CKBOOL, CKBOOL) {}
inline void CKRenderFrameCostStatsAddPrimitiveSubmit() {}
inline void CKRenderFrameCostStatsAddMeshSubmit() {}
inline void CKRenderFrameCostStatsAddSubmittedDraw() {}
inline void CKRenderFrameCostStatsCopySnapshot(CKRenderFrameCostStatsSnapshot *snapshot)
{
    if (snapshot) {
        CKRenderFrameCostStatsSnapshot empty = {};
        *snapshot = empty;
    }
}
inline void CKRenderFrameCostStatsResetForTests() {}
inline void CKRenderFrameCostStatsSetOutputEnabledForTests(CKBOOL) {}

#define CK_FRAME_COST_NOW() 0.0
#define CK_FRAME_COST_ELAPSED_US(Start) 0.0
#define CK_FRAME_COST_IS_COLLECTING() FALSE
#define CK_FRAME_COST_BEGIN_RENDER_FRAME(Entities3D, Entities2D, Cameras, Lights) do {} while (0)
#define CK_FRAME_COST_END_RENDER_FRAME() do {} while (0)
#define CK_FRAME_COST_ADD_SECTION(Section, Us) do {} while (0)
#define CK_FRAME_COST_ADD_DRAW_PRIMITIVE(FastCandidate, FastHit) do {} while (0)
#define CK_FRAME_COST_ADD_VIEWPORT_SET(Skipped) do {} while (0)
#define CK_FRAME_COST_ADD_MATERIAL_SET(NoOpCandidate, Skipped, DirtyMiss) do {} while (0)
#define CK_FRAME_COST_ADD_MESH_RENDER() do {} while (0)
#define CK_FRAME_COST_ADD_MESH_DEFAULT() do {} while (0)
#define CK_FRAME_COST_ADD_MESH_GROUP() do {} while (0)
#define CK_FRAME_COST_ADD_2D_ENTITY_RENDER() do {} while (0)
#define CK_FRAME_COST_ADD_2D_ENTITY_UPDATE_EXTENTS() do {} while (0)
#define CK_FRAME_COST_ADD_2D_ENTITY_DRAW() do {} while (0)
#define CK_FRAME_COST_ADD_SPRITE_DRAW() do {} while (0)
#define CK_FRAME_COST_ADD_DRAW_PRIMITIVE_SANITIZE() do {} while (0)
#define CK_FRAME_COST_ADD_TRANSIENT_PREPARE(QuadCandidate, QuadHit, VertexBytes, IndexBytes, FanToListConversion) do {} while (0)
#define CK_FRAME_COST_ADD_TRANSIENT_SPRITE_BATCH_FAST_PATH(Candidate, Hit) do {} while (0)
#define CK_FRAME_COST_ADD_PRIMITIVE_SUBMIT() do {} while (0)
#define CK_FRAME_COST_ADD_MESH_SUBMIT() do {} while (0)
#define CK_FRAME_COST_ADD_SUBMITTED_DRAW() do {} while (0)
#define CK_FRAME_COST_DECLARE_COLLECTING(Name) do {} while (0)
#define CK_FRAME_COST_DECLARE_SECTION_START(Name, Collecting) do {} while (0)
#define CK_FRAME_COST_RESTART_SECTION(Name, Collecting) do {} while (0)
#define CK_FRAME_COST_ADD_SECTION_FROM_START(Section, Start) do {} while (0)

#endif

#endif
