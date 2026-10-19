#include <gtest/gtest.h>
#include "../src/CKMesh.cpp"
#include "TextureTestObserver.h"

namespace {
TEST(MeshStateHelpersTest, OnlyTheTwoModulateEncodingsAreEligibleAcrossAllPackedNibbles) {
    int accepted = 0;
    for (CKDWORD packed = 0; packed < 256; ++packed) {
        // These are the two interface encodings of destination*source.
        const bool expected = packed == 0x13 || packed == 0x91;
        EXPECT_EQ(expected, CKMeshMonoPassStageBlend(packed) != FALSE) << packed;
        accepted += CKMeshMonoPassStageBlend(packed) != FALSE;
    }
    EXPECT_EQ(2, accepted);
}

class StateHelperMesh : public RCKMesh {
public:
    explicit StateHelperMesh(CKContext *context) : RCKMesh(context) {}
    void ActiveChannel(int index) { m_ActiveTextureChannels.PushBack(index); }
    void Cold() { m_Valid = 0; }
    using RCKMesh::BindMonoPassTextureChannelsChecked;
};
class StateHelperMaterial : public RCKMaterial {
public:
    explicit StateHelperMaterial(CKContext *context) : RCKMaterial(context) {}
    int Calls = 0, LastStage = -1;
    CKBOOL LastLit = TRUE;
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL lit, int stage) override {
        ++Calls; LastStage = stage; LastLit = lit;
        return RCKMaterial::SetAsCurrent(context, lit, stage);
    }
};
class StateHelperObserver : public TextureTestObserver {
public:
    struct Draw {
        CKDWORD Specular = 0, Lighting = 0, Flags = 0, Source = 0, Destination = 0;
        CKDWORD Texture0 = 0, Texture1 = 0, Blend1 = 0;
        void *Color = nullptr, *SpecularColor = nullptr;
        Vx2DVector UV0, UV1;
        bool HasUV1 = false;
    };
    Draw Draws[12];
    int DrawCount = 0, SpecularWrites = 0, RejectedBlendWrites = 0;
    CKDWORD LastSpecularRequest = 0;
    bool RejectNextModulate = false;
    CKBOOL SetRenderState(VXRENDERSTATETYPE state, CKDWORD value) override {
        if (state == VXRENDERSTATE_SPECULARENABLE) { ++SpecularWrites; LastSpecularRequest = value; }
        return MaterialRenderObserver::SetRenderState(state, value);
    }
    CKBOOL SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD value) override {
        if (RejectNextModulate && stage == 1 && state == CKRST_TSS_STAGEBLEND && (value == 0x13 || value == 0x91)) {
            RejectNextModulate = false; ++RejectedBlendWrites; return FALSE;
        }
        return MaterialRenderObserver::SetTextureStageState(stage, state, value);
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_EQ(3, count);
        EXPECT_NE(nullptr, indices); EXPECT_NE(nullptr, data); EXPECT_LT(DrawCount, 12);
        if (!data || DrawCount >= 12) return FALSE;
        Draw &draw = Draws[DrawCount++];
        draw.Specular = States[VXRENDERSTATE_SPECULARENABLE];
        draw.Lighting = States[VXRENDERSTATE_LIGHTING]; draw.Flags = data->Flags;
        draw.Source = States[VXRENDERSTATE_SRCBLEND]; draw.Destination = States[VXRENDERSTATE_DESTBLEND];
        draw.Texture0 = Textures[0]; draw.Texture1 = Textures[1];
        draw.Blend1 = StageStates[1][CKRST_TSS_STAGEBLEND];
        draw.Color = data->ColorPtr; draw.SpecularColor = data->SpecularColorPtr;
        if (data->TexCoordPtr) memcpy(&draw.UV0, data->TexCoordPtr, sizeof(draw.UV0));
        draw.HasUV1 = data->TexCoordPtrs[0] != nullptr;
        if (draw.HasUV1) memcpy(&draw.UV1, data->TexCoordPtrs[0], sizeof(draw.UV1));
        return TRUE;
    }
};
class MeshStateHelperWorldTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    StateHelperObserver State;
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
        Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = 4;
        VxImageDescEx format; VxPixelFormat2ImageDesc(_32_ARGB8888, format);
        Manager->m_Drivers[0].TextureFormats.PushBack(format);
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        Device = new RCKRenderContext(Context); Device->DetachAll();
        Device->m_RasterizerContext = &State; Device->m_RasterizerDriver = &Driver;
        Device->m_DriverIndex = 0; Device->m_DisplayWireframe = FALSE;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static void Geometry(StateHelperMesh &mesh, RCKMaterial &material) {
        ASSERT_TRUE(mesh.SetVertexCount(3)); ASSERT_TRUE(mesh.SetFaceCount(1));
        mesh.SetFaceVertexIndex(0, 0, 1, 2);
        const VxVector positions[] = {VxVector(0, 0, 1), VxVector(2, 0, 1), VxVector(0, 2, 1)};
        for (int i = 0; i < 3; ++i) {
            VxVector position = positions[i], normal(0, 0, 1);
            mesh.SetVertexPosition(i, &position); mesh.SetVertexNormal(i, &normal);
            mesh.SetVertexTextureCoordinates(i, 0.125f, 0.25f, -1);
        }
        mesh.SetFaceMaterial(0, &material);
        mesh.SetFlags((mesh.GetFlags() & ~(VXMESH_PRELITMODE | VXMESH_STRIPIFY)) | VXMESH_RENDERCHANNELS);
    }
    static void Pixel(RCKTexture &texture, CKDWORD color) {
        ASSERT_TRUE(texture.CreateImage(1, 1, 32, 0));
        CKBYTE *pixels = texture.LockSurfacePtr(0); ASSERT_NE(nullptr, pixels);
        memcpy(pixels, &color, 4); texture.ReleaseSurfacePtr(0);
        texture.SetDesiredVideoFormat(_32_ARGB8888);
    }
    static int Channel(StateHelperMesh &mesh, RCKMaterial &material, VXBLEND_MODE src, VXBLEND_MODE dst) {
        int index = mesh.AddChannel(&material, FALSE);
        EXPECT_GE(index, 0);
        if (index < 0) return index;
        mesh.ActivateChannel(index, TRUE);
        mesh.SetChannelSourceBlend(index, src); mesh.SetChannelDestBlend(index, dst);
        for (int i = 0; i < 3; ++i) mesh.SetVertexTextureCoordinates(i, 0.375f, 0.625f, index);
        return index;
    }
};

