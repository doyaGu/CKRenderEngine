#include "TextureTestObserver.h"
#include "RCKMaterial.h"

namespace {
CKMaterialData DevicePayload(float value) {
    CKMaterialData data = {};
    data.Diffuse = VxColor(value, .2f, .3f, .4f);
    data.Ambient = VxColor(.5f, value, .7f, .8f);
    data.Specular = VxColor(.9f, .1f, value, .3f);
    data.Emissive = VxColor(.4f, .5f, .6f, value);
    data.SpecularPower = value * 40.0f;
    return data;
}
void ExpectDevicePayload(const CKMaterialData &actual, const CKMaterialData &expected) {
    const VxColor *a[] = {&actual.Diffuse, &actual.Ambient, &actual.Specular, &actual.Emissive};
    const VxColor *b[] = {&expected.Diffuse, &expected.Ambient, &expected.Specular, &expected.Emissive};
    for (int i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(b[i]->r, a[i]->r); EXPECT_FLOAT_EQ(b[i]->g, a[i]->g);
        EXPECT_FLOAT_EQ(b[i]->b, a[i]->b); EXPECT_FLOAT_EQ(b[i]->a, a[i]->a);
    }
    EXPECT_FLOAT_EQ(expected.SpecularPower, actual.SpecularPower);
}
class DeviceObserver : public TextureTestObserver {
public:
    CKBOOL ShutdownResult = TRUE;
    int ShutdownCalls = 0, ResizeCalls = 0;
    DeviceObserver() { SetContextDesc(nullptr, 3, 4, 320, 200, 32, FALSE, 60, 24, 8); }
    CKBOOL BeginShutdown() override { ++ShutdownCalls; return ShutdownResult; }
    CKBOOL ApplyMaterial(const CKMaterialRenderState &state) override {
        Material = state.Material; return TextureTestObserver::ApplyMaterial(state);
    }
    CKBOOL Resize(int x, int y, int width, int height, CKDWORD flags) override {
        ++ResizeCalls;
        CKRasterizerContextDesc desc = {}; EXPECT_TRUE(GetDesc(&desc));
        SetContextDesc(nullptr, flags & VX_RESIZE_NOMOVE ? desc.PosX : x,
                       flags & VX_RESIZE_NOMOVE ? desc.PosY : y,
                       flags & VX_RESIZE_NOSIZE ? desc.Width : width,
                       flags & VX_RESIZE_NOSIZE ? desc.Height : height,
                       32, FALSE, 60, 24, 8);
        return TRUE;
    }
};
class DeviceDriver : public CKRasterizerDriver {
public:
    CKBOOL DestroyResult = TRUE;
    int DestroyCalls = 0;
    CKRasterizerContext *LastDestroyed = nullptr;
    CKBOOL DestroyContext(CKRasterizerContext *context) override {
        ++DestroyCalls; LastDestroyed = context;
        // The fixture lends a stack backend. Success ends its logical device
        // lifetime; the test never uses its cached interface after success.
        return DestroyResult;
    }
};
struct DeviceCall {
    CKRasterizerContext *Seen = nullptr, *Expected = nullptr;
    CKBOOL Destroy = FALSE, Paint = FALSE;
    CKMaterialData Material = DevicePayload(.65f);
    int Calls = 0;
    void Run(CKRenderContext *context) {
        ++Calls; Seen = context->GetRasterizerContext(); ASSERT_NE(nullptr, Seen);
        if (Expected) EXPECT_EQ(Expected, Seen);
        if (Paint) ASSERT_TRUE(Seen->SetMaterial(&Material));
        if (Destroy) {
            ASSERT_TRUE(static_cast<RCKRenderContext *>(context)->DestroyDevice());
            EXPECT_EQ(nullptr, context->GetRasterizerContext());
        }
    }
};
int DeviceMaterialCallback(CKRenderContext *context, CKMaterial *, void *argument) {
    static_cast<DeviceCall *>(argument)->Run(context); return TRUE;
}
class CallbackObservationDeviceTest : public TextureTestWorld {
protected:
    DeviceObserver Target;
    DeviceDriver TestDriver;
    void SetUp() override {
        TextureTestWorld::SetUp();
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        Device->DetachAll(); Device->m_RasterizerContext = &Target;
        Device->m_RasterizerDriver = &TestDriver; Device->m_Fullscreen = FALSE;
        Manager->m_Drivers[0].RasterizerDriver = &TestDriver;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&Target, 0));
    }
    void Execute(DeviceCall &call) {
        RCKMaterial material(Context); material.SetCallback(DeviceMaterialCallback, &call);
        ASSERT_TRUE(material.SetAsCurrent(Device, TRUE, 0));
    }
    void CheckCachedCalls(CKRasterizerContext *cached) {
        ASSERT_NE(nullptr, cached);
        CKMaterialData direct = DevicePayload(.45f), applied = DevicePayload(.85f);
        ASSERT_TRUE(cached->SetMaterial(&direct)); ExpectDevicePayload(Target.Material, direct);
        CKMaterialRenderState state = {}; state.Material = applied;
        ASSERT_TRUE(cached->ApplyMaterial(state)); ExpectDevicePayload(Target.Material, applied);
        CKViewportData viewport = {};
        viewport.ViewX = 5; viewport.ViewY = 7; viewport.ViewWidth = 97; viewport.ViewHeight = 83;
        viewport.ViewZMin = .1f; viewport.ViewZMax = .9f;
        ASSERT_TRUE(cached->SetViewport(&viewport));
        EXPECT_EQ(5u, Target.Viewport.ViewX); EXPECT_EQ(7u, Target.Viewport.ViewY);
        EXPECT_EQ(97u, Target.Viewport.ViewWidth); EXPECT_EQ(83u, Target.Viewport.ViewHeight);
        EXPECT_FLOAT_EQ(.1f, Target.Viewport.ViewZMin); EXPECT_FLOAT_EQ(.9f, Target.Viewport.ViewZMax);
        CKRasterizerContextDesc desc = {}; ASSERT_TRUE(cached->GetDesc(&desc));
        EXPECT_EQ(320, desc.Width); EXPECT_EQ(200, desc.Height);
    }
};

