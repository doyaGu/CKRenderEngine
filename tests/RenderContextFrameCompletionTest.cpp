#include <gtest/gtest.h>
#include "CKAll.h"
#include "TextureTestObserver.h"
#include "RCK2dEntity.h"
#include "RCKMaterial.h"
#include "RCKSprite3D.h"
#include "../src/CKRenderDrawAnnotation.h"

namespace {
class FrameBoundary : public TextureTestObserver {
public:
    int Draws = 0, Begins = 0, Ends = 0, Clears = 0, Overlays = 0;
    CKBOOL DrawResult = TRUE, ClearResult = TRUE, RejectNextCullState = FALSE;
    int RejectedMaterialStates = 0;
    CKBOOL BeginResult = TRUE, EndResult = TRUE, OverlayResult = TRUE, PresentResult = TRUE;
    CKERROR DeviceStatus = CK_OK;
    CKDWORD ClearFlags = 0, ClearStencil = 0;
    int RectCount = -1;
    CKRECT Rect = {};
    VxVector4 Points[4];
    Vx2DVector UV[4];
    CKWORD Indices[12] = {};
    int Vertices = 0, IndexCount = 0;
    VXPRIMITIVETYPE Primitive = VX_POINTLIST;
    CKViewportData DrawViewport = {};
    CKDWORD DrawTexture = 0, DrawDepth = 0, DrawBlend = 0, DrawMinFilter = 0, DrawMagFilter = 0;
    CKBOOL Clear(CKDWORD flags, CKDWORD, float, CKDWORD stencil, int count, CKRECT *rects) override {
        ++Clears; ClearFlags = flags; ClearStencil = stencil; RectCount = count;
        if (count) { EXPECT_NE(nullptr, rects); if (rects) Rect = rects[0]; }
        else EXPECT_EQ(nullptr, rects);
        return ClearResult;
    }
    CKBOOL BeginScene() override { ++Begins; return BeginResult; }
    CKBOOL EndScene() override { ++Ends; return EndResult; }
    CKBOOL BeginOverlayPhase() override { ++Overlays; return OverlayResult; }
    void InitDefaultRenderStatesValue() override {}
    CKBOOL EnableLight(CKDWORD, CKBOOL) override { return TRUE; }
    CKBOOL BackToFront(CKBOOL) override { ++PresentCount; return PresentResult; }
    CKERROR GetDeviceStatus() const override { return DeviceStatus; }
    CKBOOL SetRenderState(VXRENDERSTATETYPE state, CKDWORD value) override {
        if (state == VXRENDERSTATE_CULLMODE && RejectNextCullState) {
            RejectNextCullState = FALSE; ++RejectedMaterialStates; return FALSE;
        }
        return TextureTestObserver::SetRenderState(state, value);
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        ++Draws; Primitive = type; IndexCount = count; Vertices = data->VertexCount;
        DrawViewport = Viewport; DrawTexture = Textures[0];
        DrawDepth = States[VXRENDERSTATE_ZENABLE]; DrawBlend = States[VXRENDERSTATE_ALPHABLENDENABLE];
        DrawMinFilter = StageStates[0][CKRST_TSS_MINFILTER]; DrawMagFilter = StageStates[0][CKRST_TSS_MAGFILTER];
        for (int i = 0; i < 4 && i < data->VertexCount; ++i) {
            Points[i] = *reinterpret_cast<VxVector4 *>(static_cast<char *>(data->PositionPtr) + i * data->PositionStride);
            if (data->TexCoordPtr) UV[i] = *reinterpret_cast<Vx2DVector *>(static_cast<char *>(data->TexCoordPtr) + i * data->TexCoordStride);
        }
        for (int i = 0; indices && i < count && i < 12; ++i) Indices[i] = indices[i];
        return DrawResult;
    }
};

// Observe the public frame scheduler at its virtual phase boundaries. The
// actual Clear/DrawScene paths are exercised separately against FrameBoundary.
class FrameScheduleContext : public RCKRenderContext {
public:
    int Count = 0, Phases[4] = {};
    CK_RENDER_FLAGS Seen[4] = {};
    CKERROR ClearError = CK_OK, SceneError = CK_OK, PresentError = CK_OK;
    FrameScheduleContext(CKContext *context, CKRasterizerContext *boundary) : RCKRenderContext(context) {
        m_RasterizerContext = boundary; m_Active = TRUE;
        m_ViewportData.ViewX = 3; m_ViewportData.ViewY = 4;
        m_ViewportData.ViewWidth = 100; m_ViewportData.ViewHeight = 80;
    }
    ~FrameScheduleContext() override { m_RasterizerContext = nullptr; }
    void Note(int phase, CK_RENDER_FLAGS flags) { ASSERT_LT(Count, 4); Phases[Count] = phase; Seen[Count++] = flags; }
    void PrepareCameras(CK_RENDER_FLAGS flags) override { Note(1, flags); }
    CKERROR Clear(CK_RENDER_FLAGS flags, CKDWORD) override { Note(2, flags); return ClearError; }
    CKERROR DrawScene(CK_RENDER_FLAGS flags) override { Note(3, flags); return SceneError; }
    CKERROR BackToFront(CK_RENDER_FLAGS flags) override { Note(4, flags); return PresentError; }
};

class RenderContextFrameCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    FrameBoundary State;
    CKRasterizerDriver Driver;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKSprite3D, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        ASSERT_GT(Manager->m_DriverCount, 0);
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 8;
        Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = 8;
        Manager->m_Drivers[0].Caps3D.CKRasterizerSpecificCaps |= CKRST_SPECIFICCAPS_CLAMPEDGEALPHA;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        VxImageDescEx format; VxPixelFormat2ImageDesc(_32_ARGB8888, format);
        Manager->m_Drivers[0].TextureFormats.PushBack(format);
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        Device = new RCKRenderContext(Context);
        Device->m_RasterizerContext = &State; Device->m_RasterizerDriver = &Driver;
        Device->m_Settings.m_Rect.right = 320; Device->m_Settings.m_Rect.bottom = 200;
        Device->m_ViewportData.ViewX = 10; Device->m_ViewportData.ViewY = 20;
        Device->m_ViewportData.ViewWidth = 100; Device->m_ViewportData.ViewHeight = 80;
        Device->m_Active = TRUE; State.Viewport = Device->m_ViewportData;
        Device->BeginFrameErrorTracking();
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    RCKSprite3D *Sprite(RCKMaterial **material = nullptr) {
        RCKMaterial *mat = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
        RCKSprite3D *sprite = static_cast<RCKSprite3D *>(Context->CreateObject(CKCID_SPRITE3D));
        if (sprite && mat) { sprite->SetMaterial(mat); sprite->SetMode(VXSPRITE3D_ORIENTABLE); sprite->SetPosition3f(0, 0, 5); }
        if (material) *material = mat;
        return sprite;
    }
    RCKTexture *Background() {
        RCKTexture *texture = static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE));
        if (!texture || !texture->CreateImage(2, 2, 32, 0)) return nullptr;
        const CKDWORD pixels[4] = {0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0xFFFFFFFFu};
        CKBYTE *image = texture->LockSurfacePtr(0);
        if (!image) return nullptr;
        memcpy(image, pixels, sizeof(pixels)); texture->ReleaseSurfacePtr(0);
        Device->GetBackgroundMaterial()->SetTexture(0, texture);
        return texture;
    }
};

