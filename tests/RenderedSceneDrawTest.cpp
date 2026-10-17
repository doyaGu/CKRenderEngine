#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "CKBaseManager.h"
#include "CKRenderedScene.h"
#include "RCK2dEntity.h"
#include "RCK3dEntity.h"
#include "RCKCamera.h"
#include "RCKLight.h"
#include "RCKMaterial.h"
#include "RCKMesh.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();
extern int g_FogProjectionMode;
extern int g_UpdateTransparency;

namespace {
enum DrawEvent {
    Begin = 1, Background, PreCallback, PreManager, Opaque, BeforeTransparent,
    Transparent, PostCallback, PostManager, End, Overlay, Foreground,
    PostSpriteManager, FinalCallback
};
struct DrawTrace {
    int Count = 0, Events[128] = {};
    void Add(int event) {
        EXPECT_LT(Count, 128);
        if (Count < 128) Events[Count++] = event;
    }
    void Expect(const int *events, int count) const {
        ASSERT_EQ(count, Count);
        for (int i = 0; i < count; ++i) {
            SCOPED_TRACE(i);
            EXPECT_EQ(events[i], Events[i]);
        }
    }
};
struct CallbackEvent { DrawTrace *Trace; int Event; };
void TraceCallback(CKRenderContext *, void *argument) {
    CallbackEvent *event = static_cast<CallbackEvent *>(argument);
    event->Trace->Add(event->Event);
}

class DrawOrderManager : public CKBaseManager {
public:
    DrawTrace &Trace;
    DrawOrderManager(CKContext *context, DrawTrace &trace)
        : CKBaseManager(context, CKGUID(0x132dcc01, 0x48192aa1), "Draw order observer"), Trace(trace) {
        EXPECT_EQ(CK_OK, context->RegisterNewManager(this));
    }
    CKDWORD GetValidFunctionsMask() override {
        return CKMANAGER_FUNC_OnPreRender | CKMANAGER_FUNC_OnPostRender | CKMANAGER_FUNC_OnPostSpriteRender;
    }
    CKERROR OnPreRender(CKRenderContext *) override { Trace.Add(PreManager); return CK_OK; }
    CKERROR OnPostRender(CKRenderContext *) override { Trace.Add(PostManager); return CK_OK; }
    CKERROR OnPostSpriteRender(CKRenderContext *) override { Trace.Add(PostSpriteManager); return CK_OK; }
};

// Real engine scene/object/material paths run normally. This observer records
// only their public device boundary; it does not implement a GPU or pipeline.
class SceneDrawBoundary : public MaterialRenderObserver {
public:
    DrawTrace *Trace = nullptr;
    RCKRenderContext *Device = nullptr;
    CKBOOL BeginResult = TRUE, EndResult = TRUE, OverlayResult = TRUE;
    CKERROR DeviceStatus = CK_OK;
    CKBOOL InOverlay = FALSE;
    int Begins = 0, Ends = 0, Overlays = 0, Draws = 0;
    int LightSets = 0, LightChanges = 0;
    CKLightData Lights[CKRST_MAX_LIGHTS];
    CKDWORD SetSlots[64] = {}, ChangeSlots[128] = {};
    CKBOOL ChangeEnabled[128] = {};
    CKDWORD DrawShade[32] = {}, DrawFill[32] = {}, DrawSpecular[32] = {};
    CK_ID DrawEntity[32] = {};
    CKViewportData DrawViewport[32] = {};
    CKBOOL Clear(CKDWORD, CKDWORD, float, CKDWORD, int, CKRECT *) override { return TRUE; }
    CKERROR GetDeviceStatus() const override { return DeviceStatus; }
    CKBOOL BeginScene() override {
        ++Begins; InOverlay = FALSE;
        if (Trace) Trace->Add(Begin);
        return BeginResult;
    }
    CKBOOL EndScene() override {
        ++Ends;
        if (Trace) Trace->Add(End);
        return EndResult;
    }
    CKBOOL BeginOverlayPhase() override {
        ++Overlays; InOverlay = TRUE;
        if (Trace) Trace->Add(Overlay);
        return OverlayResult;
    }
    void InitDefaultRenderStatesValue() override {}
    CKBOOL SetLight(CKDWORD slot, const CKLightData *data) override {
        EXPECT_LT(slot, static_cast<CKDWORD>(CKRST_MAX_LIGHTS));
        EXPECT_LT(LightSets, 64);
        if (slot >= CKRST_MAX_LIGHTS || LightSets >= 64) return FALSE;
        Lights[slot] = *data; SetSlots[LightSets++] = slot;
        return TRUE;
    }
    CKBOOL EnableLight(CKDWORD slot, CKBOOL enabled) override {
        EXPECT_LT(LightChanges, 128);
        if (LightChanges >= 128) return FALSE;
        ChangeSlots[LightChanges] = slot; ChangeEnabled[LightChanges++] = enabled;
        return TRUE;
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE, CKWORD *, int count, VxDrawPrimitiveData *data) override {
        EXPECT_NE(nullptr, data); EXPECT_GT(count, 0); EXPECT_LT(Draws, 32);
        if (!data || Draws >= 32) return FALSE;
        EXPECT_NE(nullptr, data->PositionPtr);
        DrawShade[Draws] = States[VXRENDERSTATE_SHADEMODE];
        DrawFill[Draws] = States[VXRENDERSTATE_FILLMODE];
        DrawSpecular[Draws] = States[VXRENDERSTATE_SPECULARENABLE];
        DrawViewport[Draws] = Viewport;
        DrawEntity[Draws] = Device && Device->m_Current3dEntity ? Device->m_Current3dEntity->GetID() : 0;
        ++Draws;
        if (Trace) {
            if (Device && Device->m_Current3dEntity)
                Trace->Add(Device->m_SortTransparentObjects ? Transparent : Opaque);
            else
                Trace->Add(InOverlay ? Foreground : Background);
        }
        return TRUE;
    }
};

class RenderedSceneDrawTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    SceneDrawBoundary Boundary;
    CKRasterizerDriver Driver;
    DrawTrace Trace;
    int SavedFogProjection = 0;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        ASSERT_GT(Manager->m_DriverCount, 0);
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 8;
        Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = 8;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&Boundary, 0));
        Device = static_cast<RCKRenderContext *>(Context->CreateObject(CKCID_RENDERCONTEXT));
        ASSERT_NE(nullptr, Device);
        Device->DetachAll();
        Device->m_RasterizerContext = &Boundary;
        Device->m_RasterizerDriver = &Driver;
        Boundary.Device = Device;
        Device->m_Settings.m_Rect = {0, 0, 100, 100};
        Device->m_ViewportData.ViewWidth = Device->m_ViewportData.ViewHeight = 100;
        Device->m_ViewportData.ViewZMin = 0; Device->m_ViewportData.ViewZMax = 1;
        Boundary.Viewport = Device->m_ViewportData;
        Device->m_Fov = PI / 2; Device->m_NearPlane = 1; Device->m_FarPlane = 100;
        Context->m_BehaviorContext.CurrentRenderContext = Device;
        Device->Get2dRoot(TRUE)->SetRect(VxRect(0, 0, 100, 100));
        Device->Get2dRoot(FALSE)->SetRect(VxRect(0, 0, 100, 100));
        SavedFogProjection = g_FogProjectionMode;
        g_FogProjectionMode = 0;
    }
    void TearDown() override {
        g_FogProjectionMode = SavedFogProjection;
        if (Context) Context->m_BehaviorContext.CurrentRenderContext = nullptr;
        if (Device) {
            Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr;
        }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    RCK3dEntity *Triangle(float depth, CKBOOL transparent, RCKMaterial **output = nullptr, CKBOOL lit = FALSE) {
        RCK3dEntity *entity = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
        RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
        RCKMaterial *material = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
        if (!entity || !mesh || !material || !mesh->SetVertexCount(3) || !mesh->SetFaceCount(1)) return nullptr;
        mesh->SetLitMode(lit ? VX_LITMESH : VX_PRELITMESH);
        VxVector points[3] = {VxVector(-.25f, -.25f, 0), VxVector(0, .25f, 0), VxVector(.25f, -.25f, 0)};
        VxVector normal(0, 0, -1);
        for (int i = 0; i < 3; ++i) {
            mesh->SetVertexPosition(i, &points[i]);
            if (lit) mesh->SetVertexNormal(i, &normal);
            else mesh->SetVertexColor(i, 0xffffffffu);
        }
        material->SetShadeMode(VXSHADE_GOURAUD); material->SetFillMode(VXFILL_SOLID);
        mesh->SetFaceVertexIndex(0, 0, 1, 2); mesh->SetFaceMaterial(0, material);
        mesh->BuildFaceNormals(); mesh->VertexMove();
        entity->SetCurrentMesh(mesh); entity->SetPosition3f(0, 0, depth); entity->UpdateBox(TRUE);
        entity->SetRenderAsTransparent(transparent);
        Device->AddObject(entity); Device->AddRemoveSequence(FALSE);
        if (output) *output = material;
        return entity;
    }
    RCK2dEntity *Sprite(CKBOOL background) {
        RCK2dEntity *sprite = static_cast<RCK2dEntity *>(Context->CreateObject(CKCID_2DENTITY));
        RCKMaterial *material = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
        if (!sprite || !material) return nullptr;
        sprite->ModifyObjectFlags(CK_OBJECT_INTERFACEOBJ, 0);
        sprite->EnableRatioOffset(FALSE); sprite->SetRect(VxRect(10, 10, 40, 40));
        sprite->SetMaterial(material); sprite->SetBackground(background);
        Device->AddObject(sprite);
        return sprite;
    }
    RCKLight *Light(float x, CKBOOL active = TRUE) {
        RCKLight *light = static_cast<RCKLight *>(Context->CreateObject(CKCID_LIGHT));
        if (!light) return nullptr;
        light->SetPosition3f(x, 2, 3); light->SetType(VX_LIGHTPOINT);
        light->SetConstantAttenuation(1); light->Active(active);
        Device->AddObject(light);
        return light;
    }
    static CKDWORD Bits(float value) { CKDWORD result; std::memcpy(&result, &value, sizeof(result)); return result; }
    static void Matrix(const VxMatrix &actual, const VxMatrix &expected) {
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                EXPECT_NEAR(expected[row][column], actual[row][column], 1e-5f);
    }
};

