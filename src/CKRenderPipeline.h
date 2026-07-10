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
#define CKRP_VIEW_STENCIL_CLEAR 4
#define CKRP_VIEW_TRANSPARENT  5
#define CKRP_VIEW_POSTPROCESS  6
#define CKRP_VIEW_FOREGROUND2D 7
#define CKRP_VIEW_COUNT        8

struct CKRenderPipelineResourceIds {
    CKDWORD SceneColorTexture;
    CKDWORD SceneDepthTexture;
    CKDWORD SceneFrameBuffer;
    CKDWORD PostVertexShader;
    CKDWORD PostPixelShader;
    CKDWORD PostProgram;
    CKDWORD PostSamplerUniform;
    CKDWORD PostParamsUniform;
    CKDWORD PostVertexLayout;

    CKRenderPipelineResourceIds()
        : SceneColorTexture(0), SceneDepthTexture(0), SceneFrameBuffer(0),
          PostVertexShader(0), PostPixelShader(0), PostProgram(0),
          PostSamplerUniform(0), PostParamsUniform(0), PostVertexLayout(0) {}
};

struct CKRenderPipelineConfig {
    CKBOOL FXAA;
    float RenderScale;
    float Sharpness;

    CKRenderPipelineConfig() : FXAA(FALSE), RenderScale(1.0f), Sharpness(0.0f) {}
    CKBOOL NeedsSceneFrameBuffer() const {
        return FXAA || Sharpness > 0.001f || RenderScale < 0.999f || RenderScale > 1.001f;
    }
};

float CKRenderPipelineClampRenderScale(float scale);
float CKRenderPipelineClampSharpness(float sharpness);
CKRenderPipelineConfig CKRenderPipelineConfigFromSettings();

class CKRenderPipeline {
public:
    CKRenderPipeline();
    ~CKRenderPipeline();

    void Init(CKRasterizerContext *ctx);
    void Shutdown();
    void SetResourceIds(const CKRenderPipelineResourceIds &ids);
    void SetExternalRenderTarget(CKBOOL enabled);

    // Begin a new frame: configure views, acquire encoder
    void BeginFrame(const CKRECT &viewport, CKDWORD clearFlags,
                    CKDWORD clearColor, float clearZ,
                    const VxMatrix &view, const VxMatrix &proj);

    // Composite the optional offscreen scene target to the backbuffer before
    // foreground 2D is drawn.
    void CompositeScene();

    // End the frame: release encoder, call Frame()
    void EndFrame(CKRST_FRAME_SYNC_MODE syncMode);

    // Queue a stencil-only clear in a dedicated view that sorts after opaque
    // scene draws and before transparent draws.
    CKBOOL QueueStencilClearBeforeTransparent(const CKRECT &viewport, CKDWORD stencil);

    // Access the current encoder (valid between BeginFrame/EndFrame)
    CKRasterizerEncoder *GetEncoder() const { return m_Encoder; }

    // View accessors for draw routing
    CKRenderView GetClearView() const        { return CKRP_VIEW_CLEAR; }
    CKRenderView GetBackground2DView() const { return CKRP_VIEW_BACKGROUND2D; }
    CKRenderView GetRenderFirst3DView() const { return CKRP_VIEW_RENDERFIRST3D; }
    CKRenderView GetOpaqueView() const       { return CKRP_VIEW_OPAQUE3D; }
    CKRenderView GetStencilClearView() const { return CKRP_VIEW_STENCIL_CLEAR; }
    CKRenderView GetTransparentView() const  { return CKRP_VIEW_TRANSPARENT; }
    CKRenderView GetPostprocessView() const  { return CKRP_VIEW_POSTPROCESS; }
    CKRenderView GetForeground2DView() const { return CKRP_VIEW_FOREGROUND2D; }

    // Check if we're inside a frame
    CKBOOL IsInFrame() const { return m_Encoder != nullptr; }
    CKBOOL IsSceneFrameBufferEnabled() const { return m_SceneFrameBufferActive; }
    CKDWORD GetFrameNumber() const { return m_FrameNumber; }

private:
    CKBOOL EnsurePostprocessResources();
    CKBOOL EnsureSceneFrameBuffer(const CKRECT &viewport);
    void DestroySceneFrameBuffer();
    void DestroyPostprocessResources();
    void BindFrameBuffer(CKRenderView view, CKDWORD frameBuffer);
    void ConfigurePostprocessView(const CKRECT &viewport);
    CKBOOL SubmitPostprocess();

    CKRasterizerContext *m_Context;
    CKRasterizerEncoder *m_Encoder;
    VxMatrix m_OrthoProj;
    CKRenderPipelineResourceIds m_ResourceIds;
    CKRenderPipelineConfig m_Config;
    CKBOOL m_ExternalRenderTarget;
    CKBOOL m_SceneFrameBufferActive;
    CKBOOL m_PostprocessSubmitted;
    CKDWORD m_SceneWidth;
    CKDWORD m_SceneHeight;
    CKDWORD m_PostVertexShaderProfile;
    CKDWORD m_FrameNumber;
};

#endif // CKRENDERPIPELINE_H
