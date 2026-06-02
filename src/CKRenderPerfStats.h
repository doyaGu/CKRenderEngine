#ifndef CK_RENDER_PERF_STATS_H
#define CK_RENDER_PERF_STATS_H

#include "CKRenderConfig.h"
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

#if CKRE_ENABLE_RENDER_STATS
bool CKRenderPerfStatsEnabled();
double CKRenderPerfNow();
double CKRenderPerfElapsedUs(double start);
void CKRenderPerfResetNowCallCountForTests();
CKDWORD CKRenderPerfNowCallCountForTests();
void CKRenderPerfBeginFrame(CKDWORD entities3D, CKDWORD entities2D, CKDWORD cameras, CKDWORD lights);
void CKRenderPerfAddSection(CKRenderPerfSection section, double us);
void CKRenderPerfLogAndReset();
CKRenderPerfStats &CKRenderPerfCurrent();

#define CK_RENDER_PERF_DECLARE_ENABLED(Name) const bool Name = CKRenderPerfStatsEnabled()
#define CK_RENDER_PERF_DECLARE_TIMER(Name, Enabled) const double Name = (Enabled) ? CKRenderPerfNow() : 0.0
#define CK_RENDER_PERF_RESTART_TIMER(Name, Enabled) do { (Name) = (Enabled) ? CKRenderPerfNow() : 0.0; } while (0)
#define CK_RENDER_PERF_BEGIN_FRAME(Entities3D, Entities2D, Cameras, Lights) \
    CKRenderPerfBeginFrame((Entities3D), (Entities2D), (Cameras), (Lights))
#define CK_RENDER_PERF_ADD_SECTION(Enabled, Section, Start) \
    do { if (Enabled) CKRenderPerfAddSection((Section), CKRenderPerfElapsedUs(Start)); } while (0)
#define CK_RENDER_PERF_LOG_AND_RESET() CKRenderPerfLogAndReset()
#define CK_RENDER_PERF_INC(Enabled, Field) do { if (Enabled) ++CKRenderPerfCurrent().Field; } while (0)
#define CK_RENDER_PERF_ADD(Enabled, Field, Value) do { if (Enabled) CKRenderPerfCurrent().Field += (Value); } while (0)
#define CK_RENDER_PERF_IF(Enabled, Code) do { if (Enabled) { Code } } while (0)

#else
inline bool CKRenderPerfStatsEnabled() { return false; }
inline double CKRenderPerfNow() { return 0.0; }
inline double CKRenderPerfElapsedUs(double) { return 0.0; }
inline void CKRenderPerfResetNowCallCountForTests() {}
inline CKDWORD CKRenderPerfNowCallCountForTests() { return 0; }
inline void CKRenderPerfBeginFrame(CKDWORD, CKDWORD, CKDWORD, CKDWORD) {}
inline void CKRenderPerfAddSection(CKRenderPerfSection, double) {}
inline void CKRenderPerfLogAndReset() {}
inline CKRenderPerfStats &CKRenderPerfCurrent() { static CKRenderPerfStats s = {}; return s; }

#define CK_RENDER_PERF_DECLARE_ENABLED(Name) do {} while (0)
#define CK_RENDER_PERF_DECLARE_TIMER(Name, Enabled) do {} while (0)
#define CK_RENDER_PERF_RESTART_TIMER(Name, Enabled) do {} while (0)
#define CK_RENDER_PERF_BEGIN_FRAME(Entities3D, Entities2D, Cameras, Lights) do {} while (0)
#define CK_RENDER_PERF_ADD_SECTION(Enabled, Section, Start) do {} while (0)
#define CK_RENDER_PERF_LOG_AND_RESET() do {} while (0)
#define CK_RENDER_PERF_INC(Enabled, Field) do {} while (0)
#define CK_RENDER_PERF_ADD(Enabled, Field, Value) do {} while (0)
#define CK_RENDER_PERF_IF(Enabled, Code) do {} while (0)

#endif

#endif
