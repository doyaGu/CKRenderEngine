#ifndef CKPOSTPROCESSPASS_H
#define CKPOSTPROCESSPASS_H

#include "VxMath.h"
#include "CKTypes.h"
#include "CKRasterizerDeviceEnums.h"

class CKRasterizerDevice;
class CKRasterizerEncoder;

// One internal render target: color + depth-stencil + framebuffer.
struct CKPostprocessTarget {
    CKDWORD ColorTexture;
    CKDWORD DepthTexture;
    CKDWORD FrameBuffer;
    CKDWORD Width;
    CKDWORD Height;
    CKDWORD Samples;      // 0 = single sampled
    CKBOOL Readable;      // color texture created with CKRST_TEXTURE_READBACK

    CKPostprocessTarget()
        : ColorTexture(0), DepthTexture(0), FrameBuffer(0), Width(0), Height(0), Samples(0),
          Readable(FALSE) {}
    CKBOOL IsActive() const { return FrameBuffer != 0; }
};

// Device handles of the fullscreen resolve / blit program.
struct CKPostprocessResourceIds {
    CKDWORD PostVertexShader;
    CKDWORD PostPixelShader;
    CKDWORD PostProgram;
    CKDWORD PostSamplerUniform;
    CKDWORD PostParamsUniform;
    CKDWORD PostVertexLayout;

    CKPostprocessResourceIds()
        : PostVertexShader(0), PostPixelShader(0), PostProgram(0),
          PostSamplerUniform(0), PostParamsUniform(0), PostVertexLayout(0) {}
};

// Virtual backbuffer (spec 4.4): the scene renders into the *scene target*
// (window size x RenderScale, MSAA), the composite resolves it (scale, FXAA,
// sharpen) into the *native target* (window size, single sampled, readable),
// the overlay draws on the native target and the frame ends with a plain blit
// of the native target to the swap chain. Driven by the translated context's
// frame flow; it does not own any view, the caller decides which view each
// fullscreen draw goes to.
class CKPostprocessPass {
public:
    CKPostprocessPass();
    ~CKPostprocessPass();

    void Init(CKRasterizerDevice *device);
    // Releases every device object. Safe to call twice.
    void Shutdown();

    // Scene target of the given size and sample count; reuses the current one
    // when nothing changed. Returns FALSE when the device cannot provide it.
    CKBOOL EnsureSceneTarget(CKDWORD width, CKDWORD height, CKDWORD samples);
    // Native target of the given size (single sampled, readable when the
    // device supports texture readback).
    CKBOOL EnsureNativeTarget(CKDWORD width, CKDWORD height);
    void DestroyTargets();
    CKBOOL EnsureResources();
    void DestroyResources();

    const CKPostprocessTarget &SceneTarget() const { return m_Scene; }
    const CKPostprocessTarget &NativeTarget() const { return m_Native; }
    const CKPostprocessResourceIds &GetResourceIds() const { return m_ResourceIds; }

    // Draws the scene color as a fullscreen triangle into `view` (the resolve).
    CKERROR SubmitResolve(CKRasterizerEncoder *encoder, CKRenderView view, CKBOOL fxaa, float sharpness);
    // Draws the native color as a fullscreen triangle into `view` (the blit).
    CKERROR SubmitBlit(CKRasterizerEncoder *encoder, CKRenderView view);

    static CKDWORD ScaledDimension(CKDWORD value, float scale, CKDWORD maximum);
    static float ClampRenderScale(float scale);
    static float ClampSharpness(float sharpness);

private:
    CKBOOL CreateTarget(CKPostprocessTarget &target, CKDWORD width, CKDWORD height, CKDWORD samples,
                        CKBOOL readable);
    void DestroyTarget(CKPostprocessTarget &target);
    CKERROR Submit(CKRasterizerEncoder *encoder, CKRenderView view, const CKPostprocessTarget &source,
                   CKBOOL fxaa, float sharpness);

    CKRasterizerDevice *m_Device;
    CKPostprocessResourceIds m_ResourceIds;
    CKPostprocessTarget m_Scene;
    CKPostprocessTarget m_Native;
    CKDWORD m_PostVertexShaderProfile;
};

#endif // CKPOSTPROCESSPASS_H
