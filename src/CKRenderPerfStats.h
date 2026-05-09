#ifndef CK_RENDER_PERF_STATS_H
#define CK_RENDER_PERF_STATS_H

#include "CKTypes.h"

enum CKRenderPerfSection {
    CKRPS_FRAME_SETUP = 0,
    CKRPS_BEGIN_FRAME,
    CKRPS_DEFAULT_STATES,
    CKRPS_BACKGROUND_2D,
    CKRPS_SETUP_LIGHTS,
    CKRPS_PRE_CALLBACKS,
    CKRPS_OPAQUE_TRAVERSAL,
    CKRPS_SPRITE3D,
    CKRPS_POST_CALLBACKS,
    CKRPS_TRANSPARENT_SORT_RENDER,
    CKRPS_FOREGROUND_2D,
    CKRPS_POST_SPRITE_CALLBACKS,
    CKRPS_SECTION_COUNT
};

struct CKRenderPerfStats {
    CKDWORD FrameIndex;
    CKDWORD Entities3D;
    CKDWORD Entities2D;
    CKDWORD Cameras;
    CKDWORD Lights;
    CKDWORD MeshRenderCalls;
    CKDWORD MeshDefaultCalls;
    CKDWORD MeshGroupCalls;
    CKDWORD MeshChannelPasses;
    CKDWORD MaterialSetCalls;
    CKDWORD DrawPrimitiveCalls;
    CKDWORD DrawVertexBufferCalls;
    CKDWORD PrimitiveEntries;
    CKDWORD SoftwarePrimitiveEntries;
    CKDWORD HardwarePrimitiveEntries;
    CKDWORD ChannelPrimitiveEntries;
    CKDWORD RenderChannelMeshes;
    CKDWORD AlphaGroups;
    CKDWORD VertexBufferChecks;
    CKDWORD VertexBufferReady;
    CKDWORD TotalGroupIndices;
    CKDWORD TotalChannelIndices;
    double SectionsUs[CKRPS_SECTION_COUNT];
    double MeshRenderUs;
    double MeshDefaultUs;
    double MeshGroupUs;
    double MeshChannelsUs;
    double MaterialSetUs;
    double DrawPrimitiveWrapperUs;
};

bool CKRenderPerfStatsEnabled();
double CKRenderPerfNow();
double CKRenderPerfElapsedUs(double start);
void CKRenderPerfBeginFrame(CKDWORD entities3D, CKDWORD entities2D, CKDWORD cameras, CKDWORD lights);
void CKRenderPerfAddSection(CKRenderPerfSection section, double us);
void CKRenderPerfLogAndReset();
CKRenderPerfStats &CKRenderPerfCurrent();

#endif
