#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "TextureTestObserver.h"

namespace {
class DirectGroupMesh : public RCKMesh {
public:
    explicit DirectGroupMesh(CKContext *context) : RCKMesh(context) {}
    CKMaterialGroup *Group(CKMaterial *material) {
        for (int i = 0; i < m_MaterialGroups.Size(); ++i)
            if (m_MaterialGroups[i] && m_MaterialGroups[i]->m_Material == material &&
                m_MaterialGroups[i]->m_HasValidPrimitives)
                return m_MaterialGroups[i];
        return nullptr;
    }
    int ActiveCount() const { return m_ActiveTextureChannels.Size(); }
    int ActiveAt(int index) const { return m_ActiveTextureChannels[index]; }
};

class DirectGroupObserver : public TextureTestObserver {
public:
    struct Draw {
        CKDWORD Textures[3] = {}, Flags = 0;
        int Indices = 0;
        float X = 0;
        Vx2DVector UV[3];
        CKBOOL HasUV[3] = {};
    };
    Draw Draws[16];
    int Count = 0, Attempts = 0;
    CKBOOL ThrowNext = FALSE;
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count,
                         VxDrawPrimitiveData *data) override {
        ++Attempts;
        if (ThrowNext) { ThrowNext = FALSE; throw 100; }
        EXPECT_EQ(VX_TRIANGLELIST, type);
        EXPECT_GT(count, 0); EXPECT_EQ(0, count % 3);
        EXPECT_NE(nullptr, indices); EXPECT_NE(nullptr, data);
        EXPECT_LT(Count, 16);
        if (!indices || !data || count <= 0 || Count >= 16) return FALSE;
        const int vertex = indices[0];
        EXPECT_LT(vertex, data->VertexCount);
        Draw &draw = Draws[Count++]; draw.Flags = data->Flags; draw.Indices = count;
        memcpy(&draw.X, static_cast<CKBYTE *>(data->PositionPtr) + vertex * data->PositionStride, sizeof(float));
        for (int stage = 0; stage < 3; ++stage) {
            draw.Textures[stage] = Textures[stage];
            if ((data->Flags & CKRST_DP_STAGESMASK) < CKRST_DP_STAGE(stage)) continue;
            void *uv = stage ? data->TexCoordPtrs[stage - 1] : data->TexCoordPtr;
            const CKDWORD stride = stage ? data->TexCoordStrides[stage - 1] : data->TexCoordStride;
            if (uv) {
                draw.HasUV[stage] = TRUE;
                memcpy(&draw.UV[stage], static_cast<CKBYTE *>(uv) + vertex * stride, sizeof(Vx2DVector));
            }
        }
        return TRUE;
    }
    void ResetDraws() { Count = Attempts = 0; }
};

struct DirectGroupCall {
    DirectGroupMesh *Mesh = nullptr;
    RCKRenderContext *Outer = nullptr, *Inner = nullptr;
    CKMaterialGroup *InnerGroup = nullptr;
    VxDrawPrimitiveData *InnerData = nullptr;
    int Stage = 0, TriggerStage = 0, Entries = 0, Result = 0;
    int ActiveAfterNested = -1, OuterPreCalls = 0, InnerPreCalls = 0;
    CKBOOL Fired = FALSE, Direct = FALSE, CatchThrow = FALSE, Caught = FALSE;
    void Enter(CKRenderContext *context) {
        if (context != Outer || Fired || Stage != TriggerStage) return;
        Fired = TRUE; ++Entries;
        try {
            Result = Direct ? Mesh->RenderGroup(Inner, InnerGroup, nullptr, InnerData)
                            : Mesh->DefaultRender(Inner, nullptr);
        } catch (int value) {
            if (!CatchThrow) throw;
            EXPECT_EQ(100, value); Caught = TRUE;
        }
        ActiveAfterNested = Mesh->ActiveCount();
    }
};

int DirectGroupMaterialCallback(CKRenderContext *context, CKMaterial *, void *argument) {
    static_cast<DirectGroupCall *>(argument)->Enter(context);
    return FALSE;
}

