#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "TextureTestObserver.h"

namespace {
class ReentryDrawObserver : public TextureTestObserver {
public:
    struct Draw {
        CKDWORD Textures[3] = {}, Flags = 0, Cull = 0;
        Vx2DVector UV[3];
        CKBOOL HasUV[3] = {};
        float X = 0;
    };
    Draw Draws[32];
    int Count = 0, Attempts = 0, ThrowAt = -1;
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count,
                         VxDrawPrimitiveData *data) override {
        ++Attempts;
        if (Count == ThrowAt)
            throw 87;
        EXPECT_EQ(VX_TRIANGLELIST, type);
        EXPECT_EQ(3, count);
        EXPECT_NE(nullptr, indices);
        EXPECT_NE(nullptr, data);
        EXPECT_LT(Count, 32);
        if (!indices || !data || count != 3 || Count >= 32)
            return FALSE;
        const int vertex = indices[0];
        EXPECT_LT(vertex, data->VertexCount);
        Draw &draw = Draws[Count++];
        draw.Flags = data->Flags;
        draw.Cull = States[VXRENDERSTATE_CULLMODE];
        memcpy(&draw.X, static_cast<CKBYTE *>(data->PositionPtr) +
            vertex * data->PositionStride, sizeof(float));
        for (int stage = 0; stage < 3; ++stage) {
            draw.Textures[stage] = Textures[stage];
            void *uv = stage ? data->TexCoordPtrs[stage - 1] : data->TexCoordPtr;
            const CKDWORD stride = stage ? data->TexCoordStrides[stage - 1] : data->TexCoordStride;
            if (uv) {
                draw.HasUV[stage] = TRUE;
                memcpy(&draw.UV[stage], static_cast<CKBYTE *>(uv) + vertex * stride,
                       sizeof(Vx2DVector));
            }
        }
        return TRUE;
    }
};

struct ReentryCall {
    RCKMesh *Mesh = nullptr;
    RCKRenderContext *Outer = nullptr, *Inner = nullptr;
    ReentryDrawObserver *OuterState = nullptr;
    int Stage = 0, FireStage = 1, MinimumOuterDraws = 0;
    int Entries = 0, NestedResult = 0;
    CKBOOL Fired = FALSE, CatchThrow = FALSE, Caught = FALSE;

    void Enter(CKRenderContext *context) {
        if (context != Outer || Fired || Stage != FireStage ||
            OuterState->Count < MinimumOuterDraws)
            return;
        Fired = TRUE;
        ++Entries;
        try {
            NestedResult = Mesh->DefaultRender(Inner, nullptr);
        } catch (int value) {
            if (!CatchThrow)
                throw;
            EXPECT_EQ(87, value);
            Caught = TRUE;
        }
    }
};

int ReentryMaterialCallback(CKRenderContext *context, CKMaterial *, void *argument) {
    static_cast<ReentryCall *>(argument)->Enter(context);
    return FALSE;
}

void ReentryPostCallback(CKRenderContext *context, CK3dEntity *, CKMesh *,
                         CKMaterial *, void *argument) {
    static_cast<ReentryCall *>(argument)->Enter(context);
}

class ReentryMaterial : public RCKMaterial {
public:
    RCKRenderContext *Outer = nullptr;
    ReentryCall *Call = nullptr;
    int Calls[2][CKRST_MAX_TEXTURE_STAGES] = {};
    explicit ReentryMaterial(CKContext *context) : RCKMaterial(context) {}
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL lit, int stage) override {
        ++Calls[context == Outer ? 0 : 1][stage];
        if (!Call)
            return RCKMaterial::SetAsCurrent(context, lit, stage);
        struct RestoreStage {
            ReentryCall &Call;
            int Saved;
            explicit RestoreStage(ReentryCall &call, int stage)
                : Call(call), Saved(call.Stage) { Call.Stage = stage; }
            ~RestoreStage() { Call.Stage = Saved; }
        } restore(*Call, stage);
        return RCKMaterial::SetAsCurrent(context, lit, stage);
    }
};

class MeshRenderReentryTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Outer = nullptr, *Inner = nullptr;
    ReentryDrawObserver State, OtherState;
    CKRasterizerDriver Driver;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        Manager->m_DriverCount = 2;
        Manager->m_Drivers = new VxDriverDescEx[2]();
        VxImageDescEx alpha;
        VxPixelFormat2ImageDesc(_32_ARGB8888, alpha);
        for (int i = 0; i < 2; ++i) {
            Manager->m_Drivers[i].CapsUpToDate = TRUE;
            Manager->m_Drivers[i].RasterizerDriver = &Driver;
            Manager->m_Drivers[i].Caps3D.MaxNumberTextureStage = i ? 1 : 3;
            Manager->m_Drivers[i].TextureFormats.PushBack(alpha);
        }
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&OtherState, 1));
        Outer = new RCKRenderContext(Context);
        Inner = new RCKRenderContext(Context);
        Outer->DetachAll(); Inner->DetachAll();
        Outer->m_RasterizerContext = &State;
        Inner->m_RasterizerContext = &OtherState;
        Outer->m_RasterizerDriver = Inner->m_RasterizerDriver = &Driver;
        Outer->m_DriverIndex = 0; Inner->m_DriverIndex = 1;
        Outer->m_DisplayWireframe = Inner->m_DisplayWireframe = FALSE;
    }
    void TearDown() override {
        if (Outer) {
            Outer->m_RasterizerContext = nullptr; Outer->m_RasterizerDriver = nullptr;
            delete Outer;
        }
        if (Inner) {
            Inner->m_RasterizerContext = nullptr; Inner->m_RasterizerDriver = nullptr;
            delete Inner;
        }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static void Pixel(RCKTexture &texture, CKDWORD value) {
        ASSERT_TRUE(texture.CreateImage(1, 1, 32, 0));
        CKBYTE *bytes = texture.LockSurfacePtr(0);
        ASSERT_NE(nullptr, bytes);
        memcpy(bytes, &value, sizeof(value));
        texture.ReleaseSurfacePtr(0);
        texture.SetDesiredVideoFormat(_32_ARGB8888);
    }
    static void Geometry(RCKMesh &mesh, RCKMaterial &base, RCKMaterial *second = nullptr) {
        const int faces = second ? 2 : 1;
        ASSERT_TRUE(mesh.SetVertexCount(faces * 3));
        ASSERT_TRUE(mesh.SetFaceCount(faces));
        for (int f = 0; f < faces; ++f) {
            mesh.SetFaceVertexIndex(f, f * 3, f * 3 + 1, f * 3 + 2);
            mesh.SetFaceMaterial(f, f ? second : &base);
            for (int v = 0; v < 3; ++v) {
                VxVector point(f * 10.0f + (v == 1 ? 1.0f : 0), v == 2 ? 1.0f : 0, 1);
                mesh.SetVertexPosition(f * 3 + v, &point);
                mesh.SetVertexTextureCoordinates(f * 3 + v, .125f + f * .0625f, .25f, -1);
            }
        }
        mesh.SetFlags((mesh.GetFlags() & ~(VXMESH_PRELITMODE | VXMESH_STRIPIFY)) | VXMESH_RENDERCHANNELS);
    }
    int Channel(RCKMesh &mesh, ReentryMaterial &material, float u, float v) {
        material.Outer = Outer;
        const int channel = mesh.AddChannel(&material, FALSE);
        EXPECT_GE(channel, 0);
        if (channel < 0) return channel;
        mesh.ActivateChannel(channel, TRUE);
        mesh.SetChannelSourceBlend(channel, VXBLEND_ZERO);
        mesh.SetChannelDestBlend(channel, VXBLEND_SRCCOLOR);
        for (int vertex = 0; vertex < mesh.GetVertexCount(); ++vertex)
            mesh.SetVertexTextureCoordinates(vertex, u + (vertex >= 3 ? .0625f : 0), v, channel);
        return channel;
    }
    void Callback(ReentryMaterial &material, ReentryCall &call, RCKMesh &mesh) {
        call.Mesh = &mesh; call.Outer = Outer; call.Inner = Inner; call.OuterState = &State;
        material.Call = &call;
        material.SetCallback(ReentryMaterialCallback, &call);
    }
    static void UV(const ReentryDrawObserver::Draw &draw, int slot, float u, float v) {
        ASSERT_TRUE(draw.HasUV[slot]);
        EXPECT_FLOAT_EQ(u, draw.UV[slot].x);
        EXPECT_FLOAT_EQ(v, draw.UV[slot].y);
        EXPECT_GE(draw.Flags & CKRST_DP_STAGESMASK, static_cast<CKDWORD>(CKRST_DP_STAGE(slot)));
    }
};
}

