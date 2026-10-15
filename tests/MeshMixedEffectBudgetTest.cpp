#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "TextureTestObserver.h"

namespace {
class MixedBudgetObserver : public TextureTestObserver {
public:
    struct Draw {
        CKDWORD Textures[CKRST_MAX_TEXTURE_STAGES] = {};
        CKDWORD Operations[CKRST_MAX_TEXTURE_STAGES] = {};
        CKDWORD Coordinates[CKRST_MAX_TEXTURE_STAGES] = {};
        CKDWORD Flags = 0;
        CKDWORD Cull = 0, Source = 0, Dest = 0;
        Vx2DVector UVs[CKRST_MAX_TEXTURE_STAGES];
        CKBOOL HasUV[CKRST_MAX_TEXTURE_STAGES] = {};
        int IndexCount = 0;
        float FirstX = 0;
    };
    Draw Draws[32];
    int DrawCount = 0;
    int RejectedStage = -1;
    int Rejections = 0;
    CKBOOL SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD value) override {
        if (state == CKRST_TSS_STAGEBLEND &&
            (value == STAGEBLEND(VXBLEND_ZERO, VXBLEND_SRCCOLOR) ||
             value == STAGEBLEND(VXBLEND_DESTCOLOR, VXBLEND_ZERO))) {
            if (stage == RejectedStage) { ++Rejections; return FALSE; }
            StageStates[stage][CKRST_TSS_OP] = CKRST_TOP_MODULATE;
            StageStates[stage][CKRST_TSS_ARG1] = CKRST_TA_TEXTURE;
            StageStates[stage][CKRST_TSS_ARG2] = CKRST_TA_CURRENT;
        }
        if (state == CKRST_TSS_TEXTUREMAPBLEND && value == VXTEXTUREBLEND_MODULATEALPHA)
            StageStates[stage][CKRST_TSS_OP] = CKRST_TOP_MODULATE;
        return TextureTestObserver::SetTextureStageState(stage, state, value);
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type);
        EXPECT_NE(nullptr, indices); EXPECT_NE(nullptr, data);
        EXPECT_LT(DrawCount, 32);
        if (!indices || !data || !count || DrawCount >= 32) return FALSE;
        Draw &draw = Draws[DrawCount++];
        draw.Flags = data->Flags; draw.IndexCount = count;
        draw.Cull = States[VXRENDERSTATE_CULLMODE];
        draw.Source = States[VXRENDERSTATE_SRCBLEND]; draw.Dest = States[VXRENDERSTATE_DESTBLEND];
        const int vertex = indices[0];
        EXPECT_LT(vertex, data->VertexCount);
        if (data->PositionPtr)
            memcpy(&draw.FirstX, static_cast<CKBYTE *>(data->PositionPtr) + vertex * data->PositionStride, sizeof(float));
        for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) {
            draw.Textures[stage] = Textures[stage];
            draw.Operations[stage] = StageStates[stage][CKRST_TSS_OP];
            draw.Coordinates[stage] = StageStates[stage][CKRST_TSS_TEXCOORDINDEX];
            void *uv = stage ? data->TexCoordPtrs[stage - 1] : data->TexCoordPtr;
            CKDWORD stride = stage ? data->TexCoordStrides[stage - 1] : data->TexCoordStride;
            if (uv) {
                draw.HasUV[stage] = TRUE;
                memcpy(&draw.UVs[stage], static_cast<CKBYTE *>(uv) + vertex * stride, sizeof(Vx2DVector));
            }
        }
        return TRUE;
    }
};

class CountingMixedMaterial : public RCKMaterial {
public:
    int Calls[CKRST_MAX_TEXTURE_STAGES] = {};
    explicit CountingMixedMaterial(CKContext *context) : RCKMaterial(context) {}
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL lit, int stage) override {
        ++Calls[stage];
        return RCKMaterial::SetAsCurrent(context, lit, stage);
    }
};

class OwnedMixedMaterial : public RCKMaterial {
public:
    int Calls = 0;
    explicit OwnedMixedMaterial(CKContext *context) : RCKMaterial(context) {}
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL, int stage) override {
        ++Calls; EXPECT_EQ(0, stage);
        CKRasterizerContext *state = static_cast<RCKRenderContext *>(context)->m_RasterizerContext;
        state->SetTexture(901, 0); state->SetTexture(902, 1);
        state->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE);
        state->SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_ADD);
        state->SetTextureStageState(0, CKRST_TSS_TEXCOORDINDEX, 0);
        state->SetTextureStageState(1, CKRST_TSS_TEXCOORDINDEX, 0);
        state->SetTextureStageState(2, CKRST_TSS_OP, CKRST_TOP_DISABLE);
        return TRUE;
    }
};

class MeshMixedEffectBudgetTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    MixedBudgetObserver State;
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
        Manager->m_DriverCount = 1; Manager->m_Drivers = new VxDriverDescEx[1]();
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 4;
        VxImageDescEx alpha; VxPixelFormat2ImageDesc(_32_ARGB8888, alpha);
        Manager->m_Drivers[0].TextureFormats.PushBack(alpha);
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        Device = new RCKRenderContext(Context); Device->DetachAll();
        Device->m_RasterizerContext = &State; Device->m_RasterizerDriver = &Driver;
        Device->m_DriverIndex = 0; Device->m_DisplayWireframe = FALSE;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    static void Pixel(RCKTexture &texture, CKDWORD color) {
        ASSERT_TRUE(texture.CreateImage(1, 1, 32, 0));
        CKBYTE *pixels = texture.LockSurfacePtr(0); ASSERT_NE(nullptr, pixels);
        memcpy(pixels, &color, sizeof(color)); texture.ReleaseSurfacePtr(0);
        texture.SetDesiredVideoFormat(_32_ARGB8888);
    }
    static void Geometry(RCKMesh &mesh, RCKMaterial &first, RCKMaterial *second = nullptr) {
        const int faces = second ? 2 : 1;
        ASSERT_TRUE(mesh.SetVertexCount(faces * 3)); ASSERT_TRUE(mesh.SetFaceCount(faces));
        for (int face = 0; face < faces; ++face) {
            const int start = face * 3;
            mesh.SetFaceVertexIndex(face, start, start + 1, start + 2);
            mesh.SetFaceMaterial(face, face ? second : &first);
            for (int i = 0; i < 3; ++i) {
                VxVector point(face * 10.0f + (i == 1 ? 1.0f : 0.0f), i == 2 ? 1.0f : 0.0f, 1.0f);
                mesh.SetVertexPosition(start + i, &point);
                mesh.SetVertexTextureCoordinates(start + i, .125f + face * .0625f, .25f, -1);
            }
        }
        mesh.SetFlags((mesh.GetFlags() & ~(VXMESH_PRELITMODE | VXMESH_STRIPIFY)) | VXMESH_RENDERCHANNELS);
    }
    static int Channel(RCKMesh &mesh, RCKMaterial &material, float u, float v) {
        int index = mesh.AddChannel(&material, FALSE); EXPECT_GE(index, 0);
        if (index < 0) return index;
        mesh.ActivateChannel(index, TRUE);
        mesh.SetChannelSourceBlend(index, VXBLEND_ZERO);
        mesh.SetChannelDestBlend(index, VXBLEND_SRCCOLOR);
        for (int i = 0; i < mesh.GetVertexCount(); ++i)
            mesh.SetVertexTextureCoordinates(i, u + (i >= 3 ? .0625f : 0), v, index);
        return index;
    }
    const MixedBudgetObserver::Draw *Find(CKDWORD texture, float x, int ordinal = 0) {
        for (int i = 0; i < State.DrawCount; ++i)
            if (State.Draws[i].Textures[0] == texture && State.Draws[i].FirstX == x && ordinal-- == 0)
                return &State.Draws[i];
        return nullptr;
    }
    static void Bound(CKDWORD actual, RCKTexture &texture) {
        EXPECT_NE(0u, texture.GetRstTextureIndex()); EXPECT_EQ(texture.GetRstTextureIndex(), actual);
    }
    static void UV(const MixedBudgetObserver::Draw &draw, int slot, float u, float v) {
        ASSERT_TRUE(draw.HasUV[slot]); EXPECT_FLOAT_EQ(u, draw.UVs[slot].x); EXPECT_FLOAT_EQ(v, draw.UVs[slot].y);
        EXPECT_GE(draw.Flags & CKRST_DP_STAGESMASK, static_cast<CKDWORD>(CKRST_DP_STAGE(slot)));
    }
};
}

TEST_F(MeshMixedEffectBudgetTest, CombineTwoReservesItsProviderAtTheExactTwoStageBudget) {
    RCKMesh mesh(Context); CountingMixedMaterial base(Context), provider(Context);
    RCKTexture primary(Context), extra(Context);
    Pixel(primary, 0xFF112233); Pixel(extra, 0xFF445566);
    base.SetTexture(0, &primary); base.SetTexture(1, &extra); base.SetEffect(VXEFFECT_2TEXTURES);
    Geometry(mesh, base); Channel(mesh, provider, .375f, .625f);
    Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 2;
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
    Bound(State.Draws[0].Textures[1], extra); UV(State.Draws[0], 1, .375f, .625f);
    EXPECT_EQ(0, provider.Calls[0]); EXPECT_EQ(0, provider.Calls[1]);
}

TEST_F(MeshMixedEffectBudgetTest, CombineThreeReservesBothProvidersAtTheExactThreeStageBudget) {
    RCKMesh mesh(Context); RCKMaterial base(Context), first(Context), second(Context);
    RCKTexture primary(Context), extra1(Context), extra2(Context);
    Pixel(primary, 0xFF112233); Pixel(extra1, 0xFF445566); Pixel(extra2, 0xFF778899);
    base.SetTexture(0, &primary); base.SetTexture(1, &extra1); base.SetTexture(2, &extra2);
    base.SetEffect(VXEFFECT_3TEXTURES); Geometry(mesh, base);
    Channel(mesh, first, .375f, .625f); Channel(mesh, second, .75f, .875f);
    Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 3;
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
    Bound(State.Draws[0].Textures[1], extra1); Bound(State.Draws[0].Textures[2], extra2);
    UV(State.Draws[0], 1, .375f, .625f); UV(State.Draws[0], 2, .75f, .875f);
}

