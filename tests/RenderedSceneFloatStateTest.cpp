#include <gtest/gtest.h>
#include "../src/CKRenderedScene.cpp"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
TEST(RenderedSceneFloatStateTest, SignedZerosAndFractionalValuesUseTheirIeeeWords) {
    EXPECT_EQ(0x00000000u, FloatRenderState(0.0f));
    EXPECT_EQ(0x80000000u, FloatRenderState(-0.0f));
    EXPECT_EQ(0x3f800000u, FloatRenderState(1.0f));
    EXPECT_EQ(0xc0200000u, FloatRenderState(-2.5f));
    EXPECT_EQ(0x3e000000u, FloatRenderState(0.125f));
    EXPECT_EQ(0x3dcccccdu, FloatRenderState(0.1f));
}

TEST(RenderedSceneFloatStateTest, NormalAndSubnormalBoundariesRetainEveryPayloadBit) {
    const CKDWORD words[] = {
        0x00000001u, 0x80000001u, 0x007fffffu, 0x807fffffu,
        0x00800000u, 0x80800000u, 0x3f7fffffu, 0x3f800001u,
        0xbf7fffffu, 0xbf800001u, 0x7f7fffffu, 0xff7fffffu
    };
    for (unsigned int i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
        SCOPED_TRACE(i);
        float value;
        memcpy(&value, &words[i], sizeof(value));
        EXPECT_EQ(words[i], FloatRenderState(value));
    }
}

TEST(RenderedSceneFloatStateTest, InfinitiesAndQuietNaNsPreserveSignAndPayload) {
    const CKDWORD words[] = {
        0x7f800000u, 0xff800000u, 0x7fc00000u, 0xffc00000u,
        0x7fc12345u, 0xffc54321u, 0x7fffffffu, 0xffffffffu
    };
    for (unsigned int i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
        SCOPED_TRACE(i);
        float value;
        memcpy(&value, &words[i], sizeof(value));
        EXPECT_EQ(words[i], FloatRenderState(value));
    }
}

class FloatStateScene : public CKRenderedScene {
public:
    explicit FloatStateScene(CKRenderContext *context) : CKRenderedScene(context) {}
    using CKRenderedScene::m_FogMode;
    using CKRenderedScene::m_FogStart;
    using CKRenderedScene::m_FogEnd;
    using CKRenderedScene::m_FogDensity;
    using CKRenderedScene::m_FogColor;
};

class FloatStateObserver : public MaterialRenderObserver {
public:
    int StartWrites = 0, EndWrites = 0, DensityWrites = 0;
    CKBOOL SetRenderState(VXRENDERSTATETYPE state, CKDWORD value) override {
        if (state == VXRENDERSTATE_FOGSTART) ++StartWrites;
        if (state == VXRENDERSTATE_FOGEND) ++EndWrites;
        if (state == VXRENDERSTATE_FOGDENSITY) ++DensityWrites;
        return MaterialRenderObserver::SetRenderState(state, value);
    }
};

class RenderedSceneFloatStateWorldTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    FloatStateScene *Scene = nullptr;
    FloatStateObserver Boundary;
    CKRasterizerDriver Driver;
    int SavedProjection = 0;
    void SetUp() override {
        SavedProjection = g_FogProjectionMode;
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
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Manager->m_Drivers[0].Caps3D.RasterCaps = CKRST_RASTERCAPS_FOGRANGE | CKRST_RASTERCAPS_FOGPIXEL;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&Boundary, 0));
        Device = static_cast<RCKRenderContext *>(Context->CreateObject(CKCID_RENDERCONTEXT));
        ASSERT_NE(nullptr, Device); Device->DetachAll();
        Device->m_RasterizerContext = &Boundary; Device->m_RasterizerDriver = &Driver;
        Device->m_DriverIndex = 0;
        Device->m_ProjectionMatrix.SetIdentity();
        Scene = new FloatStateScene(Device);
        Scene->m_FogMode = VXFOG_EXP2; Scene->m_FogColor = 0xff123456u;
        g_FogProjectionMode = 0;
    }
    void TearDown() override {
        delete Scene;
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
        g_FogProjectionMode = SavedProjection;
    }
    void ExactProjection() {
        Scene->m_FogStart = 2.0f; Scene->m_FogEnd = 6.0f; Scene->m_FogDensity = 0.125f;
        // z = 3*d + 2, w = 2: projected start=4, end=10, reciprocal w=0.5.
        Device->m_ProjectionMatrix[2][2] = 3.0f;
        Device->m_ProjectionMatrix[3][2] = 2.0f;
        Device->m_ProjectionMatrix[2][3] = 0.0f;
        Device->m_ProjectionMatrix[3][3] = 2.0f;
    }
};

