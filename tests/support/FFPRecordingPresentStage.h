#ifndef CKRE_FFP_RECORDING_PRESENT_STAGE_H
#define CKRE_FFP_RECORDING_PRESENT_STAGE_H

#include "VxMath.h"
#include "CKTypes.h"
#include "CKRasterizerContextEnums.h"
#include "CKRasterizerContextData.h"
#include "CKBuiltinShaders.h"
#include "CKFFPresentDraw.h"
#include "CKFFShaderInterface.h"

class CKRecordingBackend;

// One internal render target: color + depth-stencil + backend render target.
struct FFPRecordingPresentTarget {
    CKDWORD ColorTexture;
    CKDWORD DepthTexture;
    CKDWORD FrameBuffer;  // backend render target (CKRST_OBJ_RENDERTARGET)
    CKDWORD Width;
    CKDWORD Height;
    CKDWORD Samples;      // 0 = single sampled

    FFPRecordingPresentTarget()
        : ColorTexture(0), DepthTexture(0), FrameBuffer(0), Width(0), Height(0), Samples(0) {}
    CKBOOL IsActive() const { return FrameBuffer != 0; }
};

// Backend handles of the fullscreen resolve / blit program (shaders
// vs_postprocess / fs_postprocess). The source is sampled through
// CKFF_SLOT_PRESENT, the parameters go to CKRST_BLOCK_PRESENT_PARAMS.
struct FFPRecordingPresentResources {
    CKDWORD VertexShader;
    CKDWORD PixelShader;
    CKDWORD Program;
    CKDWORD VertexLayout;

    FFPRecordingPresentResources()
        : VertexShader(0), PixelShader(0), Program(0), VertexLayout(0) {}
};

// Presentation stage of the virtual backbuffer (spec 4.4): the scene renders
// into the *scene target* (window size x RenderScale, MSAA), and the resolve
// draws it (scale, FXAA, sharpen) into the *native target* (window size,
// single sampled). The recording Context may render the scene straight into
// the native target when that resolve would be an identity operation. The
// overlay draws on the native target and the frame ends with a plain blit to
// the swap chain. Readbacks blit the native target into the readback texture.
// This class does not open passes; the caller begins each fullscreen pass.
class FFPRecordingPresentStage {
public:
    FFPRecordingPresentStage();
    ~FFPRecordingPresentStage();

    void Init(CKRecordingBackend *Context, const CKFFShaderSet &Shaders);
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

    const FFPRecordingPresentTarget &SceneTarget() const { return m_Scene; }
    const FFPRecordingPresentTarget &NativeTarget() const { return m_Native; }
    // Readback texture (BLIT_DST | READBACK) of the given size, recreated when
    // the size changes; 0 when the backend cannot blit or read textures back.
    // Render targets cannot be read directly, readbacks blit into this one.
    CKDWORD AcquireReadbackTexture(CKDWORD width, CKDWORD height);
    CKDWORD GetReadbackTexture() const { return m_ReadbackTexture; }
    const FFPRecordingPresentResources &GetResourceIds() const { return m_ResourceIds; }

    // Draws the scene color as a fullscreen triangle into the current pass (the resolve).
    CKERROR SubmitResolve(CKBOOL fxaa, float sharpness);
    // Draws the native color as a fullscreen triangle into the current pass (the blit).
    CKERROR SubmitBlit();
    // Point-samples a 2D snapshot into the current pass, without postprocessing.
    // Destination pixel centers select source texels, including when scaled.
    CKERROR SubmitCopy(CKDWORD texture, CKDWORD width, CKDWORD height);

private:
    CKBOOL CreateTarget(FFPRecordingPresentTarget &target, CKDWORD width, CKDWORD height, CKDWORD samples);
    void DestroyTarget(FFPRecordingPresentTarget &target);
    void DestroyReadbackTexture();
    CKERROR Submit(const FFPRecordingPresentTarget &source, CKBOOL fxaa, float sharpness);
    CKERROR SubmitTexture(CKDWORD texture, CKDWORD width, CKDWORD height,
                          CKBOOL linear, CKBOOL fxaa, float sharpness,
                          CKBOOL flipV);

    CKRecordingBackend *m_Backend;
    CKFFPresentDraw m_Draw;
    CKFFShaderSet m_Shaders;
    FFPRecordingPresentResources m_ResourceIds;
    FFPRecordingPresentTarget m_Scene;
    FFPRecordingPresentTarget m_Native;
    CKDWORD m_ReadbackTexture;    // BLIT_DST | READBACK copy target for readbacks
    CKDWORD m_ReadbackWidth;
    CKDWORD m_ReadbackHeight;
    CK_SHADER_FORMAT m_ShaderFormat;
    CK_SHADER_PROFILE m_ShaderProfile;
};

#endif // CKRE_FFP_RECORDING_PRESENT_STAGE_H