TEST_F(MeshMixedEffectBudgetTest, CombineTwoAppendsAnIndependentLayerOnlyAfterItsReservedStage) {
    RCKMesh mesh(Context); RCKMaterial base(Context), provider(Context), tail(Context);
    RCKTexture primary(Context), extra(Context), last(Context);
    Pixel(primary, 0xFF112233); Pixel(extra, 0xFF445566); Pixel(last, 0xFF778899);
    base.SetTexture(0, &primary); base.SetTexture(1, &extra); base.SetEffect(VXEFFECT_2TEXTURES);
    tail.SetTexture(0, &last); Geometry(mesh, base);
    Channel(mesh, provider, .375f, .625f); Channel(mesh, tail, .75f, .875f);
    Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 3;
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
    Bound(State.Draws[0].Textures[1], extra); Bound(State.Draws[0].Textures[2], last);
    UV(State.Draws[0], 1, .375f, .625f); UV(State.Draws[0], 2, .75f, .875f);
    EXPECT_EQ(CKRST_TOP_MODULATE, State.Draws[0].Operations[2]);
}

TEST_F(MeshMixedEffectBudgetTest, FullCombineThreeBudgetDefersOnlyTheIndependentTail) {
    RCKMesh mesh(Context); RCKMaterial base(Context), first(Context), second(Context), tail(Context);
    RCKTexture primary(Context), extra1(Context), extra2(Context), last(Context);
    Pixel(primary, 0xFF112233); Pixel(extra1, 0xFF445566); Pixel(extra2, 0xFF778899); Pixel(last, 0xFF99AABB);
    base.SetTexture(0, &primary); base.SetTexture(1, &extra1); base.SetTexture(2, &extra2);
    base.SetEffect(VXEFFECT_3TEXTURES); tail.SetTexture(0, &last); Geometry(mesh, base);
    Channel(mesh, first, .375f, .625f); Channel(mesh, second, .75f, .875f); Channel(mesh, tail, .5f, .75f);
    Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 3;
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.DrawCount);
    Bound(State.Draws[0].Textures[1], extra1); Bound(State.Draws[0].Textures[2], extra2);
    UV(State.Draws[0], 1, .375f, .625f); UV(State.Draws[0], 2, .75f, .875f);
    Bound(State.Draws[1].Textures[0], last); UV(State.Draws[1], 0, .5f, .75f);
    EXPECT_EQ(0u, State.Draws[1].Textures[1]);
}

TEST_F(MeshMixedEffectBudgetTest, MixedOrdinaryAndCombineTwoKeepTexturelessProviderOnlyForTheEffectGroup) {
    RCKMesh mesh(Context); RCKMaterial plain(Context), combined(Context), provider(Context);
    RCKTexture plainTexture(Context), primary(Context), extra(Context);
    Pixel(plainTexture, 0xFF112233); Pixel(primary, 0xFF445566); Pixel(extra, 0xFF778899);
    plain.SetTexture(0, &plainTexture); combined.SetTexture(0, &primary);
    combined.SetTexture(1, &extra); combined.SetEffect(VXEFFECT_2TEXTURES);
    Geometry(mesh, plain, &combined); Channel(mesh, provider, .375f, .625f);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr));
    const MixedBudgetObserver::Draw *effect = Find(primary.GetRstTextureIndex(), 10);
    ASSERT_NE(nullptr, effect); Bound(effect->Textures[1], extra);
    UV(*effect, 1, .4375f, .625f);
    // The textureless independent layer remains authored on the ordinary face.
    // It must never be replayed over the face whose effect uses its UVs.
    int effectFaceDraws = 0;
    for (int i = 0; i < State.DrawCount; ++i) {
        if (State.Draws[i].FirstX == 10) ++effectFaceDraws;
        EXPECT_EQ(3, State.Draws[i].IndexCount);
    }
    EXPECT_EQ(1, effectFaceDraws);
}

TEST_F(MeshMixedEffectBudgetTest, MixedProviderBlendIsUsedOnlyByTheOrdinaryGroup) {
    RCKMesh mesh(Context); RCKMaterial plain(Context), combined(Context), provider(Context);
    RCKTexture plainTexture(Context), primary(Context), extra(Context), layer(Context);
    Pixel(plainTexture, 0xFF112233); Pixel(primary, 0xFF445566); Pixel(extra, 0xFF778899); Pixel(layer, 0xFF99AABB);
    plain.SetTexture(0, &plainTexture); combined.SetTexture(0, &primary);
    combined.SetTexture(1, &extra); combined.SetEffect(VXEFFECT_2TEXTURES); provider.SetTexture(0, &layer);
    Geometry(mesh, plain, &combined); int channel = Channel(mesh, provider, .375f, .625f);
    mesh.SetChannelSourceBlend(channel, VXBLEND_SRCALPHA); mesh.SetChannelDestBlend(channel, VXBLEND_INVSRCALPHA);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(3, State.DrawCount);
    const MixedBudgetObserver::Draw *effect = Find(primary.GetRstTextureIndex(), 10);
    ASSERT_NE(nullptr, effect); Bound(effect->Textures[1], extra); UV(*effect, 1, .4375f, .625f);
    const MixedBudgetObserver::Draw *ordinaryLayer = Find(layer.GetRstTextureIndex(), 0);
    ASSERT_NE(nullptr, ordinaryLayer); UV(*ordinaryLayer, 0, .375f, .625f);
    EXPECT_EQ(nullptr, Find(layer.GetRstTextureIndex(), 10));
}

