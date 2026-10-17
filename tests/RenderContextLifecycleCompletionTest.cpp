#include "MaterialRenderObserver.h"
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKCamera.h"
#include "RCKLight.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();
extern XClassArray<CKRasterizerInfo> g_RasterizersInfo;

namespace {
void LifecycleCallback(CKRenderContext *, void *) {}
class RenderContextLifecycleCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    XClassArray<CKRasterizerInfo> Saved;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        Saved = g_RasterizersInfo; g_RasterizersInfo.Clear();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
    }
    void TearDown() override {
        if (Context) CKCloseContext(Context);
        g_RasterizersInfo = Saved; CKShutdown();
    }
};
}

TEST_F(RenderContextLifecycleCompletionTest, RegisteredFactoryInitializesIndependentSceneAndReservedExtents) {
    const CKDWORD originalMask = Manager->m_RenderContextMaskFree;
    RCKRenderContext *first = static_cast<RCKRenderContext *>(Context->CreateObject(CKCID_RENDERCONTEXT));
    RCKRenderContext *second = static_cast<RCKRenderContext *>(Context->CreateObject(CKCID_RENDERCONTEXT));
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    CKObject *base = first;
    EXPECT_EQ(CKCID_RENDERCONTEXT, base->GetClassID());
    EXPECT_STREQ("Render Context", RCKRenderContext::GetClassName());
    for (int mode = 0; mode <= CK_DEPENDENCIES_BUILD; ++mode) {
        EXPECT_EQ(0, RCKRenderContext::GetDependenciesCount(mode));
        EXPECT_EQ(nullptr, RCKRenderContext::GetDependencies(0, mode));
    }
    EXPECT_EQ(Manager, first->m_RenderManager);
    EXPECT_NE(0u, first->m_MaskFree); EXPECT_NE(first->m_MaskFree, second->m_MaskFree);
    EXPECT_NE(first->m_RenderedScene, second->m_RenderedScene);
    EXPECT_NE(first->m_UserDrawPrimitiveData, second->m_UserDrawPrimitiveData);
    EXPECT_NE(first->GetViewpoint(), second->GetViewpoint());
    EXPECT_NE(first->GetBackgroundMaterial(), second->GetBackgroundMaterial());
    EXPECT_EQ(0, first->m_ObjectExtents.Size()); EXPECT_GE(first->m_ObjectExtents.Allocated(), 500);
    EXPECT_EQ(nullptr, first->m_RasterizerContext); EXPECT_EQ(nullptr, first->m_RasterizerDriver);
    EXPECT_FALSE(first->m_Fullscreen); EXPECT_FALSE(first->m_Active); EXPECT_TRUE(first->m_Perspective);
    EXPECT_FALSE(first->m_ProjectionUpdated); EXPECT_FALSE(first->m_DeviceDestroying);
    EXPECT_EQ(CK_RENDER_DEFAULTSETTINGS, first->m_RenderFlags);
    EXPECT_FLOAT_EQ(PI / 4, first->m_Fov); EXPECT_FLOAT_EQ(1, first->m_Zoom);
    EXPECT_FLOAT_EQ(1, first->m_NearPlane); EXPECT_FLOAT_EQ(4000, first->m_FarPlane);
    EXPECT_EQ(0, first->m_ViewportData.ViewWidth); EXPECT_EQ(0, first->m_ViewportData.ViewHeight);
    EXPECT_FLOAT_EQ(0, first->m_ViewportData.ViewZMin); EXPECT_FLOAT_EQ(1, first->m_ViewportData.ViewZMax);
    EXPECT_EQ(-1, first->m_WinRect.left); EXPECT_EQ(-1, first->m_WinRect.right);
    float separation = 0, focal = 0; first->GetStereoParameters(separation, focal);
    EXPECT_FLOAT_EQ(.4f, separation); EXPECT_FLOAT_EQ(100, focal);
    EXPECT_EQ(static_cast<CKDWORD>(-1), first->m_StartIndex);
    Context->DestroyObject(first); Context->DestroyObject(second);
    EXPECT_EQ(originalMask, Manager->m_RenderContextMaskFree);
}