TEST_F(MeshStateHelperWorldTest, SceneSpecularRestoreUsesTheLiveOptionDespiteConflictingPhysicalState) {
    Device->SetState(VXRENDERSTATE_SPECULARENABLE, FALSE);
    const int writes = State.SpecularWrites, allWrites = State.StateWrites;
    State.States[VXRENDERSTATE_ZWRITEENABLE] = 19;
    const CKDWORD disable[] = {0, 7, 0};
    for (int i = 0; i < 3; ++i) {
        Manager->m_DisableSpecular.Value = disable[i];
        const CKDWORD expected = disable[i] ? FALSE : TRUE;
        State.States[VXRENDERSTATE_SPECULARENABLE] = !expected;
        RestoreSceneSpecularState(Device);
        EXPECT_EQ(expected, State.LastSpecularRequest);
        EXPECT_EQ(expected, State.States[VXRENDERSTATE_SPECULARENABLE]);
        EXPECT_EQ(19u, State.States[VXRENDERSTATE_ZWRITEENABLE]);
    }
    EXPECT_EQ(writes + 3, State.SpecularWrites);
    EXPECT_EQ(allWrites + 3, State.StateWrites);
}

TEST_F(MeshStateHelperWorldTest, PrelitDrawRestoresSpecularAndStillHonorsTheGlobalDisableOption) {
    StateHelperMesh mesh(Context); RCKMaterial material(Context);
    Geometry(mesh, material); mesh.SetLitMode(VX_PRELITMESH);
    const CKDWORD disable[] = {0, 2, 0};
    for (int i = 0; i < 3; ++i) {
        Manager->m_DisableSpecular.Value = disable[i];
        State.States[VXRENDERSTATE_SPECULARENABLE] = disable[i] ? TRUE : FALSE;
        mesh.Cold();
        ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr));
        ASSERT_EQ(i + 1, State.DrawCount);
        EXPECT_EQ(disable[i] ? FALSE : TRUE, State.Draws[i].Specular);
        EXPECT_FALSE(State.Draws[i].Lighting);
        EXPECT_TRUE(State.Draws[i].Flags & CKRST_DP_SPECULAR);
    }
}

