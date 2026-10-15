#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "TextureTestObserver.h"

namespace {
class EffectChannelObserver : public TextureTestObserver {
public:
    struct Draw {
        CKDWORD Textures[4] = {}, Coordinates[4] = {}, Operations[4] = {};
        CKDWORD Flags = 0;
        CKBOOL HasNormals = FALSE;
        Vx2DVector UVs[3]; CKBOOL HasUV[3] = {};
        int Indices = 0;
    };
    Draw Draws[12]; int DrawCount = 0;
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_EQ(3, count); EXPECT_NE(nullptr, indices); EXPECT_NE(nullptr, data);
        EXPECT_LT(DrawCount, 12); if (DrawCount >= 12 || !data) return FALSE;
        Draw &draw = Draws[DrawCount++]; draw.Indices = count;
        draw.Flags = data->Flags; draw.HasNormals = data->NormalPtr != nullptr;
        for (int i = 0; i < 4; ++i) {
            draw.Textures[i] = Textures[i]; draw.Coordinates[i] = StageStates[i][CKRST_TSS_TEXCOORDINDEX];
            draw.Operations[i] = StageStates[i][CKRST_TSS_OP];
        }
        for (int i = 0; i < 3; ++i) {
            void *uv = i == 0 ? data->TexCoordPtr : data->TexCoordPtrs[i - 1];
            if (uv) { draw.HasUV[i] = TRUE; draw.UVs[i] = *static_cast<Vx2DVector *>(uv); }
        }
        return TRUE;
    }
};
class MeshEffectChannelsTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr; RCKRenderManager *Manager = nullptr; RCKRenderContext *Device = nullptr;
    EffectChannelObserver State; CKRasterizerDriver Driver;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); Manager = new RCKRenderManager(Context);
        Manager->m_DriverCount = 1; Manager->m_Drivers = new VxDriverDescEx[1]();
        Manager->m_Drivers[0].CapsUpToDate = TRUE; Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 4;
        VxImageDescEx alpha; VxPixelFormat2ImageDesc(_32_ARGB8888, alpha); Manager->m_Drivers[0].TextureFormats.PushBack(alpha);
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0)); Device = new RCKRenderContext(Context);
        Device->DetachAll(); Device->m_RasterizerContext = &State; Device->m_RasterizerDriver = &Driver; Device->m_DriverIndex = 0;
        Device->m_DisplayWireframe = FALSE;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    static void BumpReflection(RCKMaterial &material) {
        ASSERT_EQ(VXEFFECT_BUMPENV, material.GetEffect());
        ASSERT_NE(nullptr, material.GetEffectParameter());
        CKStructHelper helper(material.GetEffectParameter());
        ASSERT_EQ(4, helper.GetMemberCount());
        ASSERT_NE(nullptr, helper[2]);
        CKDWORD generation = VXEFFECT_TGREFLECT;
        ASSERT_EQ(CK_OK, helper[2]->SetValue(&generation));
        CKDWORD observedGeneration = 0;
        ASSERT_EQ(CK_OK, helper[2]->GetValue(&observedGeneration));
        ASSERT_EQ(VXEFFECT_TGREFLECT, observedGeneration);
    }
    static void Pixel(RCKTexture &texture, CKDWORD color) {
        ASSERT_TRUE(texture.CreateImage(1, 1, 32, 0)); CKBYTE *pixels = texture.LockSurfacePtr(0); ASSERT_NE(nullptr, pixels);
        memcpy(pixels, &color, 4); texture.ReleaseSurfacePtr(0); texture.SetDesiredVideoFormat(_32_ARGB8888);
    }
    static void Triangle(RCKMesh &mesh, RCKMaterial &material) {
        ASSERT_TRUE(mesh.SetVertexCount(3)); ASSERT_TRUE(mesh.SetFaceCount(1)); mesh.SetFaceVertexIndex(0, 0, 1, 2);
        VxVector points[3] = {VxVector(0, 0, 1), VxVector(1, 0, 1), VxVector(0, 1, 1)};
        for (int i = 0; i < 3; ++i) {
            mesh.SetVertexPosition(i, &points[i]); mesh.SetVertexTextureCoordinates(i, .125f, .25f, -1);
        }
        mesh.SetFaceMaterial(0, &material);
        mesh.SetFlags((mesh.GetFlags() & ~(VXMESH_PRELITMODE | VXMESH_STRIPIFY)) | VXMESH_RENDERCHANNELS);
    }
    static int Channel(RCKMesh &mesh, RCKMaterial &material, float u, float v) {
        int index = mesh.AddChannel(&material, FALSE); EXPECT_GE(index, 0); if (index < 0) return index;
        mesh.ActivateChannel(index, TRUE); mesh.SetChannelSourceBlend(index, VXBLEND_ZERO); mesh.SetChannelDestBlend(index, VXBLEND_SRCCOLOR);
        for (int i = 0; i < 3; ++i) mesh.SetVertexTextureCoordinates(i, u, v, index);
        return index;
    }
    static void Bound(CKDWORD actual, RCKTexture &texture) { EXPECT_NE(0u, texture.GetRstTextureIndex()); EXPECT_EQ(texture.GetRstTextureIndex(), actual); }
    static void UV(const EffectChannelObserver::Draw &draw, int index, float u, float v) {
        ASSERT_TRUE(draw.HasUV[index]); EXPECT_FLOAT_EQ(u, draw.UVs[index].x); EXPECT_FLOAT_EQ(v, draw.UVs[index].y);
    }
};
}

