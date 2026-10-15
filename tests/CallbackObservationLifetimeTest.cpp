#include "TextureTestObserver.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"

namespace {
CKMaterialData LifetimePayload(float value) {
    CKMaterialData data = {};
    data.Diffuse = VxColor(value, .2f, .3f, .4f);
    data.Ambient = VxColor(.5f, value, .7f, .8f);
    data.Specular = VxColor(.9f, .1f, value, .3f);
    data.Emissive = VxColor(.4f, .5f, .6f, value);
    data.SpecularPower = value * 40.0f;
    return data;
}
CKViewportData LifetimeViewport(CKDWORD width) {
    CKViewportData viewport = {};
    viewport.ViewX = 7; viewport.ViewY = 9;
    viewport.ViewWidth = width; viewport.ViewHeight = width - 11;
    viewport.ViewZMin = .2f; viewport.ViewZMax = .85f;
    return viewport;
}
void ExpectLifetimePayload(const CKMaterialData &actual, const CKMaterialData &expected) {
    const VxColor *a[] = {&actual.Diffuse, &actual.Ambient, &actual.Specular, &actual.Emissive};
    const VxColor *b[] = {&expected.Diffuse, &expected.Ambient, &expected.Specular, &expected.Emissive};
    for (int i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(b[i]->r, a[i]->r); EXPECT_FLOAT_EQ(b[i]->g, a[i]->g);
        EXPECT_FLOAT_EQ(b[i]->b, a[i]->b); EXPECT_FLOAT_EQ(b[i]->a, a[i]->a);
    }
    EXPECT_FLOAT_EQ(expected.SpecularPower, actual.SpecularPower);
}
void ExpectLifetimeViewport(const CKViewportData &actual, const CKViewportData &expected) {
    EXPECT_EQ(expected.ViewX, actual.ViewX); EXPECT_EQ(expected.ViewY, actual.ViewY);
    EXPECT_EQ(expected.ViewWidth, actual.ViewWidth); EXPECT_EQ(expected.ViewHeight, actual.ViewHeight);
    EXPECT_FLOAT_EQ(expected.ViewZMin, actual.ViewZMin); EXPECT_FLOAT_EQ(expected.ViewZMax, actual.ViewZMax);
}
class LifetimeObserver : public TextureTestObserver {
public:
    explicit LifetimeObserver(int width = 320) {
        SetContextDesc(nullptr, 3, 4, width, 200, 32, FALSE, 60, 24, 8);
    }
    CKBOOL RefuseMaterial = FALSE, RefuseViewport = FALSE, RefuseApply = FALSE;
    CKMaterialData DrawMaterial = {};
    CKViewportData DrawViewport = {};
    int Draws = 0;
    CKBOOL SetMaterial(const CKMaterialData *data) override {
        if (RefuseMaterial) { RefuseMaterial = FALSE; return FALSE; }
        return TextureTestObserver::SetMaterial(data);
    }
    CKBOOL ApplyMaterial(const CKMaterialRenderState &state) override {
        if (RefuseApply) { RefuseApply = FALSE; return FALSE; }
        Material = state.Material;
        return TextureTestObserver::ApplyMaterial(state);
    }
    CKBOOL SetViewport(const CKViewportData *viewport) override {
        if (RefuseViewport) { RefuseViewport = FALSE; return FALSE; }
        return TextureTestObserver::SetViewport(viewport);
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count,
                         VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_EQ(3, count);
        EXPECT_NE(nullptr, data); EXPECT_NE(nullptr, indices);
        if (!data || !indices || count != 3) return FALSE;
        EXPECT_NE(nullptr, data->PositionPtr);
        if (!data->PositionPtr) return FALSE;
        for (int i = 0; i < 3; ++i) {
            EXPECT_LT(indices[i], 3);
            if (indices[i] >= 3) return FALSE;
            VxVector position;
            memcpy(&position, static_cast<CKBYTE *>(data->PositionPtr) + indices[i] * data->PositionStride,
                   sizeof(position));
            EXPECT_FLOAT_EQ(i == 1 ? 2.0f : 0.0f, position.x);
            EXPECT_FLOAT_EQ(i == 2 ? 3.0f : 0.0f, position.y);
            EXPECT_FLOAT_EQ(1.0f, position.z);
        }
        DrawMaterial = Material; DrawViewport = Viewport; ++Draws;
        return TRUE;
    }
};

struct LifetimeCall {
    RCKMesh *Nested = nullptr;
    LifetimeObserver *State = nullptr;
    TextureTestProbe *Texture = nullptr;
    CKRasterizerContext *Seen = nullptr, *ExpectedInterface = nullptr;
    CKMaterialData Outer = LifetimePayload(.75f), Inner = LifetimePayload(.25f);
    CKViewportData OuterViewport = LifetimeViewport(123), InnerViewport = LifetimeViewport(31);
    CKBOOL WriteOuter = TRUE, RestoreOuter = TRUE, ApplyOuter = FALSE, RejectAfterAccept = FALSE;
    int Calls = 0, PreCalls = 0, PostCalls = 0, ExpectedWidth = 320;
    void Paint(CKRenderContext *context) {
        CKRasterizerContext *rasterizer = context->GetRasterizerContext();
        if (ApplyOuter) {
            CKMaterialRenderState state = {}; state.Material = Outer;
            ASSERT_TRUE(rasterizer->ApplyMaterial(state));
        } else {
            ASSERT_TRUE(rasterizer->SetMaterial(&Outer));
        }
        ASSERT_TRUE(rasterizer->SetViewport(&OuterViewport));
    }
    void Run(CKRenderContext *context) {
        ++Calls; Seen = context->GetRasterizerContext();
        ASSERT_NE(nullptr, Seen);
        if (ExpectedInterface) EXPECT_EQ(ExpectedInterface, Seen);
        CKRasterizerContextDesc desc = {}; ASSERT_TRUE(Seen->GetDesc(&desc));
        EXPECT_EQ(ExpectedWidth, desc.Width); EXPECT_EQ(200, desc.Height);
        if (Texture) ASSERT_TRUE(Texture->SetAsCurrent(context, FALSE, 0));
        if (WriteOuter) Paint(context);
        if (RejectAfterAccept) {
            CKMaterialData rejected = LifetimePayload(.95f);
            CKMaterialRenderState apply = {}; apply.Material = rejected;
            CKViewportData viewport = LifetimeViewport(211);
            State->RefuseMaterial = TRUE; EXPECT_FALSE(Seen->SetMaterial(&rejected));
            State->RefuseApply = TRUE; EXPECT_FALSE(Seen->ApplyMaterial(apply));
            State->RefuseViewport = TRUE; EXPECT_FALSE(Seen->SetViewport(&viewport));
        }
        if (!Nested) return;
        const int draws = State->Draws;
        RCKRenderContext *device = static_cast<RCKRenderContext *>(context);
        ASSERT_EQ(1, Nested->DefaultRender(device, nullptr));
        EXPECT_EQ(draws + 1, State->Draws);
        ExpectLifetimePayload(State->DrawMaterial, Inner);
        ExpectLifetimeViewport(State->DrawViewport, InnerViewport);
        ExpectLifetimePayload(State->Material, RestoreOuter ? Outer : Inner);
        ExpectLifetimeViewport(State->Viewport, RestoreOuter ? OuterViewport : device->m_ViewportData);
        EXPECT_EQ(64u, device->m_ViewportData.ViewWidth);
        EXPECT_EQ(48u, device->m_ViewportData.ViewHeight);
    }
};
int LifetimeMaterialCallback(CKRenderContext *context, CKMaterial *, void *argument) {
    static_cast<LifetimeCall *>(argument)->Run(context); return TRUE;
}
int LifetimeInnerCallback(CKRenderContext *context, CKMaterial *, void *argument) {
    LifetimeCall &call = *static_cast<LifetimeCall *>(argument);
    EXPECT_TRUE(context->GetRasterizerContext()->SetMaterial(&call.Inner));
    EXPECT_TRUE(context->GetRasterizerContext()->SetViewport(&call.InnerViewport));
    return TRUE;
}
void LifetimePreCallback(CKRenderContext *context, CK3dEntity *, CKMesh *, void *argument) {
    LifetimeCall &call = *static_cast<LifetimeCall *>(argument);
    ++call.PreCalls; call.Paint(context);
}
void LifetimeMainCallback(CKRenderContext *context, CK3dEntity *, CKMesh *, void *argument) {
    static_cast<LifetimeCall *>(argument)->Run(context);
}
void LifetimePostCallback(CKRenderContext *, CK3dEntity *, CKMesh *, void *argument) {
    LifetimeCall &call = *static_cast<LifetimeCall *>(argument);
    ++call.PostCalls;
    ExpectLifetimePayload(call.State->Material, call.Outer);
    ExpectLifetimeViewport(call.State->Viewport, call.OuterViewport);
}

class CallbackObservationLifetimeTest : public TextureTestWorld {
protected:
    LifetimeObserver Target, OtherTarget{640};
    void SetUp() override {
        TextureTestWorld::SetUp();
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        Device->DetachAll(); OtherDevice->DetachAll();
        Device->m_RasterizerContext = &Target; OtherDevice->m_RasterizerContext = &OtherTarget;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&Target, 0));
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&OtherTarget, 0));
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 2;
        Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = 2;
        Device->m_DisplayWireframe = OtherDevice->m_DisplayWireframe = FALSE;
        Device->m_ViewportData.ViewWidth = OtherDevice->m_ViewportData.ViewWidth = 64;
        Device->m_ViewportData.ViewHeight = OtherDevice->m_ViewportData.ViewHeight = 48;
        Device->m_ViewportData.ViewZMax = OtherDevice->m_ViewportData.ViewZMax = 1.0f;
        ASSERT_TRUE(Target.SetViewport(&Device->m_ViewportData));
        ASSERT_TRUE(OtherTarget.SetViewport(&OtherDevice->m_ViewportData));
        OtherTarget.NextHandle = 13;
    }
    void Geometry(RCKMesh &mesh, RCKMaterial &material, LifetimeCall &call) {
        call.Nested = &mesh;
        ASSERT_TRUE(mesh.SetVertexCount(3)); ASSERT_TRUE(mesh.SetFaceCount(1));
        mesh.SetFaceVertexIndex(0, 0, 1, 2); mesh.SetFaceMaterial(0, &material);
        for (int i = 0; i < 3; ++i) {
            VxVector position(i == 1 ? 2.0f : 0.0f, i == 2 ? 3.0f : 0.0f, 1.0f);
            mesh.SetVertexPosition(i, &position);
        }
        mesh.SetFlags(mesh.GetFlags() & ~VXMESH_STRIPIFY);
        material.SetCallback(LifetimeInnerCallback, &call);
    }
    void Execute(LifetimeCall &call, RCKRenderContext *device = nullptr) {
        if (!device) device = Device;
        RCKMaterial material(Context); material.SetCallback(LifetimeMaterialCallback, &call);
        ASSERT_TRUE(material.SetAsCurrent(device, TRUE, 0));
        EXPECT_EQ(device == Device ? &Target : &OtherTarget, device->GetRasterizerContext());
    }
};

