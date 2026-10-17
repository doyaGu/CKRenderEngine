#include <gtest/gtest.h>
#include <cstdint>
#include <type_traits>
#include <chrono>
#include <thread>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKCamera.h"
#include "RCKLight.h"
#include "RCKMaterial.h"
#include "RCKMesh.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "TextureTestObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();
extern XClassArray<CKRasterizerInfo> g_RasterizersInfo;

namespace {
static_assert(std::is_base_of<CKRenderContext, RCKRenderContext>::value, "SDK base must be real inheritance");
static_assert(std::has_virtual_destructor<CKRenderContext>::value, "SDK deletion must dispatch virtually");
static_assert(!std::is_abstract<RCKRenderContext>::value, "All SDK pure virtual entries must be implemented");

// Compile the complete 128-slot SDK contract on each architecture. Slot 4 is
// the virtual deleting destructor, checked separately above. The two overload
// pairs follow the compiler vtable order rather than source declaration order.
#define CKRE_CHECK_SDK_SLOT(Slot, Return, Name, Args) \
    using SdkSlot##Slot = Return (CKRenderContext::*) Args; \
    using ImplementationSlot##Slot = Return (RCKRenderContext::*) Args; \
    static_assert(std::is_same<decltype(static_cast<SdkSlot##Slot>(&CKRenderContext::Name)), SdkSlot##Slot>::value, "SDK virtual signature"); \
    static_assert(std::is_same<decltype(static_cast<ImplementationSlot##Slot>(&RCKRenderContext::Name)), ImplementationSlot##Slot>::value, "Implementation virtual signature")
CKRE_CHECK_SDK_SLOT(0, void, Show, (CK_OBJECT_SHOWOPTION));
CKRE_CHECK_SDK_SLOT(1, CKBOOL, IsHiddenByParent, ());
CKRE_CHECK_SDK_SLOT(2, int, CanBeHide, ());
CKRE_CHECK_SDK_SLOT(3, CKBOOL, IsVisible, ());
CKRE_CHECK_SDK_SLOT(5, CK_CLASSID, GetClassID, ());
CKRE_CHECK_SDK_SLOT(6, void, PreSave, (CKFile *, CKDWORD));
CKRE_CHECK_SDK_SLOT(7, CKStateChunk *, Save, (CKFile *, CKDWORD));
CKRE_CHECK_SDK_SLOT(8, CKERROR, Load, (CKStateChunk *, CKFile *));
CKRE_CHECK_SDK_SLOT(9, void, PostLoad, ());
CKRE_CHECK_SDK_SLOT(10, void, PreDelete, ());
CKRE_CHECK_SDK_SLOT(11, void, CheckPreDeletion, ());
CKRE_CHECK_SDK_SLOT(12, void, CheckPostDeletion, ());
CKRE_CHECK_SDK_SLOT(13, int, GetMemoryOccupation, ());
CKRE_CHECK_SDK_SLOT(14, CKBOOL, IsObjectUsed, (CKObject *, CK_CLASSID));
CKRE_CHECK_SDK_SLOT(15, CKERROR, PrepareDependencies, (CKDependenciesContext &));
CKRE_CHECK_SDK_SLOT(16, CKERROR, RemapDependencies, (CKDependenciesContext &));
CKRE_CHECK_SDK_SLOT(17, CKERROR, Copy, (CKObject &, CKDependenciesContext &));
CKRE_CHECK_SDK_SLOT(18, void, AddObject, (CKRenderObject *));
CKRE_CHECK_SDK_SLOT(19, void, AddObjectWithHierarchy, (CKRenderObject *));
CKRE_CHECK_SDK_SLOT(20, void, RemoveObject, (CKRenderObject *));
CKRE_CHECK_SDK_SLOT(21, CKBOOL, IsObjectAttached, (CKRenderObject *));
CKRE_CHECK_SDK_SLOT(22, const XObjectArray &, Compute3dRootObjects, ());
CKRE_CHECK_SDK_SLOT(23, const XObjectArray &, Compute2dRootObjects, ());
CKRE_CHECK_SDK_SLOT(24, CK2dEntity *, Get2dRoot, (CKBOOL));
CKRE_CHECK_SDK_SLOT(25, void, DetachAll, ());
CKRE_CHECK_SDK_SLOT(26, void, ForceCameraSettingsUpdate, ());
CKRE_CHECK_SDK_SLOT(27, void, PrepareCameras, (CK_RENDER_FLAGS));
CKRE_CHECK_SDK_SLOT(28, CKERROR, Clear, (CK_RENDER_FLAGS, CKDWORD));
CKRE_CHECK_SDK_SLOT(29, CKERROR, DrawScene, (CK_RENDER_FLAGS));
CKRE_CHECK_SDK_SLOT(30, CKERROR, BackToFront, (CK_RENDER_FLAGS));
CKRE_CHECK_SDK_SLOT(31, CKERROR, Render, (CK_RENDER_FLAGS));
CKRE_CHECK_SDK_SLOT(32, void, AddPreRenderCallBack, (CK_RENDERCALLBACK, void *, CKBOOL));
CKRE_CHECK_SDK_SLOT(33, void, RemovePreRenderCallBack, (CK_RENDERCALLBACK, void *));
CKRE_CHECK_SDK_SLOT(34, void, AddPostRenderCallBack, (CK_RENDERCALLBACK, void *, CKBOOL, CKBOOL));
CKRE_CHECK_SDK_SLOT(35, void, RemovePostRenderCallBack, (CK_RENDERCALLBACK, void *));
CKRE_CHECK_SDK_SLOT(36, void, AddPostSpriteRenderCallBack, (CK_RENDERCALLBACK, void *, CKBOOL));
CKRE_CHECK_SDK_SLOT(37, void, RemovePostSpriteRenderCallBack, (CK_RENDERCALLBACK, void *));
CKRE_CHECK_SDK_SLOT(38, VxDrawPrimitiveData *, GetDrawPrimitiveStructure, (CKRST_DPFLAGS, int));
CKRE_CHECK_SDK_SLOT(39, CKWORD *, GetDrawPrimitiveIndices, (int));
CKRE_CHECK_SDK_SLOT(40, void, Transform, (VxVector *, VxVector *, CK3dEntity *));
CKRE_CHECK_SDK_SLOT(41, void, TransformVertices, (int, VxTransformData *, CK3dEntity *));
CKRE_CHECK_SDK_SLOT(42, CKERROR, GoFullScreen, (int, int, int, int, int));
CKRE_CHECK_SDK_SLOT(43, CKERROR, StopFullScreen, ());
CKRE_CHECK_SDK_SLOT(44, CKBOOL, IsFullScreen, ());
CKRE_CHECK_SDK_SLOT(45, int, GetDriverIndex, ());
CKRE_CHECK_SDK_SLOT(46, CKBOOL, ChangeDriver, (int));
CKRE_CHECK_SDK_SLOT(47, WIN_HANDLE, GetWindowHandle, ());
CKRE_CHECK_SDK_SLOT(48, void, ScreenToClient, (Vx2DVector *));
CKRE_CHECK_SDK_SLOT(49, void, ClientToScreen, (Vx2DVector *));
CKRE_CHECK_SDK_SLOT(50, CKERROR, SetWindowRect, (VxRect &, CKDWORD));
CKRE_CHECK_SDK_SLOT(51, void, GetWindowRect, (VxRect &, CKBOOL));
CKRE_CHECK_SDK_SLOT(52, int, GetHeight, ());
CKRE_CHECK_SDK_SLOT(53, int, GetWidth, ());
CKRE_CHECK_SDK_SLOT(54, CKERROR, Resize, (int, int, int, int, CKDWORD));
CKRE_CHECK_SDK_SLOT(55, void, SetViewRect, (VxRect &));
CKRE_CHECK_SDK_SLOT(56, void, GetViewRect, (VxRect &));
CKRE_CHECK_SDK_SLOT(57, VX_PIXELFORMAT, GetPixelFormat, (int *, int *, int *));
CKRE_CHECK_SDK_SLOT(58, void, SetState, (VXRENDERSTATETYPE, CKDWORD));
CKRE_CHECK_SDK_SLOT(59, CKDWORD, GetState, (VXRENDERSTATETYPE));
CKRE_CHECK_SDK_SLOT(60, CKBOOL, SetTexture, (CKTexture *, CKBOOL, int));
CKRE_CHECK_SDK_SLOT(61, CKBOOL, SetTextureStageState, (CKRST_TEXTURESTAGESTATETYPE, CKDWORD, int));
CKRE_CHECK_SDK_SLOT(62, CKRasterizerContext *, GetRasterizerContext, ());
CKRE_CHECK_SDK_SLOT(63, void, SetClearBackground, (CKBOOL));
CKRE_CHECK_SDK_SLOT(64, CKBOOL, GetClearBackground, ());
CKRE_CHECK_SDK_SLOT(65, void, SetClearZBuffer, (CKBOOL));
CKRE_CHECK_SDK_SLOT(66, CKBOOL, GetClearZBuffer, ());
CKRE_CHECK_SDK_SLOT(67, void, GetGlobalRenderMode, (VxShadeType *, CKBOOL *, CKBOOL *));
CKRE_CHECK_SDK_SLOT(68, void, SetGlobalRenderMode, (VxShadeType, CKBOOL, CKBOOL));
CKRE_CHECK_SDK_SLOT(69, void, SetCurrentRenderOptions, (CKDWORD));
CKRE_CHECK_SDK_SLOT(70, CKDWORD, GetCurrentRenderOptions, ());
CKRE_CHECK_SDK_SLOT(71, void, ChangeCurrentRenderOptions, (CKDWORD, CKDWORD));
CKRE_CHECK_SDK_SLOT(72, void, SetCurrentExtents, (VxRect &));
CKRE_CHECK_SDK_SLOT(73, void, GetCurrentExtents, (VxRect &));
CKRE_CHECK_SDK_SLOT(74, void, SetAmbientLight, (CKDWORD));
CKRE_CHECK_SDK_SLOT(75, void, SetAmbientLight, (float, float, float));
CKRE_CHECK_SDK_SLOT(76, CKDWORD, GetAmbientLight, ());
CKRE_CHECK_SDK_SLOT(77, void, SetFogMode, (VXFOG_MODE));
CKRE_CHECK_SDK_SLOT(78, void, SetFogStart, (float));
CKRE_CHECK_SDK_SLOT(79, void, SetFogEnd, (float));
CKRE_CHECK_SDK_SLOT(80, void, SetFogDensity, (float));
CKRE_CHECK_SDK_SLOT(81, void, SetFogColor, (CKDWORD));
CKRE_CHECK_SDK_SLOT(82, VXFOG_MODE, GetFogMode, ());
CKRE_CHECK_SDK_SLOT(83, float, GetFogStart, ());
CKRE_CHECK_SDK_SLOT(84, float, GetFogEnd, ());
CKRE_CHECK_SDK_SLOT(85, float, GetFogDensity, ());
CKRE_CHECK_SDK_SLOT(86, CKDWORD, GetFogColor, ());
CKRE_CHECK_SDK_SLOT(87, CKBOOL, DrawPrimitive, (VXPRIMITIVETYPE, CKWORD *, int, VxDrawPrimitiveData *));
CKRE_CHECK_SDK_SLOT(88, void, SetWorldTransformationMatrix, (const VxMatrix &));
CKRE_CHECK_SDK_SLOT(89, void, SetProjectionTransformationMatrix, (const VxMatrix &));
CKRE_CHECK_SDK_SLOT(90, void, SetViewTransformationMatrix, (const VxMatrix &));
CKRE_CHECK_SDK_SLOT(91, const VxMatrix &, GetWorldTransformationMatrix, ());
CKRE_CHECK_SDK_SLOT(92, const VxMatrix &, GetProjectionTransformationMatrix, ());
CKRE_CHECK_SDK_SLOT(93, const VxMatrix &, GetViewTransformationMatrix, ());
CKRE_CHECK_SDK_SLOT(94, CKBOOL, SetUserClipPlane, (CKDWORD, const VxPlane &));
CKRE_CHECK_SDK_SLOT(95, CKBOOL, GetUserClipPlane, (CKDWORD, VxPlane &));
CKRE_CHECK_SDK_SLOT(96, CKRenderObject *, Pick, (CKPOINT, CKPICKRESULT *, CKBOOL));
CKRE_CHECK_SDK_SLOT(97, CKRenderObject *, Pick, (int, int, CKPICKRESULT *, CKBOOL));
CKRE_CHECK_SDK_SLOT(98, CKERROR, RectPick, (const VxRect &, XObjectPointerArray &, CKBOOL));
CKRE_CHECK_SDK_SLOT(99, void, AttachViewpointToCamera, (CKCamera *));
CKRE_CHECK_SDK_SLOT(100, void, DetachViewpointFromCamera, ());
CKRE_CHECK_SDK_SLOT(101, CKCamera *, GetAttachedCamera, ());
CKRE_CHECK_SDK_SLOT(102, CK3dEntity *, GetViewpoint, ());
CKRE_CHECK_SDK_SLOT(103, CKMaterial *, GetBackgroundMaterial, ());
CKRE_CHECK_SDK_SLOT(104, void, GetBoundingBox, (VxBbox *));
CKRE_CHECK_SDK_SLOT(105, void, GetStats, (VxStats *));
CKRE_CHECK_SDK_SLOT(106, void, SetCurrentMaterial, (CKMaterial *, CKBOOL));
CKRE_CHECK_SDK_SLOT(107, void, Activate, (CKBOOL));
CKRE_CHECK_SDK_SLOT(108, int, DumpToMemory, (const VxRect *, VXBUFFER_TYPE, VxImageDescEx &));
CKRE_CHECK_SDK_SLOT(109, int, CopyToVideo, (const VxRect *, VXBUFFER_TYPE, VxImageDescEx &));
CKRE_CHECK_SDK_SLOT(110, CKERROR, DumpToFile, (CKSTRING, const VxRect *, VXBUFFER_TYPE));
CKRE_CHECK_SDK_SLOT(111, VxDirectXData *, GetDirectXInfo, ());
CKRE_CHECK_SDK_SLOT(112, void, WarnEnterThread, ());
CKRE_CHECK_SDK_SLOT(113, void, WarnExitThread, ());
CKRE_CHECK_SDK_SLOT(114, CK2dEntity *, Pick2D, (const Vx2DVector &));
CKRE_CHECK_SDK_SLOT(115, CKBOOL, SetRenderTarget, (CKTexture *, int));
CKRE_CHECK_SDK_SLOT(116, void, AddRemoveSequence, (CKBOOL));
CKRE_CHECK_SDK_SLOT(117, void, SetTransparentMode, (CKBOOL));
CKRE_CHECK_SDK_SLOT(118, void, AddDirtyRect, (CKRECT *));
CKRE_CHECK_SDK_SLOT(119, void, RestoreScreenBackup, ());
CKRE_CHECK_SDK_SLOT(120, CKDWORD, GetStencilFreeMask, ());
CKRE_CHECK_SDK_SLOT(121, void, UsedStencilBits, (CKDWORD));
CKRE_CHECK_SDK_SLOT(122, int, GetFirstFreeStencilBits, ());
CKRE_CHECK_SDK_SLOT(123, VxDrawPrimitiveData *, LockCurrentVB, (CKDWORD));
CKRE_CHECK_SDK_SLOT(124, CKBOOL, ReleaseCurrentVB, ());
CKRE_CHECK_SDK_SLOT(125, void, SetTextureMatrix, (const VxMatrix &, int));
CKRE_CHECK_SDK_SLOT(126, void, SetStereoParameters, (float, float));
CKRE_CHECK_SDK_SLOT(127, void, GetStereoParameters, (float &, float &));
#undef CKRE_CHECK_SDK_SLOT

class AbiBoundary : public TextureTestObserver {
public:
    int Draws = 0, Begins = 0, Ends = 0;
    void *Positions = nullptr, *Colors = nullptr;
    CKWORD *Indices = nullptr;
    VxVector LastPosition;
    CKDWORD LastColor = 0;
    CKDWORD Target = 0;
    CKBOOL RejectTargetReset = FALSE;
    CKBOOL SetTargetTexture(CKDWORD target, int, int, CKRST_CUBEFACE) override {
        if (!target && RejectTargetReset) return FALSE;
        Target=target; return TRUE;
    }
    CKBOOL BeginScene() override { ++Begins; return TRUE; }
    CKBOOL EndScene() override { ++Ends; return TRUE; }
    CKBOOL BeginOverlayPhase() override { return TRUE; }
    CKBOOL EnableLight(CKDWORD, CKBOOL) override { return TRUE; }
    void InitDefaultRenderStatesValue() override {}
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE, CKWORD *indices, int, VxDrawPrimitiveData *data) override {
        ++Draws; Positions = data->PositionPtr; Colors = data->ColorPtr; Indices = indices;
        LastPosition = *reinterpret_cast<VxVector *>(static_cast<CKBYTE *>(data->PositionPtr)
            + (data->VertexCount - 1) * data->PositionStride);
        if (data->ColorPtr) LastColor = *static_cast<CKDWORD *>(data->ColorPtr);
        return TRUE;
    }
};

struct AbiCallbackPayload {
    std::uint64_t Sentinel = 0x1122334455667788ull;
    CKRenderContext *Expected = nullptr;
    CKRenderContext *SeenContext = nullptr;
    void *SeenArgument = nullptr;
    int Calls = 0;
    CKBOOL Delay = FALSE;
};
void AbiCallback(CKRenderContext *context, void *argument) {
    AbiCallbackPayload *payload = static_cast<AbiCallbackPayload *>(argument);
    payload->SeenContext = context; payload->SeenArgument = argument; ++payload->Calls;
    EXPECT_EQ(0x1122334455667788ull, payload->Sentinel);
    EXPECT_EQ(payload->Expected, context);
    if (payload->Delay) std::this_thread::sleep_for(std::chrono::milliseconds(3));
}

class RenderContextAbiCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    CKRenderContext *Sdk = nullptr;
    AbiBoundary Boundary;
    CKRasterizerDriver Driver;
    XClassArray<CKRasterizerInfo> SavedRasterizers;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT); CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY); CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
        CKBuildClassHierarchyTable(); SavedRasterizers = g_RasterizersInfo; g_RasterizersInfo.Clear();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); Manager = new RCKRenderManager(Context);
        CKObject *object = Context->CreateObject(CKCID_RENDERCONTEXT); ASSERT_NE(nullptr, object);
        Sdk = static_cast<CKRenderContext *>(object); Device = static_cast<RCKRenderContext *>(Sdk);
        Sdk->DetachAll(); Device->m_RasterizerContext = &Boundary;
        Device->m_Settings.m_Rect = {20, 30, 100, 100};
        Device->m_ViewportData.ViewX = Device->m_ViewportData.ViewY = 0;
        Device->m_ViewportData.ViewWidth = Device->m_ViewportData.ViewHeight = 100;
        Device->m_ViewportData.ViewZMin = 0; Device->m_ViewportData.ViewZMax = 1;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; }
        if (Context) CKCloseContext(Context);
        g_RasterizersInfo = SavedRasterizers; CKShutdown();
    }
};

