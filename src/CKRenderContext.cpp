#include "RCKRenderContext.h"

CK_CLASSID RCKRenderContext::GetClassID() {
    return CKObject::GetClassID();
}

void RCKRenderContext::PreDelete() {
    CKObject::PreDelete();
}

void RCKRenderContext::CheckPreDeletion() {
    CKObject::CheckPreDeletion();
}

void RCKRenderContext::CheckPostDeletion() {
    CKObject::CheckPostDeletion();
}

int RCKRenderContext::GetMemoryOccupation() {
    return CKObject::GetMemoryOccupation();
}

CKBOOL RCKRenderContext::IsObjectUsed(CKObject *obj, CK_CLASSID cid) {
    return CKObject::IsObjectUsed(obj, cid);
}

CKERROR RCKRenderContext::PrepareDependencies(CKDependenciesContext &context, CKBOOL iCaller) {
    return CKObject::PrepareDependencies(context, iCaller);
}

void RCKRenderContext::AddObject(CKRenderObject *obj) {

}

void RCKRenderContext::AddObjectWithHierarchy(CKRenderObject *obj) {

}

void RCKRenderContext::RemoveObject(CKRenderObject *obj) {

}

CKBOOL RCKRenderContext::IsObjectAttached(CKRenderObject *obj) {
    return 0;
}

const XObjectArray &RCKRenderContext::Compute3dRootObjects() {
    return <#initializer#>;
}

const XObjectArray &RCKRenderContext::Compute2dRootObjects() {
    return <#initializer#>;
}

CK2dEntity *RCKRenderContext::Get2dRoot(CKBOOL background) {
    return nullptr;
}

void RCKRenderContext::DetachAll() {

}

void RCKRenderContext::ForceCameraSettingsUpdate() {

}

CKERROR RCKRenderContext::Clear(CK_RENDER_FLAGS Flags, CKDWORD Stencil) {
    return 0;
}

CKERROR RCKRenderContext::DrawScene(CK_RENDER_FLAGS Flags) {
    return 0;
}

CKERROR RCKRenderContext::BackToFront(CK_RENDER_FLAGS Flags) {
    return 0;
}

CKERROR RCKRenderContext::Render(CK_RENDER_FLAGS Flags) {
    return 0;
}

void RCKRenderContext::AddPreRenderCallBack(CK_RENDERCALLBACK Function, void *Argument, CKBOOL Temporary) {

}

void RCKRenderContext::RemovePreRenderCallBack(CK_RENDERCALLBACK Function, void *Argument) {

}

void RCKRenderContext::AddPostRenderCallBack(CK_RENDERCALLBACK Function, void *Argument, CKBOOL Temporary,
                                             CKBOOL BeforeTransparent) {

}

void RCKRenderContext::RemovePostRenderCallBack(CK_RENDERCALLBACK Function, void *Argument) {

}

void RCKRenderContext::AddPostSpriteRenderCallBack(CK_RENDERCALLBACK Function, void *Argument, CKBOOL Temporary) {

}

void RCKRenderContext::RemovePostSpriteRenderCallBack(CK_RENDERCALLBACK Function, void *Argument) {

}

VxDrawPrimitiveData *RCKRenderContext::GetDrawPrimitiveStructure(CKRST_DPFLAGS Flags, int VertexCount) {
    return nullptr;
}

CKWORD *RCKRenderContext::GetDrawPrimitiveIndices(int IndicesCount) {
    return nullptr;
}

void RCKRenderContext::Transform(VxVector *Dest, VxVector *Src, CK3dEntity *Ref) {

}

void RCKRenderContext::TransformVertices(int VertexCount, VxTransformData *data, CK3dEntity *Ref) {

}

CKERROR RCKRenderContext::GoFullScreen(int Width, int Height, int Bpp, int Driver, int RefreshRate) {
    return 0;
}

CKERROR RCKRenderContext::StopFullScreen() {
    return 0;
}

CKBOOL RCKRenderContext::IsFullScreen() {
    return 0;
}

int RCKRenderContext::GetDriverIndex() {
    return 0;
}

CKBOOL RCKRenderContext::ChangeDriver(int NewDriver) {
    return 0;
}

WIN_HANDLE RCKRenderContext::GetWindowHandle() {
    return nullptr;
}