TEST_F(CallbackObservationLifetimeTest, PreCallbackPayloadSurvivesMainNestedDrawAndPostCallback) {
    RCKMesh outer(Context), nested(Context); RCKMaterial inner(Context); LifetimeCall call;
    call.State = &Target; call.WriteOuter = FALSE;
    Geometry(nested, inner, call);
    ASSERT_TRUE(outer.AddPreRenderCallBack(LifetimePreCallback, &call, FALSE));
    outer.SetRenderCallBack(LifetimeMainCallback, &call);
    ASSERT_TRUE(outer.AddPostRenderCallBack(LifetimePostCallback, &call, FALSE));
    ASSERT_EQ(CK_OK, outer.Render(Device, nullptr));
    EXPECT_EQ(1, call.PreCalls); EXPECT_EQ(1, call.Calls); EXPECT_EQ(1, call.PostCalls);
    EXPECT_EQ(1, Target.Draws); EXPECT_EQ(&Target, Device->GetRasterizerContext());
}

// Do not run this case against the old temporary-adapter implementation: calls
// through the saved public interface intentionally expose its use after free.
TEST_F(CallbackObservationLifetimeTest, CachedInterfaceRemainsUsableAfterCallbackAndAcrossLaterReentry) {
    LifetimeCall capture; capture.State = &Target; Execute(capture);
    ASSERT_EQ(1, capture.Calls); ASSERT_NE(nullptr, capture.Seen);
    CKMaterialData direct = LifetimePayload(.6f), applied = LifetimePayload(.45f);
    CKViewportData viewport = LifetimeViewport(145);
    ASSERT_TRUE(capture.Seen->SetMaterial(&direct)); ExpectLifetimePayload(Target.Material, direct);
    CKMaterialRenderState state = {}; state.Material = applied;
    ASSERT_TRUE(capture.Seen->ApplyMaterial(state)); ExpectLifetimePayload(Target.Material, applied);
    ASSERT_TRUE(capture.Seen->SetViewport(&viewport)); ExpectLifetimeViewport(Target.Viewport, viewport);
    CKRasterizerContextDesc desc = {}; ASSERT_TRUE(capture.Seen->GetDesc(&desc));
    EXPECT_EQ(320, desc.Width); EXPECT_EQ(200, desc.Height); EXPECT_EQ(3, desc.PosX);
    RCKMesh nested(Context); RCKMaterial inner(Context); LifetimeCall later;
    later.State = &Target; later.ExpectedInterface = capture.Seen; later.ApplyOuter = TRUE;
    later.Outer = LifetimePayload(.85f); later.OuterViewport = LifetimeViewport(167);
    Geometry(nested, inner, later); Execute(later);
    EXPECT_EQ(1, later.Calls); EXPECT_EQ(1, Target.Draws);
}