TEST_F(RenderContextAbiCompletionTest, SdkAmbientOverloadsDispatchTheCorrectArgumentShapeAndKeepScenesIndependent) {
    CKRenderContext *other = static_cast<CKRenderContext *>(Context->CreateObject(CKCID_RENDERCONTEXT));
    ASSERT_NE(nullptr, other); const CKDWORD initial = other->GetAmbientLight();
    EXPECT_EQ(CKCID_RENDERCONTEXT, static_cast<CKObject *>(Sdk)->GetClassID());
    EXPECT_EQ(Context, static_cast<CKObject *>(Sdk)->GetCKContext());
    Sdk->SetAmbientLight(0x80112233u); EXPECT_EQ(0x80112233u, Sdk->GetAmbientLight());
    Sdk->SetAmbientLight(1.0f, 0.0f, 1.0f); EXPECT_EQ(0xffff00ffu, Sdk->GetAmbientLight());
    EXPECT_EQ(initial, other->GetAmbientLight());
    float eye = 0, focal = 0; Sdk->GetStereoParameters(eye, focal);
    EXPECT_FLOAT_EQ(.4f, eye); EXPECT_FLOAT_EQ(100, focal);
    Sdk->SetStereoParameters(1.25f, 987.5f); Sdk->GetStereoParameters(eye, focal);
    EXPECT_FLOAT_EQ(1.25f, eye); EXPECT_FLOAT_EQ(987.5f, focal);
    EXPECT_FLOAT_EQ(1.25f, Device->m_EyeSeparation); EXPECT_FLOAT_EQ(987.5f, Device->m_FocalLength);
}