TEST_F(RenderContextFrameCompletionTest, ClearSelectsFullTargetOrTheUncachedPhysicalViewport) {
    EXPECT_EQ(CK_OK, Device->Clear(CK_RENDER_CLEARZ));
    EXPECT_EQ(1, State.RectCount); EXPECT_EQ(0, State.Rect.left); EXPECT_EQ(0, State.Rect.top);
    EXPECT_EQ(320, State.Rect.right); EXPECT_EQ(200, State.Rect.bottom);
    // A callback can change the physical viewport without updating the cache.
    CKViewportData actual = State.Viewport; actual.ViewX = 31; actual.ViewWidth = 17;
    State.SetViewport(&actual);
    EXPECT_EQ(CK_OK, Device->Clear(static_cast<CK_RENDER_FLAGS>(CK_RENDER_CLEARZ | CK_RENDER_CLEARVIEWPORT)));
    EXPECT_EQ(0, State.RectCount); EXPECT_EQ(31u, State.Viewport.ViewX);
    EXPECT_EQ(17u, State.Viewport.ViewWidth); EXPECT_EQ(10u, Device->m_ViewportData.ViewX);
}

TEST_F(RenderContextFrameCompletionTest, CurrentFlagsResolveHighOnlyInputsAndClearFailuresPropagate) {
    Device->SetCurrentRenderOptions(CK_RENDER_CLEARSTENCIL);
    EXPECT_EQ(CK_OK, Device->Clear(CK_RENDER_PLAYERCONTEXT, 0x55));
    EXPECT_EQ(CKRST_CTXCLEAR_STENCIL, State.ClearFlags); EXPECT_EQ(0x55u, State.ClearStencil);
    EXPECT_EQ(0x55u, Device->m_StencilFreeMask);
    State.ClearResult = FALSE;
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->Clear(CK_RENDER_CLEARBACK));
}

