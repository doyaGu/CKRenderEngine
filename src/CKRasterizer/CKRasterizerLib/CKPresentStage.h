#ifndef CKPRESENTSTAGE_H
#define CKPRESENTSTAGE_H

#include "VxMath.h"
#include "CKTypes.h"
#include "CKRasterizerDeviceEnums.h"
#include "CKRasterizerBackend.h"

// One internal render target: color + depth-stencil + backend render target.
struct CKPresentTarget {
    CKDWORD ColorTexture;
    CKDWORD DepthTexture;
    CKDWORD FrameBuffer;  // backend render target (CKRST_OBJ_RENDERTARGET)
    CKDWORD Width;
    CKDWORD Height;
    CKDWORD Samples;      // 0 = single sampled

    CKPresentTarget()
        : ColorTexture(0), DepthTexture(0), FrameBuffer(0), Width(0), Height(0), Samples(0) {}
    CKBOOL IsActive() const { return FrameBuffer != 0; }
};

// Backend handles of the fullscreen resolve / blit program (shaders
// vs_postprocess / fs_postprocess). The source is sampled through
// CKRST_BACKEND_SLOT_PRESENT, the parameters go to CKRST_BLOCK_PRESENT_PARAMS.
struct CKPresentResources {
    CKDWORD VertexShader;
    CKDWORD PixelShader;
    CKDWORD Program;
    CKDWORD VertexLayout;

    CKPresentResources()
        : VertexShader(0), PixelShader(0), Program(0), VertexLayout(0) {}
};

// Presentation stage of the virtual backbuffer (spec 4.4): the scene renders
// into the *scene target* (window size x RenderScale, MSAA), the resolve
// draws it (scale, FXAA, sharpen) into the *native target* (window size,
// single sampled), the overlay draws on the native target and the frame ends
// with a plain blit of the native target to the swap chain. Readbacks blit the
// native target into the readback texture. Driven by the translated context's
// frame flow; it does not open passes, the caller begins the pass each
// fullscreen draw goes to.
class CKPresentStage {
public:
    CKPresentStage();
    ~CKPresentStage();

    void Init(CKRasterizerBackend *backend);
    // Releases every backend object. Safe to call twice.
    void Shutdown();

    // Scene target of the given size and sample count; reuses the current one
    // when nothing changed. Returns FALSE when the device cannot provide it.
    CKBOOL EnsureSceneTarget(CKDWORD width, CKDWORD height, CKDWORD samples);
    // Native target of the given size (single sampled).
    CKBOOL EnsureNativeTarget(CKDWORD width, CKDWORD height);
    void DestroyTargets();
    CKBOOL EnsureResources();
    void DestroyResources();

    const CKPresentTarget &SceneTarget() const { return m_Scene; }
    const CKPresentTarget &NativeTarget() const { return m_Native; }
    // Readback texture (BLIT_DST | READBACK) of the given size, recreated when
    // the size changes; 0 when the device cannot blit or read textures back.
    // Render targets cannot be read directly, readbacks blit into this one.
    CKDWORD AcquireReadbackTexture(CKDWORD width, CKDWORD height);
    CKDWORD GetReadbackTexture() const { return m_ReadbackTexture; }
    const CKPresentResources &GetResourceIds() const { return m_ResourceIds; }

    // Draws the scene color as a fullscreen triangle into the current pass (the resolve).
    CKERROR SubmitResolve(CKBOOL fxaa, float sharpness);
    // Draws the native color as a fullscreen triangle into the current pass (the blit).
    CKERROR SubmitBlit();

    static CKDWORD ScaledDimension(CKDWORD value, float scale, CKDWORD maximum);
    static float ClampRenderScale(float scale);
    static float ClampSharpness(float sharpness);

private:
    CKBOOL CreateTarget(CKPresentTarget &target, CKDWORD width, CKDWORD height, CKDWORD samples);
    void DestroyTarget(CKPresentTarget &target);
    void DestroyReadbackTexture();
    CKERROR Submit(const CKPresentTarget &source, CKBOOL fxaa, float sharpness);

    CKRasterizerBackend *m_Backend;
    CKPresentResources m_ResourceIds;
    CKPresentTarget m_Scene;
    CKPresentTarget m_Native;
    CKDWORD m_ReadbackTexture;    // BLIT_DST | READBACK copy target for readbacks
    CKDWORD m_ReadbackWidth;
    CKDWORD m_ReadbackHeight;
    CKDWORD m_VertexShaderProfile;
};

#endif // CKPRESENTSTAGE_H
