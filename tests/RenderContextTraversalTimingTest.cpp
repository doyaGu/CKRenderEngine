#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include "CKAll.h"
#include "RCK2dEntity.h"
#include "RCK3dEntity.h"
#include "RCKCamera.h"
#include "RCKLight.h"
#include "RCKMaterial.h"
#include "RCKMesh.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "CKSceneGraph.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
float WaitForMeasuredWork(int milliseconds) {
    VxTimeProfiler elapsed;
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    return elapsed.Current();
}

// Only the external submission boundary waits. The actual scene traversal,
// entity/mesh/sprite implementations and all statistics accounting run intact.
class TraversalTimingBoundary : public MaterialRenderObserver {
public:
    RCKRenderContext *Device = nullptr;
    int Draws = 0, MeshDraws = 0, BackgroundDraws = 0, ForegroundDraws = 0;
    int MeshDelay = 0, BackgroundDelay = 0, ForegroundDelay = 0;
    CKBOOL Overlay = FALSE;
    float MeshWork = 0, BackgroundWork = 0, ForegroundWork = 0;
    float TraversalAtMesh = 0, SpriteAtForeground = 0;
    CKBOOL BeginScene() override { Overlay = FALSE; return TRUE; }
    CKBOOL EndScene() override { return TRUE; }
    CKBOOL BeginOverlayPhase() override { Overlay = TRUE; return TRUE; }
    CKBOOL EnableLight(CKDWORD, CKBOOL) override { return TRUE; }
    void InitDefaultRenderStatesValue() override {}
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE, CKWORD *, int count, VxDrawPrimitiveData *data) override {
        EXPECT_NE(nullptr, data);
        EXPECT_GT(count, 0);
        if (!data) return FALSE;
        EXPECT_NE(nullptr, data->PositionPtr);
        ++Draws;
        if (Device->m_Current3dEntity) {
            ++MeshDraws;
            TraversalAtMesh = Device->m_Stats.SceneTraversalTime;
            MeshWork += WaitForMeasuredWork(MeshDelay);
        } else if (Overlay) {
            ++ForegroundDraws;
            SpriteAtForeground = Device->m_Stats.SpriteTime;
            ForegroundWork += WaitForMeasuredWork(ForegroundDelay);
        } else {
            ++BackgroundDraws;
            BackgroundWork += WaitForMeasuredWork(BackgroundDelay);
        }
        return TRUE;
    }
};

struct ObjectDelay {
    int Calls = 0, Milliseconds = 0;
    float Measured = 0;
};
CKBOOL DelayObject(CKRenderContext *, CKRenderObject *, void *argument) {
    ObjectDelay *delay = static_cast<ObjectDelay *>(argument);
    ++delay->Calls;
    delay->Measured += WaitForMeasuredWork(delay->Milliseconds);
    return FALSE;
}

struct FrameMeasurements {
    VxTimeProfiler AfterPre;
    int PreCalls = 0, ObjectCaptures = 0, SpriteCaptures = 0;
    float RawObjects = 0, ObjectEpoch = 0, SincePre = 0;
    VxStats BeforeFinal = {};
};
void BeginObjectMeasurement(CKRenderContext *, void *argument) {
    FrameMeasurements *measurements = static_cast<FrameMeasurements *>(argument);
    ++measurements->PreCalls;
    WaitForMeasuredWork(25);
    measurements->AfterPre.Reset();
}
void CaptureRawObjects(CKRenderContext *context, void *argument) {
    FrameMeasurements *measurements = static_cast<FrameMeasurements *>(argument);
    RCKRenderContext *device = static_cast<RCKRenderContext *>(context);
    ++measurements->ObjectCaptures;
    measurements->RawObjects = device->m_Stats.ObjectsRenderTime;
    measurements->ObjectEpoch = device->m_ObjectsRenderTimeProfiler.Current();
    measurements->SincePre = measurements->AfterPre.Current();
}
void CaptureBeforeFinalSubtraction(CKRenderContext *context, void *argument) {
    FrameMeasurements *measurements = static_cast<FrameMeasurements *>(argument);
    ++measurements->SpriteCaptures;
    context->GetStats(&measurements->BeforeFinal);
}