TEST_F(RenderedSceneFloatStateWorldTest, RawFogFieldsReachTheirOwnStatesWithoutNumericConversion) {
    const CKDWORD start = 0x80000000u, end = 0x3f800001u, density = 0x00000001u;
    memcpy(&Scene->m_FogStart, &start, sizeof(start));
    memcpy(&Scene->m_FogEnd, &end, sizeof(end));
    memcpy(&Scene->m_FogDensity, &density, sizeof(density));
    Scene->SetDefaultRenderStates(nullptr);
    EXPECT_EQ(start, Boundary.States[VXRENDERSTATE_FOGSTART]);
    EXPECT_EQ(end, Boundary.States[VXRENDERSTATE_FOGEND]);
    EXPECT_EQ(density, Boundary.States[VXRENDERSTATE_FOGDENSITY]);
    EXPECT_EQ(0xff123456u, Boundary.States[VXRENDERSTATE_FOGCOLOR]);
    EXPECT_EQ(1, Boundary.StartWrites); EXPECT_EQ(1, Boundary.EndWrites); EXPECT_EQ(1, Boundary.DensityWrites);
}

TEST_F(RenderedSceneFloatStateWorldTest, DensitySpecialPayloadsStayRawInEveryProjectionModeAndExplicitDevice) {
    FloatStateObserver other;
    const CKDWORD words[] = {0x80000000u, 0x00000001u, 0x807fffffu, 0x7f7fffffu,
        0xff7fffffu, 0x7f800000u, 0xff800000u, 0x7fc12345u, 0xffc54321u};
    ExactProjection();
    for (int mode = 0; mode < 3; ++mode) {
        g_FogProjectionMode = mode;
        for (unsigned int i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
            SCOPED_TRACE(mode);
            SCOPED_TRACE(i);
            memcpy(&Scene->m_FogDensity, &words[i], sizeof(Scene->m_FogDensity));
            Scene->SetDefaultRenderStates(&other);
            EXPECT_EQ(words[i], other.States[VXRENDERSTATE_FOGDENSITY]);
        }
    }
    EXPECT_EQ(27, other.DensityWrites); EXPECT_EQ(0, Boundary.StateWrites);
}

TEST_F(RenderedSceneFloatStateWorldTest, ProjectionModeOnePublishesTheComputedDepthWords) {
    ExactProjection(); g_FogProjectionMode = 1;
    Scene->SetDefaultRenderStates(nullptr);
    EXPECT_EQ(0x40800000u, Boundary.States[VXRENDERSTATE_FOGSTART]);
    EXPECT_EQ(0x41200000u, Boundary.States[VXRENDERSTATE_FOGEND]);
    EXPECT_EQ(0x3e000000u, Boundary.States[VXRENDERSTATE_FOGDENSITY]);
    EXPECT_EQ(1, Boundary.StartWrites); EXPECT_EQ(1, Boundary.EndWrites); EXPECT_EQ(1, Boundary.DensityWrites);
}

TEST_F(RenderedSceneFloatStateWorldTest, ProjectionModeTwoPublishesReciprocalWAndProjectedStartWords) {
    ExactProjection(); g_FogProjectionMode = 2;
    Scene->SetDefaultRenderStates(nullptr);
    EXPECT_EQ(0x3f000000u, Boundary.States[VXRENDERSTATE_FOGSTART]);
    EXPECT_EQ(0x40800000u, Boundary.States[VXRENDERSTATE_FOGEND]);
    EXPECT_EQ(0x3e000000u, Boundary.States[VXRENDERSTATE_FOGDENSITY]);
    EXPECT_EQ(1, Boundary.StartWrites); EXPECT_EQ(1, Boundary.EndWrites); EXPECT_EQ(1, Boundary.DensityWrites);
}

TEST_F(RenderedSceneFloatStateWorldTest, DisabledFogDoesNotPublishAnyFloatingPointState) {
    Scene->m_FogMode = VXFOG_NONE;
    Boundary.States[VXRENDERSTATE_FOGSTART] = 0x11111111u;
    Boundary.States[VXRENDERSTATE_FOGEND] = 0x22222222u;
    Boundary.States[VXRENDERSTATE_FOGDENSITY] = 0x33333333u;
    Scene->SetDefaultRenderStates(nullptr);
    EXPECT_FALSE(Boundary.States[VXRENDERSTATE_FOGENABLE]);
    EXPECT_EQ(0x11111111u, Boundary.States[VXRENDERSTATE_FOGSTART]);
    EXPECT_EQ(0x22222222u, Boundary.States[VXRENDERSTATE_FOGEND]);
    EXPECT_EQ(0x33333333u, Boundary.States[VXRENDERSTATE_FOGDENSITY]);
    EXPECT_EQ(0, Boundary.StartWrites); EXPECT_EQ(0, Boundary.EndWrites); EXPECT_EQ(0, Boundary.DensityWrites);
}
} // namespace
