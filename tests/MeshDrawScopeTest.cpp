#include <gtest/gtest.h>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <type_traits>
#include "CKAll.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MaterialRenderObserver.h"
#include "../src/CKMeshDrawScope.h"

extern void SetProcessorSpecific_FunctionsPtr();

static_assert(!std::is_copy_constructible<CKMeshDrawScope>::value, "A draw scope cannot duplicate its TLS ownership");
static_assert(!std::is_copy_assignable<CKMeshDrawScope>::value, "A draw scope cannot replace its TLS ownership");
static_assert(!std::is_copy_constructible<CKMeshDrawCall>::value, "A base call cannot duplicate its recursion ownership");
static_assert(!std::is_copy_assignable<CKMeshDrawCall>::value, "A base call cannot replace its recursion ownership");

namespace {
struct DrawScopePacket {
    VxVector Positions[3], Normals[3];
    CKDWORD Colors[3] = {0xff204060, 0xff406080, 0xff6080a0};
    CKDWORD Specular[3] = {0, 0, 0};
    VxDrawPrimitiveData Data = {};
    DrawScopePacket() {
        Positions[0] = VxVector(0, 0, 0);
        Positions[1] = VxVector(1, 0, 0);
        Positions[2] = VxVector(0, 1, 0);
        for (int i = 0; i < 3; ++i) Normals[i] = VxVector(0, 0, 1);
        Data.VertexCount = 3;
        Data.Flags = (CKRST_DPFLAGS)(CKRST_DP_TRANSFORM | CKRST_DP_LIGHT);
        Data.PositionPtr = Positions; Data.PositionStride = sizeof(VxVector);
        Data.NormalPtr = Normals; Data.NormalStride = sizeof(VxVector);
        Data.ColorPtr = Colors; Data.ColorStride = sizeof(CKDWORD);
        Data.SpecularColorPtr = Specular; Data.SpecularColorStride = sizeof(CKDWORD);
    }
};

class DrawScopeObserver : public MaterialRenderObserver {
public:
    int LightingWrites = 0, Draws = 0;
    CKDWORD DrawLighting[8] = {};
    CKBOOL SetRenderState(VXRENDERSTATETYPE state, CKDWORD value) override {
        if (state == VXRENDERSTATE_LIGHTING) ++LightingWrites;
        return MaterialRenderObserver::SetRenderState(state, value);
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *, int count, VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_EQ(3, count);
        EXPECT_EQ(3, data->VertexCount); EXPECT_NE(nullptr, data->PositionPtr);
        EXPECT_LT(Draws, 8);
        if (Draws < 8) DrawLighting[Draws] = States[VXRENDERSTATE_LIGHTING];
        ++Draws;
        return TRUE;
    }
};

bool Preserves(CKRenderContext *context, VxDrawPrimitiveData *data) {
    CKMeshDrawCall call(context, data);
    return call.PreservesLighting() != FALSE;
}

class MeshDrawScopeTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *Device = nullptr, *OtherDevice = nullptr;
    DrawScopeObserver State, OtherState;
    DrawScopePacket Packet, OtherPacket;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context); OtherDevice = new RCKRenderContext(Context);
        Device->m_RasterizerContext = &State; OtherDevice->m_RasterizerContext = &OtherState;
        Device->m_ViewportData.ViewWidth = OtherDevice->m_ViewportData.ViewWidth = 64;
        Device->m_ViewportData.ViewHeight = OtherDevice->m_ViewportData.ViewHeight = 48;
    }
    void TearDown() override {
        if (OtherDevice) { OtherDevice->m_RasterizerContext = nullptr; delete OtherDevice; }
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
};
}

TEST_F(MeshDrawScopeTest, WithoutMeshScopeBothPublicContextsUseTheOrdinaryLightingRule) {
    EXPECT_FALSE(Preserves(Device, &Packet.Data)); EXPECT_FALSE(Preserves(OtherDevice, &OtherPacket.Data));
    State.States[VXRENDERSTATE_LIGHTING] = 0;
    ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Packet.Data));
    OtherPacket.Data.Flags = CKRST_DP_TRANSFORM;
    OtherState.States[VXRENDERSTATE_LIGHTING] = 1;
    ASSERT_TRUE(OtherDevice->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &OtherPacket.Data));
    EXPECT_EQ(1, State.LightingWrites); EXPECT_EQ(1u, State.DrawLighting[0]);
    EXPECT_EQ(1, OtherState.LightingWrites); EXPECT_EQ(0u, OtherState.DrawLighting[0]);
}

