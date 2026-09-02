#ifndef CKPOSTPROCESSPASS_H
#define CKPOSTPROCESSPASS_H

#include "VxMath.h"
#include "CKTypes.h"
#include "CKRasterizerDeviceEnums.h"

class CKRasterizerDevice;
class CKRasterizerEncoder;

// Device handles of the postprocess (scene framebuffer + composite) pass.
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

// Offscreen scene target plus the fullscreen composite that resolves it to
// the presented backbuffer (RenderScale / FXAA / Sharpness). Shared by the
// legacy CKRenderPipeline frame flow and the v3 translated context; it does
// not own any view, the caller decides which view the composite goes to.
class CKPostprocessPass {
public:
    CKPostprocessPass();
    ~CKPostprocessPass();

    void Init(CKRasterizerDevice *device);
    // Releases every device object. Safe to call twice.
    void Shutdown();

    // Scene color + depth + framebuffer of the given size; reuses the current
    // ones when the size did not change. Returns FALSE when the device cannot
    // provide them (the caller then renders straight to the backbuffer).
    CKBOOL EnsureSceneFrameBuffer(CKDWORD width, CKDWORD height);
    void DestroySceneFrameBuffer();
    CKBOOL EnsureResources();
    void DestroyResources();

    CKBOOL IsSceneFrameBufferActive() const { return m_SceneFrameBufferActive; }
    CKDWORD GetSceneFrameBuffer() const { return m_ResourceIds.SceneFrameBuffer; }
    CKDWORD GetSceneColorTexture() const { return m_ResourceIds.SceneColorTexture; }
    CKDWORD GetSceneWidth() const { return m_SceneWidth; }
    CKDWORD GetSceneHeight() const { return m_SceneHeight; }
    const CKRenderPipelineResourceIds &GetResourceIds() const { return m_ResourceIds; }

    // Draws the scene color texture as a fullscreen triangle into `view`.
    CKERROR Submit(CKRasterizerEncoder *encoder, CKRenderView view, CKBOOL fxaa, float sharpness);

    static CKDWORD ScaledDimension(CKDWORD value, float scale, CKDWORD maximum);
    static float ClampRenderScale(float scale);
    static float ClampSharpness(float sharpness);

private:
    CKRasterizerDevice *m_Device;
    CKRenderPipelineResourceIds m_ResourceIds;
    CKBOOL m_SceneFrameBufferActive;
    CKDWORD m_SceneWidth;
    CKDWORD m_SceneHeight;
    CKDWORD m_PostVertexShaderProfile;
};

#endif // CKPOSTPROCESSPASS_H