TEST_F(RenderContextLifecycleCompletionTest, PreDeleteDetachesMembershipAndExtentsBeforeTheObjectDies) {
    RCKRenderContext view(Context); RCK3dEntity entity(Context);
    view.AddObject(reinterpret_cast<CKRenderObject *>(&entity));
    CKObjectExtents extents = {}; extents.m_Entity = reinterpret_cast<CK3dEntity *>(&entity);
    view.m_ObjectExtents.PushBack(extents);
    ASSERT_TRUE(entity.IsInRenderContext(&view)); ASSERT_TRUE(view.IsObjectAttached(reinterpret_cast<CKRenderObject *>(&entity)));
    view.PreDelete();
    EXPECT_FALSE(entity.IsInRenderContext(&view));
    EXPECT_FALSE(view.IsObjectAttached(reinterpret_cast<CKRenderObject *>(&entity)));
    EXPECT_EQ(0, view.m_ObjectExtents.Size());
    view.PreDelete();
}

TEST_F(RenderContextLifecycleCompletionTest, UsageQueriesHonorTheRequestedClassAndActualMembership) {
    RCKRenderContext view(Context); RCK3dEntity entity(Context), other(Context);
    RCKCamera camera(Context); RCKLight light(Context);
    view.AddObject(reinterpret_cast<CKRenderObject *>(&entity));
    view.AddObject(reinterpret_cast<CKRenderObject *>(&camera));
    view.AddObject(reinterpret_cast<CKRenderObject *>(&light));
    EXPECT_TRUE(view.IsObjectUsed(&entity, CKCID_3DENTITY));
    EXPECT_TRUE(view.IsObjectUsed(&camera, CKCID_CAMERA));
    EXPECT_TRUE(view.IsObjectUsed(&camera, CKCID_3DENTITY));
    EXPECT_TRUE(view.IsObjectUsed(&light, CKCID_LIGHT));
    EXPECT_TRUE(view.IsObjectUsed(&light, CKCID_3DENTITY));
    EXPECT_FALSE(view.IsObjectUsed(&entity, CKCID_OBJECT));
    // Native checks each requested-class ancestor independently: a LIGHT
    // query also visits the 3D list, which contains the attached camera.
    EXPECT_TRUE(view.IsObjectUsed(&camera, CKCID_LIGHT));
    EXPECT_TRUE(view.IsObjectUsed(&light, CKCID_CAMERA));
    EXPECT_FALSE(view.IsObjectUsed(&camera, CKCID_OBJECT));
    EXPECT_FALSE(view.IsObjectUsed(&other, CKCID_LIGHT));
    EXPECT_FALSE(view.IsObjectUsed(&other, CKCID_3DENTITY));
    view.DetachAll(); EXPECT_FALSE(view.IsObjectUsed(&entity, CKCID_3DENTITY));
}

TEST_F(RenderContextLifecycleCompletionTest, DeletionNotificationDropsDoomedReferencesAndAttachedCamera) {
    RCKRenderContext view(Context); RCK3dEntity entity(Context), live(Context); RCKCamera camera(Context); RCKLight light(Context);
    view.AddObject(reinterpret_cast<CKRenderObject *>(&entity)); view.AddObject(reinterpret_cast<CKRenderObject *>(&live));
    view.AddObject(reinterpret_cast<CKRenderObject *>(&camera)); view.AddObject(reinterpret_cast<CKRenderObject *>(&light));
    view.AttachViewpointToCamera(reinterpret_cast<CKCamera *>(&camera));
    CKObjectExtents extent = {}; extent.m_Entity = reinterpret_cast<CK3dEntity *>(&entity); view.m_ObjectExtents.PushBack(extent);
    extent.m_Entity = reinterpret_cast<CK3dEntity *>(&live); view.m_ObjectExtents.PushBack(extent);
    entity.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); camera.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); light.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
    view.CheckPreDeletion();
    EXPECT_EQ(nullptr, view.GetAttachedCamera()); EXPECT_EQ(nullptr, view.m_ObjectExtents[0].m_Entity);
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&live), view.m_ObjectExtents[1].m_Entity);
    EXPECT_FALSE(view.IsObjectUsed(&entity, CKCID_3DENTITY)); EXPECT_FALSE(view.IsObjectUsed(&camera, CKCID_CAMERA)); EXPECT_FALSE(view.IsObjectUsed(&light, CKCID_LIGHT));
    EXPECT_TRUE(view.IsObjectUsed(&live, CKCID_3DENTITY));
    entity.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED); camera.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED); light.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
}