TEST_F(MeshDrawScopeTest, ExactIdentityAllowsEachSequentialBaseDelegation) {
    {
        CKMeshDrawScope mesh(Device, &Packet.Data);
        for (int i = 0; i < 3; ++i) EXPECT_TRUE(Preserves(Device, &Packet.Data));
    }
    EXPECT_FALSE(Preserves(Device, &Packet.Data));
}

TEST_F(MeshDrawScopeTest, AnotherContextAndACopiedPacketCannotConsumeTheOriginalDelegation) {
    VxDrawPrimitiveData copiedPacket = Packet.Data;
    CKMeshDrawScope mesh(Device, &Packet.Data);
    CKMeshDrawCall otherContext(OtherDevice, &Packet.Data);
    CKMeshDrawCall otherPacket(Device, &copiedPacket);
    EXPECT_FALSE(otherContext.PreservesLighting()); EXPECT_FALSE(otherPacket.PreservesLighting());
    EXPECT_TRUE(Preserves(Device, &Packet.Data));
    EXPECT_FALSE(Preserves(OtherDevice, &copiedPacket));
    EXPECT_TRUE(Preserves(Device, &Packet.Data));
}

TEST_F(MeshDrawScopeTest, RecursiveBaseCallsCannotBorrowLightingOrReleaseTheOuterCallMarker) {
    CKMeshDrawScope mesh(Device, &Packet.Data);
    {
        CKMeshDrawCall outer(Device, &Packet.Data);
        EXPECT_TRUE(outer.PreservesLighting());
        {
            CKMeshDrawCall recursive(Device, &Packet.Data);
            EXPECT_FALSE(recursive.PreservesLighting());
        }
        EXPECT_FALSE(Preserves(Device, &Packet.Data));
    }
    EXPECT_TRUE(Preserves(Device, &Packet.Data));
}

TEST_F(MeshDrawScopeTest, NestedMeshScopeShadowsThenRestoresTheEnclosingPacketAndContext) {
    {
        CKMeshDrawScope outer(Device, &Packet.Data);
        EXPECT_TRUE(Preserves(Device, &Packet.Data));
        {
            CKMeshDrawScope inner(OtherDevice, &OtherPacket.Data);
            EXPECT_FALSE(Preserves(Device, &Packet.Data));
            EXPECT_FALSE(Preserves(OtherDevice, &Packet.Data));
            EXPECT_TRUE(Preserves(OtherDevice, &OtherPacket.Data));
        }
        EXPECT_FALSE(Preserves(OtherDevice, &OtherPacket.Data));
        EXPECT_TRUE(Preserves(Device, &Packet.Data));
    }
    EXPECT_FALSE(Preserves(Device, &Packet.Data));
}

TEST_F(MeshDrawScopeTest, NestedMeshDuringAnActiveBaseCallDoesNotReleaseTheOuterRecursionMarker) {
    CKMeshDrawScope outer(Device, &Packet.Data);
    {
        CKMeshDrawCall outerCall(Device, &Packet.Data);
        EXPECT_TRUE(outerCall.PreservesLighting());
        {
            CKMeshDrawScope inner(Device, &OtherPacket.Data);
            EXPECT_TRUE(Preserves(Device, &OtherPacket.Data));
            EXPECT_FALSE(Preserves(Device, &Packet.Data));
        }
        EXPECT_FALSE(Preserves(Device, &Packet.Data));
    }
    EXPECT_TRUE(Preserves(Device, &Packet.Data));
}