TEST_F(RenderedSceneDrawTest, DefaultStatesHonorTheExplicitDeviceAndKeepNullAsTheContextFallback) {
    SceneDrawBoundary other;
    Device->SetFogMode(VXFOG_NONE);
    Device->SetGlobalRenderMode(FlatShading, TRUE, FALSE);
    Device->m_RenderedScene->SetDefaultRenderStates(&other);
    EXPECT_EQ(0, Boundary.StateWrites);
    EXPECT_EQ(TRUE, other.States[VXRENDERSTATE_ZENABLE]);
    EXPECT_EQ(VXCULL_CCW, other.States[VXRENDERSTATE_CULLMODE]);
    EXPECT_EQ(VXCMP_LESSEQUAL, other.States[VXRENDERSTATE_ZFUNC]);
    EXPECT_EQ(VXSHADE_FLAT, other.States[VXRENDERSTATE_SHADEMODE]);
    Device->m_RenderedScene->SetDefaultRenderStates(nullptr);
    EXPECT_EQ(TRUE, Boundary.States[VXRENDERSTATE_ZENABLE]);
    EXPECT_EQ(VXSHADE_FLAT, Boundary.States[VXRENDERSTATE_SHADEMODE]);
}

TEST_F(RenderedSceneDrawTest, LightSetupUsesItsDeviceCompactsEligibleLightsAndDisablesThePreviousSlots) {
    SceneDrawBoundary other;
    RCKLight *first = Light(7), *inactive = Light(20, FALSE), *second = Light(11), *hidden = Light(30);
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, inactive); ASSERT_NE(nullptr, second); ASSERT_NE(nullptr, hidden);
    hidden->Show(CKHIDE);
    Device->SetAmbientLight(0xff123456u);
    Device->m_RenderedScene->SetupLights(&other);
    EXPECT_EQ(0, Boundary.LightSets);
    ASSERT_EQ(2, other.LightSets); ASSERT_EQ(2, other.LightChanges);
    EXPECT_EQ(0u, other.SetSlots[0]); EXPECT_EQ(1u, other.SetSlots[1]);
    EXPECT_FLOAT_EQ(7, other.Lights[0].Position.x); EXPECT_FLOAT_EQ(11, other.Lights[1].Position.x);
    EXPECT_EQ(TRUE, other.ChangeEnabled[0]); EXPECT_EQ(TRUE, other.ChangeEnabled[1]);
    EXPECT_EQ(0xff123456u, other.States[VXRENDERSTATE_AMBIENT]);
    first->Active(FALSE);
    other.LightSets = other.LightChanges = 0;
    Device->m_RenderedScene->SetupLights(&other);
    ASSERT_EQ(1, other.LightSets); ASSERT_EQ(3, other.LightChanges);
    EXPECT_EQ(0u, other.ChangeSlots[0]); EXPECT_EQ(FALSE, other.ChangeEnabled[0]);
    EXPECT_EQ(1u, other.ChangeSlots[1]); EXPECT_EQ(FALSE, other.ChangeEnabled[1]);
    EXPECT_EQ(0u, other.SetSlots[0]); EXPECT_FLOAT_EQ(11, other.Lights[0].Position.x);
    EXPECT_EQ(0u, other.ChangeSlots[2]); EXPECT_EQ(TRUE, other.ChangeEnabled[2]);
    second->Active(FALSE);
    Boundary.LightChanges = Boundary.LightSets = 0;
    Device->m_RenderedScene->SetupLights(nullptr);
    EXPECT_EQ(0, Boundary.LightSets); ASSERT_EQ(1, Boundary.LightChanges);
    EXPECT_EQ(0u, Boundary.ChangeSlots[0]); EXPECT_EQ(FALSE, Boundary.ChangeEnabled[0]);
    EXPECT_EQ(0xff123456u, Boundary.States[VXRENDERSTATE_AMBIENT]);
}