TEST_F(CallbackObservationLifetimeTest, DistinctContextsKeepAdaptersDescriptorsAndTextureResidencySeparate) {
    RCKMesh firstMesh(Context), secondMesh(Context); RCKMaterial firstInner(Context), secondInner(Context);
    LifetimeCall first, second; first.State = &Target; second.State = &OtherTarget; second.ExpectedWidth = 640;
    second.Outer = LifetimePayload(.55f); second.OuterViewport = LifetimeViewport(177);
    second.Inner = LifetimePayload(.35f); second.InnerViewport = LifetimeViewport(43);
    Geometry(firstMesh, firstInner, first); Geometry(secondMesh, secondInner, second);
    TextureTestProbe firstTexture(Context), secondTexture(Context);
    const CKDWORD firstPixels[] = {0xFF123456, 0xFF789ABC};
    const CKDWORD secondPixels[] = {0xFF654321, 0xFFABCDEF};
    Pixels(firstTexture, firstPixels, 2, 1); Pixels(secondTexture, secondPixels, 2, 1);
    firstTexture.SetDesiredVideoFormat(_32_ARGB8888); secondTexture.SetDesiredVideoFormat(_32_ARGB8888);
    first.Texture = &firstTexture; second.Texture = &secondTexture;
    Execute(first); Execute(second, OtherDevice);
    ASSERT_NE(nullptr, first.Seen); ASSERT_NE(nullptr, second.Seen); EXPECT_NE(first.Seen, second.Seen);
    EXPECT_EQ(&Target, firstTexture.SelectedContext()); EXPECT_EQ(&OtherTarget, secondTexture.SelectedContext());
    ASSERT_EQ(1, Target.Creates); ASSERT_EQ(1, OtherTarget.Creates);
    ASSERT_EQ(1, Target.Loads); ASSERT_EQ(1, OtherTarget.Loads);
    EXPECT_EQ(0, memcmp(firstPixels, Target.Uploads[0].Bytes, sizeof(firstPixels)));
    EXPECT_EQ(0, memcmp(secondPixels, OtherTarget.Uploads[0].Bytes, sizeof(secondPixels)));
    EXPECT_EQ(1, firstTexture.GetRstTextureIndex()); EXPECT_EQ(13, secondTexture.GetRstTextureIndex());
    ExpectLifetimePayload(Target.Material, first.Outer); ExpectLifetimeViewport(Target.Viewport, first.OuterViewport);
    ExpectLifetimePayload(OtherTarget.Material, second.Outer);
    ExpectLifetimeViewport(OtherTarget.Viewport, second.OuterViewport);
    // Use each interface only during its next callback; the cached-use case above
    // separately covers use outside an invocation without risking old baselines here.
    first.ExpectedInterface = first.Seen; second.ExpectedInterface = second.Seen;
    Execute(first); Execute(second, OtherDevice);
    EXPECT_EQ(2, Target.Draws); EXPECT_EQ(2, OtherTarget.Draws);
    EXPECT_EQ(1, Target.Creates); EXPECT_EQ(1, OtherTarget.Creates);
}