TEST_F(MeshStateHelperWorldTest, LitMaterialPowerRemainsAuthoritativeAfterTheSceneRestore) {
    StateHelperMesh mesh(Context); RCKMaterial material(Context);
    Geometry(mesh, material);
    const float power[] = {0, 1, 1};
    const CKDWORD disable[] = {0, 0, 1};
    const CKDWORD expected[] = {FALSE, TRUE, FALSE};
    for (int i = 0; i < 3; ++i) {
        material.SetPower(power[i]); Manager->m_DisableSpecular.Value = disable[i]; mesh.Cold();
        ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr));
        ASSERT_EQ(i + 1, State.DrawCount);
        EXPECT_EQ(expected[i], State.Draws[i].Specular);
        EXPECT_TRUE(State.Draws[i].Lighting);
    }
}

TEST_F(MeshStateHelperWorldTest, PrelitDrawRecoversAfterALitMaterialDisablesSpecular) {
    StateHelperMesh prelit(Context), lit(Context); RCKMaterial material(Context);
    Geometry(prelit, material); Geometry(lit, material);
    prelit.SetLitMode(VX_PRELITMESH); material.SetPower(0);
    Manager->m_DisableSpecular.Value = 0;
    ASSERT_EQ(1, prelit.DefaultRender(Device, nullptr));
    ASSERT_EQ(1, lit.DefaultRender(Device, nullptr));
    prelit.Cold();
    ASSERT_EQ(1, prelit.DefaultRender(Device, nullptr));
    Manager->m_DisableSpecular.Value = 1; prelit.Cold();
    ASSERT_EQ(1, prelit.DefaultRender(Device, nullptr));
    ASSERT_EQ(4, State.DrawCount);
    EXPECT_TRUE(State.Draws[0].Specular); EXPECT_FALSE(State.Draws[1].Specular);
    EXPECT_TRUE(State.Draws[2].Specular); EXPECT_FALSE(State.Draws[3].Specular);
    EXPECT_FALSE(State.Draws[0].Lighting); EXPECT_TRUE(State.Draws[1].Lighting);
    EXPECT_FALSE(State.Draws[2].Lighting); EXPECT_FALSE(State.Draws[3].Lighting);
}

TEST_F(MeshStateHelperWorldTest, BothModulatePairsFoldWithTheChannelTextureAndUvInOneDraw) {
    RCKMaterial base(Context), channel(Context); RCKTexture primary(Context), extra(Context);
    Pixel(primary, 0xff112233); Pixel(extra, 0xff445566);
    base.SetTexture(0, &primary); channel.SetTexture(0, &extra);
    const VXBLEND_MODE sources[] = {VXBLEND_ZERO, VXBLEND_DESTCOLOR};
    const VXBLEND_MODE destinations[] = {VXBLEND_SRCCOLOR, VXBLEND_ZERO};
    for (int i = 0; i < 2; ++i) {
        StateHelperMesh mesh(Context); Geometry(mesh, base);
        Channel(mesh, channel, sources[i], destinations[i]); State.DrawCount = 0;
        ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
        EXPECT_EQ(primary.GetRstTextureIndex(), State.Draws[0].Texture0);
        EXPECT_EQ(extra.GetRstTextureIndex(), State.Draws[0].Texture1);
        EXPECT_NE(0u, State.Draws[0].Texture1);
        EXPECT_EQ(static_cast<CKDWORD>(STAGEBLEND(sources[i], destinations[i])), State.Draws[0].Blend1);
        EXPECT_TRUE(State.Draws[0].HasUV1);
        EXPECT_FLOAT_EQ(0.375f, State.Draws[0].UV1.x); EXPECT_FLOAT_EQ(0.625f, State.Draws[0].UV1.y);
    }
}