TEST_F(RenderContextFrameCompletionTest, TexturedBackgroundSubmitsOneFullTargetQuadAndRestoresState) {
    ASSERT_NE(nullptr, Background());
    State.States[VXRENDERSTATE_ZENABLE] = TRUE;
    State.States[VXRENDERSTATE_ALPHABLENDENABLE] = TRUE;
    EXPECT_EQ(CK_OK, Device->Clear(static_cast<CK_RENDER_FLAGS>(CK_RENDER_CLEARBACK | CK_RENDER_CLEARZ)));
    EXPECT_EQ(CKRST_CTXCLEAR_DEPTH, State.ClearFlags);
    ASSERT_EQ(1, State.Draws); EXPECT_EQ(1, State.Begins); EXPECT_EQ(1, State.Ends);
    EXPECT_EQ(VX_TRIANGLEFAN, State.Primitive); EXPECT_EQ(4, State.Vertices);
    EXPECT_NE(0u, State.DrawTexture); EXPECT_EQ(0u, State.DrawDepth); EXPECT_EQ(0u, State.DrawBlend);
    EXPECT_EQ(VXTEXTUREFILTER_LINEAR, State.DrawMinFilter); EXPECT_EQ(VXTEXTUREFILTER_LINEAR, State.DrawMagFilter);
    const float x[4] = {0, 320, 320, 0}, y[4] = {0, 0, 200, 200};
    const float u[4] = {0, 1, 1, 0}, v[4] = {0, 0, 1, 1};
    for (int i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(x[i], State.Points[i].x); EXPECT_FLOAT_EQ(y[i], State.Points[i].y);
        EXPECT_FLOAT_EQ(1, State.Points[i].w); EXPECT_FLOAT_EQ(u[i], State.UV[i].x); EXPECT_FLOAT_EQ(v[i], State.UV[i].y);
    }
    EXPECT_EQ(320u, State.DrawViewport.ViewWidth); EXPECT_EQ(100u, State.Viewport.ViewWidth);
    EXPECT_EQ(TRUE, State.States[VXRENDERSTATE_ZENABLE]); EXPECT_EQ(TRUE, State.States[VXRENDERSTATE_ALPHABLENDENABLE]);
}

TEST_F(RenderContextFrameCompletionTest, TexturedViewportClearReusesAnActiveSceneAndReportsRejectedDraw) {
    ASSERT_NE(nullptr, Background());
    Device->m_DrawSceneCalls = 1; State.DrawResult = FALSE;
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->Clear(static_cast<CK_RENDER_FLAGS>(CK_RENDER_CLEARBACK | CK_RENDER_CLEARVIEWPORT)));
    EXPECT_EQ(0, State.Begins); EXPECT_EQ(0, State.Ends); EXPECT_EQ(1, State.Draws);
    EXPECT_EQ(10u, State.DrawViewport.ViewX); EXPECT_EQ(100u, State.DrawViewport.ViewWidth);
    Device->m_DrawSceneCalls = 0;
}

TEST_F(RenderContextFrameCompletionTest, BackgroundTextureBindFailureEndsItsSceneWithoutSubmittingGeometry) {
    ASSERT_NE(nullptr, Background()); State.CreateResult = FALSE;
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->Clear(CK_RENDER_CLEARBACK));
    EXPECT_EQ(1, State.Begins); EXPECT_EQ(1, State.Ends); EXPECT_EQ(0, State.Draws);
}