TEST_F(MeshRenderReentryTest, PositiveStageCallbackRetainsOuterFoldedSuffixAndItsUVs) {
    RCKMesh mesh(Context); RCKMaterial base(Context);
    ReentryMaterial first(Context), second(Context);
    RCKTexture primary(Context), texture1(Context), texture2(Context);
    Pixel(primary, 0xFF112233); Pixel(texture1, 0xFF445566); Pixel(texture2, 0xFF778899);
    base.SetTexture(0, &primary); first.SetTexture(0, &texture1); second.SetTexture(0, &texture2);
    Geometry(mesh, base);
    const int a = Channel(mesh, first, .375f, .625f), b = Channel(mesh, second, .75f, .875f);
    ReentryCall call; Callback(first, call, mesh);
    ASSERT_EQ(1, mesh.DefaultRender(Outer, nullptr));
    EXPECT_EQ(1, call.Entries); EXPECT_EQ(1, call.NestedResult);
    ASSERT_EQ(1, State.Count); EXPECT_EQ(3, OtherState.Count);
    EXPECT_EQ(1, first.Calls[0][1]); EXPECT_EQ(1, second.Calls[0][2]);
    EXPECT_EQ(1, first.Calls[1][0]); EXPECT_EQ(1, second.Calls[1][0]);
    EXPECT_NE(0u, State.Draws[0].Textures[1]); EXPECT_NE(0u, State.Draws[0].Textures[2]);
    UV(State.Draws[0], 1, .375f, .625f); UV(State.Draws[0], 2, .75f, .875f);
    EXPECT_NE(0u, mesh.GetChannelFlags(a) & VXCHANNEL_MONO);
    EXPECT_NE(0u, mesh.GetChannelFlags(b) & VXCHANNEL_MONO);
    // Native channel multipass marks modulation channels NOTLIT persistently.
    // Reentry restores the draw plan, without undoing that channel state.
    EXPECT_NE(0u, mesh.GetChannelFlags(a) & VXCHANNEL_NOTLIT);
    EXPECT_NE(0u, mesh.GetChannelFlags(b) & VXCHANNEL_NOTLIT);
}

TEST_F(MeshRenderReentryTest, PostSubmeshReentryDoesNotMarkOuterMultipassLayersAsMono) {
    Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 1;
    Manager->m_Drivers[1].Caps3D.MaxNumberTextureStage = 3;
    RCKMesh mesh(Context); RCKMaterial base(Context);
    ReentryMaterial first(Context), second(Context);
    RCKTexture primary(Context), texture1(Context), texture2(Context);
    Pixel(primary, 0xFF112233); Pixel(texture1, 0xFF445566); Pixel(texture2, 0xFF778899);
    base.SetTexture(0, &primary); first.SetTexture(0, &texture1); second.SetTexture(0, &texture2);
    Geometry(mesh, base); Channel(mesh, first, .375f, .625f); Channel(mesh, second, .75f, .875f);
    ReentryCall call; call.Mesh = &mesh; call.Outer = Outer; call.Inner = Inner;
    call.OuterState = &State; call.FireStage = 0;
    ASSERT_TRUE(mesh.AddSubMeshPostRenderCallBack(ReentryPostCallback, &call, FALSE));
    ASSERT_EQ(1, mesh.DefaultRender(Outer, nullptr));
    EXPECT_EQ(1, call.Entries); EXPECT_EQ(1, call.NestedResult);
    ASSERT_EQ(3, State.Count); EXPECT_EQ(1, OtherState.Count);
    EXPECT_EQ(1, first.Calls[0][0]); EXPECT_EQ(1, second.Calls[0][0]);
    EXPECT_EQ(1, first.Calls[1][1]); EXPECT_EQ(1, second.Calls[1][2]);
    UV(State.Draws[1], 0, .375f, .625f); UV(State.Draws[2], 0, .75f, .875f);
    EXPECT_EQ(0u, mesh.GetChannelFlags(0) & VXCHANNEL_MONO);
    EXPECT_EQ(0u, mesh.GetChannelFlags(1) & VXCHANNEL_MONO);
}