TEST_F(MeshEffectChannelsTest, CombineTwoUsesChannelAsUVProviderWithoutReplacingEffectExtraTexture) {
    RCKMesh mesh(Context); RCKMaterial base(Context), provider(Context); RCKTexture primary(Context), extra(Context);
    Pixel(primary, 0xFF112233); Pixel(extra, 0xFF445566); base.SetTexture(0, &primary); base.SetTexture(1, &extra); base.SetEffect(VXEFFECT_2TEXTURES);
    Triangle(mesh, base); Channel(mesh, provider, .375f, .625f);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
    Bound(State.Draws[0].Textures[0], primary); Bound(State.Draws[0].Textures[1], extra); EXPECT_EQ(0u, State.Draws[0].Textures[2]);
    EXPECT_EQ(1u, State.Draws[0].Coordinates[1]); UV(State.Draws[0], 1, .375f, .625f); EXPECT_EQ(2, State.Loads);
    mesh.SetChannelFlags(0, mesh.GetChannelFlags(0) | VXCHANNEL_SAMEUV);
    State.DrawCount = 0;
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
    Bound(State.Draws[0].Textures[1], extra);
    UV(State.Draws[0], 0, .125f, .25f); UV(State.Draws[0], 1, .125f, .25f);
}
TEST_F(MeshEffectChannelsTest, ProviderMaterialTextureDoesNotBecomeAnExtraColorMultiplication) {
    RCKMesh mesh(Context); RCKMaterial base(Context), provider(Context); RCKTexture primary(Context), extra(Context), ignored(Context);
    Pixel(primary, 0xFF112233); Pixel(extra, 0xFF445566); Pixel(ignored, 0xFF778899);
    base.SetTexture(0, &primary); base.SetTexture(1, &extra); base.SetEffect(VXEFFECT_2TEXTURES); provider.SetTexture(0, &ignored);
    Triangle(mesh, base); Channel(mesh, provider, .375f, .625f);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
    Bound(State.Draws[0].Textures[1], extra); EXPECT_EQ(0u, State.Draws[0].Textures[2]);
    UV(State.Draws[0], 1, .375f, .625f);
}
TEST_F(MeshEffectChannelsTest, CombineThreeKeepsBothExtraTexturesAndBothProviderUVStreams) {
    RCKMesh mesh(Context); RCKMaterial base(Context), first(Context), second(Context); RCKTexture primary(Context), extra1(Context), extra2(Context);
    Pixel(primary, 0xFF112233); Pixel(extra1, 0xFF445566); Pixel(extra2, 0xFF778899);
    base.SetTexture(0, &primary); base.SetTexture(1, &extra1); base.SetTexture(2, &extra2); base.SetEffect(VXEFFECT_3TEXTURES);
    Triangle(mesh, base); Channel(mesh, first, .375f, .625f); Channel(mesh, second, .75f, .875f);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
    Bound(State.Draws[0].Textures[0], primary); Bound(State.Draws[0].Textures[1], extra1); Bound(State.Draws[0].Textures[2], extra2);
    EXPECT_EQ(1u, State.Draws[0].Coordinates[1]); EXPECT_EQ(2u, State.Draws[0].Coordinates[2]);
    UV(State.Draws[0], 1, .375f, .625f); UV(State.Draws[0], 2, .75f, .875f);
}
TEST_F(MeshEffectChannelsTest, DotProductEffectKeepsNormalAndColorStagesWithSharedBaseUV) {
    RCKMesh mesh(Context); RCKMaterial base(Context), provider(Context); RCKTexture color(Context), normal(Context);
    Pixel(color, 0xFF112233); Pixel(normal, 0xFF8080FF); base.SetTexture(0, &color); base.SetTexture(1, &normal); base.SetEffect(VXEFFECT_DP3);
    Triangle(mesh, base); Channel(mesh, provider, .375f, .625f);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
    Bound(State.Draws[0].Textures[0], normal); Bound(State.Draws[0].Textures[1], color);
    EXPECT_EQ(CKRST_TOP_DOTPRODUCT3, State.Draws[0].Operations[0]); EXPECT_EQ(CKRST_TOP_MODULATE, State.Draws[0].Operations[1]);
    EXPECT_EQ(0u, State.Draws[0].Coordinates[0]); EXPECT_EQ(0u, State.Draws[0].Coordinates[1]); UV(State.Draws[0], 0, .125f, .25f);
}
TEST_F(MeshEffectChannelsTest, BumpEnvironmentKeepsColorBumpAndEnvironmentStages) {
    RCKMesh mesh(Context); RCKMaterial base(Context), first(Context), second(Context); RCKTexture color(Context), bump(Context), environment(Context);
    Pixel(color, 0xFF112233); Pixel(bump, 0xFF445566); Pixel(environment, 0xFF778899);
    base.SetTexture(0, &color); base.SetTexture(1, &bump); base.SetTexture(2, &environment); base.SetEffect(VXEFFECT_BUMPENV);
    BumpReflection(base);
    Triangle(mesh, base); Channel(mesh, first, .375f, .625f); Channel(mesh, second, .75f, .875f);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
    Bound(State.Draws[0].Textures[0], color); Bound(State.Draws[0].Textures[1], bump); Bound(State.Draws[0].Textures[2], environment);
    EXPECT_EQ(CKRST_TOP_BUMPENVMAP, State.Draws[0].Operations[1]); EXPECT_EQ(0u, State.Draws[0].Coordinates[1]);
    EXPECT_EQ(CKRSTPackTexcoordIndex(2, CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR), State.Draws[0].Coordinates[2]);
}
TEST_F(MeshEffectChannelsTest, EnvironmentWithoutBumpUsesStageOneAndKeepsGeneratedCoordinates) {
    RCKMesh mesh(Context); RCKMaterial base(Context), provider(Context); RCKTexture color(Context), environment(Context);
    Pixel(color, 0xFF112233); Pixel(environment, 0xFF778899);
    base.SetTexture(0, &color); base.SetTexture(2, &environment); base.SetEffect(VXEFFECT_BUMPENV);
    BumpReflection(base);
    Triangle(mesh, base); Channel(mesh, provider, .375f, .625f);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
    Bound(State.Draws[0].Textures[0], color); Bound(State.Draws[0].Textures[1], environment); EXPECT_EQ(0u, State.Draws[0].Textures[2]);
    EXPECT_EQ(CKRSTPackTexcoordIndex(1, CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR), State.Draws[0].Coordinates[1]);
}
TEST_F(MeshEffectChannelsTest, ReservedProviderBlendDoesNotForceAnotherColorPass) {
    RCKMesh mesh(Context); RCKMaterial base(Context), provider(Context); RCKTexture primary(Context), extra(Context);
    Pixel(primary, 0xFF112233); Pixel(extra, 0xFF445566); base.SetTexture(0, &primary); base.SetTexture(1, &extra); base.SetEffect(VXEFFECT_2TEXTURES);
    Triangle(mesh, base); int index = Channel(mesh, provider, .375f, .625f);
    mesh.SetChannelSourceBlend(index, VXBLEND_SRCALPHA); mesh.SetChannelDestBlend(index, VXBLEND_INVSRCALPHA);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, State.DrawCount);
    Bound(State.Draws[0].Textures[1], extra); UV(State.Draws[0], 1, .375f, .625f);
}
TEST_F(MeshEffectChannelsTest, IndependentChannelDotProductEffectRendersItsCompleteChainInSeparatePass) {
    RCKMesh mesh(Context); RCKMaterial base(Context), layer(Context); RCKTexture primary(Context), color(Context), normal(Context);
    Pixel(primary, 0xFF112233); Pixel(color, 0xFF445566); Pixel(normal, 0xFF8080FF);
    base.SetTexture(0, &primary); layer.SetTexture(0, &color); layer.SetTexture(1, &normal); layer.SetEffect(VXEFFECT_DP3);
    Triangle(mesh, base); Channel(mesh, layer, .375f, .625f);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.DrawCount);
    Bound(State.Draws[0].Textures[0], primary); EXPECT_EQ(0u, State.Draws[0].Textures[1]);
    Bound(State.Draws[1].Textures[0], normal); Bound(State.Draws[1].Textures[1], color);
    EXPECT_EQ(CKRST_TOP_DOTPRODUCT3, State.Draws[1].Operations[0]); EXPECT_EQ(CKRST_TOP_MODULATE, State.Draws[1].Operations[1]);
    EXPECT_EQ(0u, State.Draws[1].Coordinates[0]); EXPECT_EQ(0u, State.Draws[1].Coordinates[1]);
    EXPECT_EQ(CKRST_DP_STAGES0, State.Draws[1].Flags & CKRST_DP_STAGESMASK);
    UV(State.Draws[1], 0, .375f, .625f);
}
TEST_F(MeshEffectChannelsTest, IndependentTwoTextureChannelRendersItsOwnPairInSeparatePass) {
    RCKMesh mesh(Context); RCKMaterial base(Context), layer(Context); RCKTexture primary(Context), first(Context), second(Context);
    Pixel(primary, 0xFF112233); Pixel(first, 0xFF445566); Pixel(second, 0xFF778899);
    base.SetTexture(0, &primary); layer.SetTexture(0, &first); layer.SetTexture(1, &second); layer.SetEffect(VXEFFECT_2TEXTURES);
    Triangle(mesh, base); Channel(mesh, layer, .375f, .625f);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.DrawCount);
    Bound(State.Draws[0].Textures[0], primary); EXPECT_EQ(0u, State.Draws[0].Textures[1]);
    Bound(State.Draws[1].Textures[0], first); Bound(State.Draws[1].Textures[1], second);
    EXPECT_EQ(0u, State.Draws[1].Coordinates[0]); EXPECT_EQ(1u, State.Draws[1].Coordinates[1]);
    EXPECT_NE(0u, State.Draws[1].Flags & CKRST_DP_STAGES1);
    UV(State.Draws[1], 0, .375f, .625f); UV(State.Draws[1], 1, .375f, .625f);
    mesh.SetChannelFlags(0, mesh.GetChannelFlags(0) | VXCHANNEL_SAMEUV);
    State.DrawCount = 0;
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.DrawCount);
    EXPECT_NE(0u, State.Draws[1].Flags & CKRST_DP_STAGES1);
    Bound(State.Draws[1].Textures[1], second);
    UV(State.Draws[1], 0, .125f, .25f); UV(State.Draws[1], 1, .125f, .25f);
}
TEST_F(MeshEffectChannelsTest, IndependentBumpChannelStartsItsEffectAtStageZeroInSeparatePass) {
    RCKMesh mesh(Context); RCKMaterial base(Context), layer(Context); RCKTexture primary(Context), color(Context), bump(Context), environment(Context);
    Pixel(primary, 0xFF112233); Pixel(color, 0xFF445566); Pixel(bump, 0xFF667788); Pixel(environment, 0xFF778899);
    base.SetTexture(0, &primary); layer.SetTexture(0, &color); layer.SetTexture(1, &bump); layer.SetTexture(2, &environment); layer.SetEffect(VXEFFECT_BUMPENV);
    BumpReflection(layer);
    Triangle(mesh, base); Channel(mesh, layer, .375f, .625f);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(2, State.DrawCount);
    Bound(State.Draws[0].Textures[0], primary); Bound(State.Draws[1].Textures[0], color);
    Bound(State.Draws[1].Textures[1], bump); Bound(State.Draws[1].Textures[2], environment);
    EXPECT_EQ(CKRST_TOP_BUMPENVMAP, State.Draws[1].Operations[1]);
    EXPECT_NE(0u, State.Draws[1].Flags & CKRST_DP_STAGES0);
    EXPECT_EQ(0u, State.Draws[1].Coordinates[0]); EXPECT_EQ(0u, State.Draws[1].Coordinates[1]);
    EXPECT_EQ(CKRSTPackTexcoordIndex(2, CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR), State.Draws[1].Coordinates[2]);
    UV(State.Draws[1], 0, .375f, .625f);
}
TEST_F(MeshEffectChannelsTest, IndependentEffectAndFollowingChannelKeepAuthoredPassOrder) {
    RCKMesh mesh(Context); RCKMaterial base(Context), layer(Context), tail(Context);
    RCKTexture primary(Context), first(Context), second(Context), last(Context);
    Pixel(primary, 0xFF112233); Pixel(first, 0xFF445566); Pixel(second, 0xFF778899); Pixel(last, 0xFF99AABB);
    base.SetTexture(0, &primary); layer.SetTexture(0, &first); layer.SetTexture(1, &second); layer.SetEffect(VXEFFECT_2TEXTURES); tail.SetTexture(0, &last);
    Triangle(mesh, base); Channel(mesh, layer, .375f, .625f); Channel(mesh, tail, .75f, .875f);
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(3, State.DrawCount);
    Bound(State.Draws[0].Textures[0], primary); Bound(State.Draws[1].Textures[0], first); Bound(State.Draws[1].Textures[1], second);
    Bound(State.Draws[2].Textures[0], last); EXPECT_EQ(0u, State.Draws[2].Textures[1]);
}