void DirectGroupPreCallback(CKRenderContext *context, CK3dEntity *, CKMesh *,
                            CKMaterial *, void *argument) {
    DirectGroupCall *call = static_cast<DirectGroupCall *>(argument);
    if (context == call->Outer) ++call->OuterPreCalls; else ++call->InnerPreCalls;
    call->Enter(context);
    CKRasterizerContext *state = static_cast<RCKRenderContext *>(context)->m_RasterizerContext;
    state->SetTexture(901, 0); state->SetTexture(902, 1);
    state->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE);
    state->SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_ADD);
    state->SetTextureStageState(0, CKRST_TSS_TEXCOORDINDEX, 0);
    state->SetTextureStageState(1, CKRST_TSS_TEXCOORDINDEX, 1);
}

class DirectGroupMaterial : public RCKMaterial {
public:
    DirectGroupCall *Call = nullptr;
    RCKRenderContext *Outer = nullptr;
    int Calls[2][CKRST_MAX_TEXTURE_STAGES] = {};
    explicit DirectGroupMaterial(CKContext *context) : RCKMaterial(context) {}
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL lit, int stage) override {
        ++Calls[context == Outer ? 0 : 1][stage];
        if (!Call) return RCKMaterial::SetAsCurrent(context, lit, stage);
        struct RestoreStage {
            DirectGroupCall &Call;
            int Saved;
            RestoreStage(DirectGroupCall &call, int stage) : Call(call), Saved(call.Stage) { Call.Stage = stage; }
            ~RestoreStage() { Call.Stage = Saved; }
        } restore(*Call, stage);
        return RCKMaterial::SetAsCurrent(context, lit, stage);
    }
    void ResetCalls() { memset(Calls, 0, sizeof(Calls)); }
};

class MeshDirectGroupReentryTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Outer = nullptr, *Inner = nullptr;
    DirectGroupObserver State, OtherState;
    CKRasterizerDriver Driver;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        Manager->m_DriverCount = 2; Manager->m_Drivers = new VxDriverDescEx[2]();
        VxImageDescEx format; VxPixelFormat2ImageDesc(_32_ARGB8888, format);
        for (int i = 0; i < 2; ++i) {
            Manager->m_Drivers[i].CapsUpToDate = TRUE;
            Manager->m_Drivers[i].RasterizerDriver = &Driver;
            Manager->m_Drivers[i].Caps3D.MaxNumberTextureStage = i ? 1 : 3;
            Manager->m_Drivers[i].TextureFormats.PushBack(format);
        }
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&OtherState, 1));
        Outer = new RCKRenderContext(Context); Inner = new RCKRenderContext(Context);
        Outer->DetachAll(); Inner->DetachAll();
        Outer->m_RasterizerContext = &State; Inner->m_RasterizerContext = &OtherState;
        Outer->m_RasterizerDriver = Inner->m_RasterizerDriver = &Driver;
        Outer->m_DriverIndex = 0; Inner->m_DriverIndex = 1;
        Outer->m_DisplayWireframe = Inner->m_DisplayWireframe = FALSE;
    }
    void TearDown() override {
        if (Outer) { Outer->m_RasterizerContext = nullptr; Outer->m_RasterizerDriver = nullptr; delete Outer; }
        if (Inner) { Inner->m_RasterizerContext = nullptr; Inner->m_RasterizerDriver = nullptr; delete Inner; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static void Pixel(RCKTexture &texture, CKDWORD value) {
        ASSERT_TRUE(texture.CreateImage(1, 1, 32, 0)); CKBYTE *pixels = texture.LockSurfacePtr(0);
        ASSERT_NE(nullptr, pixels); memcpy(pixels, &value, sizeof(value));
        texture.ReleaseSurfacePtr(0); texture.SetDesiredVideoFormat(_32_ARGB8888);
    }
    static void Geometry(DirectGroupMesh &mesh, CKMaterial &first, CKMaterial *second = nullptr) {
        const int faces = second ? 2 : 1;
        ASSERT_TRUE(mesh.SetVertexCount(3 * faces)); ASSERT_TRUE(mesh.SetFaceCount(faces));
        for (int f = 0; f < faces; ++f) {
            mesh.SetFaceVertexIndex(f, f * 3, f * 3 + 1, f * 3 + 2);
            mesh.SetFaceMaterial(f, f ? second : &first);
            for (int v = 0; v < 3; ++v) {
                VxVector position(f * 10.0f + (v == 1 ? 1.0f : 0), v == 2 ? 1.0f : 0, 1);
                mesh.SetVertexPosition(f * 3 + v, &position);
                mesh.SetVertexTextureCoordinates(f * 3 + v, .125f + f * .0625f, .25f, -1);
            }
        }
        mesh.SetFlags((mesh.GetFlags() & ~(VXMESH_PRELITMODE | VXMESH_STRIPIFY)) | VXMESH_RENDERCHANNELS);
    }
    void Channel(DirectGroupMesh &mesh, DirectGroupMaterial &material, RCKTexture &texture, float u, float v) {
        material.Outer = Outer; Pixel(texture, 0xFF445566); material.SetTexture(0, &texture);
        const int channel = mesh.AddChannel(&material, FALSE); ASSERT_GE(channel, 0);
        mesh.ActivateChannel(channel, TRUE);
        mesh.SetChannelSourceBlend(channel, VXBLEND_ZERO); mesh.SetChannelDestBlend(channel, VXBLEND_SRCCOLOR);
        for (int vertex = 0; vertex < mesh.GetVertexCount(); ++vertex)
            mesh.SetVertexTextureCoordinates(vertex, u + (vertex >= 3 ? .0625f : 0), v, channel);
    }
    void Prepare(DirectGroupMesh &mesh, DirectGroupMaterial &base, DirectGroupMaterial &first,
                 DirectGroupMaterial &second, RCKTexture &primary, RCKTexture &a, RCKTexture &b,
                 CKMaterial *other = nullptr) {
        base.Outer = Outer; Pixel(primary, 0xFF112233); base.SetTexture(0, &primary);
        Geometry(mesh, base, other);
        Channel(mesh, first, a, .375f, .625f); Channel(mesh, second, b, .75f, .875f);
        ASSERT_EQ(1, mesh.DefaultRender(Outer, nullptr));
        ASSERT_EQ(2, mesh.ActiveCount()); EXPECT_EQ(0, mesh.ActiveAt(0)); EXPECT_EQ(1, mesh.ActiveAt(1));
        State.ResetDraws(); OtherState.ResetDraws(); base.ResetCalls(); first.ResetCalls(); second.ResetCalls();
    }
    static VxDrawPrimitiveData Packet(DirectGroupMesh &mesh) {
        VxDrawPrimitiveData data = {};
        data.VertexCount = mesh.GetVertexCount();
        data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_STAGE(2);
        data.PositionPtr = mesh.GetPositionsPtr(&data.PositionStride);
        data.NormalPtr = mesh.GetNormalsPtr(&data.NormalStride);
        data.TexCoordPtr = mesh.GetTextureCoordinatesPtr(&data.TexCoordStride, -1);
        data.ColorPtr = mesh.GetColorsPtr(&data.ColorStride);
        data.SpecularColorPtr = mesh.GetSpecularColorsPtr(&data.SpecularColorStride);
        for (int c = 0; c < 2; ++c)
            data.TexCoordPtrs[c] = mesh.GetTextureCoordinatesPtr(&data.TexCoordStrides[c], c);
        return data;
    }
    void Callback(DirectGroupMaterial &material, DirectGroupCall &call, DirectGroupMesh &mesh) {
        call.Mesh = &mesh; call.Outer = Outer; call.Inner = Inner;
        material.Call = &call; material.SetCallback(DirectGroupMaterialCallback, &call);
    }
    static void UV(const DirectGroupObserver::Draw &draw, int slot, float u, float v) {
        ASSERT_TRUE(draw.HasUV[slot]); EXPECT_FLOAT_EQ(u, draw.UV[slot].x); EXPECT_FLOAT_EQ(v, draw.UV[slot].y);
        EXPECT_GE(draw.Flags & CKRST_DP_STAGESMASK, static_cast<CKDWORD>(CKRST_DP_STAGE(slot)));
    }
    static void Plan(DirectGroupMesh &mesh) {
        ASSERT_EQ(2, mesh.ActiveCount()); EXPECT_EQ(0, mesh.ActiveAt(0)); EXPECT_EQ(1, mesh.ActiveAt(1));
        EXPECT_NE(0u, mesh.GetChannelFlags(0) & VXCHANNEL_MONO);
        EXPECT_NE(0u, mesh.GetChannelFlags(1) & VXCHANNEL_MONO);
    }
};
}