TEST_F(CallbackObservationLifetimeTest, NewOuterInvocationDoesNotReplayPreviousOpaquePayloadShadow) {
    LifetimeCall previous; previous.State = &Target; Execute(previous);
    ExpectLifetimePayload(Target.Material, previous.Outer);
    RCKMesh nested(Context); RCKMaterial inner(Context); LifetimeCall current;
    current.State = &Target; current.WriteOuter = FALSE; current.RestoreOuter = FALSE;
    current.Inner = LifetimePayload(.4f); Geometry(nested, inner, current);
    // No successful entry write identifies this invocation's opaque payload.
    // The inner draw is real, but an earlier invocation is not a readback oracle.
    Execute(current); EXPECT_EQ(1, current.Calls); EXPECT_EQ(1, Target.Draws);
    ExpectLifetimePayload(Target.Material, current.Inner);
    ExpectLifetimeViewport(Target.Viewport, Device->m_ViewportData);
}

TEST_F(CallbackObservationLifetimeTest, ReusedInterfaceRejectedWritesKeepThisInvocationLastAcceptedEntry) {
    LifetimeCall previous; previous.State = &Target; Execute(previous);
    RCKMesh nested(Context); RCKMaterial inner(Context); LifetimeCall current;
    current.State = &Target; current.ExpectedInterface = previous.Seen;
    current.Outer = LifetimePayload(.65f); current.OuterViewport = LifetimeViewport(189);
    current.RejectAfterAccept = TRUE; Geometry(nested, inner, current);
    Execute(current); EXPECT_EQ(1, current.Calls); EXPECT_EQ(1, Target.Draws);
    EXPECT_FALSE(Target.RefuseMaterial); EXPECT_FALSE(Target.RefuseApply); EXPECT_FALSE(Target.RefuseViewport);
}
} // namespace