TEST_F(RenderedSceneDrawTest, FogCapabilityFallbackAndAllProjectionModesPublishTheActualFloatPayloads) {
    Device->SetFogMode(VXFOG_EXP2); Device->SetFogStart(2); Device->SetFogEnd(8);
    Device->SetFogDensity(.125f); Device->SetFogColor(0xff224466u);
    VxMatrix projection = VxMatrix::Identity();
    projection[2][2] = 2; projection[3][2] = 3;
    projection[2][3] = .25f; projection[3][3] = 2;
    Device->SetProjectionTransformationMatrix(projection);
    Manager->m_Drivers[0].Caps3D.RasterCaps = CKRST_RASTERCAPS_FOGRANGE | CKRST_RASTERCAPS_FOGPIXEL;
    const float starts[3] = {2, 7.0f / 2.5f, 1.0f / 2.5f};
    const float ends[3] = {8, 19.0f / 4, 7.0f / 2.5f};
    for (int mode = 0; mode < 3; ++mode) {
        SCOPED_TRACE(mode); g_FogProjectionMode = mode;
        Device->m_RenderedScene->SetDefaultRenderStates(nullptr);
        EXPECT_EQ(TRUE, Boundary.States[VXRENDERSTATE_FOGENABLE]);
        EXPECT_EQ(VXFOG_EXP2, Boundary.States[VXRENDERSTATE_FOGPIXELMODE]);
        EXPECT_EQ(Bits(starts[mode]), Boundary.States[VXRENDERSTATE_FOGSTART]);
        EXPECT_EQ(Bits(ends[mode]), Boundary.States[VXRENDERSTATE_FOGEND]);
        EXPECT_EQ(Bits(.125f), Boundary.States[VXRENDERSTATE_FOGDENSITY]);
        EXPECT_EQ(0xff224466u, Boundary.States[VXRENDERSTATE_FOGCOLOR]);
    }
    Manager->m_Drivers[0].Caps3D.RasterCaps = CKRST_RASTERCAPS_FOGPIXEL;
    Device->m_RenderedScene->SetDefaultRenderStates(nullptr);
    EXPECT_EQ(VXFOG_LINEAR, Boundary.States[VXRENDERSTATE_FOGVERTEXMODE]);
    EXPECT_EQ(VXFOG_EXP2, Device->GetFogMode()); // Requested scene value is not permanently rewritten by a fallback.
    Manager->m_Drivers[0].Caps3D.RasterCaps |= CKRST_RASTERCAPS_FOGRANGE;
    Manager->m_ForceLinearFog.Value = TRUE;
    Device->m_RenderedScene->SetDefaultRenderStates(nullptr);
    EXPECT_EQ(VXFOG_LINEAR, Boundary.States[VXRENDERSTATE_FOGPIXELMODE]);
    EXPECT_EQ(VXFOG_EXP2, Device->GetFogMode());
    Device->SetFogMode(VXFOG_NONE);
    Device->m_RenderedScene->SetDefaultRenderStates(nullptr);
    EXPECT_EQ(FALSE, Boundary.States[VXRENDERSTATE_FOGENABLE]);
}