TEST_F(RenderContextLifecycleCompletionTest, MemoryOccupationCountsAllocatedVertexStreamsAndWordIndices) {
    RCKRenderContext view(Context); const int empty = view.GetMemoryOccupation();
    ASSERT_NE(nullptr, view.GetDrawPrimitiveStructure(CKRST_DP_TRANSFORM, 3));
    const int vertexBytes = sizeof(VxVector4) + 3 * sizeof(VxVector) + 2 * sizeof(CKDWORD) + 8 * sizeof(Vx2DVector);
    EXPECT_EQ(empty + 3 * vertexBytes, view.GetMemoryOccupation());
    ASSERT_NE(nullptr, view.GetDrawPrimitiveIndices(7)); EXPECT_EQ(empty + 3 * vertexBytes + 7 * int(sizeof(CKWORD)), view.GetMemoryOccupation());
    ASSERT_NE(nullptr, view.GetDrawPrimitiveStructure(CKRST_DP_TRANSFORM, 1));
    ASSERT_NE(nullptr, view.GetDrawPrimitiveIndices(2)); EXPECT_EQ(empty + 3 * vertexBytes + 7 * int(sizeof(CKWORD)), view.GetMemoryOccupation());
}

TEST_F(RenderContextLifecycleCompletionTest, CopyAndRemapUseObjectContractAndPreserveIndependentDeviceState) {
    RCKRenderContext source(Context, "Source"), destination(Context, "Destination");
    int application = 7; source.SetAppData(&application); source.Show(CKHIERARCHICALHIDE);
    source.m_Fullscreen = TRUE; source.m_DriverIndex = 11; source.m_RenderFlags = CK_RENDER_CLEARZ;
    source.m_Settings.m_Rect.right = 999; source.m_WinRect.left = 25;
    destination.m_DriverIndex = 3; destination.m_Settings.m_Rect.right = 73; destination.m_WinRect.left = 9;
    CKRenderedScene *scene = destination.m_RenderedScene; UserDrawPrimitiveDataClass *draw = destination.m_UserDrawPrimitiveData;
    CKDependencies options; CKCopyDefaultClassDependencies(options, CK_DEPENDENCIES_COPY);
    options.m_Flags = CK_DEPENDENCIES_CUSTOM;
    options.ModifyOptions(CKCID_OBJECT, CK_DEPENDENCIES_COPY_OBJECT_NAME, CK_DEPENDENCIES_COPY_OBJECT_UNIQUENAME);
    CKDependenciesContext dependencies(Context); dependencies.SetOperationMode(CK_DEPENDENCIES_COPY); dependencies.StartDependencies(&options);
    EXPECT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_EQ(CK_OK, destination.RemapDependencies(dependencies)); dependencies.StopDependencies();
    EXPECT_STREQ("Source", destination.GetName()); EXPECT_EQ(&application, destination.GetAppData()); EXPECT_FALSE(destination.IsVisible());
    EXPECT_TRUE(destination.GetObjectFlags() & CK_OBJECT_HIERACHICALHIDE);
    EXPECT_FALSE(destination.m_Fullscreen); EXPECT_EQ(3, destination.m_DriverIndex); EXPECT_EQ(73, destination.m_Settings.m_Rect.right);
    EXPECT_EQ(9, destination.m_WinRect.left); EXPECT_EQ(CK_RENDER_DEFAULTSETTINGS, destination.m_RenderFlags);
    EXPECT_EQ(scene, destination.m_RenderedScene); EXPECT_EQ(draw, destination.m_UserDrawPrimitiveData);
}

TEST_F(RenderContextLifecycleCompletionTest, SaveAndLoadUseIndependentObjectWireWithoutDeviceSettings) {
    RCKRenderContext view(Context); view.m_Settings.m_Rect.right = 73;
    view.PreSave(nullptr, 0);
    CKStateChunk *saved = view.Save(nullptr, 0); ASSERT_NE(nullptr, saved); saved->CloseChunk();
    EXPECT_EQ(CKCID_OBJECT, saved->GetChunkClassID()); EXPECT_EQ(CHUNK_DEV_2_1, saved->GetDataVersion()); DeleteCKStateChunk(saved);
    CKStateChunk wire(CKCID_OBJECT, nullptr); wire.StartWrite(); wire.SetDataVersion(CHUNK_DEV_2_1);
    wire.WriteIdentifier(CK_STATESAVE_OBJECTHIERAHIDDEN); wire.CloseChunk();
    EXPECT_EQ(CK_OK, view.Load(&wire, nullptr)); view.PostLoad(); EXPECT_FALSE(view.IsVisible());
    EXPECT_TRUE(view.GetObjectFlags() & CK_OBJECT_HIERACHICALHIDE); EXPECT_EQ(73, view.m_Settings.m_Rect.right);
    EXPECT_EQ(nullptr, view.m_RasterizerContext);
}