void RCKRenderContext::ScreenToClient(Vx2DVector *ioPoint) {

}

void RCKRenderContext::ClientToScreen(Vx2DVector *ioPoint) {

}

CKERROR RCKRenderContext::SetWindowRect(VxRect &rect, CKDWORD Flags) {
    return 0;
}

void RCKRenderContext::GetWindowRect(VxRect &rect, CKBOOL ScreenRelative) {

}

int RCKRenderContext::GetHeight() {
    return 0;
}

int RCKRenderContext::GetWidth() {
    return 0;
}

CKERROR RCKRenderContext::Resize(int PosX, int PosY, int SizeX, int SizeY, CKDWORD Flags) {
    return 0;
}

void RCKRenderContext::SetViewRect(VxRect &rect) {

}

void RCKRenderContext::GetViewRect(VxRect &rect) {

}

VX_PIXELFORMAT RCKRenderContext::GetPixelFormat(int *Bpp, int *Zbpp, int *StencilBpp) {
    return _16_RGB555;
}

void RCKRenderContext::SetState(VXRENDERSTATETYPE State, CKDWORD Value) {

}

CKDWORD RCKRenderContext::GetState(VXRENDERSTATETYPE State) {
    return 0;
}

CKBOOL RCKRenderContext::SetTexture(CKTexture *tex, CKBOOL Clamped, int Stage) {
    return 0;
}

CKBOOL RCKRenderContext::SetTextureStageState(CKRST_TEXTURESTAGESTATETYPE State, CKDWORD Value, int Stage) {
    return 0;
}

CKRasterizerContext *RCKRenderContext::GetRasterizerContext() {
    return nullptr;
}

void RCKRenderContext::SetClearBackground(CKBOOL ClearBack) {

}

CKBOOL RCKRenderContext::GetClearBackground() {
    return 0;
}

void RCKRenderContext::SetClearZBuffer(CKBOOL ClearZ) {

}

CKBOOL RCKRenderContext::GetClearZBuffer() {
    return 0;
}

void RCKRenderContext::GetGlobalRenderMode(VxShadeType *Shading, CKBOOL *Texture, CKBOOL *Wireframe) {

}

void RCKRenderContext::SetGlobalRenderMode(VxShadeType Shading, CKBOOL Texture, CKBOOL Wireframe) {

}

void RCKRenderContext::SetCurrentRenderOptions(CKDWORD flags) {

}

CKDWORD RCKRenderContext::GetCurrentRenderOptions() {
    return 0;
}

void RCKRenderContext::ChangeCurrentRenderOptions(CKDWORD Add, CKDWORD Remove) {

}

void RCKRenderContext::SetCurrentExtents(VxRect &extents) {

}

void RCKRenderContext::GetCurrentExtents(VxRect &extents) {

}

void RCKRenderContext::SetAmbientLight(float R, float G, float B) {

}

void RCKRenderContext::SetAmbientLight(CKDWORD Color) {

}

CKDWORD RCKRenderContext::GetAmbientLight() {
    return 0;
}

void RCKRenderContext::SetFogMode(VXFOG_MODE Mode) {

}

void RCKRenderContext::SetFogStart(float Start) {

}

void RCKRenderContext::SetFogEnd(float End) {

}

void RCKRenderContext::SetFogDensity(float Density) {

}

void RCKRenderContext::SetFogColor(CKDWORD Color) {

}

VXFOG_MODE RCKRenderContext::GetFogMode() {
    return VXFOG_NONE;
}

float RCKRenderContext::GetFogStart() {
    return 0;
}

float RCKRenderContext::GetFogEnd() {
    return 0;
}

float RCKRenderContext::GetFogDensity() {
    return 0;
}

CKDWORD RCKRenderContext::GetFogColor() {
    return 0;
}

CKBOOL
RCKRenderContext::DrawPrimitive(VXPRIMITIVETYPE pType, CKWORD *indices, int indexcount, VxDrawPrimitiveData *data) {
    return 0;
}

void RCKRenderContext::SetWorldTransformationMatrix(const VxMatrix &M) {

}

void RCKRenderContext::SetProjectionTransformationMatrix(const VxMatrix &M) {

}

void RCKRenderContext::SetViewTransformationMatrix(const VxMatrix &M) {

}