TEST_F(RenderContextAbiCompletionTest, SdkPickOverloadsSelectTheSameActualTriangleAndPreserveResultStorage) {
    Context->m_BehaviorContext.CurrentRenderContext = Sdk;
    Device->m_Fov = PI / 2; Device->m_NearPlane = 1; Device->m_FarPlane = 20;
    VxMatrix projection; projection.Perspective(PI / 2, 1, 1, 20);
    Sdk->SetProjectionTransformationMatrix(projection); Sdk->SetViewTransformationMatrix(VxMatrix::Identity());
    Sdk->SetWorldTransformationMatrix(VxMatrix::Identity());
    RCK3dEntity *entity = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
    ASSERT_NE(nullptr, entity); ASSERT_NE(nullptr, mesh);
    ASSERT_TRUE(mesh->SetVertexCount(3)); ASSERT_TRUE(mesh->SetFaceCount(1));
    VxVector positions[] = {VxVector(-1,-1,0), VxVector(0,1,0), VxVector(1,-1,0)};
    VxVector normal(0,0,-1);
    for (int i=0;i<3;++i) { mesh->SetVertexPosition(i,&positions[i]); mesh->SetVertexNormal(i,&normal); }
    mesh->SetFaceVertexIndex(0,0,1,2); mesh->BuildFaceNormals(); mesh->VertexMove();
    entity->SetCurrentMesh(mesh); entity->SetPosition3f(0,0,4); entity->UpdateBox(TRUE);
    Sdk->AddObject(entity); Device->AddExtents2D(VxRect(0,0,100,100),entity);
    struct ResultStorage { std::uint64_t Before; CKPICKRESULT Result; std::uint64_t After; } a={},b={};
    a.Before=b.Before=0x1122334455667788ull; a.After=b.After=0x8877665544332211ull;
    const CKPOINT point = {70,80};
    EXPECT_EQ(entity,Sdk->Pick(70,80,&a.Result,FALSE));
    EXPECT_EQ(entity,Sdk->Pick(point,&b.Result,FALSE));
    EXPECT_EQ(0,a.Result.FaceIndex); EXPECT_EQ(0,b.Result.FaceIndex);
    EXPECT_NEAR(4,a.Result.Distance,1e-4f); EXPECT_NEAR(4,b.Result.Distance,1e-4f);
    EXPECT_EQ(0x1122334455667788ull,a.Before); EXPECT_EQ(0x1122334455667788ull,b.Before);
    EXPECT_EQ(0x8877665544332211ull,a.After); EXPECT_EQ(0x8877665544332211ull,b.After);
    EXPECT_EQ(70,point.x); EXPECT_EQ(80,point.y);
    EXPECT_EQ(entity,Sdk->Pick(point,nullptr,TRUE));
    Context->m_BehaviorContext.CurrentRenderContext=nullptr;
}

