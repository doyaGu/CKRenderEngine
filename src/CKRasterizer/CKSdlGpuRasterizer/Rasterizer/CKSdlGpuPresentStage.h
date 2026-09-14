#ifndef CKSDLGPUPRESENTSTAGE_H
#define CKSDLGPUPRESENTSTAGE_H

#include "VxMath.h"
#include "CKTypes.h"
#include "CKRasterizerContextEnums.h"
#include "CKRasterizerContextData.h"
#include "CKBuiltinShaders.h"
#include "CKFFPresentDraw.h"
#include "CKFFShaderInterface.h"

class CKSdlGpuRasterizerContext;

// One internal render target: color + depth-stencil + backend render target.
struct CKSdlGpuPresentTarget {
    CKDWORD ColorTexture;
    CKDWORD DepthTexture;
    CKDWORD FrameBuffer;  // backend render target (CKRST_OBJ_RENDERTARGET)
    CKDWORD Width;
    CKDWORD Height;
    CKDWORD Samples;      // 0 = single sampled

    CKSdlGpuPresentTarget()
        : ColorTexture(0), DepthTexture(0), FrameBuffer(0), Width(0), Height(0), Samples(0) {}
    CKBOOL IsActive() const { return FrameBuffer != 0; }
};

// Backend handles of the fullscreen resolve / blit program (shaders
// vs_postprocess / fs_postprocess). The source is sampled through
// CKFF_SLOT_PRESENT, the parameters go to CKRST_BLOCK_PRESENT_PARAMS.
struct CKSdlGpuPresentResources {
    CKDWORD VertexShader;
    CKDWORD PixelShader;
    CKDWORD Program;
    CKDWORD VertexLayout;

    CKSdlGpuPresentResources()
        : VertexShader(0), PixelShader(0), Program(0), VertexLayout(0) {}
};

// Presentation stage of the virtual backbuffer: the scene renders
// into the *scene target* (window size x RenderScale, MSAA), and the resolve
// draws it (scale, FXAA, sharpen) into the *native target* (window size,
// single sampled). The concrete Context may render the scene straight into
// the native target when that resolve would be an identity operation. The
// overlay draws on the native target and the frame ends with a plain blit to
// the swap chain. Readbacks blit the native target into the readback texture.
// This class does not open passes; the caller begins each fullscreen pass.
class CKSdlGpuPresentStage {
public:
    CKSdlGpuPresentStage();
    ~CKSdlGpuPresentStage();

    void Init(CKSdlGpuRasterizerContext *Context,
              const CKFFShaderSet &Shaders);
    // Releases every backend object. Safe to call twice.
    void Shutdown();

    // Scene target of the given size and sample count; reuses the current one
    // when nothing changed. Returns FALSE when the backend cannot provide it.
    CKBOOL EnsureSceneTarget(CKDWORD width, CKDWORD height, CKDWORD samples);
    // Native target of the given size (single sampled).
    CKBOOL EnsureNativeTarget(CKDWORD width, CKDWORD height);
    void DestroyTargets();
    CKBOOL EnsureResources();
    void DestroyResources();

    const CKSdlGpuPresentTarget &SceneTarget() const { return m_Scene; }
    const CKSdlGpuPresentTarget &NativeTarget() const { return m_Native; }
    // Readback texture (BLIT_DST | READBACK) of the given size, recreated when
    // the size changes; 0 when the backend cannot blit or read textures back.
    // Render targets cannot be read directly, readbacks blit into this one.
    CKDWORD AcquireReadbackTexture(CKDWORD width, CKDWORD height);
    CKDWORD GetReadbackTexture() const { return m_ReadbackTexture; }
    const CKSdlGpuPresentResources &GetResourceIds() const { return m_ResourceIds; }

    // Draws the scene color as a fullscreen triangle into the current pass (the resolve).
    CKERROR SubmitResolve(CKBOOL fxaa, float sharpness);
    // Draws the native color as a fullscreen triangle into the current pass (the blit).
    CKERROR SubmitBlit();
    // Point-samples a 2D snapshot into the current pass, without postprocessing.
    // Destination pixel centers select source texels, including when scaled.
    CKERROR SubmitCopy(CKDWORD texture, CKDWORD width, CKDWORD height);

private:
    CKBOOL CreateTarget(CKSdlGpuPresentTarget &target, CKDWORD width, CKDWORD height, CKDWORD samples);
    void DestroyTarget(CKSdlGpuPresentTarget &target);
    void DestroyReadbackTexture();
    CKERROR Submit(const CKSdlGpuPresentTarget &source, CKBOOL fxaa, float sharpness);
    CKERROR SubmitTexture(CKDWORD texture, CKDWORD width, CKDWORD height,
                          CKBOOL linear, CKBOOL fxaa, float sharpness);

    CKSdlGpuRasterizerContext *m_Context;
    CKFFPresentDraw m_Draw;
    CKFFShaderSet m_Shaders;
    CKSdlGpuPresentResources m_ResourceIds;
    CKSdlGpuPresentTarget m_Scene;
    CKSdlGpuPresentTarget m_Native;
    CKDWORD m_ReadbackTexture;    // BLIT_DST | READBACK copy target for readbacks
    CKDWORD m_ReadbackWidth;
    CKDWORD m_ReadbackHeight;
    CK_SHADER_FORMAT m_ShaderFormat;
    CK_SHADER_PROFILE m_ShaderProfile;
};

#endif // CKSDLGPUPRESENTSTAGE_H