TEST_F(RenderedSceneDrawTest, GlobalFlatAndWireframeSurviveTheActualMaterialSubmissionAndReleaseOnTheNextFrame) {
    ASSERT_NE(nullptr, Triangle(5, FALSE));
    const VxShadeType modes[3] = {FlatShading, static_cast<VxShadeType>(0), GouraudShading};
    const CKDWORD shade[3] = {VXSHADE_FLAT, VXSHADE_GOURAUD, VXSHADE_GOURAUD};
    const CKDWORD fill[3] = {VXFILL_SOLID, VXFILL_WIREFRAME, VXFILL_SOLID};
    for (int i = 0; i < 3; ++i) {
        SCOPED_TRACE(i); Boundary.Draws = 0;
        Device->SetGlobalRenderMode(modes[i], TRUE, FALSE);
        ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
        ASSERT_EQ(1, Boundary.Draws);
        EXPECT_EQ(shade[i], Boundary.DrawShade[0]);
        EXPECT_EQ(fill[i], Boundary.DrawFill[0]);
    }
}

TEST_F(RenderedSceneDrawTest, DisableSpecularSurvivesALitMaterialAndCanBeReleasedForTheNextFrame) {
    RCKMaterial *material = nullptr;
    ASSERT_NE(nullptr, Triangle(5, FALSE, &material, TRUE));
    ASSERT_NE(nullptr, material); material->SetPower(8);
    material->SetSpecular(VxColor(1, 1, 1, 1));
    Manager->m_DisableSpecular.Value = TRUE;
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
    ASSERT_EQ(1, Boundary.Draws); EXPECT_EQ(FALSE, Boundary.DrawSpecular[0]);
    Manager->m_DisableSpecular.Value = FALSE;
    Boundary.Draws = 0;
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
    ASSERT_EQ(1, Boundary.Draws); EXPECT_EQ(TRUE, Boundary.DrawSpecular[0]);
}