TEST_F(RenderContextAbiCompletionTest, SdkGeometrySubmissionRetainsFullWidthStreamAndIndexPointersAcrossContexts) {
    VxDrawPrimitiveData *data = Sdk->GetDrawPrimitiveStructure(static_cast<CKRST_DPFLAGS>(CKRST_DP_TRANSFORM|CKRST_DP_DIFFUSE),3);
    CKWORD *indices = Sdk->GetDrawPrimitiveIndices(3); ASSERT_NE(nullptr,data); ASSERT_NE(nullptr,indices);
    ASSERT_NE(nullptr,data->PositionPtr); ASSERT_NE(nullptr,data->ColorPtr);
    for (int i=0;i<3;++i) {
        *reinterpret_cast<VxVector *>(static_cast<CKBYTE *>(data->PositionPtr)+i*data->PositionStride)=VxVector(3+i,5+i,7+i);
        *reinterpret_cast<CKDWORD *>(static_cast<CKBYTE *>(data->ColorPtr)+i*data->ColorStride)=0xff123456u;
        indices[i]=static_cast<CKWORD>(i);
    }
    CKRenderContext *other=static_cast<CKRenderContext *>(Context->CreateObject(CKCID_RENDERCONTEXT)); ASSERT_NE(nullptr,other);
    VxDrawPrimitiveData *otherData=other->GetDrawPrimitiveStructure(CKRST_DP_TRANSFORM,5); ASSERT_NE(nullptr,otherData);
    EXPECT_NE(data->PositionPtr,otherData->PositionPtr);
    ASSERT_TRUE(Sdk->DrawPrimitive(VX_TRIANGLELIST,indices,3,data));
    EXPECT_EQ(data->PositionPtr,Boundary.Positions); EXPECT_EQ(data->ColorPtr,Boundary.Colors); EXPECT_EQ(indices,Boundary.Indices);
    EXPECT_FLOAT_EQ(9,Boundary.LastPosition.z); EXPECT_EQ(0xff123456u,Boundary.LastColor);
    EXPECT_EQ(2,indices[2]);
}