TEST_F(RenderContextFrameCompletionTest, SpriteBatchCountsSubmittedGeometryOnceAndConsumesTheQueuedQuad) {
    RCKMaterial *material = nullptr; RCKSprite3D *sprite = Sprite(&material);
    ASSERT_NE(nullptr, sprite); ASSERT_NE(nullptr, material);
    Device->AddSprite3DBatch(sprite); Device->CallSprite3DBatches();
    ASSERT_EQ(1, State.Draws); EXPECT_EQ(4, State.Vertices); EXPECT_EQ(6, State.IndexCount);
    const CKWORD indices[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i) EXPECT_EQ(indices[i], State.Indices[i]);
    EXPECT_EQ(1, Device->m_Stats.NbObjectDrawn); EXPECT_EQ(2, Device->m_Stats.NbTrianglesDrawn);
    EXPECT_EQ(4, Device->m_Stats.NbVerticesProcessed); EXPECT_EQ(0, Device->m_Sprite3DBatches.Size());
    EXPECT_EQ(0, material->GetSprite3DBatch()->m_Vertices.Size());
}

TEST_F(RenderContextFrameCompletionTest, SpriteMaterialRejectionSkipsSubmissionAndReportsFrameFailure) {
    RCKMaterial *material = nullptr; RCKSprite3D *sprite = Sprite(&material);
    ASSERT_NE(nullptr, sprite); State.RejectNextCullState = TRUE;
    Device->AddSprite3DBatch(sprite); Device->CallSprite3DBatches();
    EXPECT_EQ(1, State.RejectedMaterialStates);
    EXPECT_EQ(0, State.Draws); EXPECT_EQ(0, Device->m_Stats.NbObjectDrawn);
    EXPECT_EQ(0, Device->m_Stats.NbTrianglesDrawn); EXPECT_EQ(0, Device->m_Sprite3DBatches.Size());
    EXPECT_EQ(0, material->GetSprite3DBatch()->m_Vertices.Size());
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->BackToFront(CK_RENDER_DOBACKTOFRONT));
}

TEST_F(RenderContextFrameCompletionTest, RejectedSpriteGeometryDoesNotCountAsSubmittedObjectsOrTriangles) {
    RCKSprite3D *sprite = Sprite(); ASSERT_NE(nullptr, sprite); State.DrawResult = FALSE;
    Device->AddSprite3DBatch(sprite); Device->CallSprite3DBatches();
    EXPECT_EQ(1, State.Draws); EXPECT_EQ(0, Device->m_Stats.NbObjectDrawn);
    EXPECT_EQ(0, Device->m_Stats.NbTrianglesDrawn); EXPECT_EQ(0, Device->m_Stats.NbVerticesProcessed);
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->BackToFront(CK_RENDER_DOBACKTOFRONT));
}

TEST_F(RenderContextFrameCompletionTest, PresentationPreservesFirstFrameErrorAndNewTrackingStartsClean) {
    Device->RecordFrameRenderError(CKERR_INVALIDSIZE); Device->RecordFrameRenderError(CKERR_INVALIDPARAMETER);
    EXPECT_EQ(CKERR_INVALIDSIZE, Device->BackToFront(CK_RENDER_DOBACKTOFRONT));
    Device->BeginFrameErrorTracking(); EXPECT_EQ(CK_OK, Device->BackToFront(CK_RENDER_DOBACKTOFRONT));
    State.PresentResult = FALSE; State.DeviceStatus = CKERR_INVALIDRENDERCONTEXT;
    EXPECT_EQ(CKERR_INVALIDRENDERCONTEXT, Device->BackToFront(CK_RENDER_DOBACKTOFRONT));
}

TEST_F(RenderContextFrameCompletionTest, DrawAnnotationConsumesExplicitPayloadThenUsesCallbackFallback) {
    Device->ApplyDrawAnnotationDebugFlags(CKRST_DEBUG_DRAWMAP_SUBMITS);
    CKDrawAnnotation pending, actual;
    CKDrawAnnotationInit(&pending, CKDRAW_SOURCE_MESH); pending.IndexCount = 6; pending.VertexCount = 4;
    Device->SetDrawAnnotation(&pending);
    ASSERT_TRUE(Device->ConsumeDrawAnnotation(&actual, VX_POINTLIST, 1, 1));
    EXPECT_EQ(CKDRAW_SOURCE_MESH, actual.Source); EXPECT_EQ(6u, actual.IndexCount); EXPECT_NE(0u, actual.Token);
    CKDrawAnnotationObjectRef object = {}; object.Id = 91;
    Device->SetDrawCallbackObject(&object); CKDrawAnnotationObjectRef read = {};
    ASSERT_TRUE(Device->GetDrawCallbackObject(&read)); EXPECT_EQ(91u, read.Id);
    EXPECT_FALSE(Device->ConsumeDrawAnnotation(&actual, VX_LINELIST, 2, 2));
    EXPECT_EQ(CKDRAW_SOURCE_CALLBACK, actual.Source); EXPECT_EQ(91u, actual.Object.Id);
    EXPECT_EQ(VX_LINELIST, actual.PrimitiveType); EXPECT_EQ(2u, actual.IndexCount);
    Device->ApplyDrawAnnotationDebugFlags(0); EXPECT_FALSE(Device->GetDrawCallbackObject(&read));
}

