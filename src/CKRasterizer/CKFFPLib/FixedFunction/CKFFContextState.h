#ifndef CKFFCONTEXTSTATE_H
#define CKFFCONTEXTSTATE_H

#include "CKRasterizer.h"

class CKFixedFunctionPipeline;
class CKFFBufferUseTracker;
struct CKRasterizerDeviceCaps;

enum CKFFFramePhase {
    CKFF_FRAME_IDLE = 0,
    CKFF_FRAME_SCENE,
    CKFF_FRAME_OVERLAY
};

// CPU frame-flow state shared by concrete contexts. Native pass/target handles
// remain beside this record in each concrete context.
struct CKFFFrameState {
    CKFFFramePhase Phase;
    CKBOOL PassOpen;
    CKRECT PassRect;
    CKBOOL InternalTargets;
    CKBOOL SceneUsesNative;
    CKBOOL Composited;
    CKBOOL TargetDecided;
    CKBOOL Open;
    CKBOOL NativePresented;

    CKFFFrameState()
        : Phase(CKFF_FRAME_IDLE), PassOpen(FALSE), PassRect{0, 0, 0, 0},
          InternalTargets(FALSE), SceneUsesNative(FALSE), Composited(FALSE),
          TargetDecided(FALSE), Open(FALSE), NativePresented(FALSE) {}

    CKBOOL IsSceneActive() const { return Phase == CKFF_FRAME_SCENE ? TRUE : FALSE; }
    CKBOOL IsOverlayActive() const { return Phase == CKFF_FRAME_OVERLAY ? TRUE : FALSE; }

    void FinishFrame()
    {
        Phase = CKFF_FRAME_IDLE;
        PassOpen = FALSE;
        PassRect.left = PassRect.top = PassRect.right = PassRect.bottom = 0;
        SceneUsesNative = FALSE;
        Composited = FALSE;
        TargetDecided = FALSE;
        Open = FALSE;
    }

    void Reset() { *this = CKFFFrameState(); }
};

CKBOOL CKFFValidatePrimitive(VXPRIMITIVETYPE Type, int ElementCount);
int CKFFPrimitiveCount(VXPRIMITIVETYPE Type, int ElementCount);
CKRECT CKFFMakeRect(int Width, int Height);
CKRECT CKFFScaleRect(const CKRECT &Rect, const CKRECT &Logical,
                     const CKRECT &Physical);
CKDWORD CKFFScaledDimension(CKDWORD Value, float Scale, CKDWORD Maximum);
CKBOOL CKFFCanContinuePass(const CKFFFrameState &Frame,
                           CKDWORD OpenRenderTarget,
                           CKDWORD RenderTarget, const CKRECT &Rect);
void CKFFCountDraw(CKDWORD &DrawCalls, CKDWORD &Primitives,
                   VXPRIMITIVETYPE Type, int ElementCount);
CKRST_DIAGNOSTIC CKFFDrawRejectDiagnostic(
    const CKFixedFunctionPipeline &Pipeline);
void CKFFRecordDrawApproximations(CKRenderStats &Stats, uint64_t Mask);
CKBOOL CKFFIsPublicResourceType(CKRST_OBJECTTYPE Type);
CKBOOL CKFFIsPublicResourceMask(CKRST_OBJECTMASK TypeMask);
CKBOOL CKFFDriverHasCapability(const CKRasterizerDriver *Driver, CKRST_CAPS Capability);
void CKFFUpdateDriverCaps(const CKRasterizerDeviceCaps &DeviceCaps,
                          CKRasterizerNativeCapsDesc &DriverCaps);
CKBOOL CKFFGetContextCaps(const CKRasterizerDeviceCaps &DeviceCaps,
                          const CKRasterizerDriver *Driver,
                          CKRasterizerCapsDesc *Caps);
void CKFFDetachDeletedResource(CKFixedFunctionPipeline &Pipeline,
                               CKFFBufferUseTracker &BufferUses,
                               CKRST_HANDLE Handle,
                               CKRST_OBJECTTYPE Type);