TEST_F(RenderContextLifecycleCompletionTest, BuildDependenciesIncludeSceneListsRootsAndBackgroundMaterial) {
    RCKRenderContext view(Context); RCK3dEntity entity(Context); RCKCamera camera(Context); RCKLight light(Context);
    view.AddObject(reinterpret_cast<CKRenderObject *>(&entity)); view.AddObject(reinterpret_cast<CKRenderObject *>(&camera)); view.AddObject(reinterpret_cast<CKRenderObject *>(&light));
    CKDependencies options; CKCopyDefaultClassDependencies(options, CK_DEPENDENCIES_BUILD);
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    EXPECT_EQ(CK_OK, view.PrepareDependencies(dependencies));
    EXPECT_TRUE(dependencies.IsDependenciesHere(view.GetID())); EXPECT_TRUE(dependencies.IsDependenciesHere(entity.GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(camera.GetID())); EXPECT_TRUE(dependencies.IsDependenciesHere(light.GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(view.Get2dRoot(TRUE)->GetID())); EXPECT_TRUE(dependencies.IsDependenciesHere(view.Get2dRoot(FALSE)->GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(view.GetBackgroundMaterial()->GetID())); dependencies.StopDependencies();
}

TEST_F(RenderContextLifecycleCompletionTest, ClearAllDropsCallbacksAndAttachmentsAndRestoresFullViewport) {
    RCKRenderContext view(Context); RCK3dEntity entity(Context); MaterialRenderObserver boundary;
    view.m_RasterizerContext = &boundary; view.m_Settings.m_Rect.right = 97; view.m_Settings.m_Rect.bottom = 83;
    view.m_ViewportData.ViewX = 3; view.m_ViewportData.ViewY = 7; view.m_ViewportData.ViewWidth = 11; view.m_ViewportData.ViewHeight = 13;
    view.AddObject(reinterpret_cast<CKRenderObject *>(&entity)); view.AddPreRenderCallBack(LifecycleCallback, nullptr, TRUE);
    view.AddPostRenderCallBack(LifecycleCallback, nullptr, TRUE, TRUE); view.AddPostSpriteRenderCallBack(LifecycleCallback, nullptr, TRUE);
    view.OnClearAll(); EXPECT_FALSE(entity.IsInRenderContext(&view));
    EXPECT_EQ(0, view.m_PreRenderCallBacks.m_PreCallBacks.Size()); EXPECT_EQ(0, view.m_PostRenderCallBacks.m_PostCallBacks.Size());
    EXPECT_EQ(0, view.m_PostSpriteRenderCallBacks.m_PostCallBacks.Size()); EXPECT_EQ(0, boundary.Viewport.ViewX); EXPECT_EQ(0, boundary.Viewport.ViewY);
    EXPECT_EQ(97, boundary.Viewport.ViewWidth); EXPECT_EQ(83, boundary.Viewport.ViewHeight);
    EXPECT_EQ(nullptr, view.m_NCUTex); view.Activate(TRUE); EXPECT_TRUE(view.m_Active); view.Activate(FALSE); EXPECT_FALSE(view.m_Active);
    view.m_RasterizerContext = nullptr;
}
TEST_F(RenderContextLifecycleCompletionTest, RegisteredRenderObjectNotificationClearsExtentDuringActualDeletion) {
    RCKRenderContext *view = static_cast<RCKRenderContext *>(Context->CreateObject(CKCID_RENDERCONTEXT));
    CK3dEntity *entity = static_cast<CK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    ASSERT_NE(nullptr, view); ASSERT_NE(nullptr, entity);
    view->AddObject(entity); CKObjectExtents extent = {}; extent.m_Entity = entity; view->m_ObjectExtents.PushBack(extent);
    ASSERT_EQ(entity, view->m_ObjectExtents[0].m_Entity);
    ASSERT_EQ(CK_OK, Context->DestroyObject(entity));
    ASSERT_EQ(1, view->m_ObjectExtents.Size()); EXPECT_EQ(nullptr, view->m_ObjectExtents[0].m_Entity);
    Context->DestroyObject(view);
}