TEST_F(RenderContextAbiCompletionTest, SdkCallbackArgumentsAndTimedPhaseStorageRemainIndependent) {
    AbiCallbackPayload pre,post,final;
    pre.Expected=post.Expected=final.Expected=Sdk; pre.Delay=post.Delay=final.Delay=TRUE;
    Sdk->AddPreRenderCallBack(AbiCallback,&pre,FALSE);
    Sdk->AddPostRenderCallBack(AbiCallback,&post,FALSE,TRUE);
    Sdk->AddPostSpriteRenderCallBack(AbiCallback,&final,FALSE);
    ASSERT_EQ(CK_OK,Sdk->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
    EXPECT_EQ(1,pre.Calls); EXPECT_EQ(1,post.Calls); EXPECT_EQ(1,final.Calls);
    EXPECT_EQ(&pre,pre.SeenArgument); EXPECT_EQ(&post,post.SeenArgument); EXPECT_EQ(&final,final.SeenArgument);
    EXPECT_EQ(Sdk,pre.SeenContext); EXPECT_EQ(Sdk,post.SeenContext); EXPECT_EQ(Sdk,final.SeenContext);
    VxStats stats; Sdk->GetStats(&stats);
    EXPECT_GT(stats.DevicePreCallbacks,0); EXPECT_GT(stats.DevicePostCallbacks,0);
    Sdk->RemovePreRenderCallBack(AbiCallback,&pre); Sdk->RemovePostRenderCallBack(AbiCallback,&post);
    Sdk->RemovePostSpriteRenderCallBack(AbiCallback,&final);
    ASSERT_EQ(CK_OK,Sdk->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
    EXPECT_EQ(1,pre.Calls); EXPECT_EQ(1,post.Calls); EXPECT_EQ(1,final.Calls);
    EXPECT_EQ(2,Boundary.Begins); EXPECT_EQ(2,Boundary.Ends);
}

TEST_F(RenderContextAbiCompletionTest, FactoryDeletionUsesSdkVirtualLifetimeAndReleasesOwnedSceneAndMembershipMask) {
    CKRenderContext *owned=static_cast<CKRenderContext *>(Context->CreateObject(CKCID_RENDERCONTEXT)); ASSERT_NE(nullptr,owned);
    const CK_ID id=owned->GetID(), root=owned->GetViewpoint()->GetID(), background=owned->GetBackgroundMaterial()->GetID();
    RCKRenderContext *implementation=static_cast<RCKRenderContext *>(owned);
    const CKDWORD mask=implementation->m_MaskFree;
    ASSERT_NE(nullptr,owned->GetDrawPrimitiveStructure(CKRST_DP_TRANSFORM,4096));
    ASSERT_NE(nullptr,owned->GetDrawPrimitiveIndices(16384));
    EXPECT_EQ(0u,Manager->m_RenderContextMaskFree & mask);
    CKObject *object=owned;
    ASSERT_EQ(CK_OK,Context->DestroyObject(object));
    EXPECT_EQ(nullptr,Context->GetObject(id)); EXPECT_EQ(nullptr,Context->GetObject(root));
    EXPECT_EQ(nullptr,Context->GetObject(background)); EXPECT_EQ(mask,Manager->m_RenderContextMaskFree & mask);
    EXPECT_NE(nullptr,Context->GetObject(Sdk->GetID()));
    EXPECT_EQ(Context,Sdk->GetCKContext());
}

TEST_F(RenderContextAbiCompletionTest, RealRenderPublishesTheTimedFpsSampleAndRestartsItsIndependentCounter) {
    Context->GetTimeManager()->ChangeLimitOptions(CK_FRAMERATE_FREE);
    Sdk->Activate(TRUE);
    ASSERT_EQ(CK_OK,Sdk->Render(CK_RENDER_DONOTUPDATEEXTENTS));
    ASSERT_EQ(1u,Device->m_TimeFpsCalc);
    std::this_thread::sleep_for(std::chrono::milliseconds(1050));
    const float elapsed=Device->m_RenderTimeProfiler.Current();
    const auto start=std::chrono::steady_clock::now();
    ASSERT_EQ(CK_OK,Sdk->Render(CK_RENDER_DONOTUPDATEEXTENTS));
    const float callTime=std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-start).count();
    VxStats stats; Sdk->GetStats(&stats);
    EXPECT_EQ(0u,Device->m_TimeFpsCalc);
    EXPECT_GT(stats.SmoothedFps,0);
    // The two accepted frames supply the first 90%-weighted sample. Bracket
    // its denominator with an independent wall clock, including scheduler time.
    EXPECT_GE(stats.SmoothedFps,.9f*2000/(elapsed+callTime+1));
    EXPECT_LE(stats.SmoothedFps,.9f*2000/(elapsed-1));
    EXPECT_FLOAT_EQ(Device->m_SmoothedFps,stats.SmoothedFps);
    EXPECT_LT(Device->m_RenderTimeProfiler.Current(),elapsed);
    ASSERT_EQ(CK_OK,Sdk->Render(CK_RENDER_DONOTUPDATEEXTENTS));
    EXPECT_EQ(1u,Device->m_TimeFpsCalc);
    Sdk->GetStats(&stats); EXPECT_FLOAT_EQ(Device->m_SmoothedFps,stats.SmoothedFps);
    EXPECT_EQ(3,Boundary.Begins); EXPECT_EQ(3,Boundary.Ends);
}

TEST_F(RenderContextAbiCompletionTest, MasterCameraAttributeSelectsTheActualViewAndFrustumThenFallsBackAfterRemoval) {
    RCKCamera *attached=static_cast<RCKCamera *>(Context->CreateObject(CKCID_CAMERA));
    RCKCamera *master=static_cast<RCKCamera *>(Context->CreateObject(CKCID_CAMERA));
    ASSERT_NE(nullptr,attached); ASSERT_NE(nullptr,master);
    attached->SetPosition3f(1,2,3); attached->SetFov(PI/2);
    attached->SetFrontPlane(2); attached->SetBackPlane(300);
    master->SetPosition3f(25,6,7);
    const CKAttributeType attribute=Context->GetAttributeManager()->RegisterNewAttributeType(
        "1CamPl8ne4SterCube2Rend",CKPGUID_CAMERA,CKCID_CAMERA);
    ASSERT_GE(attribute,0); ASSERT_TRUE(attached->SetAttribute(attribute));
    CKParameterOut *parameter=attached->GetAttributeParameter(attribute); ASSERT_NE(nullptr,parameter);
    const CK_ID masterId=master->GetID(); ASSERT_EQ(CK_OK,parameter->SetValue(&masterId));
    Sdk->AttachViewpointToCamera(reinterpret_cast<CKCamera *>(attached));
    Sdk->Activate(TRUE); Context->GetTimeManager()->ChangeLimitOptions(CK_FRAMERATE_FREE);
    ASSERT_EQ(CK_OK,Sdk->Render(CK_RENDER_DONOTUPDATEEXTENTS));
    EXPECT_EQ(master,Device->m_Camera);
    EXPECT_FLOAT_EQ(25,Sdk->GetViewpoint()->GetWorldMatrix()[3].x);
    EXPECT_FLOAT_EQ(-25,Boundary.Matrices[VXMATRIX_VIEW][3].x);
    EXPECT_FLOAT_EQ(25,Device->m_Frustum.GetOrigin().x);
    EXPECT_FLOAT_EQ(6,Device->m_Frustum.GetOrigin().y);
    EXPECT_FLOAT_EQ(7,Device->m_Frustum.GetOrigin().z);
    EXPECT_FLOAT_EQ(2,Device->m_Frustum.GetDMin()); EXPECT_FLOAT_EQ(300,Device->m_Frustum.GetDMax());
    EXPECT_EQ(0u,Device->m_Frustum.Classify(VxVector(25,6,12)));
    EXPECT_NE(0u,Device->m_Frustum.Classify(VxVector(25,6,5)));
    ASSERT_TRUE(attached->RemoveAttribute(attribute));
    ASSERT_EQ(CK_OK,Sdk->Render(CK_RENDER_DONOTUPDATEEXTENTS));
    EXPECT_EQ(nullptr,Device->m_Camera);
    EXPECT_FLOAT_EQ(1,Sdk->GetViewpoint()->GetWorldMatrix()[3].x);
    EXPECT_FLOAT_EQ(-1,Boundary.Matrices[VXMATRIX_VIEW][3].x);
    EXPECT_FLOAT_EQ(1,Device->m_Frustum.GetOrigin().x);
    EXPECT_EQ(master,Context->GetObject(masterId));
    EXPECT_EQ(2,Boundary.Begins); EXPECT_EQ(2,Boundary.Ends);
}

TEST_F(RenderContextAbiCompletionTest, DeletingTheCachedMasterCameraLetsDirectDrawSceneUseTheLiveAttachedCamera) {
    RCKCamera *attached=static_cast<RCKCamera *>(Context->CreateObject(CKCID_CAMERA));
    RCKCamera *master=static_cast<RCKCamera *>(Context->CreateObject(CKCID_CAMERA));
    ASSERT_NE(nullptr,attached); ASSERT_NE(nullptr,master);
    attached->SetPosition3f(1,2,3); master->SetPosition3f(25,6,7);
    const CKAttributeType attribute=Context->GetAttributeManager()->RegisterNewAttributeType(
        "1CamPl8ne4SterCube2Rend",CKPGUID_CAMERA,CKCID_CAMERA);
    ASSERT_GE(attribute,0); ASSERT_TRUE(attached->SetAttribute(attribute));
    CKParameterOut *parameter=attached->GetAttributeParameter(attribute); ASSERT_NE(nullptr,parameter);
    const CK_ID masterId=master->GetID(); ASSERT_EQ(CK_OK,parameter->SetValue(&masterId));
    Sdk->AttachViewpointToCamera(reinterpret_cast<CKCamera *>(attached)); Sdk->Activate(TRUE);
    Context->GetTimeManager()->ChangeLimitOptions(CK_FRAMERATE_FREE);
    ASSERT_EQ(CK_OK,Sdk->Render(CK_RENDER_DONOTUPDATEEXTENTS)); ASSERT_EQ(master,Device->m_Camera);
    ASSERT_EQ(CK_OK,Context->DestroyObject(master)); ASSERT_EQ(nullptr,Context->GetObject(masterId));
    // Stop before DrawScene if the borrowed pointer was not removed by the
    // real object-deletion notification. The regression never dereferences it.
    ASSERT_EQ(nullptr,Device->m_Camera);
    EXPECT_EQ(reinterpret_cast<CKCamera *>(attached),Sdk->GetAttachedCamera());
    ASSERT_EQ(CK_OK,Sdk->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
    EXPECT_FLOAT_EQ(1,Sdk->GetViewpoint()->GetWorldMatrix()[3].x);
    EXPECT_FLOAT_EQ(-1,Boundary.Matrices[VXMATRIX_VIEW][3].x);
    EXPECT_EQ(2,Boundary.Begins); EXPECT_EQ(2,Boundary.Ends);
}

TEST_F(RenderContextAbiCompletionTest, DeletingTheActiveTextureTargetRestoresTheWindowBeforePixelFormatQuery) {
    CKCLASSREGISTERCID(RCKTexture,CKCID_BEOBJECT); CKBuildClassHierarchyTable();
    ASSERT_EQ(1,Manager->m_DriverCount);
    Manager->m_Drivers[0].CapsUpToDate=TRUE;
    Manager->m_Drivers[0].RasterizerDriver=&Driver; Device->m_RasterizerDriver=&Driver;
    VxImageDescEx format; VxPixelFormat2ImageDesc(_32_ARGB8888,format);
    Manager->m_Drivers[0].TextureFormats.PushBack(format);
    ASSERT_TRUE(Manager->RegisterRasterizerContext(&Boundary,0));
    Device->m_Settings.m_Bpp=32; Device->m_Settings.m_Zbpp=24; Device->m_Settings.m_StencilBpp=8;
    Device->m_WindowSettings=Device->m_Settings;
    RCKTexture *target=static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE)); ASSERT_NE(nullptr,target);
    ASSERT_TRUE(target->CreateImage(4,2,32,0));
    ASSERT_TRUE(Sdk->SetRenderTarget(target)); ASSERT_EQ(target,Device->m_TargetTexture);
    ASSERT_NE(0u,Boundary.Target); ASSERT_EQ(4,Sdk->GetWidth()); ASSERT_EQ(2,Sdk->GetHeight());
    const CK_ID targetId=target->GetID();
    ASSERT_EQ(CK_OK,Context->DestroyObject(target)); ASSERT_EQ(nullptr,Context->GetObject(targetId));
    // Query only after proving deletion removed the borrowed object pointer.
    ASSERT_EQ(nullptr,Device->m_TargetTexture); EXPECT_EQ(0u,Boundary.Target);
    EXPECT_EQ(100,Sdk->GetWidth()); EXPECT_EQ(100,Sdk->GetHeight());
    int bpp=0,depth=0,stencil=0;
    EXPECT_EQ(UNKNOWN_PF,Sdk->GetPixelFormat(&bpp,&depth,&stencil));
    EXPECT_EQ(32,bpp); EXPECT_EQ(24,depth); EXPECT_EQ(8,stencil);
    EXPECT_EQ(100,Device->m_ViewportData.ViewWidth); EXPECT_EQ(100,Device->m_ViewportData.ViewHeight);
}