struct SortMeasurements {
    VxTimeProfiler Call;
    int Calls = 0;
    CK_ID Order[8] = {};
    float Published[8] = {}, Epoch[8] = {}, SinceCall[8] = {}, Work = 0;
};
CKBOOL CaptureSortedRender(CKRenderContext *context, CKRenderObject *object, void *argument) {
    SortMeasurements *measurements = static_cast<SortMeasurements *>(argument);
    RCKRenderContext *device = static_cast<RCKRenderContext *>(context);
    EXPECT_LT(measurements->Calls, 8);
    const int index = measurements->Calls++;
    if (index < 8) {
        measurements->Order[index] = object->GetID();
        measurements->Published[index] = device->m_Stats.TransparentObjectsSortTime;
        measurements->Epoch[index] = device->m_TransparentObjectsSortTimeProfiler.Current();
        measurements->SinceCall[index] = measurements->Call.Current();
    }
    measurements->Work += WaitForMeasuredWork(15);
    return FALSE;
}

class RenderContextTraversalTimingTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    TraversalTimingBoundary Boundary;
    CKRasterizerDriver Driver;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        ASSERT_GT(Manager->m_DriverCount, 0);
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 8;
        Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = 8;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&Boundary, 0));
        Device = new RCKRenderContext(Context);
        Device->DetachAll();
        Device->m_RasterizerContext = &Boundary;
        Device->m_RasterizerDriver = &Driver;
        Boundary.Device = Device;
        Device->m_Settings.m_Rect = {0, 0, 100, 100};
        Device->m_ViewportData.ViewWidth = Device->m_ViewportData.ViewHeight = 100;
        Device->m_ViewportData.ViewZMin = 0;
        Device->m_ViewportData.ViewZMax = 1;
        Boundary.Viewport = Device->m_ViewportData;
        Device->m_NearPlane = 1;
        Device->m_FarPlane = 100;
        Device->m_Fov = PI / 2;
        VxMatrix projection;
        projection.Perspective(PI / 2, 1, 1, 100);
        Device->SetProjectionTransformationMatrix(projection);
        Device->SetViewTransformationMatrix(VxMatrix::Identity());
        Context->m_BehaviorContext.CurrentRenderContext = Device;
        Device->Get2dRoot(TRUE)->SetRect(VxRect(0, 0, 100, 100));
        Device->Get2dRoot(FALSE)->SetRect(VxRect(0, 0, 100, 100));
    }
    void TearDown() override {
        if (Context) Context->m_BehaviorContext.CurrentRenderContext = nullptr;
        if (Device) {
            Device->m_RasterizerContext = nullptr;
            Device->m_RasterizerDriver = nullptr;
            delete Device;
        }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    RCK3dEntity *Box(float z = 5) {
        RCK3dEntity *entity = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
        if (!entity) return nullptr;
        VxBbox box;
        box.Min.Set(-.25f, -.25f, -.25f);
        box.Max.Set(.25f, .25f, .25f);
        if (!entity->SetBoundingBox(&box, TRUE)) return nullptr;
        entity->SetPosition3f(0, 0, z);
        return entity;
    }
    RCK3dEntity *Triangle() {
        RCK3dEntity *entity = Box();
        RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
        RCKMaterial *material = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
        if (!entity || !mesh || !material || !mesh->SetVertexCount(3) || !mesh->SetFaceCount(1)) return nullptr;
        VxVector positions[3] = {VxVector(-.25f, -.25f, 0), VxVector(0, .25f, 0), VxVector(.25f, -.25f, 0)};
        mesh->SetLitMode(VX_PRELITMESH);
        for (int i = 0; i < 3; ++i) {
            mesh->SetVertexPosition(i, &positions[i]);
            mesh->SetVertexColor(i, 0xffffffffu);
        }
        mesh->SetFaceVertexIndex(0, 0, 1, 2);
        mesh->SetFaceMaterial(0, material);
        mesh->BuildFaceNormals();
        mesh->VertexMove();
        entity->SetCurrentMesh(mesh);
        Device->AddObject(entity);
        Device->AddRemoveSequence(FALSE);
        return entity;
    }
    RCK2dEntity *Sprite(CKBOOL background) {
        RCK2dEntity *sprite = static_cast<RCK2dEntity *>(Context->CreateObject(CKCID_2DENTITY));
        RCKMaterial *material = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
        if (!sprite || !material) return nullptr;
        sprite->ModifyObjectFlags(CK_OBJECT_INTERFACEOBJ, 0);
        sprite->EnableRatioOffset(FALSE);
        sprite->SetRect(VxRect(10, 10, 50, 50));
        sprite->SetMaterial(material);
        sprite->SetBackground(background);
        Device->AddObject(sprite);
        return sprite;
    }
};