// These are current-only lifetime checks. The old library frees the interface
// when the first material callback returns, so its baseline must skip this suite.
TEST_F(CallbackObservationDeviceTest, RefusedBeginShutdownPreservesBackendAndCachedInterface) {
    DeviceCall capture; Execute(capture); ASSERT_EQ(1, capture.Calls);
    Target.ShutdownResult = FALSE;
    EXPECT_FALSE(Device->DestroyDevice()); EXPECT_EQ(1, Target.ShutdownCalls);
    EXPECT_EQ(0, TestDriver.DestroyCalls); EXPECT_EQ(&Target, Device->GetRasterizerContext());
    EXPECT_NE(nullptr, Manager->GetDriverDescription(&Target));
    CheckCachedCalls(capture.Seen);
    DeviceCall later; later.Expected = capture.Seen; later.Paint = TRUE; Execute(later);
    EXPECT_EQ(1, later.Calls); ExpectDevicePayload(Target.Material, later.Material);
    EXPECT_EQ(&Target, Device->GetRasterizerContext());
}

TEST_F(CallbackObservationDeviceTest, DriverRefusalPreservesInterfaceThenCallbackDestructionDoesNotResurrectIt) {
    DeviceCall capture; Execute(capture); ASSERT_EQ(1, capture.Calls);
    TestDriver.DestroyResult = FALSE;
    EXPECT_FALSE(Device->DestroyDevice()); EXPECT_EQ(1, Target.ShutdownCalls);
    EXPECT_EQ(1, TestDriver.DestroyCalls); EXPECT_EQ(&Target, TestDriver.LastDestroyed);
    EXPECT_EQ(&Target, Device->GetRasterizerContext());
    EXPECT_NE(nullptr, Manager->GetDriverDescription(&Target));
    CheckCachedCalls(capture.Seen);
    TestDriver.DestroyResult = TRUE;
    DeviceCall destroy; destroy.Expected = capture.Seen; destroy.Destroy = TRUE; Execute(destroy);
    EXPECT_EQ(1, destroy.Calls); EXPECT_EQ(2, Target.ShutdownCalls);
    EXPECT_EQ(2, TestDriver.DestroyCalls); EXPECT_EQ(&Target, TestDriver.LastDestroyed);
    EXPECT_EQ(nullptr, Device->GetRasterizerContext());
    EXPECT_EQ(nullptr, Manager->GetDriverDescription(&Target));
    EXPECT_EQ(nullptr, Device->m_RasterizerDriver);
    // capture.Seen / destroy.Seen are logically invalid from this point onward.
}

TEST_F(CallbackObservationDeviceTest, InPlaceResizeKeepsCachedInterfaceForwardingUpdatedDescriptor) {
    DeviceCall capture; Execute(capture); ASSERT_EQ(1, capture.Calls);
    ASSERT_EQ(CK_OK, Device->Resize(11, 13, 480, 270, 0));
    EXPECT_EQ(1, Target.ResizeCalls); EXPECT_EQ(0, Target.ShutdownCalls);
    EXPECT_EQ(0, TestDriver.DestroyCalls); EXPECT_EQ(&Target, Device->GetRasterizerContext());
    CKRasterizerContextDesc desc = {}; ASSERT_TRUE(capture.Seen->GetDesc(&desc));
    EXPECT_EQ(480, desc.Width); EXPECT_EQ(270, desc.Height); EXPECT_EQ(11, desc.PosX); EXPECT_EQ(13, desc.PosY);
    CKMaterialData payload = DevicePayload(.55f);
    ASSERT_TRUE(capture.Seen->SetMaterial(&payload)); ExpectDevicePayload(Target.Material, payload);
    DeviceCall later; later.Expected = capture.Seen; later.Paint = TRUE; Execute(later);
    EXPECT_EQ(1, later.Calls); ExpectDevicePayload(Target.Material, later.Material);
    EXPECT_EQ(&Target, Device->GetRasterizerContext());
    ASSERT_TRUE(later.Seen->GetDesc(&desc)); EXPECT_EQ(480, desc.Width); EXPECT_EQ(270, desc.Height);
}
} // namespace