// Caller-visible render-target selection. Concrete contexts keep framebuffer,
// depth texture and all other native realization state beside this record.
struct CKFFRenderTargetState {
    CKDWORD Texture;
    CKRST_CUBEFACE Face;
    CKDWORD Width;
    CKDWORD Height;

    CKFFRenderTargetState();

    CKBOOL IsActive() const { return Texture != 0; }
    CKBOOL Set(CKDWORD TextureHandle, const CKTextureDesc &Desc,
               int RequestedWidth, int RequestedHeight,
               CKRST_CUBEFACE RequestedFace);
    void Reset();
};

CKDWORD CKFFTargetAlphaTestPrecision(const CKTextureDesc *TargetTexture,
                                     int BackBufferBpp);

// Shared CPU-side implementation of the fixed-function state portion of the
// private CKRasterizerContext API. Concrete contexts still own the pipeline,
// resource validation and every native operation.
CKBOOL CKFFSetRenderState(CKFixedFunctionPipeline &Pipeline,
                          CKRenderStats &Stats,
                          VXRENDERSTATETYPE State, CKDWORD Value);
CKBOOL CKFFGetRenderState(CKFixedFunctionPipeline &Pipeline,
                          CKRenderStats &Stats,
                          VXRENDERSTATETYPE State, CKDWORD *Value);
CKBOOL CKFFSetTextureStageState(CKFixedFunctionPipeline &Pipeline,
                                CKRenderStats &Stats, int Stage,
                                CKRST_TEXTURESTAGESTATETYPE State,
                                CKDWORD Value);
CKBOOL CKFFGetTextureStageState(CKFixedFunctionPipeline &Pipeline,
                                CKRenderStats &Stats, int Stage,
                                CKRST_TEXTURESTAGESTATETYPE State,
                                CKDWORD *Value);
CKBOOL CKFFResetTextureStages(CKFixedFunctionPipeline &Pipeline,
                              CKRenderStats &Stats, int FirstStage,
                              int StageCount);
CKBOOL CKFFSetTextureBinding(CKFixedFunctionPipeline &Pipeline,
                             CKRenderStats &Stats, CKDWORD Texture,
                             const CKTextureDesc *TextureDesc, int Stage);
CKBOOL CKFFGetTextureBinding(CKFixedFunctionPipeline &Pipeline,
                             CKRenderStats &Stats, int Stage,
                             CKDWORD *Texture);
CKBOOL CKFFSetTransform(CKFixedFunctionPipeline &Pipeline,
                        CKRenderStats &Stats, VXMATRIX_TYPE Type,
                        const VxMatrix &Matrix);
CKBOOL CKFFGetTransform(CKFixedFunctionPipeline &Pipeline,
                        CKRenderStats &Stats, VXMATRIX_TYPE Type,
                        VxMatrix &Matrix);
CKBOOL CKFFSetLight(CKFixedFunctionPipeline &Pipeline,
                    CKRenderStats &Stats, CKDWORD Index,
                    const CKLightData *Data);
CKBOOL CKFFEnableLight(CKFixedFunctionPipeline &Pipeline,
                       CKRenderStats &Stats, CKDWORD Index,
                       CKBOOL Enable);
CKBOOL CKFFSetMaterial(CKFixedFunctionPipeline &Pipeline,
                       const CKMaterialData *Data);
CKBOOL CKFFApplyMaterial(CKFixedFunctionPipeline &Pipeline,
                         const CKMaterialRenderState &State);
CKBOOL CKFFSetViewport(CKFixedFunctionPipeline &Pipeline,
                       CKRenderStats &Stats, const CKViewportData *Data);
CKBOOL CKFFSetUserClipPlane(CKFixedFunctionPipeline &Pipeline,
                            CKRenderStats &Stats, CKDWORD Index,
                            const VxPlane &Plane);
CKBOOL CKFFGetUserClipPlane(CKFixedFunctionPipeline &Pipeline,
                            CKRenderStats &Stats, CKDWORD Index,
                            VxPlane &Plane);

#endif