TEST_F(RenderedSceneDrawTest, ActualSceneDrawOrdersSpritesLightsManagersCallbacksAndBothGeometryPasses) {
    ASSERT_NE(nullptr, Sprite(TRUE)); ASSERT_NE(nullptr, Sprite(FALSE));
    RCK3dEntity *opaque = Triangle(4, FALSE), *transparent = Triangle(8, TRUE);
    ASSERT_NE(nullptr, opaque); ASSERT_NE(nullptr, transparent); ASSERT_NE(nullptr, Light(7));
    new DrawOrderManager(Context, Trace);
    CallbackEvent pre = {&Trace, PreCallback}, before = {&Trace, BeforeTransparent};
    CallbackEvent post = {&Trace, PostCallback}, final = {&Trace, FinalCallback};
    Device->AddPreRenderCallBack(TraceCallback, &pre, FALSE);
    Device->AddPostRenderCallBack(TraceCallback, &before, FALSE, TRUE);
    Device->AddPostRenderCallBack(TraceCallback, &post, FALSE, FALSE);
    Device->AddPostSpriteRenderCallBack(TraceCallback, &final, FALSE);
    Boundary.Trace = &Trace;
    const CK_RENDER_FLAGS flags = static_cast<CK_RENDER_FLAGS>(CK_RENDER_DONOTUPDATEEXTENTS | CK_RENDER_BACKGROUNDSPRITES | CK_RENDER_FOREGROUNDSPRITES);
    ASSERT_EQ(CK_OK, Device->DrawScene(flags));
    const int expected[] = {Begin, Background, PreCallback, PreManager, Opaque, BeforeTransparent, Transparent, PostCallback, PostManager, End, Overlay, Foreground, PostSpriteManager, FinalCallback};
    Trace.Expect(expected, sizeof(expected) / sizeof(expected[0]));
    ASSERT_EQ(4, Boundary.Draws); EXPECT_EQ(1, Boundary.LightSets);
    EXPECT_EQ(opaque->GetID(), Boundary.DrawEntity[1]);
    EXPECT_EQ(transparent->GetID(), Boundary.DrawEntity[2]);
    EXPECT_EQ(0u, Device->m_DrawSceneCalls); EXPECT_FALSE(Device->m_SortTransparentObjects);
    EXPECT_EQ(FALSE, g_UpdateTransparency);
    Trace.Count = Boundary.Draws = 0;
    const int previousLights = Boundary.LightSets;
    ASSERT_EQ(CK_OK, Device->DrawScene(static_cast<CK_RENDER_FLAGS>(flags | CK_RENDER_SKIP3D)));
    const int skipped[] = {Begin, Background, End, Overlay, Foreground, PostSpriteManager, FinalCallback};
    Trace.Expect(skipped, sizeof(skipped) / sizeof(skipped[0]));
    EXPECT_EQ(2, Boundary.Draws); EXPECT_EQ(previousLights, Boundary.LightSets);
    Device->RemovePreRenderCallBack(TraceCallback, &pre);
    Device->RemovePostRenderCallBack(TraceCallback, &before);
    Device->RemovePostRenderCallBack(TraceCallback, &post);
    Device->RemovePostSpriteRenderCallBack(TraceCallback, &final);
}

