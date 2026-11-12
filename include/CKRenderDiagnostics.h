#pragma once
#include "CKRenderConfiguration.h"
#include <cstdint>

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
    CKDWORD TransientPrepareCalls;
    CKDWORD TransientFanToListConversions;
    CKDWORD TransientVertexBytes;
    CKDWORD TransientIndexBytes;
    CKDWORD ViewportSetCalls;
    CKDWORD PrimitiveSubmits;
    CKDWORD MeshSubmits;
    CKDWORD SubmittedDraws;
};


struct CKRenderDiagnosticsSnapshot {
    uint64_t ContextId;
    CKRenderPerfStats Render;
    CKRenderFrameCostStatsSnapshot FrameCost;
};

// Context must belong to this engine. Read on its render thread or while idle.
// Pass sizeof(*snapshot). Disabled diagnostic families return zeros.
extern "C" CKRE_CONFIGURATION_EXPORT CKERROR CKGetRenderDiagnostics(
    CKContext *context, CKRenderDiagnosticsSnapshot *snapshot, CKDWORD size);