TEST_F(RenderContextTraversalTimingTest, TraversalCounterCountsEnteredNodesAcrossFramesAndIgnoresSkippedThreeD) {
    EXPECT_EQ(0u, Device->m_SceneTraversalCalls);
    RCK3dEntity *parent = Box(), *child = Box();
    ASSERT_NE(nullptr, parent);
    ASSERT_NE(nullptr, child);
    ASSERT_TRUE(child->SetParent(reinterpret_cast<CK3dEntity *>(parent), TRUE));
    Device->AddObjectWithHierarchy(parent);
    Device->AddRemoveSequence(FALSE);
    ASSERT_EQ(1, parent->m_SceneGraphNode->m_ChildToBeParsedCount);
    parent->m_SceneGraphNode->NoTestsTraversal(Device, CK_RENDER_DONOTUPDATEEXTENTS);
    EXPECT_EQ(2u, Device->m_SceneTraversalCalls);
    parent->m_SceneGraphNode->NoTestsTraversal(Device, CK_RENDER_DONOTUPDATEEXTENTS);
    EXPECT_EQ(4u, Device->m_SceneTraversalCalls);
    child->Show(CKHIDE);
    Device->AddRemoveSequence(FALSE);
    ASSERT_EQ(0, parent->m_SceneGraphNode->m_ChildToBeParsedCount);
    parent->m_SceneGraphNode->NoTestsTraversal(Device, CK_RENDER_DONOTUPDATEEXTENTS);
    EXPECT_EQ(5u, Device->m_SceneTraversalCalls);
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_SKIP3D));
    EXPECT_EQ(5u, Device->m_SceneTraversalCalls);
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
    EXPECT_EQ(7u, Device->m_SceneTraversalCalls); // Real root and its visible parent.
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
    EXPECT_EQ(9u, Device->m_SceneTraversalCalls);
    parent->Show(CKHIDE);
    Device->AddRemoveSequence(FALSE);
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
    EXPECT_EQ(10u, Device->m_SceneTraversalCalls); // Empty root still entered.
}

TEST_F(RenderContextTraversalTimingTest, NoTestsTraversalAccumulatesTraversalThenRestartsAfterTheRealMeshDraw) {
    RCK3dEntity *entity = Triangle();
    ASSERT_NE(nullptr, entity);
    Boundary.MeshDelay = 25;
    Device->m_Stats.SceneTraversalTime = 17.0f;
    Device->m_SceneTraversalTimeProfiler.Reset();
    WaitForMeasuredWork(20);
    const float idle = Device->m_SceneTraversalTimeProfiler.Current();
    VxTimeProfiler call;
    entity->m_SceneGraphNode->NoTestsTraversal(Device, CK_RENDER_DONOTUPDATEEXTENTS);
    const float afterRender = Device->m_SceneTraversalTimeProfiler.Current();
    const float total = call.Current();
    ASSERT_EQ(1, Boundary.MeshDraws);
    EXPECT_GE(Boundary.MeshWork, 25.0f);
    EXPECT_GE(Boundary.TraversalAtMesh, 17.0f + idle);
    EXPECT_FLOAT_EQ(Boundary.TraversalAtMesh, Device->m_Stats.SceneTraversalTime);
    EXPECT_LE(afterRender, total - Boundary.MeshWork + .5f);
    EXPECT_EQ(1u, Device->m_SceneTraversalCalls);
    RecordProperty("opaque_boundary_work_ms", Boundary.MeshWork);
    RecordProperty("traversal_epoch_after_opaque_ms", afterRender);
}