TEST_F(MeshDrawScopeTest, ExceptionUnwindingRestoresTheEnclosingScopeAndCallEligibility) {
    {
        CKMeshDrawScope outer(Device, &Packet.Data);
        try {
            CKMeshDrawScope inner(OtherDevice, &OtherPacket.Data);
            CKMeshDrawCall innerCall(OtherDevice, &OtherPacket.Data);
            EXPECT_TRUE(innerCall.PreservesLighting());
            throw 17;
        } catch (int value) { EXPECT_EQ(17, value); }
        EXPECT_FALSE(Preserves(OtherDevice, &OtherPacket.Data));
        EXPECT_TRUE(Preserves(Device, &Packet.Data));
        try {
            CKMeshDrawCall outerCall(Device, &Packet.Data);
            EXPECT_TRUE(outerCall.PreservesLighting());
            throw 23;
        } catch (int value) { EXPECT_EQ(23, value); }
        EXPECT_TRUE(Preserves(Device, &Packet.Data));
    }
    EXPECT_FALSE(Preserves(Device, &Packet.Data));
}

TEST_F(MeshDrawScopeTest, ActualPublicSubmissionPreservesMeshLightingOnlyForTheEligibleIdentity) {
    VxDrawPrimitiveData copiedPacket = Packet.Data;
    State.States[VXRENDERSTATE_LIGHTING] = 0;
    OtherState.States[VXRENDERSTATE_LIGHTING] = 0;
    {
        CKMeshDrawScope mesh(Device, &Packet.Data);
        ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Packet.Data));
        EXPECT_EQ(0, State.LightingWrites); EXPECT_EQ(0u, State.DrawLighting[0]);
        ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &copiedPacket));
        EXPECT_EQ(1, State.LightingWrites); EXPECT_EQ(1u, State.DrawLighting[1]);
        ASSERT_TRUE(OtherDevice->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Packet.Data));
        EXPECT_EQ(1, OtherState.LightingWrites); EXPECT_EQ(1u, OtherState.DrawLighting[0]);
        State.States[VXRENDERSTATE_LIGHTING] = 0;
        ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Packet.Data));
        EXPECT_EQ(1, State.LightingWrites); EXPECT_EQ(0u, State.DrawLighting[2]);
    }
    ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Packet.Data));
    EXPECT_EQ(2, State.LightingWrites); EXPECT_EQ(1u, State.DrawLighting[3]);
}

TEST_F(MeshDrawScopeTest, ConcurrentThreadScopesAreIsolatedAndLeaveNoWorkerScopeBehind) {
    std::mutex mutex;
    std::condition_variable changed;
    bool workerReady = false, mainObserved = false;
    bool workerInheritsMain = true, workerOwnsPacket = false, workerBorrowsMain = true;
    bool workerKeepsScope = false, workerLeaksOwnScope = true, workerLeaksMainScope = true;
    {
        CKMeshDrawScope mainScope(Device, &Packet.Data);
        std::thread worker([&]() {
            workerInheritsMain = Preserves(Device, &Packet.Data);
            {
                CKMeshDrawScope workerScope(OtherDevice, &OtherPacket.Data);
                workerOwnsPacket = Preserves(OtherDevice, &OtherPacket.Data);
                workerBorrowsMain = Preserves(Device, &Packet.Data);
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    workerReady = true; changed.notify_one();
                    changed.wait(lock, [&]() { return mainObserved; });
                }
                workerKeepsScope = Preserves(OtherDevice, &OtherPacket.Data);
            }
            workerLeaksOwnScope = Preserves(OtherDevice, &OtherPacket.Data);
            workerLeaksMainScope = Preserves(Device, &Packet.Data);
        });
        {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock, [&]() { return workerReady; });
            EXPECT_TRUE(Preserves(Device, &Packet.Data));
            EXPECT_FALSE(Preserves(OtherDevice, &OtherPacket.Data));
            mainObserved = true; changed.notify_one();
        }
        worker.join();
        EXPECT_TRUE(Preserves(Device, &Packet.Data));
    }
    EXPECT_FALSE(workerInheritsMain); EXPECT_TRUE(workerOwnsPacket); EXPECT_FALSE(workerBorrowsMain);
    EXPECT_TRUE(workerKeepsScope); EXPECT_FALSE(workerLeaksOwnScope); EXPECT_FALSE(workerLeaksMainScope);
    EXPECT_FALSE(Preserves(Device, &Packet.Data));
}