TEST_F(RenderedSceneDrawTest, MasterPlaneAttributePreservesTheEyeAndPublishesOffAxisProjectionThenRestoresNormalView) {
    RCKCamera *eye = static_cast<RCKCamera *>(Context->CreateObject(CKCID_CAMERA));
    RCKCamera *plane = static_cast<RCKCamera *>(Context->CreateObject(CKCID_CAMERA));
    ASSERT_NE(nullptr, eye); ASSERT_NE(nullptr, plane);
    eye->SetPosition3f(1, .5f, 0); eye->SetFov(PI / 2);
    eye->SetFrontPlane(1); eye->SetBackPlane(100);
    plane->SetPosition3f(0, 0, 10);
    VxBbox bounds; bounds.Min.Set(-2, -1, 0); bounds.Max.Set(2, 1, 0);
    ASSERT_TRUE(plane->SetBoundingBox(&bounds, TRUE));
    const CKAttributeType attribute = Context->GetAttributeManager()->RegisterNewAttributeType(
        "1CamPl8ne4SterCube2Rend", CKPGUID_CAMERA, CKCID_CAMERA);
    ASSERT_GE(attribute, 0); ASSERT_TRUE(eye->SetAttribute(attribute));
    CKParameterOut *parameter = eye->GetAttributeParameter(attribute); ASSERT_NE(nullptr, parameter);
    const CK_ID planeId = plane->GetID(); ASSERT_EQ(CK_OK, parameter->SetValue(&planeId));
    Device->AttachViewpointToCamera(reinterpret_cast<CKCamera *>(eye));
    Device->Activate(TRUE);
    Context->GetTimeManager()->ChangeLimitOptions(CK_FRAMERATE_FREE);
    const CKViewportData viewport = Device->m_ViewportData;
    ASSERT_EQ(CK_OK, Device->Render(CK_RENDER_DONOTUPDATEEXTENTS));
    EXPECT_EQ(plane, Device->m_Camera);
    EXPECT_FLOAT_EQ(1, Device->GetViewpoint()->GetWorldMatrix()[3].x);
    EXPECT_FLOAT_EQ(.5f, Device->GetViewpoint()->GetWorldMatrix()[3].y);
    EXPECT_FLOAT_EQ(0, Device->GetViewpoint()->GetWorldMatrix()[3].z);
    EXPECT_FLOAT_EQ(-1, Boundary.Matrices[VXMATRIX_VIEW][3].x);
    EXPECT_FLOAT_EQ(-.5f, Boundary.Matrices[VXMATRIX_VIEW][3].y);
    const VxMatrix &projected = Boundary.Matrices[VXMATRIX_PROJECTION];
    EXPECT_NEAR(5, projected[0][0], 1e-5f); EXPECT_NEAR(10, projected[1][1], 1e-5f);
    EXPECT_NEAR(.5f, projected[2][0], 1e-5f); EXPECT_NEAR(.5f, projected[2][1], 1e-5f);
    EXPECT_NEAR(100.0f / 99, projected[2][2], 1e-5f);
    EXPECT_NEAR(-100.0f / 99, projected[3][2], 1e-5f);
    EXPECT_FLOAT_EQ(1, projected[2][3]); EXPECT_FLOAT_EQ(0, projected[3][3]);
    EXPECT_EQ(viewport.ViewX, Boundary.Viewport.ViewX); EXPECT_EQ(viewport.ViewY, Boundary.Viewport.ViewY);
    EXPECT_EQ(viewport.ViewWidth, Boundary.Viewport.ViewWidth); EXPECT_EQ(viewport.ViewHeight, Boundary.Viewport.ViewHeight);
    VxMatrix rotated = VxMatrix::Identity();
    rotated[0].Set(0, 0, -1, 0); rotated[2].Set(1, 0, 0, 0); rotated[3].Set(10, 0, 0, 1);
    plane->SetWorldMatrix(rotated);
    eye->SetPosition3f(0, .5f, -1);
    ASSERT_EQ(CK_OK, Device->Render(CK_RENDER_DONOTUPDATEEXTENTS));
    EXPECT_FLOAT_EQ(0, Device->GetViewpoint()->GetWorldMatrix()[3].x);
    EXPECT_FLOAT_EQ(.5f, Device->GetViewpoint()->GetWorldMatrix()[3].y);
    EXPECT_FLOAT_EQ(-1, Device->GetViewpoint()->GetWorldMatrix()[3].z);
    EXPECT_FLOAT_EQ(-1, Device->GetViewpoint()->GetWorldMatrix()[0].z);
    EXPECT_FLOAT_EQ(1, Device->GetViewpoint()->GetWorldMatrix()[2].x);
    EXPECT_FLOAT_EQ(1, Boundary.Matrices[VXMATRIX_VIEW][0][2]);
    EXPECT_FLOAT_EQ(-1, Boundary.Matrices[VXMATRIX_VIEW][2][0]);
    EXPECT_NEAR(5, Boundary.Matrices[VXMATRIX_PROJECTION][0][0], 1e-5f);
    EXPECT_NEAR(.5f, Boundary.Matrices[VXMATRIX_PROJECTION][2][0], 1e-5f);
    ASSERT_TRUE(eye->RemoveAttribute(attribute));
    ASSERT_EQ(CK_OK, Context->DestroyObject(plane));
    // This actual deletion notification must clear the borrow before the
    // following direct DrawScene is permitted to use the remaining objects.
    ASSERT_EQ(nullptr, Device->m_Camera);
    ASSERT_EQ(CK_OK, Device->DrawScene(CK_RENDER_DONOTUPDATEEXTENTS));
    VxMatrix normal; normal.Perspective(PI / 2, 1, 1, 100);
    Matrix(Boundary.Matrices[VXMATRIX_PROJECTION], normal);
    ASSERT_EQ(CK_OK, Device->Render(CK_RENDER_DONOTUPDATEEXTENTS));
    EXPECT_EQ(nullptr, Device->m_Camera);
    Matrix(Boundary.Matrices[VXMATRIX_PROJECTION], normal);
    EXPECT_FLOAT_EQ(1, Device->GetViewpoint()->GetWorldMatrix()[0].x);
    EXPECT_FLOAT_EQ(-1, Device->GetViewpoint()->GetWorldMatrix()[3].z);
}