TEST_F(RenderContextAbiCompletionTest, RejectedTargetResetDuringDeletionDropsTheBorrowWithoutClaimingWindowRestoration) {
    CKCLASSREGISTERCID(RCKTexture,CKCID_BEOBJECT); CKBuildClassHierarchyTable();
    ASSERT_EQ(1,Manager->m_DriverCount);
    Manager->m_Drivers[0].CapsUpToDate=TRUE;
    Manager->m_Drivers[0].RasterizerDriver=&Driver; Device->m_RasterizerDriver=&Driver;
    VxImageDescEx format; VxPixelFormat2ImageDesc(_32_ARGB8888,format);
    Manager->m_Drivers[0].TextureFormats.PushBack(format);
    ASSERT_TRUE(Manager->RegisterRasterizerContext(&Boundary,0));
    Device->m_Settings.m_Bpp=32; Device->m_Settings.m_Zbpp=24; Device->m_Settings.m_StencilBpp=8;
    Device->m_WindowSettings=Device->m_Settings;
    RCKTexture *target=static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE)); ASSERT_NE(nullptr,target);
    ASSERT_TRUE(target->CreateImage(4,2,32,0));
    ASSERT_TRUE(Sdk->SetRenderTarget(target)); ASSERT_EQ(target,Device->m_TargetTexture);
    const CKDWORD physicalTarget=Boundary.Target; ASSERT_NE(0u,physicalTarget);
    Boundary.RejectTargetReset=TRUE;
    const CK_ID targetId=target->GetID();
    ASSERT_EQ(CK_OK,Context->DestroyObject(target)); ASSERT_EQ(nullptr,Context->GetObject(targetId));
    // Deletion cannot retain the object borrow when the physical reset fails.
    // Stop here on the old implementation; no freed-object query is attempted.
    ASSERT_EQ(nullptr,Device->m_TargetTexture);
    EXPECT_EQ(physicalTarget,Boundary.Target);
    EXPECT_EQ(4,Sdk->GetWidth()); EXPECT_EQ(2,Sdk->GetHeight());
    EXPECT_EQ(4,Device->m_ViewportData.ViewWidth); EXPECT_EQ(2,Device->m_ViewportData.ViewHeight);
    int bpp=0,depth=0,stencil=0;
    EXPECT_EQ(UNKNOWN_PF,Sdk->GetPixelFormat(&bpp,&depth,&stencil));
    EXPECT_EQ(32,bpp); EXPECT_EQ(24,depth); EXPECT_EQ(8,stencil);
    EXPECT_EQ(CK_OK,Device->m_FrameRenderError);
    Boundary.RejectTargetReset=FALSE;
    ASSERT_TRUE(Sdk->SetRenderTarget(nullptr)); EXPECT_EQ(0u,Boundary.Target);
    EXPECT_EQ(100,Sdk->GetWidth()); EXPECT_EQ(100,Sdk->GetHeight());
    EXPECT_EQ(100,Device->m_ViewportData.ViewWidth); EXPECT_EQ(100,Device->m_ViewportData.ViewHeight);
}
} // namespace