TEST_F(RenderContextFrameCompletionTest, MonoRenderResolvesFlagsAndCompletesPhasesBeforeRecordingExtents) {
    FrameScheduleContext schedule(Context, &State);
    Context->GetTimeManager()->ChangeLimitOptions(CK_FRAMERATE_SYNC);
    schedule.SetCurrentRenderOptions(CK_RENDER_DOBACKTOFRONT | CK_RENDER_DONOTUPDATEEXTENTS);
    EXPECT_EQ(CK_OK, schedule.Render(CK_RENDER_PLAYERCONTEXT));
    ASSERT_EQ(4, schedule.Count);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(i + 1, schedule.Phases[i]);
        EXPECT_EQ(CK_RENDER_DOBACKTOFRONT | CK_RENDER_DONOTUPDATEEXTENTS | CK_RENDER_WAITVBL,
                  schedule.Seen[i]);
    }
    ASSERT_EQ(1, schedule.m_Extents.Size());
    EXPECT_FLOAT_EQ(3, schedule.m_Extents[0].m_Rect.left);
    EXPECT_FLOAT_EQ(103, schedule.m_Extents[0].m_Rect.right);
    EXPECT_EQ(0u, schedule.m_Extents[0].m_Camera);
    EXPECT_EQ(1u, schedule.m_TimeFpsCalc);
}

TEST_F(RenderContextFrameCompletionTest, MonoRenderStopsAtEachFailedPhaseWithoutRecordingCompletedExtents) {
    FrameScheduleContext schedule(Context, &State);
    const CK_RENDER_FLAGS flags = static_cast<CK_RENDER_FLAGS>(CK_RENDER_DOBACKTOFRONT | CK_RENDER_DONOTUPDATEEXTENTS);
    schedule.ClearError = CKERR_INVALIDPARAMETER;
    EXPECT_EQ(CKERR_INVALIDPARAMETER, schedule.Render(flags)); EXPECT_EQ(2, schedule.Count);
    EXPECT_EQ(0u, schedule.m_TimeFpsCalc); EXPECT_EQ(0, schedule.m_Extents.Size());
    schedule.Count = 0; schedule.ClearError = CK_OK; schedule.SceneError = CKERR_INVALIDSIZE;
    EXPECT_EQ(CKERR_INVALIDSIZE, schedule.Render(flags)); EXPECT_EQ(3, schedule.Count);
    EXPECT_EQ(0u, schedule.m_TimeFpsCalc); EXPECT_EQ(0, schedule.m_Extents.Size());
    schedule.Count = 0; schedule.SceneError = CK_OK; schedule.PresentError = CKERR_INVALIDOPERATION;
    EXPECT_EQ(CKERR_INVALIDOPERATION, schedule.Render(flags)); EXPECT_EQ(4, schedule.Count);
    EXPECT_EQ(1u, schedule.m_TimeFpsCalc); EXPECT_EQ(0, schedule.m_Extents.Size());
}

TEST_F(RenderContextFrameCompletionTest, InactiveMissingDeviceAndEmptyViewportNeverEnterFramePhases) {
    FrameScheduleContext schedule(Context, &State);
    schedule.m_Active = FALSE;
    EXPECT_EQ(CKERR_RENDERCONTEXTINACTIVE, schedule.Render(CK_RENDER_DOBACKTOFRONT));
    schedule.m_Active = TRUE; schedule.m_RasterizerContext = nullptr;
    EXPECT_EQ(CKERR_INVALIDRENDERCONTEXT, schedule.Render(CK_RENDER_DOBACKTOFRONT));
    schedule.m_RasterizerContext = &State; schedule.m_ViewportData.ViewWidth = 0;
    EXPECT_EQ(CK_OK, schedule.Render(CK_RENDER_DOBACKTOFRONT)); EXPECT_EQ(0, schedule.Count);
}