TEST_F(RenderedSceneDrawTest, PhaseFailuresStopLaterWorkAndAnEmptyViewportDoesNotOpenTheDevice) {
    ASSERT_NE(nullptr, Sprite(TRUE)); ASSERT_NE(nullptr, Sprite(FALSE));
    CallbackEvent final = {&Trace, FinalCallback};
    Device->AddPostSpriteRenderCallBack(TraceCallback, &final, FALSE);
    Boundary.Trace = &Trace;
    const CK_RENDER_FLAGS flags = static_cast<CK_RENDER_FLAGS>(CK_RENDER_SKIP3D | CK_RENDER_BACKGROUNDSPRITES | CK_RENDER_FOREGROUNDSPRITES);
    Device->m_ViewportData.ViewWidth = 0;
    ASSERT_EQ(CK_OK, Device->DrawScene(flags)); EXPECT_EQ(0, Trace.Count);
    Device->m_ViewportData.ViewWidth = 100;
    Boundary.BeginResult = FALSE; Boundary.DeviceStatus = CKERR_INVALIDRENDERCONTEXT;
    EXPECT_EQ(CKERR_INVALIDRENDERCONTEXT, Device->DrawScene(flags));
    const int beginFailed[] = {Begin}; Trace.Expect(beginFailed, 1);
    Trace.Count = 0; Boundary.BeginResult = TRUE; Boundary.EndResult = FALSE;
    EXPECT_EQ(CKERR_INVALIDRENDERCONTEXT, Device->DrawScene(flags));
    const int endFailed[] = {Begin, Background, End}; Trace.Expect(endFailed, 3);
    EXPECT_EQ(FALSE, g_UpdateTransparency);
    Trace.Count = 0; Boundary.EndResult = TRUE; Boundary.OverlayResult = FALSE; Boundary.DeviceStatus = CK_OK;
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->DrawScene(flags));
    const int overlayFailed[] = {Begin, Background, End, Overlay}; Trace.Expect(overlayFailed, 4);
    Trace.Count = 0; Boundary.OverlayResult = TRUE;
    ASSERT_EQ(CK_OK, Device->DrawScene(flags));
    const int recovered[] = {Begin, Background, End, Overlay, Foreground, FinalCallback};
    Trace.Expect(recovered, 6); EXPECT_EQ(0u, Device->m_DrawSceneCalls);
    Device->RemovePostSpriteRenderCallBack(TraceCallback, &final);
}
} // namespace