TEST_F(MeshRenderReentryTest, MixedTransparentFrontRetainsPrivateProviderUVAfterBackLayerReentry) {
    Manager->m_Drivers[1].Caps3D.MaxNumberTextureStage = 2;
    Manager->m_Drivers[1].Caps3D.MaxNumberBlendStage = 2;
    RCKMesh mesh(Context); RCKMaterial plain(Context), combined(Context);
    ReentryMaterial provider(Context), layer(Context);
    RCKTexture primary(Context), extra(Context), texture1(Context), texture2(Context);
    Pixel(primary, 0xFF112233); Pixel(extra, 0xFF445566);
    Pixel(texture1, 0xFF778899); Pixel(texture2, 0xFFABCDEF);
    combined.SetTexture(0, &primary); combined.SetTexture(1, &extra);
    combined.SetEffect(VXEFFECT_2TEXTURES);
    combined.SetTwoSided(TRUE); combined.SetFillMode(VXFILL_SOLID);
    combined.EnableAlphaBlend(TRUE); combined.SetSourceBlend(VXBLEND_SRCALPHA);
    combined.SetDestBlend(VXBLEND_INVSRCALPHA);
    provider.SetTexture(0, &texture1); layer.SetTexture(0, &texture2);
    Geometry(mesh, plain, &combined);
    Channel(mesh, provider, .375f, .625f); Channel(mesh, layer, .75f, .875f);
    ReentryCall call; call.FireStage = 0; call.MinimumOuterDraws = 4;
    Callback(layer, call, mesh);
    ASSERT_EQ(1, mesh.DefaultRender(Outer, nullptr));
    EXPECT_EQ(1, call.Entries); EXPECT_EQ(1, call.NestedResult);
    ASSERT_EQ(7, State.Count); EXPECT_EQ(7, OtherState.Count);
    EXPECT_FLOAT_EQ(10, State.Draws[3].X); EXPECT_FLOAT_EQ(10, State.Draws[5].X);
    EXPECT_EQ(VXCULL_CW, State.Draws[3].Cull); EXPECT_EQ(VXCULL_CCW, State.Draws[5].Cull);
    UV(State.Draws[3], 1, .4375f, .625f); UV(State.Draws[5], 1, .4375f, .625f);
    EXPECT_NE(0u, State.Draws[3].Textures[1]);
    EXPECT_EQ(State.Draws[3].Textures[1], State.Draws[5].Textures[1]);
}

TEST_F(MeshRenderReentryTest, CaughtNestedDrawExceptionRestoresOuterSelectionBeforeContinuing) {
    RCKMesh mesh(Context); RCKMaterial base(Context);
    ReentryMaterial first(Context), second(Context);
    RCKTexture primary(Context), texture1(Context), texture2(Context);
    Pixel(primary, 0xFF112233); Pixel(texture1, 0xFF445566); Pixel(texture2, 0xFF778899);
    base.SetTexture(0, &primary); first.SetTexture(0, &texture1); second.SetTexture(0, &texture2);
    Geometry(mesh, base); Channel(mesh, first, .375f, .625f); Channel(mesh, second, .75f, .875f);
    ReentryCall call; call.CatchThrow = TRUE; Callback(first, call, mesh);
    OtherState.ThrowAt = 0;
    ASSERT_EQ(1, mesh.DefaultRender(Outer, nullptr));
    EXPECT_EQ(1, call.Entries); EXPECT_TRUE(call.Caught);
    EXPECT_EQ(1, OtherState.Attempts); EXPECT_EQ(0, OtherState.Count);
    ASSERT_EQ(1, State.Count); EXPECT_EQ(1, second.Calls[0][2]);
    UV(State.Draws[0], 1, .375f, .625f); UV(State.Draws[0], 2, .75f, .875f);
}
