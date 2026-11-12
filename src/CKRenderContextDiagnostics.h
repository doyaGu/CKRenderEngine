#pragma once
#include "CKRenderDiagnosticsContext.h"
#include "CKRenderConfig.h"
#include "CKTypes.h"

const CKRenderDiagnosticHandle *CKRenderDiagnosticsForContext(CKContext *context);

#if CKRE_ENABLE_DEBUG_LOGGER || CKRE_ENABLE_RENDER_STATS || CKRE_ENABLE_FRAME_COST_STATS || CKRE_ENABLE_FRAME_DIAGNOSTICS || CKRE_ENABLE_MESH_DIAGNOSTICS || CKRE_ENABLE_FFP_DIAGNOSTICS
#define CK_CONTEXT_DIAGNOSTIC_SCOPE(context) CKRenderDiagnosticScope contextDiagnosticsScope(CKRenderDiagnosticsForContext(context))
#else
#define CK_CONTEXT_DIAGNOSTIC_SCOPE(context) ((void)0)
#endif

struct CKRenderLogLimits {
    int PresentSync, PresentFrames, CameraAttach, SceneCamera, AttachedCamera, TargetCamera;
    int MeshDraw, ChannelMeshDraw, IndexRange;
};
inline CKRenderLogLimits &CKRenderContextLogLimits() {
    return CKRenderDiagnosticState<CKRenderLogLimits>(CK_DIAGNOSTIC_LOG_LIMITS);
}