TEST_F(MeshMixedEffectBudgetTest, MixedProviderMaterialEffectDoesNotExecuteForTheBaseEffectGroup) {
    RCKMesh mesh(Context); RCKMaterial plain(Context), combined(Context); CountingMixedMaterial provider(Context);
    RCKTexture plainTexture(Context), primary(Context), extra(Context), layer0(Context), layer1(Context);
    Pixel(plainTexture, 0xFF112233); Pixel(primary, 0xFF445566); Pixel(extra, 0xFF778899);
    Pixel(layer0, 0xFF99AABB); Pixel(layer1, 0xFF223344);
    plain.SetTexture(0, &plainTexture); combined.SetTexture(0, &primary);
    combined.SetTexture(1, &extra); combined.SetEffect(VXEFFECT_2TEXTURES);
    provider.SetTexture(0, &layer0); provider.SetTexture(1, &layer1); provider.SetEffect(VXEFFECT_2TEXTURES);
    Geometry(mesh, plain, &combined); Channel(mesh, provider, .375f, .625f);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(3, State.DrawCount);
    const MixedBudgetObserver::Draw *effect = Find(primary.GetRstTextureIndex(), 10);
    ASSERT_NE(nullptr, effect); Bound(effect->Textures[1], extra); UV(*effect, 1, .4375f, .625f);
    const MixedBudgetObserver::Draw *ordinaryLayer = Find(layer0.GetRstTextureIndex(), 0);
    ASSERT_NE(nullptr, ordinaryLayer); Bound(ordinaryLayer->Textures[1], layer1);
    EXPECT_EQ(nullptr, Find(layer0.GetRstTextureIndex(), 10)); EXPECT_EQ(1, provider.Calls[0]);
}

TEST_F(MeshMixedEffectBudgetTest, MixedTwoAndThreeTextureEffectsUseTheirOwnReservedUVPrefixes) {
    RCKMesh mesh(Context); RCKMaterial two(Context), three(Context), first(Context), second(Context);
    RCKTexture firstPrimary(Context), firstExtra(Context), secondPrimary(Context), secondExtra1(Context), secondExtra2(Context), layer(Context);
    Pixel(firstPrimary, 0xFF112233); Pixel(firstExtra, 0xFF445566); Pixel(secondPrimary, 0xFF778899);
    Pixel(secondExtra1, 0xFF99AABB); Pixel(secondExtra2, 0xFF223344); Pixel(layer, 0xFF556677);
    two.SetTexture(0, &firstPrimary); two.SetTexture(1, &firstExtra); two.SetEffect(VXEFFECT_2TEXTURES);
    three.SetTexture(0, &secondPrimary); three.SetTexture(1, &secondExtra1); three.SetTexture(2, &secondExtra2); three.SetEffect(VXEFFECT_3TEXTURES);
    second.SetTexture(0, &layer); Geometry(mesh, two, &three);
    Channel(mesh, first, .375f, .625f); int independent = Channel(mesh, second, .75f, .875f);
    mesh.SetChannelSourceBlend(independent, VXBLEND_SRCALPHA); mesh.SetChannelDestBlend(independent, VXBLEND_INVSRCALPHA);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(3, State.DrawCount);
    const MixedBudgetObserver::Draw *a = Find(firstPrimary.GetRstTextureIndex(), 0);
    const MixedBudgetObserver::Draw *b = Find(secondPrimary.GetRstTextureIndex(), 10);
    ASSERT_NE(nullptr, a); ASSERT_NE(nullptr, b);
    Bound(a->Textures[1], firstExtra); UV(*a, 1, .375f, .625f);
    Bound(b->Textures[1], secondExtra1); Bound(b->Textures[2], secondExtra2);
    UV(*b, 1, .4375f, .625f); UV(*b, 2, .8125f, .875f);
    ASSERT_NE(nullptr, Find(layer.GetRstTextureIndex(), 0)); EXPECT_EQ(nullptr, Find(layer.GetRstTextureIndex(), 10));
}

TEST_F(MeshMixedEffectBudgetTest, MixedSameUVProviderUsesTheEffectGroupsRemappedPrimaryCoordinates) {
    RCKMesh mesh(Context); RCKMaterial plain(Context), combined(Context), provider(Context);
    RCKTexture plainTexture(Context), primary(Context), extra(Context);
    Pixel(plainTexture, 0xFF112233); Pixel(primary, 0xFF445566); Pixel(extra, 0xFF778899);
    plain.SetTexture(0, &plainTexture); combined.SetTexture(0, &primary);
    combined.SetTexture(1, &extra); combined.SetEffect(VXEFFECT_2TEXTURES);
    Geometry(mesh, plain, &combined); int channel = Channel(mesh, provider, .375f, .625f);
    mesh.SetChannelFlags(channel, mesh.GetChannelFlags(channel) | VXCHANNEL_SAMEUV);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr));
    const MixedBudgetObserver::Draw *effect = Find(primary.GetRstTextureIndex(), 10);
    ASSERT_NE(nullptr, effect); Bound(effect->Textures[1], extra);
    UV(*effect, 0, .1875f, .25f); UV(*effect, 1, .1875f, .25f);
    EXPECT_EQ(3, effect->IndexCount);
}