TEST_F(MeshStateHelperWorldTest, AdditiveAndAlphaPairsStayInSeparateChannelPasses) {
    RCKMaterial base(Context), channel(Context); RCKTexture primary(Context), extra(Context);
    Pixel(primary, 0xff112233); Pixel(extra, 0xff445566);
    base.SetTexture(0, &primary); channel.SetTexture(0, &extra);
    const VXBLEND_MODE sources[] = {VXBLEND_ONE, VXBLEND_SRCALPHA};
    const VXBLEND_MODE destinations[] = {VXBLEND_ONE, VXBLEND_INVSRCALPHA};
    for (int i = 0; i < 2; ++i) {
        StateHelperMesh mesh(Context); Geometry(mesh, base);
        Channel(mesh, channel, sources[i], destinations[i]); State.DrawCount = 0;
        ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.DrawCount);
        EXPECT_EQ(primary.GetRstTextureIndex(), State.Draws[0].Texture0);
        EXPECT_EQ(extra.GetRstTextureIndex(), State.Draws[1].Texture0);
        EXPECT_EQ(static_cast<CKDWORD>(sources[i]), State.Draws[1].Source);
        EXPECT_EQ(static_cast<CKDWORD>(destinations[i]), State.Draws[1].Destination);
        EXPECT_FLOAT_EQ(0.375f, State.Draws[1].UV0.x); EXPECT_FLOAT_EQ(0.625f, State.Draws[1].UV0.y);
    }
}

TEST_F(MeshStateHelperWorldTest, RefusedModulateStageRecoversAsAnUnlitColorFreeChannelPass) {
    StateHelperMesh mesh(Context); RCKMaterial base(Context), channel(Context);
    RCKTexture primary(Context), extra(Context);
    Pixel(primary, 0xff112233); Pixel(extra, 0xff445566);
    base.SetTexture(0, &primary); channel.SetTexture(0, &extra);
    Geometry(mesh, base); Channel(mesh, channel, VXBLEND_ZERO, VXBLEND_SRCCOLOR);
    State.RejectNextModulate = true;
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.DrawCount);
    EXPECT_EQ(1, State.RejectedBlendWrites);
    EXPECT_EQ(extra.GetRstTextureIndex(), State.Draws[1].Texture0);
    EXPECT_FALSE(State.Draws[1].Lighting);
    EXPECT_EQ(0u, State.Draws[1].Flags & (CKRST_DP_LIGHT | CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR));
    EXPECT_EQ(nullptr, State.Draws[1].Color); EXPECT_EQ(nullptr, State.Draws[1].SpecularColor);
}

TEST_F(MeshStateHelperWorldTest, DirectMonoBinderSkipsAnActiveNonModulateMaterial) {
    StateHelperMesh mesh(Context); RCKMaterial base(Context); StateHelperMaterial channel(Context);
    RCKTexture extra(Context); Pixel(extra, 0xff445566); channel.SetTexture(0, &extra);
    Geometry(mesh, base);
    const int index = Channel(mesh, channel, VXBLEND_SRCALPHA, VXBLEND_INVSRCALPHA);
    ASSERT_GE(index, 0); mesh.ActiveChannel(index);
    State.StageStates[1][CKRST_TSS_STAGEBLEND] = 0x55;
    EXPECT_EQ(0u, mesh.BindMonoPassTextureChannelsChecked(Device, 0));
    EXPECT_EQ(0, channel.Calls); EXPECT_EQ(0, State.Loads);
    EXPECT_EQ(0x55u, State.StageStates[1][CKRST_TSS_STAGEBLEND]);
}

TEST_F(MeshStateHelperWorldTest, DirectMonoBinderAppliesTheAcceptedMaterialAtPositiveStageWithoutLighting) {
    StateHelperMesh mesh(Context); RCKMaterial base(Context); StateHelperMaterial channel(Context);
    RCKTexture extra(Context); Pixel(extra, 0xff445566); channel.SetTexture(0, &extra);
    Geometry(mesh, base);
    const int index = Channel(mesh, channel, VXBLEND_DESTCOLOR, VXBLEND_ZERO);
    ASSERT_GE(index, 0); mesh.ActiveChannel(index);
    EXPECT_EQ(0u, mesh.BindMonoPassTextureChannelsChecked(Device, 0));
    EXPECT_EQ(1, channel.Calls); EXPECT_EQ(1, channel.LastStage); EXPECT_FALSE(channel.LastLit);
    EXPECT_NE(0u, extra.GetRstTextureIndex()); EXPECT_EQ(extra.GetRstTextureIndex(), State.Textures[1]);
    EXPECT_EQ(0x91u, State.StageStates[1][CKRST_TSS_STAGEBLEND]);
}
} // namespace