TEST_F(MeshDirectGroupReentryTest, BaseCallbackInnerDefaultRetainsThePreparedOuterPlan) {
    DirectGroupMesh mesh(Context); DirectGroupMaterial base(Context), first(Context), second(Context);
    RCKTexture primary(Context), a(Context), b(Context);
    Prepare(mesh, base, first, second, primary, a, b);
    VxDrawPrimitiveData data = Packet(mesh); CKMaterialGroup *group = mesh.Group(&base); ASSERT_NE(nullptr, group);
    DirectGroupCall call; Callback(base, call, mesh);
    ASSERT_EQ(1, mesh.RenderGroup(Outer, group, nullptr, &data));
    EXPECT_EQ(1, call.Entries); EXPECT_EQ(1, call.Result); EXPECT_EQ(2, call.ActiveAfterNested);
    ASSERT_EQ(1, State.Count); EXPECT_EQ(3, OtherState.Count);
    EXPECT_EQ(1, first.Calls[0][1]); EXPECT_EQ(1, second.Calls[0][2]);
    EXPECT_NE(0u, State.Draws[0].Textures[1]); EXPECT_NE(0u, State.Draws[0].Textures[2]);
    UV(State.Draws[0], 1, .375f, .625f); UV(State.Draws[0], 2, .75f, .875f); Plan(mesh);
}

TEST_F(MeshDirectGroupReentryTest, PositiveStageCallbackInnerDefaultRetainsTheRemainingFoldedStage) {
    DirectGroupMesh mesh(Context); DirectGroupMaterial base(Context), first(Context), second(Context);
    RCKTexture primary(Context), a(Context), b(Context);
    Prepare(mesh, base, first, second, primary, a, b);
    VxDrawPrimitiveData data = Packet(mesh); CKMaterialGroup *group = mesh.Group(&base); ASSERT_NE(nullptr, group);
    DirectGroupCall call; call.TriggerStage = 1; Callback(first, call, mesh);
    ASSERT_EQ(1, mesh.RenderGroup(Outer, group, nullptr, &data));
    EXPECT_EQ(1, call.Entries); EXPECT_EQ(2, call.ActiveAfterNested);
    ASSERT_EQ(1, State.Count); EXPECT_EQ(3, OtherState.Count);
    EXPECT_EQ(1, first.Calls[0][1]); EXPECT_EQ(1, second.Calls[0][2]);
    UV(State.Draws[0], 1, .375f, .625f); UV(State.Draws[0], 2, .75f, .875f); Plan(mesh);
}

TEST_F(MeshDirectGroupReentryTest, OwningSubmeshCallbackKeepsSecondGroupsRemappedPrivateUVsAfterInnerDefault) {
    DirectGroupMesh mesh(Context); DirectGroupMaterial base(Context), first(Context), second(Context);
    RCKMaterial other(Context); RCKTexture primary(Context), a(Context), b(Context);
    Prepare(mesh, base, first, second, primary, a, b, &other);
    VxDrawPrimitiveData data = Packet(mesh); CKMaterialGroup *group = mesh.Group(&other); ASSERT_NE(nullptr, group);
    DirectGroupCall call; call.Mesh = &mesh; call.Outer = Outer; call.Inner = Inner;
    ASSERT_TRUE(mesh.AddSubMeshPreRenderCallBack(DirectGroupPreCallback, &call, FALSE));
    ASSERT_EQ(1, mesh.RenderGroup(Outer, group, nullptr, &data));
    EXPECT_EQ(1, call.Entries); EXPECT_EQ(2, call.ActiveAfterNested);
    EXPECT_EQ(1, call.OuterPreCalls); EXPECT_EQ(2, call.InnerPreCalls);
    ASSERT_EQ(1, State.Count); EXPECT_EQ(4, OtherState.Count);
    EXPECT_FLOAT_EQ(10, State.Draws[0].X); EXPECT_EQ(3, State.Draws[0].Indices);
    EXPECT_EQ(901u, State.Draws[0].Textures[0]); EXPECT_EQ(902u, State.Draws[0].Textures[1]);
    UV(State.Draws[0], 1, .4375f, .625f); UV(State.Draws[0], 2, .8125f, .875f); Plan(mesh);
}