TEST_F(MeshMixedEffectBudgetTest, MixedNotLitProviderFlagDoesNotDiscardTheBaseEffectUVStream) {
    RCKMesh mesh(Context); RCKMaterial plain(Context), combined(Context), provider(Context);
    RCKTexture plainTexture(Context), primary(Context), extra(Context), layer(Context);
    Pixel(plainTexture, 0xFF112233); Pixel(primary, 0xFF445566); Pixel(extra, 0xFF778899); Pixel(layer, 0xFF99AABB);
    plain.SetTexture(0, &plainTexture); combined.SetTexture(0, &primary);
    combined.SetTexture(1, &extra); combined.SetEffect(VXEFFECT_2TEXTURES); provider.SetTexture(0, &layer);
    Geometry(mesh, plain, &combined); int channel = Channel(mesh, provider, .375f, .625f);
    mesh.SetChannelFlags(channel, mesh.GetChannelFlags(channel) | VXCHANNEL_NOTLIT);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(3, State.DrawCount);
    const MixedBudgetObserver::Draw *effect = Find(primary.GetRstTextureIndex(), 10);
    ASSERT_NE(nullptr, effect); UV(*effect, 1, .4375f, .625f); Bound(effect->Textures[1], extra);
    ASSERT_NE(nullptr, Find(layer.GetRstTextureIndex(), 0)); EXPECT_EQ(nullptr, Find(layer.GetRstTextureIndex(), 10));
}

TEST_F(MeshMixedEffectBudgetTest, InactiveAuthoredChannelDoesNotConsumeAnEffectProviderOrdinal) {
    RCKMesh mesh(Context); RCKMaterial plain(Context), combined(Context), inactive(Context), provider(Context);
    RCKTexture plainTexture(Context), primary(Context), extra(Context);
    Pixel(plainTexture, 0xFF112233); Pixel(primary, 0xFF445566); Pixel(extra, 0xFF778899);
    plain.SetTexture(0, &plainTexture); combined.SetTexture(0, &primary);
    combined.SetTexture(1, &extra); combined.SetEffect(VXEFFECT_2TEXTURES);
    Geometry(mesh, plain, &combined); int unused = Channel(mesh, inactive, .25f, .5f);
    mesh.ActivateChannel(unused, FALSE); Channel(mesh, provider, .375f, .625f);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr));
    const MixedBudgetObserver::Draw *effect = Find(primary.GetRstTextureIndex(), 10);
    ASSERT_NE(nullptr, effect); Bound(effect->Textures[1], extra); UV(*effect, 1, .4375f, .625f);
}

TEST_F(MeshMixedEffectBudgetTest, UnavailableIndependentStageKeepsOnlyShorterGroupsSeparateLayer) {
    RCKMesh mesh(Context); RCKMaterial two(Context), three(Context), first(Context); CountingMixedMaterial second(Context);
    RCKTexture primary1(Context), extra1(Context), primary2(Context), extra21(Context), extra22(Context), layer(Context);
    Pixel(primary1, 0xFF112233); Pixel(extra1, 0xFF445566); Pixel(primary2, 0xFF778899);
    Pixel(extra21, 0xFF99AABB); Pixel(extra22, 0xFF223344); Pixel(layer, 0xFF556677);
    two.SetTexture(0, &primary1); two.SetTexture(1, &extra1); two.SetEffect(VXEFFECT_2TEXTURES);
    three.SetTexture(0, &primary2); three.SetTexture(1, &extra21); three.SetTexture(2, &extra22); three.SetEffect(VXEFFECT_3TEXTURES);
    second.SetTexture(0, &layer); Geometry(mesh, two, &three);
    Channel(mesh, first, .375f, .625f); Channel(mesh, second, .75f, .875f);
    State.RejectedStage = 2;
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(3, State.DrawCount);
    const MixedBudgetObserver::Draw *a = Find(primary1.GetRstTextureIndex(), 0);
    const MixedBudgetObserver::Draw *b = Find(primary2.GetRstTextureIndex(), 10);
    ASSERT_NE(nullptr, a); ASSERT_NE(nullptr, b);
    Bound(a->Textures[1], extra1); EXPECT_EQ(0u, a->Textures[2]);
    Bound(b->Textures[1], extra21); Bound(b->Textures[2], extra22); UV(*b, 2, .8125f, .875f);
    ASSERT_NE(nullptr, Find(layer.GetRstTextureIndex(), 0)); EXPECT_EQ(nullptr, Find(layer.GetRstTextureIndex(), 10));
    EXPECT_EQ(1, second.Calls[0]);
}