TEST_F(RenderContextFrameCompletionTest, DrawSceneSkipPreservesStateAndBeginFailureBalancesRecursionAndExtents) {
    Device->m_Stats.NbObjectDrawn = 17;
    CKObjectExtents extent = {}; Device->m_ObjectExtents.PushBack(extent);
    Device->SetCurrentRenderOptions(CK_RENDER_SKIPDRAWSCENE);
    EXPECT_EQ(CK_OK, Device->DrawScene(CK_RENDER_PLAYERCONTEXT));
    EXPECT_EQ(17, Device->m_Stats.NbObjectDrawn); EXPECT_EQ(1, Device->m_ObjectExtents.Size());
    EXPECT_EQ(0, State.Begins); EXPECT_EQ(0u, Device->m_DrawSceneCalls);
    State.BeginResult = FALSE;
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->DrawScene(CK_RENDER_SKIP3D));
    EXPECT_EQ(0, Device->m_Stats.NbObjectDrawn); EXPECT_EQ(0, Device->m_ObjectExtents.Size());
    EXPECT_EQ(1, State.Begins); EXPECT_EQ(0, State.Ends); EXPECT_EQ(0u, Device->m_DrawSceneCalls);
    Device->m_ObjectExtents.PushBack(extent);
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->DrawScene(static_cast<CK_RENDER_FLAGS>(CK_RENDER_SKIP3D | CK_RENDER_DONOTUPDATEEXTENTS)));
    EXPECT_EQ(1, Device->m_ObjectExtents.Size()); EXPECT_EQ(0u, Device->m_DrawSceneCalls);
}

struct FrameCallbackCount { int Calls = 0; };
void CountFrameCallback(CKRenderContext *, void *argument) {
    ++static_cast<FrameCallbackCount *>(argument)->Calls;
}

TEST_F(RenderContextFrameCompletionTest, DrawSceneEndFailureStopsOverlayAndFinalCallbacksAndPropagatesDeviceStatus) {
    FrameCallbackCount final; Device->AddPostSpriteRenderCallBack(CountFrameCallback, &final);
    State.EndResult = FALSE; State.DeviceStatus = CKERR_INVALIDRENDERCONTEXT;
    EXPECT_EQ(CKERR_INVALIDRENDERCONTEXT, Device->DrawScene(CK_RENDER_SKIP3D));
    EXPECT_EQ(1, State.Begins); EXPECT_EQ(1, State.Ends); EXPECT_EQ(0, State.Overlays);
    EXPECT_EQ(0, final.Calls); EXPECT_EQ(0u, Device->m_DrawSceneCalls);
    EXPECT_EQ(CKERR_INVALIDRENDERCONTEXT, Device->BackToFront(CK_RENDER_DOBACKTOFRONT));
    Device->RemovePostSpriteRenderCallBack(CountFrameCallback, &final);
}

TEST_F(RenderContextFrameCompletionTest, DrawSceneOverlayFailureStopsFinalCallbacksAfterClosingScene) {
    FrameCallbackCount final; Device->AddPostSpriteRenderCallBack(CountFrameCallback, &final);
    State.OverlayResult = FALSE;
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->DrawScene(CK_RENDER_SKIP3D));
    EXPECT_EQ(1, State.Begins); EXPECT_EQ(1, State.Ends); EXPECT_EQ(1, State.Overlays);
    EXPECT_EQ(0, final.Calls); EXPECT_EQ(0u, Device->m_DrawSceneCalls);
    State.OverlayResult = TRUE;
    EXPECT_EQ(CK_OK, Device->DrawScene(CK_RENDER_SKIP3D));
    EXPECT_EQ(2, State.Begins); EXPECT_EQ(2, State.Ends); EXPECT_EQ(2, State.Overlays);
    EXPECT_EQ(1, final.Calls); EXPECT_EQ(CK_OK, Device->BackToFront(CK_RENDER_DOBACKTOFRONT));
    Device->RemovePostSpriteRenderCallBack(CountFrameCallback, &final);
}
} // namespace