TEST_F(MeshDirectGroupReentryTest, CaughtInnerDefaultDrawExceptionRestoresTheDirectOuterPlan) {
    DirectGroupMesh mesh(Context); DirectGroupMaterial base(Context), first(Context), second(Context);
    RCKTexture primary(Context), a(Context), b(Context);
    Prepare(mesh, base, first, second, primary, a, b);
    VxDrawPrimitiveData data = Packet(mesh); CKMaterialGroup *group = mesh.Group(&base); ASSERT_NE(nullptr, group);
    DirectGroupCall call; call.CatchThrow = TRUE; Callback(base, call, mesh); OtherState.ThrowNext = TRUE;
    ASSERT_EQ(1, mesh.RenderGroup(Outer, group, nullptr, &data));
    EXPECT_TRUE(call.Caught); EXPECT_EQ(1, call.Entries); EXPECT_EQ(2, call.ActiveAfterNested);
    EXPECT_EQ(1, OtherState.Attempts); EXPECT_EQ(0, OtherState.Count);
    ASSERT_EQ(1, State.Count); EXPECT_EQ(1, second.Calls[0][2]);
    UV(State.Draws[0], 1, .375f, .625f); UV(State.Draws[0], 2, .75f, .875f); Plan(mesh);
}

TEST_F(MeshDirectGroupReentryTest, InnerDirectGroupCanConsumeThePreparedPlanWithoutClearingItAtEntry) {
    Manager->m_Drivers[1].Caps3D.MaxNumberTextureStage = 3;
    DirectGroupMesh mesh(Context); DirectGroupMaterial base(Context), first(Context), second(Context);
    RCKTexture primary(Context), a(Context), b(Context);
    Prepare(mesh, base, first, second, primary, a, b);
    VxDrawPrimitiveData data = Packet(mesh), innerData = Packet(mesh);
    CKMaterialGroup *group = mesh.Group(&base); ASSERT_NE(nullptr, group);
    DirectGroupCall call; call.Direct = TRUE; call.InnerGroup = group; call.InnerData = &innerData;
    Callback(base, call, mesh);
    ASSERT_EQ(1, mesh.RenderGroup(Outer, group, nullptr, &data));
    EXPECT_EQ(1, call.Entries); EXPECT_EQ(1, call.Result); EXPECT_EQ(2, call.ActiveAfterNested);
    ASSERT_EQ(1, State.Count); ASSERT_EQ(1, OtherState.Count);
    EXPECT_EQ(1, first.Calls[0][1]); EXPECT_EQ(1, second.Calls[0][2]);
    EXPECT_EQ(1, first.Calls[1][1]); EXPECT_EQ(1, second.Calls[1][2]);
    UV(State.Draws[0], 2, .75f, .875f); UV(OtherState.Draws[0], 2, .75f, .875f); Plan(mesh);
}

TEST_F(MeshDirectGroupReentryTest, CaughtInnerDirectGroupDrawExceptionKeepsTheOuterInputPlan) {
    Manager->m_Drivers[1].Caps3D.MaxNumberTextureStage = 3;
    DirectGroupMesh mesh(Context); DirectGroupMaterial base(Context), first(Context), second(Context);
    RCKTexture primary(Context), a(Context), b(Context);
    Prepare(mesh, base, first, second, primary, a, b);
    VxDrawPrimitiveData data = Packet(mesh), innerData = Packet(mesh);
    CKMaterialGroup *group = mesh.Group(&base); ASSERT_NE(nullptr, group);
    DirectGroupCall call; call.Direct = TRUE; call.CatchThrow = TRUE;
    call.InnerGroup = group; call.InnerData = &innerData; Callback(base, call, mesh);
    OtherState.ThrowNext = TRUE;
    ASSERT_EQ(1, mesh.RenderGroup(Outer, group, nullptr, &data));
    EXPECT_TRUE(call.Caught); EXPECT_EQ(1, call.Entries); EXPECT_EQ(2, call.ActiveAfterNested);
    EXPECT_EQ(1, OtherState.Attempts); EXPECT_EQ(0, OtherState.Count);
    EXPECT_EQ(1, first.Calls[1][1]); EXPECT_EQ(1, second.Calls[1][2]);
    ASSERT_EQ(1, State.Count); UV(State.Draws[0], 1, .375f, .625f); UV(State.Draws[0], 2, .75f, .875f); Plan(mesh);
}