const VxMatrix &RCKRenderContext::GetWorldTransformationMatrix() {
    return <#initializer#>;
}

const VxMatrix &RCKRenderContext::GetProjectionTransformationMatrix() {
    return <#initializer#>;
}

const VxMatrix &RCKRenderContext::GetViewTransformationMatrix() {
    return <#initializer#>;
}

CKBOOL RCKRenderContext::SetUserClipPlane(CKDWORD ClipPlaneIndex, const VxPlane &PlaneEquation) {
    return 0;
}

CKBOOL RCKRenderContext::GetUserClipPlane(CKDWORD ClipPlaneIndex, VxPlane &PlaneEquation) {
    return 0;
}

CKRenderObject *RCKRenderContext::Pick(int x, int y, CKPICKRESULT *oRes, CKBOOL iIgnoreUnpickable) {
    return nullptr;
}

CKRenderObject *RCKRenderContext::Pick(CKPOINT pt, CKPICKRESULT *oRes, CKBOOL iIgnoreUnpickable) {
    return nullptr;
}

CKERROR RCKRenderContext::RectPick(const VxRect &r, XObjectPointerArray &oObjects, CKBOOL Intersect) {
    return 0;
}

void RCKRenderContext::AttachViewpointToCamera(CKCamera *cam) {

}

void RCKRenderContext::DetachViewpointFromCamera() {

}

CKCamera *RCKRenderContext::GetAttachedCamera() {
    return nullptr;
}

CK3dEntity *RCKRenderContext::GetViewpoint() {
    return nullptr;
}

CKMaterial *RCKRenderContext::GetBackgroundMaterial() {
    return nullptr;
}

void RCKRenderContext::GetBoundingBox(VxBbox *BBox) {

}

void RCKRenderContext::GetStats(VxStats *stats) {

}

void RCKRenderContext::SetCurrentMaterial(CKMaterial *mat, CKBOOL Lit) {

}

void RCKRenderContext::Activate(CKBOOL active) {

}

int RCKRenderContext::DumpToMemory(const VxRect *iRect, VXBUFFER_TYPE buffer, VxImageDescEx &desc) {
    return 0;
}

int RCKRenderContext::CopyToVideo(const VxRect *iRect, VXBUFFER_TYPE buffer, VxImageDescEx &desc) {
    return 0;
}

CKERROR RCKRenderContext::DumpToFile(CKSTRING filename, const VxRect *rect, VXBUFFER_TYPE buffer) {
    return 0;
}

VxDirectXData *RCKRenderContext::GetDirectXInfo() {
    return nullptr;
}

void RCKRenderContext::WarnEnterThread() {

}

void RCKRenderContext::WarnExitThread() {

}

CK2dEntity *RCKRenderContext::Pick2D(const Vx2DVector &v) {
    return nullptr;
}

CKBOOL RCKRenderContext::SetRenderTarget(CKTexture *texture, int CubeMapFace) {
    return 0;
}

void RCKRenderContext::AddRemoveSequence(CKBOOL Start) {

}

void RCKRenderContext::SetTransparentMode(CKBOOL Trans) {

}

void RCKRenderContext::AddDirtyRect(CKRECT *Rect) {

}

void RCKRenderContext::RestoreScreenBackup() {

}

CKDWORD RCKRenderContext::GetStencilFreeMask() {
    return 0;
}

void RCKRenderContext::UsedStencilBits(CKDWORD stencilBits) {

}

int RCKRenderContext::GetFirstFreeStencilBits() {
    return 0;
}

VxDrawPrimitiveData *RCKRenderContext::LockCurrentVB(CKDWORD VertexCount) {
    return nullptr;
}

CKBOOL RCKRenderContext::ReleaseCurrentVB() {
    return 0;
}

void RCKRenderContext::SetTextureMatrix(const VxMatrix &M, int Stage) {

}

void RCKRenderContext::SetStereoParameters(float EyeSeparation, float FocalLength) {

}

void RCKRenderContext::GetStereoParameters(float &EyeSeparation, float &FocalLength) {

}

CKERROR RCKRenderContext::Create(void *Window, int Driver, CKRECT *rect, CKBOOL Fullscreen, int Bpp, int Zbpp,
                                 int StencilBpp, int RefreshRate) {
    return 0;
}