TEST_F(RenderContextTraversalTimingTest, ObjectFramePublishesTheMeasuredEpochMinusItsActualExclusiveStatistics) {
    RCK3dEntity *entity = Triangle();
    ASSERT_NE(nullptr, entity);
    Boundary.MeshDelay = 20;
    ObjectDelay objectCallback;
    objectCallback.Milliseconds = 15;
    ASSERT_TRUE(entity->AddPreRenderCallBack(DelayObject, &objectCallback, FALSE));
    FrameMeasurements measured;
    Device->AddPreRenderCallBack(BeginObjectMeasurement, &measured, FALSE);
    Device->AddPostRenderCallBack(CaptureRawObjects, &measured, FALSE, FALSE);
    Device->AddPostSpriteRenderCallBack(CaptureBeforeFinalSubtraction, &measured, FALSE);
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
    ASSERT_EQ(1, Boundary.MeshDraws);
    ASSERT_EQ(1, objectCallback.Calls);
    ASSERT_EQ(1, measured.PreCalls);
    ASSERT_EQ(1, measured.ObjectCaptures);
    ASSERT_EQ(1, measured.SpriteCaptures);
    EXPECT_GE(measured.RawObjects, Boundary.MeshWork + objectCallback.Measured);
    EXPECT_LE(measured.RawObjects, measured.ObjectEpoch);
    EXPECT_LE(measured.ObjectEpoch, measured.SincePre + .5f);
    VxStats actual = {};
    Device->GetStats(&actual);
    const VxStats raw = measured.BeforeFinal;
    EXPECT_FLOAT_EQ(measured.RawObjects, raw.ObjectsRenderTime);
    EXPECT_FLOAT_EQ(raw.ObjectsRenderTime - (raw.SceneTraversalTime + raw.TransparentObjectsSortTime + raw.ObjectsCallbacksTime + raw.SkinTime), actual.ObjectsRenderTime);
    EXPECT_GE(actual.ObjectsRenderTime + .5f, Boundary.MeshWork);
    EXPECT_GE(actual.ObjectsCallbacksTime, objectCallback.Measured);
    const CKDWORD traversals = Device->m_SceneTraversalCalls;
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_SKIPDRAWSCENE));
    VxStats skipped = {};
    Device->GetStats(&skipped);
    EXPECT_FLOAT_EQ(actual.ObjectsRenderTime, skipped.ObjectsRenderTime);
    EXPECT_EQ(traversals, Device->m_SceneTraversalCalls);
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_SKIP3D));
    Device->GetStats(&skipped);
    EXPECT_FLOAT_EQ(0, skipped.ObjectsRenderTime);
    EXPECT_FLOAT_EQ(0, skipped.SceneTraversalTime);
    EXPECT_EQ(1, Boundary.MeshDraws);
    RecordProperty("object_raw_ms", raw.ObjectsRenderTime);
    RecordProperty("object_exclusive_ms", actual.ObjectsRenderTime);
    RecordProperty("object_callback_ms", actual.ObjectsCallbacksTime);
}

TEST_F(RenderContextTraversalTimingTest, BackgroundAndForegroundSpriteEpochsAccumulateAndExcludeTheirCallbacks) {
    RCK2dEntity *background = Sprite(TRUE), *foreground = Sprite(FALSE);
    ASSERT_NE(nullptr, background);
    ASSERT_NE(nullptr, foreground);
    ObjectDelay beforeBackground, beforeForeground;
    beforeBackground.Milliseconds = 15;
    beforeForeground.Milliseconds = 20;
    ASSERT_TRUE(background->AddPreRenderCallBack(DelayObject, &beforeBackground, FALSE));
    ASSERT_TRUE(foreground->AddPreRenderCallBack(DelayObject, &beforeForeground, FALSE));
    Boundary.BackgroundDelay = 12;
    Boundary.ForegroundDelay = 18;
    FrameMeasurements measured;
    Device->AddPostSpriteRenderCallBack(CaptureBeforeFinalSubtraction, &measured, FALSE);
    Device->m_SpriteTimeProfiler.Reset();
    WaitForMeasuredWork(25); // Deliberately stale epoch must be replaced by each scene phase.
    const CK_RENDER_FLAGS flags = static_cast<CK_RENDER_FLAGS>(CK_RENDER_SKIP3D | CK_RENDER_BACKGROUNDSPRITES | CK_RENDER_FOREGROUNDSPRITES);
    ASSERT_EQ(CK_OK, Device->DrawScene(flags));
    ASSERT_EQ(1, Boundary.BackgroundDraws);
    ASSERT_EQ(1, Boundary.ForegroundDraws);
    ASSERT_EQ(1, beforeBackground.Calls);
    ASSERT_EQ(1, beforeForeground.Calls);
    ASSERT_EQ(1, measured.SpriteCaptures);
    const float drawWork = Boundary.BackgroundWork + Boundary.ForegroundWork;
    const float callbacks = beforeBackground.Measured + beforeForeground.Measured;
    EXPECT_GE(Boundary.SpriteAtForeground, Boundary.BackgroundWork + beforeBackground.Measured);
    EXPECT_GE(measured.BeforeFinal.SpriteTime, drawWork + callbacks);
    VxStats actual = {};
    Device->GetStats(&actual);
    EXPECT_FLOAT_EQ(measured.BeforeFinal.SpriteTime - measured.BeforeFinal.SpriteCallbacksTime, actual.SpriteTime);
    EXPECT_GE(actual.SpriteCallbacksTime, callbacks);
    EXPECT_GE(actual.SpriteTime + .5f, drawWork);
    EXPECT_FLOAT_EQ(0, actual.ObjectsRenderTime);
    RecordProperty("sprite_raw_ms", measured.BeforeFinal.SpriteTime);
    RecordProperty("sprite_exclusive_ms", actual.SpriteTime);
    RecordProperty("sprite_draw_work_ms", drawWork);
}

