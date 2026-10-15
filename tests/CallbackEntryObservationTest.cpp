#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "TextureTestObserver.h"

namespace {
class EntryObserver : public TextureTestObserver {
public:
    CKMaterialData DrawMaterial = {};
    CKViewportData DrawViewport = {};
    int Draws = 0;
    CKBOOL ApplyMaterial(const CKMaterialRenderState &state) override {
        // ApplyMaterial's public contract uploads both payload and states.
        Material = state.Material;
        return TextureTestObserver::ApplyMaterial(state);
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count,
                         VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_EQ(3, count);
        EXPECT_NE(nullptr, indices); EXPECT_NE(nullptr, data);
        DrawMaterial = Material; DrawViewport = Viewport; ++Draws;
        return indices && data && count == 3;
    }
};

CKMaterialData Payload(float value) {
    CKMaterialData data = {};
    data.Diffuse = VxColor(value, .2f, .3f, .4f);
    data.Ambient = VxColor(.5f, value, .7f, .8f);
    data.Specular = VxColor(.9f, .1f, value, .3f);
    data.Emissive = VxColor(.4f, .5f, .6f, value);
    data.SpecularPower = value * 40.0f;
    return data;
}
void ExpectPayload(const CKMaterialData &actual, const CKMaterialData &expected) {
    const VxColor *a[4] = {&actual.Diffuse, &actual.Ambient, &actual.Specular, &actual.Emissive};
    const VxColor *b[4] = {&expected.Diffuse, &expected.Ambient, &expected.Specular, &expected.Emissive};
    for (int i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(b[i]->r, a[i]->r); EXPECT_FLOAT_EQ(b[i]->g, a[i]->g);
        EXPECT_FLOAT_EQ(b[i]->b, a[i]->b); EXPECT_FLOAT_EQ(b[i]->a, a[i]->a);
    }
    EXPECT_FLOAT_EQ(expected.SpecularPower, actual.SpecularPower);
}
void ExpectViewport(const CKViewportData &actual, const CKViewportData &expected) {
    EXPECT_EQ(expected.ViewX, actual.ViewX); EXPECT_EQ(expected.ViewY, actual.ViewY);
    EXPECT_EQ(expected.ViewWidth, actual.ViewWidth); EXPECT_EQ(expected.ViewHeight, actual.ViewHeight);
    EXPECT_FLOAT_EQ(expected.ViewZMin, actual.ViewZMin); EXPECT_FLOAT_EQ(expected.ViewZMax, actual.ViewZMax);
}
struct EntryCall {
    RCKMesh *Nested = nullptr;
    EntryObserver *State = nullptr;
    CKMaterialData Outer = Payload(.75f), Inner = Payload(.25f);
    CKViewportData OuterViewport = {}, InnerViewport = {};
    int Calls = 0;
    void Enter(CKRenderContext *context) {
        ++Calls;
        RCKRenderContext *device = static_cast<RCKRenderContext *>(context);
        CKMaterialRenderState state = {}; state.Material = Outer;
        CKRasterizerContext *rasterizer = context->GetRasterizerContext();
        ASSERT_TRUE(rasterizer->ApplyMaterial(state));
        ASSERT_TRUE(rasterizer->SetViewport(&OuterViewport));
        EXPECT_NE(OuterViewport.ViewWidth, device->m_ViewportData.ViewWidth);
        ASSERT_EQ(1, Nested->DefaultRender(device, nullptr));
        ExpectPayload(State->Material, Outer);
        ExpectViewport(State->Viewport, OuterViewport);
        // Reentry restores the physical viewport without changing this cache.
        EXPECT_EQ(64u, device->m_ViewportData.ViewWidth);
    }
};
int EnterFromMaterial(CKRenderContext *context, CKMaterial *, void *argument) {
    static_cast<EntryCall *>(argument)->Enter(context); return TRUE;
}
void EnterFromMesh(CKRenderContext *context, CK3dEntity *, CKMesh *, void *argument) {
    static_cast<EntryCall *>(argument)->Enter(context);
}
int PaintInner(CKRenderContext *context, CKMaterial *, void *argument) {
    EntryCall &call = *static_cast<EntryCall *>(argument);
    EXPECT_TRUE(context->GetRasterizerContext()->SetMaterial(&call.Inner));
    EXPECT_TRUE(context->GetRasterizerContext()->SetViewport(&call.InnerViewport));
    return TRUE;
}

class CallbackEntryObservationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    EntryObserver State;
    CKRasterizerDriver Driver;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        Manager->m_DriverCount = 1; Manager->m_Drivers = new VxDriverDescEx[1]();
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        Device = new RCKRenderContext(Context); Device->DetachAll();
        Device->m_RasterizerContext = &State; Device->m_RasterizerDriver = &Driver;
        Device->m_DriverIndex = 0; Device->m_DisplayWireframe = FALSE;
        Device->m_ViewportData.ViewWidth = 64; Device->m_ViewportData.ViewHeight = 48;
        State.SetViewport(&Device->m_ViewportData);
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    void Configure(RCKMesh &nested, RCKMaterial &inner, EntryCall &call) {
        call.Nested = &nested; call.State = &State;
        call.OuterViewport.ViewX = 9; call.OuterViewport.ViewY = 11;
        call.OuterViewport.ViewWidth = 123; call.OuterViewport.ViewHeight = 87;
        call.OuterViewport.ViewZMin = .2f; call.OuterViewport.ViewZMax = .85f;
        call.InnerViewport.ViewX = 1; call.InnerViewport.ViewY = 2;
        call.InnerViewport.ViewWidth = 17; call.InnerViewport.ViewHeight = 19;
        call.InnerViewport.ViewZMin = .1f; call.InnerViewport.ViewZMax = .6f;
        ASSERT_TRUE(nested.SetVertexCount(3)); ASSERT_TRUE(nested.SetFaceCount(1));
        nested.SetFaceVertexIndex(0, 0, 1, 2); nested.SetFaceMaterial(0, &inner);
        for (int i = 0; i < 3; ++i) {
            VxVector position(i == 1 ? 1.0f : 0.0f, i == 2 ? 1.0f : 0.0f, 1.0f);
            nested.SetVertexPosition(i, &position);
        }
        nested.SetFlags(nested.GetFlags() & ~VXMESH_STRIPIFY);
        inner.SetCallback(PaintInner, &call);
    }
    void Check(const EntryCall &call) {
        EXPECT_EQ(1, call.Calls); ASSERT_EQ(1, State.Draws);
        ExpectPayload(State.DrawMaterial, call.Inner); ExpectViewport(State.DrawViewport, call.InnerViewport);
        ExpectPayload(State.Material, call.Outer); ExpectViewport(State.Viewport, call.OuterViewport);
        EXPECT_EQ(&State, Device->GetRasterizerContext());
        EXPECT_EQ(64u, Device->m_ViewportData.ViewWidth);
    }
};
TEST_F(CallbackEntryObservationTest, StandaloneMaterialApplyPayloadSurvivesNestedDefaultRender) {
    RCKMesh nested(Context); RCKMaterial inner(Context), standalone(Context); EntryCall call;
    Configure(nested, inner, call); standalone.SetCallback(EnterFromMaterial, &call);
    ASSERT_TRUE(standalone.SetAsCurrent(Device, TRUE, 0)); Check(call);
}
TEST_F(CallbackEntryObservationTest, MainMeshCallbackApplyPayloadSurvivesNestedDefaultRender) {
    RCKMesh nested(Context), outer(Context); RCKMaterial inner(Context); EntryCall call;
    Configure(nested, inner, call); outer.SetRenderCallBack(EnterFromMesh, &call);
    ASSERT_EQ(CK_OK, outer.Render(Device, nullptr)); Check(call);
}
} // namespace