TEST_F(MeshMixedEffectBudgetTest, SmallerBlendStageBudgetDefersOnlyTheIndependentTail) {
    RCKMesh mesh(Context); RCKMaterial base(Context), provider(Context), tail(Context);
    RCKTexture primary(Context), extra(Context), last(Context);
    Pixel(primary, 0xFF112233); Pixel(extra, 0xFF445566); Pixel(last, 0xFF778899);
    base.SetTexture(0, &primary); base.SetTexture(1, &extra); base.SetEffect(VXEFFECT_2TEXTURES);
    tail.SetTexture(0, &last); Geometry(mesh, base);
    Channel(mesh, provider, .375f, .625f); Channel(mesh, tail, .75f, .875f);
    Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 4;
    Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = 2;
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.DrawCount);
    Bound(State.Draws[0].Textures[1], extra); UV(State.Draws[0], 1, .375f, .625f);
    EXPECT_EQ(0u, State.Draws[0].Textures[2]);
    Bound(State.Draws[1].Textures[0], last); UV(State.Draws[1], 0, .75f, .875f);
}

TEST_F(MeshMixedEffectBudgetTest, FaceMaskCannotTurnAProviderIntoALayerOnTheLongerEffectGroup) {
    RCKMesh mesh(Context); RCKMaterial two(Context), three(Context), first(Context), second(Context);
    RCKTexture primary1(Context), extra1(Context), primary2(Context), extra21(Context), extra22(Context), layer(Context);
    Pixel(primary1, 0xFF112233); Pixel(extra1, 0xFF445566); Pixel(primary2, 0xFF778899);
    Pixel(extra21, 0xFF99AABB); Pixel(extra22, 0xFF223344); Pixel(layer, 0xFF556677);
    two.SetTexture(0, &primary1); two.SetTexture(1, &extra1); two.SetEffect(VXEFFECT_2TEXTURES);
    three.SetTexture(0, &primary2); three.SetTexture(1, &extra21); three.SetTexture(2, &extra22); three.SetEffect(VXEFFECT_3TEXTURES);
    second.SetTexture(0, &layer); Geometry(mesh, two, &three);
    int firstChannel = Channel(mesh, first, .375f, .625f);
    int secondChannel = Channel(mesh, second, .75f, .875f);
    mesh.SetChannelSourceBlend(secondChannel, VXBLEND_SRCALPHA);
    mesh.SetChannelDestBlend(secondChannel, VXBLEND_INVSRCALPHA);
    mesh.SetFaceChannelMask(0, static_cast<CKWORD>(1u << firstChannel));
    mesh.SetFaceChannelMask(1, static_cast<CKWORD>((1u << firstChannel) | (1u << secondChannel)));
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.DrawCount);
    const MixedBudgetObserver::Draw *effect = Find(primary2.GetRstTextureIndex(), 10);
    ASSERT_NE(nullptr, effect); Bound(effect->Textures[2], extra22); UV(*effect, 2, .8125f, .875f);
    EXPECT_EQ(nullptr, Find(layer.GetRstTextureIndex(), 0)); EXPECT_EQ(nullptr, Find(layer.GetRstTextureIndex(), 10));
}

TEST_F(MeshMixedEffectBudgetTest, EmptyEffectGroupCannotOwnTheOrdinaryGroupsPrefixOrReceiveASpeculativeBind) {
    RCKMesh mesh(Context); CountingMixedMaterial empty(Context); RCKMaterial plain(Context), channel(Context);
    RCKTexture primary(Context), unusedPrimary(Context), unusedExtra(Context), layer(Context);
    Pixel(primary, 0xFF112233); Pixel(unusedPrimary, 0xFF445566); Pixel(unusedExtra, 0xFF778899); Pixel(layer, 0xFF99AABB);
    empty.SetTexture(0, &unusedPrimary); empty.SetTexture(1, &unusedExtra); empty.SetEffect(VXEFFECT_2TEXTURES);
    plain.SetTexture(0, &primary); channel.SetTexture(0, &layer);
    Geometry(mesh, empty); mesh.SetFaceMaterial(0, &plain);
    Channel(mesh, channel, .375f, .625f);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
    Bound(State.Draws[0].Textures[0], primary); Bound(State.Draws[0].Textures[1], layer);
    UV(State.Draws[0], 1, .375f, .625f); EXPECT_EQ(0, empty.Calls[0]);
    EXPECT_EQ(2, State.Loads);
}

TEST_F(MeshMixedEffectBudgetTest, MixedIndependentSuffixRunsAfterItsOwnBaseInAuthoredOrder) {
    RCKMesh mesh(Context); CountingMixedMaterial two(Context), three(Context);
    RCKMaterial first(Context), second(Context), tail(Context);
    RCKTexture primary1(Context), extra1(Context), primary2(Context), extra21(Context), extra22(Context), layer(Context), last(Context);
    Pixel(primary1, 0xFF112233); Pixel(extra1, 0xFF445566); Pixel(primary2, 0xFF778899);
    Pixel(extra21, 0xFF99AABB); Pixel(extra22, 0xFF223344); Pixel(layer, 0xFF556677); Pixel(last, 0xFF8899AA);
    two.SetTexture(0, &primary1); two.SetTexture(1, &extra1); two.SetEffect(VXEFFECT_2TEXTURES);
    three.SetTexture(0, &primary2); three.SetTexture(1, &extra21); three.SetTexture(2, &extra22); three.SetEffect(VXEFFECT_3TEXTURES);
    second.SetTexture(0, &layer); tail.SetTexture(0, &last); Geometry(mesh, two, &three);
    Channel(mesh, first, .375f, .625f); int independent = Channel(mesh, second, .75f, .875f);
    mesh.SetChannelSourceBlend(independent, VXBLEND_SRCALPHA); mesh.SetChannelDestBlend(independent, VXBLEND_INVSRCALPHA);
    Channel(mesh, tail, .5f, .75f); Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 3;
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(5, State.DrawCount);
    Bound(State.Draws[0].Textures[0], primary1); Bound(State.Draws[1].Textures[0], layer);
    Bound(State.Draws[2].Textures[0], last); Bound(State.Draws[3].Textures[0], primary2); Bound(State.Draws[4].Textures[0], last);
    EXPECT_FLOAT_EQ(0, State.Draws[0].FirstX); EXPECT_FLOAT_EQ(0, State.Draws[1].FirstX);
    EXPECT_FLOAT_EQ(0, State.Draws[2].FirstX); EXPECT_FLOAT_EQ(10, State.Draws[3].FirstX); EXPECT_FLOAT_EQ(10, State.Draws[4].FirstX);
    UV(State.Draws[1], 0, .75f, .875f); UV(State.Draws[2], 0, .5f, .75f);
    UV(State.Draws[4], 0, .5625f, .75f);
    EXPECT_EQ(1, two.Calls[0]); EXPECT_EQ(1, three.Calls[0]);
}