TEST_F(RenderContextTraversalTimingTest, TransparentSortRestartsOnlyForAnEnabledMultiObjectSortAndPublishesBeforeRendering) {
    CKSceneGraphRootNode &root = Manager->m_SceneGraphRootNode;
    Device->m_Stats.TransparentObjectsSortTime = 37.0f;
    root.SortTransparentObjects(Device, CK_RENDER_DONOTUPDATEEXTENTS);
    EXPECT_FLOAT_EQ(37, Device->m_Stats.TransparentObjectsSortTime);
    SortMeasurements measured;
    RCK3dEntity *nearEntity = Box(4);
    ASSERT_NE(nullptr, nearEntity);
    ASSERT_TRUE(nearEntity->SetRenderCallBack(CaptureSortedRender, &measured));
    nearEntity->SetRenderAsTransparent(TRUE);
    Device->AddObject(nearEntity);
    Device->AddRemoveSequence(FALSE);
    Manager->m_SortTransparentObjects.Value = FALSE;
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
    ASSERT_EQ(1, root.m_TransparentObjects.Size());
    measured.Calls = 0;
    Device->m_Stats.TransparentObjectsSortTime = 37;
    Device->m_TransparentObjectsSortTimeProfiler.Reset();
    WaitForMeasuredWork(20);
    float before = Device->m_TransparentObjectsSortTimeProfiler.Current();
    Manager->m_SortTransparentObjects.Value = TRUE;
    root.SortTransparentObjects(Device, CK_RENDER_DONOTUPDATEEXTENTS);
    ASSERT_EQ(1, measured.Calls);
    EXPECT_FLOAT_EQ(37, Device->m_Stats.TransparentObjectsSortTime);
    EXPECT_GE(Device->m_TransparentObjectsSortTimeProfiler.Current(), before);

    RCK3dEntity *farEntity = Box(8);
    ASSERT_NE(nullptr, farEntity);
    ASSERT_TRUE(farEntity->SetRenderCallBack(CaptureSortedRender, &measured));
    farEntity->SetRenderAsTransparent(TRUE);
    Device->AddObject(farEntity);
    Device->AddRemoveSequence(FALSE);
    Manager->m_SortTransparentObjects.Value = FALSE;
    measured.Calls = 0;
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
    ASSERT_EQ(2, root.m_TransparentObjects.Size());
    measured.Calls = 0;
    Device->m_Stats.TransparentObjectsSortTime = 41;
    before = Device->m_TransparentObjectsSortTimeProfiler.Current();
    root.SortTransparentObjects(Device, CK_RENDER_DONOTUPDATEEXTENTS);
    ASSERT_EQ(2, measured.Calls);
    EXPECT_FLOAT_EQ(41, Device->m_Stats.TransparentObjectsSortTime);
    EXPECT_GE(Device->m_TransparentObjectsSortTimeProfiler.Current(), before);

    Manager->m_SortTransparentObjects.Value = TRUE;
    measured.Calls = 0;
    measured.Work = 0;
    Device->m_Stats.TransparentObjectsSortTime = 43;
    Device->m_TransparentObjectsSortTimeProfiler.Reset();
    WaitForMeasuredWork(25);
    measured.Call.Reset();
    root.SortTransparentObjects(Device, CK_RENDER_DONOTUPDATEEXTENTS);
    ASSERT_EQ(2, measured.Calls);
    EXPECT_EQ(farEntity->GetID(), measured.Order[0]);
    EXPECT_EQ(nearEntity->GetID(), measured.Order[1]);
    const float published = Device->m_Stats.TransparentObjectsSortTime;
    EXPECT_NE(43, published);
    EXPECT_FLOAT_EQ(published, measured.Published[0]);
    EXPECT_FLOAT_EQ(published, measured.Published[1]);
    EXPECT_LE(published, measured.Epoch[0]);
    EXPECT_LE(measured.Epoch[0], measured.SinceCall[0] + .5f);
    EXPECT_GE(measured.Work, 30.0f);
    EXPECT_LE(published, measured.Call.Current() - measured.Work + .5f);
    RecordProperty("transparent_sort_ms", published);
    RecordProperty("transparent_render_work_ms", measured.Work);
}
} // namespace
