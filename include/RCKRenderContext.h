#ifndef RCKRENDERCONTEXT_H
#define RCKRENDERCONTEXT_H

#include "CKRenderEngineTypes.h"
#include "CKRenderContext.h"
#include "CKRenderedScene.h"

struct UserDrawPrimitiveDataClass : public VxDrawPrimitiveData {
    CKDWORD field_0[29];
};

struct CKRenderContextSettings
{
    CKRECT m_Rect;
    CKDWORD m_Bpp;
    CKDWORD m_Zbpp;
    CKDWORD m_StencilBpp;
};

struct CKObjectExtents {
    VxRect m_Rect;
    CKDWORD m_Extent;
};

class RCKRenderContext : public CKRenderContext {
    friend class RCKRenderManager;
public:
    void AddObject(CKRenderObject *obj) override;
    void AddObjectWithHierarchy(CKRenderObject *obj) override;
    void RemoveObject(CKRenderObject *obj) override;
    CKBOOL IsObjectAttached(CKRenderObject *obj) override;
    const XObjectArray & Compute3dRootObjects() override;
    const XObjectArray & Compute2dRootObjects() override;
    CK2dEntity * Get2dRoot(CKBOOL background) override;
    void DetachAll() override;
    void ForceCameraSettingsUpdate() override;
    CKERROR Clear(CK_RENDER_FLAGS Flags = CK_RENDER_USECURRENTSETTINGS, CKDWORD Stencil = 0) override;
    CKERROR DrawScene(CK_RENDER_FLAGS Flags = CK_RENDER_USECURRENTSETTINGS) override;
    CKERROR BackToFront(CK_RENDER_FLAGS Flags = CK_RENDER_USECURRENTSETTINGS) override;
    CKERROR Render(CK_RENDER_FLAGS Flags = CK_RENDER_USECURRENTSETTINGS) override;
    void AddPreRenderCallBack(CK_RENDERCALLBACK Function, void *Argument, CKBOOL Temporary = FALSE) override;
    void RemovePreRenderCallBack(CK_RENDERCALLBACK Function, void *Argument) override;
    void AddPostRenderCallBack(CK_RENDERCALLBACK Function, void *Argument, CKBOOL Temporary = FALSE, CKBOOL BeforeTransparent = FALSE) override;
    void RemovePostRenderCallBack(CK_RENDERCALLBACK Function, void *Argument) override;
    void AddPostSpriteRenderCallBack(CK_RENDERCALLBACK Function, void *Argument, CKBOOL Temporary = FALSE) override;
    void RemovePostSpriteRenderCallBack(CK_RENDERCALLBACK Function, void *Argument) override;
    VxDrawPrimitiveData *GetDrawPrimitiveStructure(CKRST_DPFLAGS Flags, int VertexCount) override;
    CKWORD *GetDrawPrimitiveIndices(int IndicesCount) override;
    void Transform(VxVector *Dest, VxVector *Src, CK3dEntity *Ref = NULL) override;
    void TransformVertices(int VertexCount, VxTransformData *data, CK3dEntity *Ref = NULL) override;
    CKERROR GoFullScreen(int Width = 640, int Height = 480, int Bpp = -1, int Driver = 0, int RefreshRate = 0) override;
    CKERROR StopFullScreen() override;
    CKBOOL IsFullScreen() override;
    int GetDriverIndex() override;
    CKBOOL ChangeDriver(int NewDriver) override;
    WIN_HANDLE GetWindowHandle() override;
    void ScreenToClient(Vx2DVector *ioPoint) override;
    void ClientToScreen(Vx2DVector *ioPoint) override;
    CKERROR SetWindowRect(VxRect &rect, CKDWORD Flags = 0) override;
    void GetWindowRect(VxRect &rect, CKBOOL ScreenRelative = FALSE) override;
    int GetHeight() override;
    int GetWidth() override;
    CKERROR Resize(int PosX = 0, int PosY = 0, int SizeX = 0, int SizeY = 0, CKDWORD Flags = 0) override;
    void SetViewRect(VxRect &rect) override;
    void GetViewRect(VxRect &rect) override;
    VX_PIXELFORMAT GetPixelFormat(int *Bpp = NULL, int *Zbpp = NULL, int *StencilBpp = NULL) override;
    void SetState(VXRENDERSTATETYPE State, CKDWORD Value) override;
    CKDWORD GetState(VXRENDERSTATETYPE State) override;
    CKBOOL SetTexture(CKTexture *tex, CKBOOL Clamped = 0, int Stage = 0) override;
    CKBOOL SetTextureStageState(CKRST_TEXTURESTAGESTATETYPE State, CKDWORD Value, int Stage = 0) override;
    CKRasterizerContext *GetRasterizerContext() override;
    void SetClearBackground(CKBOOL ClearBack = TRUE) override;
    CKBOOL GetClearBackground() override;
    void SetClearZBuffer(CKBOOL ClearZ = TRUE) override;
    CKBOOL GetClearZBuffer() override;
    void GetGlobalRenderMode(VxShadeType *Shading, CKBOOL *Texture, CKBOOL *Wireframe) override;
    void SetGlobalRenderMode(VxShadeType Shading = GouraudShading, CKBOOL Texture = TRUE, CKBOOL Wireframe = FALSE) override;
    void SetCurrentRenderOptions(CKDWORD flags) override;
    CKDWORD GetCurrentRenderOptions() override;
    void ChangeCurrentRenderOptions(CKDWORD Add, CKDWORD Remove) override;
    void SetCurrentExtents(VxRect &extents) override;
    void GetCurrentExtents(VxRect &extents) override;
    void SetAmbientLight(float R, float G, float B) override;
    void SetAmbientLight(CKDWORD Color) override;
    CKDWORD GetAmbientLight() override;
    void SetFogMode(VXFOG_MODE Mode) override;
    void SetFogStart(float Start) override;
    void SetFogEnd(float End) override;
    void SetFogDensity(float Density) override;
    void SetFogColor(CKDWORD Color) override;
    VXFOG_MODE GetFogMode() override;
    float GetFogStart() override;
    float GetFogEnd() override;
    float GetFogDensity() override;
    CKDWORD GetFogColor() override;
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE pType, CKWORD *indices, int indexcount, VxDrawPrimitiveData *data) override;
    void SetWorldTransformationMatrix(const VxMatrix &M) override;
    void SetProjectionTransformationMatrix(const VxMatrix &M) override;
    void SetViewTransformationMatrix(const VxMatrix &M) override;
    const VxMatrix &GetWorldTransformationMatrix() override;
    const VxMatrix &GetProjectionTransformationMatrix() override;
    const VxMatrix &GetViewTransformationMatrix() override;
    CKBOOL SetUserClipPlane(CKDWORD ClipPlaneIndex, const VxPlane &PlaneEquation) override;
    CKBOOL GetUserClipPlane(CKDWORD ClipPlaneIndex, VxPlane &PlaneEquation) override;
    CKRenderObject *Pick(int x, int y, CKPICKRESULT *oRes, CKBOOL iIgnoreUnpickable = FALSE) override;
    CKRenderObject *Pick(CKPOINT pt, CKPICKRESULT *oRes, CKBOOL iIgnoreUnpickable = FALSE) override;
    CKERROR RectPick(const VxRect &r, XObjectPointerArray &oObjects, CKBOOL Intersect = TRUE) override;
    void AttachViewpointToCamera(CKCamera *cam) override;
    void DetachViewpointFromCamera() override;
    CKCamera *GetAttachedCamera() override;
    CK3dEntity *GetViewpoint() override;
    CKMaterial *GetBackgroundMaterial() override;
    void GetBoundingBox(VxBbox *BBox) override;
    void GetStats(VxStats *stats) override;
    void SetCurrentMaterial(CKMaterial *mat, CKBOOL Lit = TRUE) override;
    void Activate(CKBOOL active = TRUE) override;
    int DumpToMemory(const VxRect *iRect, VXBUFFER_TYPE buffer, VxImageDescEx &desc) override;
    int CopyToVideo(const VxRect *iRect, VXBUFFER_TYPE buffer, VxImageDescEx &desc) override;
    CKERROR DumpToFile(CKSTRING filename, const VxRect *rect, VXBUFFER_TYPE buffer) override;
    VxDirectXData *GetDirectXInfo() override;
    void WarnEnterThread() override;
    void WarnExitThread() override;
    CK2dEntity *Pick2D(const Vx2DVector &v) override;
    CKBOOL SetRenderTarget(CKTexture *texture = NULL, int CubeMapFace = 0) override;
    void AddRemoveSequence(CKBOOL Start) override;
    void SetTransparentMode(CKBOOL Trans) override;
    void AddDirtyRect(CKRECT *Rect) override;
    void RestoreScreenBackup() override;
    CKDWORD GetStencilFreeMask() override;
    void UsedStencilBits(CKDWORD stencilBits) override;
    int GetFirstFreeStencilBits() override;
    VxDrawPrimitiveData *LockCurrentVB(CKDWORD VertexCount) override;
    CKBOOL ReleaseCurrentVB() override;
    void SetTextureMatrix(const VxMatrix &M, int Stage = 0) override;
    void SetStereoParameters(float EyeSeparation, float FocalLength) override;
    void GetStereoParameters(float &EyeSeparation, float &FocalLength) override;

    CKERROR Create(void *Window, int Driver, CKRECT *rect, CKBOOL Fullscreen, int Bpp, int Zbpp, int StencilBpp, int RefreshRate);
    VxStats &GetStats() {
        return m_Stats;
    }

    explicit RCKRenderContext(CKContext *Context, CKSTRING name = nullptr);
    ~RCKRenderContext() override;
    CK_CLASSID GetClassID() override;

    void PreSave(CKFile *file, CKDWORD flags) override;
    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;
    void PostLoad() override;

    void PreDelete() override;
    void CheckPreDeletion() override;
    void CheckPostDeletion() override;

    int GetMemoryOccupation() override;
    CKBOOL IsObjectUsed(CKObject *obj, CK_CLASSID cid) override;

    CKERROR PrepareDependencies(CKDependenciesContext &context, CKBOOL iCaller = TRUE) override;
    CKERROR RemapDependencies(CKDependenciesContext &context) override;
    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKPlace *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    CKDWORD m_WinHandle;
    CKDWORD m_AppHandle;
    CKRECT m_WinRect;
    CKDWORD m_RenderFlags;
    CKRenderedScene *m_RenderedScene;
    CKBOOL m_Fullscreen;
    CKBOOL m_Active;
    CKBOOL m_PerspectiveOrOrthographic;
    CKBOOL m_ProjectionUpdated;
    CKBOOL m_Start;
    CKBOOL m_TransparentMode;
    CKBOOL m_DeviceValid;
    CKCallbacksContainer m_PreRenderCallBacks;
    CKCallbacksContainer m_PreRenderTempCallBacks;
    CKCallbacksContainer m_PostRenderCallBacks;
    RCKRenderManager *m_RenderManager;
    CKRasterizerContext *m_RasterizerContext;
    CKRasterizerDriver *m_RasterizerDriver;
    CKDWORD m_Driver;
    CKDWORD m_Shading;
    CKDWORD m_TextureEnabled;
    CKDWORD m_DisplayWireframe;
    VxFrustum m_Frustum;
    float m_Fov;
    float m_Zoom;
    float m_NearPlane;
    float m_FarPlane;
    VxMatrix m_TransformMatrix;
    CKViewportData m_ViewportData;
    CKRECT m_WindowRect;
    int m_Bpp;
    int m_Zbpp;
    int m_StencilBpp;
    CKRenderContextSettings m_RenderContextSettings;
    VxRect m_CurrentExtents;
    CKDWORD field_21C;
    CKDWORD m_TimeFpsCalc;
    VxTimeProfiler m_RenderTimeProfiler;
    float m_SmoothedFps;
    VxStats m_Stats;
    VxTimeProfiler m_DevicePreCallbacksTimeProfiler;
    VxTimeProfiler m_DevicePostCallbacksTimeProfiler;
    VxTimeProfiler m_ObjectsCallbacksTimeProfiler;
    VxTimeProfiler m_SpriteCallbacksTimeProfiler;
    VxTimeProfiler m_ObjectsRenderTimeProfiler;
    VxTimeProfiler m_SceneTraversalTimeProfiler;
    VxTimeProfiler m_SkinTimeProfiler;
    VxTimeProfiler m_SpriteTimeProfiler;
    VxTimeProfiler m_TransparentObjectsSortTimeProfiler;
    RCK3dEntity *m_Current3dEntity;
    RCKTexture *m_Texture;
    CKDWORD m_CubeMapFace;
    float m_FocalLength;
    float m_EyeSeparation;
    CKDWORD m_Flags;
    CKDWORD m_FpsInterval;
    XString m_CurrentObjectDesc;
    XString m_StateString;
    CKDWORD field_33C;
    CKDWORD m_DrawSceneCalls;
    CKDWORD field_344;
    XVoidArray m_Sprite3DBatches;
    XVoidArray m_TransparentObjects;
    int m_StencilFreeMask;
    UserDrawPrimitiveDataClass *m_UserDrawPrimitiveData;
    CKDWORD m_MaskFree;
    CKDWORD m_VertexBufferIndex;
    CKDWORD m_StartIndex;
    CKDWORD m_DpFlags;
    CKDWORD m_VertexBufferCount;
    XArray<CKObjectExtents> m_ObjectExtents;
    XVoidArray field_388;
    XVoidArray m_RootObjects;
    RCKCamera *m_Camera;
    RCKTexture *m_NCUTex;
    VxTimeProfiler m_TimeProfiler_3A8;
    CKDWORD m_PVInformation;
};

#endif // RCKRENDERCONTEXT_H