TEST_F(MeshMixedEffectBudgetTest, CombineThreeWithoutMeshProvidersDeclaresSharedMainUVAtItsPhysicalBudget) {
    RCKMesh mesh(Context); RCKMaterial base(Context);
    RCKTexture primary(Context), extra1(Context), extra2(Context);
    Pixel(primary, 0xFF112233); Pixel(extra1, 0xFF445566); Pixel(extra2, 0xFF778899);
    base.SetTexture(0, &primary); base.SetTexture(1, &extra1); base.SetTexture(2, &extra2);
    base.SetEffect(VXEFFECT_3TEXTURES); Geometry(mesh, base);
    Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 3;
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
    Bound(State.Draws[0].Textures[0], primary);
    Bound(State.Draws[0].Textures[1], extra1); Bound(State.Draws[0].Textures[2], extra2);
    EXPECT_EQ(1u, State.Draws[0].Coordinates[1]); EXPECT_EQ(2u, State.Draws[0].Coordinates[2]);
    UV(State.Draws[0], 0, .125f, .25f);
    UV(State.Draws[0], 1, .125f, .25f); UV(State.Draws[0], 2, .125f, .25f);
}

TEST_F(MeshMixedEffectBudgetTest, DisabledMeshChannelsLeaveOnlyTheTwoMixedBaseGroups) {
    RCKMesh mesh(Context); RCKMaterial plain(Context), combined(Context); CountingMixedMaterial provider(Context);
    RCKTexture plainTexture(Context), primary(Context), extra(Context), layer(Context);
    Pixel(plainTexture, 0xFF112233); Pixel(primary, 0xFF445566); Pixel(extra, 0xFF778899); Pixel(layer, 0xFF99AABB);
    plain.SetTexture(0, &plainTexture); combined.SetTexture(0, &primary);
    combined.SetTexture(1, &extra); combined.SetEffect(VXEFFECT_2TEXTURES); provider.SetTexture(0, &layer);
    Geometry(mesh, plain, &combined); Channel(mesh, provider, .375f, .625f);
    mesh.SetFlags(mesh.GetFlags() & ~VXMESH_RENDERCHANNELS);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.DrawCount);
    Bound(State.Draws[0].Textures[0], plainTexture); Bound(State.Draws[1].Textures[0], primary);
    Bound(State.Draws[1].Textures[1], extra);
    EXPECT_EQ(0, provider.Calls[0]); EXPECT_EQ(0, provider.Calls[1]);
    EXPECT_EQ(nullptr, Find(layer.GetRstTextureIndex(), 0)); EXPECT_EQ(nullptr, Find(layer.GetRstTextureIndex(), 10));
}

TEST_F(MeshMixedEffectBudgetTest, DisabledEntityChannelsLeaveOnlyTheTwoMixedBaseGroups) {
    RCKMesh mesh(Context); RCK3dEntity entity(Context);
    RCKMaterial plain(Context), combined(Context); CountingMixedMaterial provider(Context);
    RCKTexture plainTexture(Context), primary(Context), extra(Context), layer(Context);
    Pixel(plainTexture, 0xFF112233); Pixel(primary, 0xFF445566); Pixel(extra, 0xFF778899); Pixel(layer, 0xFF99AABB);
    plain.SetTexture(0, &plainTexture); combined.SetTexture(0, &primary);
    combined.SetTexture(1, &extra); combined.SetEffect(VXEFFECT_2TEXTURES); provider.SetTexture(0, &layer);
    Geometry(mesh, plain, &combined); Channel(mesh, provider, .375f, .625f);
    entity.ModifyMoveableFlags(0, VX_MOVEABLE_RENDERCHANNELS);
    ASSERT_EQ(1, mesh.DefaultRender(Device, &entity)); ASSERT_EQ(2, State.DrawCount);
    Bound(State.Draws[0].Textures[0], plainTexture); Bound(State.Draws[1].Textures[0], primary);
    Bound(State.Draws[1].Textures[1], extra);
    EXPECT_EQ(0, provider.Calls[0]); EXPECT_EQ(0, provider.Calls[1]);
}

