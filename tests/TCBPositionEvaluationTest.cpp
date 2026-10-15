#include <gtest/gtest.h>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <new>
#include "CKAll.h"
#include "RCKKeyframeData.h"
#include "RCKObjectAnimation.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "TCBReferenceSamples.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
thread_local bool FailNextTangentArray = false;

class InspectTCBPosition : public RCKTCBPositionController {
public:
    const VxVector *Cache() const { return m_Tangents; }
};

void AddPosition(RCKTCBPositionController &controller, float time, float x, float y = 0, float z = 0,
                 float tension = 0, float continuity = 0, float bias = 0,
                 float easeTo = 0, float easeFrom = 0) {
    VxVector position(x, y, z);
    CKTCBPositionKey key(time, position, tension, continuity, bias, easeTo, easeFrom);
    ASSERT_GE(controller.AddKey(&key), 0);
}

void PositionIs(const VxVector &position, float x, float y = 0, float z = 0) {
    EXPECT_FLOAT_EQ(x, position.x); EXPECT_FLOAT_EQ(y, position.y); EXPECT_FLOAT_EQ(z, position.z);
}

void ReadWire(RCKTCBPositionController &controller, const float *keys, int count) {
    // 2.1 wire: DWORD count, then time,x,y,z,T,C,B,easeto,easefrom.
    unsigned char wire[4 + 36 * 8] = {};
    ASSERT_GE(count, 0); ASSERT_LE(count, 8);
    std::memcpy(wire, &count, 4);
    if (count) std::memcpy(wire + 4, keys, 36 * count);
    ASSERT_EQ(4 + 36 * count, controller.ReadKeysFrom(wire));
}

class TCBPositionEvaluationTest : public ::testing::Test {
protected:
    void SetUp() override { SetProcessorSpecific_FunctionsPtr(); }
    void TearDown() override { FailNextTangentArray = false; }
};
}

// A real failed cache allocation must leave the observable curve/output intact.
void *operator new[](size_t bytes, const std::nothrow_t &) noexcept {
    if (FailNextTangentArray) { FailNextTangentArray = false; return nullptr; }
    try { return ::operator new[](bytes); }
    catch (const std::bad_alloc &) { return nullptr; }
}

namespace {

TEST_F(TCBPositionEvaluationTest, OpposingFiniteTimeSpansKeepUnitInteriorWeightsAndCorrectCurve) {
    InspectTCBPosition controller;
    AddPosition(controller, -FLT_MAX, 0); AddPosition(controller, 0, 4); AddPosition(controller, FLT_MAX, 8);
    VxVector output; ASSERT_TRUE(controller.Evaluate(-FLT_MAX * .5f, &output)); PositionIs(output, 2);
    PositionIs(controller.Cache()[1], 4); PositionIs(controller.Cache()[2], 4); PositionIs(controller.Cache()[3], 4);
    ASSERT_TRUE(controller.Evaluate(FLT_MAX * .5f, &output)); PositionIs(output, 5.5f);
    // Native's asymmetric last endpoint rule gives incoming tangent 8, not 4.
    PositionIs(controller.Cache()[4], 8);
}

TEST_F(TCBPositionEvaluationTest, SmallFiniteTimesDoNotOverflowTheReciprocalUsedForWeights) {
    const float tiny = std::ldexp(1.0f, -130);
    InspectTCBPosition controller;
    AddPosition(controller, -tiny, 0); AddPosition(controller, 0, 4); AddPosition(controller, tiny, 8);
    VxVector output; ASSERT_TRUE(controller.Evaluate(-tiny * .5f, &output)); PositionIs(output, 2);
    PositionIs(controller.Cache()[2], 4); PositionIs(controller.Cache()[3], 4);
}

class TCBPositionAnimationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_ANIMATION); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
};

} // namespace
