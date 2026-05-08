#ifndef CKRENDERPIPELINE_H
#define CKRENDERPIPELINE_H

#include "VxMath.h"
#include "CKTypes.h"
#include "CKRasterizerEnums.h"

class CKRasterizerContext;
class CKRasterizerEncoder;

// Render view IDs
#define CKRP_VIEW_CLEAR        0
#define CKRP_VIEW_BACKGROUND2D 1
#define CKRP_VIEW_RENDERFIRST3D 2
#define CKRP_VIEW_OPAQUE3D     3
#define CKRP_VIEW_TRANSPARENT  4
#define CKRP_VIEW_FOREGROUND2D 5
#define CKRP_VIEW_COUNT        6

class CKRenderPipeline {
public:
    CKRenderPipeline();
    ~CKRenderPipeline();

    void Init(CKRasterizerContext *ctx);
    void Shutdown();

    // Begin a new frame: configure views, acquire encoder
    void BeginFrame(const CKRECT &viewport, CKDWORD clearFlags,
                    CKDWORD clearColor, float clearZ,
                    const VxMatrix &view, const VxMatrix &proj);

    // End the frame: release encoder, call Frame()
    void EndFrame(CKRST_FRAME_SYNC_MODE syncMode);

    // Access the current encoder (valid between BeginFrame/EndFrame)
    CKRasterizerEncoder *GetEncoder() const { return m_Encoder; }

    // View accessors for draw routing
    CKRenderView GetClearView() const        { return CKRP_VIEW_CLEAR; }
    CKRenderView GetBackground2DView() const { return CKRP_VIEW_BACKGROUND2D; }
    CKRenderView GetRenderFirst3DView() const { return CKRP_VIEW_RENDERFIRST3D; }
    CKRenderView GetOpaqueView() const       { return CKRP_VIEW_OPAQUE3D; }
    CKRenderView GetTransparentView() const  { return CKRP_VIEW_TRANSPARENT; }
    CKRenderView GetForeground2DView() const { return CKRP_VIEW_FOREGROUND2D; }

    // Check if we're inside a frame
    CKBOOL IsInFrame() const { return m_Encoder != nullptr; }

private:
    CKRasterizerContext *m_Context;
    CKRasterizerEncoder *m_Encoder;
    VxMatrix m_OrthoProj;
};

#endif // CKRENDERPIPELINE_H