TEST_F(MeshMixedEffectBudgetTest, MixedTransparentSidesCompleteEachSidesIndependentLayerBeforeTheNextBase) {
    RCKMesh mesh(Context); CountingMixedMaterial plain(Context), combined(Context);
    RCKMaterial provider(Context), tail(Context);
    RCKTexture plainTexture(Context), primary(Context), extra(Context), layer(Context), last(Context);
    Pixel(plainTexture, 0xFF112233); Pixel(primary, 0xFF445566); Pixel(extra, 0xFF778899);
    Pixel(layer, 0xFF99AABB); Pixel(last, 0xFF223344);
    plain.SetTexture(0, &plainTexture); combined.SetTexture(0, &primary);
    combined.SetTexture(1, &extra); combined.SetEffect(VXEFFECT_2TEXTURES);
    combined.SetFillMode(VXFILL_SOLID); combined.SetTwoSided(TRUE); combined.EnableAlphaBlend(TRUE);
    combined.SetSourceBlend(VXBLEND_SRCALPHA); combined.SetDestBlend(VXBLEND_INVSRCALPHA);
    provider.SetTexture(0, &layer); tail.SetTexture(0, &last); Geometry(mesh, plain, &combined);
    Channel(mesh, provider, .375f, .625f); Channel(mesh, tail, .75f, .875f);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(7, State.DrawCount);
    Bound(State.Draws[0].Textures[0], plainTexture); Bound(State.Draws[1].Textures[0], layer);
    Bound(State.Draws[2].Textures[0], last);
    for (int side = 0; side < 2; ++side) {
        const int base = 3 + side * 2, replay = base + 1;
        Bound(State.Draws[base].Textures[0], primary); Bound(State.Draws[base].Textures[1], extra);
        Bound(State.Draws[replay].Textures[0], last);
        EXPECT_FLOAT_EQ(10, State.Draws[base].FirstX); EXPECT_FLOAT_EQ(10, State.Draws[replay].FirstX);
        const CKDWORD cull = side ? VXCULL_CCW : VXCULL_CW;
        EXPECT_EQ(cull, State.Draws[base].Cull); EXPECT_EQ(cull, State.Draws[replay].Cull);
        EXPECT_EQ(VXBLEND_SRCALPHA, State.Draws[base].Source); EXPECT_EQ(VXBLEND_INVSRCALPHA, State.Draws[base].Dest);
        EXPECT_EQ(VXBLEND_ZERO, State.Draws[replay].Source); EXPECT_EQ(VXBLEND_SRCCOLOR, State.Draws[replay].Dest);
        UV(State.Draws[base], 1, .4375f, .625f); UV(State.Draws[replay], 0, .8125f, .875f);
    }
    EXPECT_EQ(1, plain.Calls[0]); EXPECT_EQ(1, combined.Calls[0]);
}

TEST_F(MeshMixedEffectBudgetTest, DerivedOwnedChainDoesNotReserveProvidersFromItsAuthoredEffectId) {
    RCKMesh mesh(Context); RCKMaterial plain(Context); OwnedMixedMaterial owned(Context);
    CountingMixedMaterial layerMaterial(Context);
    RCKTexture plainTexture(Context), authoredPrimary(Context), authoredExtra(Context), layer(Context);
    Pixel(plainTexture, 0xFF112233); Pixel(authoredPrimary, 0xFF445566);
    Pixel(authoredExtra, 0xFF778899); Pixel(layer, 0xFF99AABB);
    plain.SetTexture(0, &plainTexture); owned.SetTexture(0, &authoredPrimary);
    owned.SetTexture(1, &authoredExtra); owned.SetEffect(VXEFFECT_2TEXTURES);
    layerMaterial.SetTexture(0, &layer); Geometry(mesh, plain, &owned);
    Channel(mesh, layerMaterial, .375f, .625f);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(4, State.DrawCount);
    Bound(State.Draws[0].Textures[0], plainTexture); Bound(State.Draws[1].Textures[0], layer);
    EXPECT_EQ(901u, State.Draws[2].Textures[0]); EXPECT_EQ(902u, State.Draws[2].Textures[1]);
    EXPECT_EQ(CKRST_TOP_MODULATE, State.Draws[2].Operations[0]); EXPECT_EQ(CKRST_TOP_ADD, State.Draws[2].Operations[1]);
    EXPECT_EQ(0u, State.Draws[2].Coordinates[1]); EXPECT_FLOAT_EQ(10, State.Draws[2].FirstX);
    Bound(State.Draws[3].Textures[0], layer); EXPECT_FLOAT_EQ(10, State.Draws[3].FirstX);
    UV(State.Draws[3], 0, .4375f, .625f);
    EXPECT_EQ(1, owned.Calls); EXPECT_EQ(2, layerMaterial.Calls[0]); EXPECT_EQ(0, layerMaterial.Calls[1]);
    EXPECT_EQ(0u, authoredPrimary.GetRstTextureIndex()); EXPECT_EQ(0u, authoredExtra.GetRstTextureIndex());
    EXPECT_EQ(901u, State.Textures[0]); EXPECT_EQ(902u, State.Textures[1]);
}
