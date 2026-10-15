#include <gtest/gtest.h>
#include <climits>
#include <cstring>
#include <new>
#include "CKAll.h"
#include "RCKKeyframeData.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKObjectAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
thread_local bool FailNextKeyArray = false;
thread_local void *ObservedKeys = nullptr;
thread_local void *ObservedTangents = nullptr;
thread_local int KeyReleases = 0;
thread_local int TangentReleases = 0;

class InspectTCBPosition : public RCKTCBPositionController {
public:
    VxVector *Tangents() const { return m_Tangents; }
    void RecordLayout() {
#define TCB_FIELD(field) \
        ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(reinterpret_cast<const char *>(&field) - reinterpret_cast<const char *>(this))); \
        ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(field)))
        TCB_FIELD(m_Type); TCB_FIELD(m_NbKeys); TCB_FIELD(m_Length); TCB_FIELD(m_Keys); TCB_FIELD(m_Tangents);
#undef TCB_FIELD
        ::testing::Test::RecordProperty("controller_size", static_cast<int>(sizeof(RCKTCBPositionController)));
        ::testing::Test::RecordProperty("controller_alignment", static_cast<int>(alignof(RCKTCBPositionController)));
        ::testing::Test::RecordProperty("anim_base_size", static_cast<int>(sizeof(CKAnimController)));
        ::testing::Test::RecordProperty("tcb_position_key_size", static_cast<int>(sizeof(CKTCBPositionKey)));
        ::testing::Test::RecordProperty("tcb_position_key_alignment", static_cast<int>(alignof(CKTCBPositionKey)));
        CKTCBPositionKey key;
#define KEY_FIELD(field) \
        ::testing::Test::RecordProperty("key_" #field "_offset", static_cast<int>(reinterpret_cast<const char *>(&key.field) - reinterpret_cast<const char *>(&key))); \
        ::testing::Test::RecordProperty("key_" #field "_size", static_cast<int>(sizeof(key.field)))
        KEY_FIELD(TimeStep); KEY_FIELD(Pos); KEY_FIELD(tension); KEY_FIELD(continuity);
        KEY_FIELD(bias); KEY_FIELD(easeto); KEY_FIELD(easefrom);
#undef KEY_FIELD
    }
};

class DeletedTCBPosition : public InspectTCBPosition {
public:
    explicit DeletedTCBPosition(int *destroyed) : Destroyed(destroyed) {}
    ~DeletedTCBPosition() override { ++*Destroyed; }
private:
    int *Destroyed;
};

class CustomTCBPosition : public InspectTCBPosition {
public:
    int Calls = 0;
    // No override specifier, so the same behavioral regression can run on
    // the frozen pre-fix class where this slot was mistakenly nonvirtual.
    void ComputeTangents() {
        ++Calls;
        RCKTCBPositionController::ComputeTangents();
        if (m_Tangents && m_NbKeys == 2)
            m_Tangents[1].Set(0.0f, 0.0f, 0.0f);
    }
};

class TCBPositionLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override { SetProcessorSpecific_FunctionsPtr(); }
    void TearDown() override {
        FailNextKeyArray = false; ObservedKeys = nullptr; ObservedTangents = nullptr;
        KeyReleases = 0; TangentReleases = 0;
    }
    static void Vec(const VxVector &value, const VxVector &expected) {
        EXPECT_NEAR(expected.x, value.x, 2.0e-6f);
        EXPECT_NEAR(expected.y, value.y, 2.0e-6f);
        EXPECT_NEAR(expected.z, value.z, 2.0e-6f);
    }
    static void Author(CKAnimController &controller) {
        VxVector first(2.0f, 4.0f, 6.0f), last(10.0f, 8.0f, 2.0f);
        CKTCBPositionKey a(2.0f, first), b(6.0f, last);
        ASSERT_EQ(0, controller.AddKey(&a)); ASSERT_EQ(1, controller.AddKey(&b));
        controller.SetLength(12.0f);
    }
    static void Consume(CKAnimController &controller) {
        EXPECT_EQ(CKANIMATION_TCBPOS_CONTROL, controller.GetType());
        ASSERT_EQ(2, controller.GetKeyCount()); EXPECT_FLOAT_EQ(12.0f, controller.GetLength());
        VxVector result; ASSERT_TRUE(controller.Evaluate(3.0f, &result)); Vec(result, VxVector(4.0f, 5.0f, 5.0f));
        const float expected[] = {2.0f, 2.0f, 4.0f, 6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  6.0f, 10.0f, 8.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        unsigned char wire[80]; std::memset(wire, 0xa5, sizeof(wire));
        ASSERT_EQ(76, controller.DumpKeysTo(nullptr)); ASSERT_EQ(76, controller.DumpKeysTo(wire));
        int count = -1; std::memcpy(&count, wire, sizeof(count)); EXPECT_EQ(2, count);
        EXPECT_EQ(0, std::memcmp(expected, wire + 4, sizeof(expected)));
        for (int i = 76; i < 80; ++i) EXPECT_EQ(0xa5, wire[i]);
    }
    static void Observe(InspectTCBPosition &controller) {
        ObservedKeys = controller.GetKey(0); ObservedTangents = controller.Tangents();
        KeyReleases = 0; TangentReleases = 0;
    }
};
}

// The hooks count releases of two exact owned arrays, not unrelated test
// allocations. The nothrow hook also reaches the real Clone failure branch.
void *operator new[](size_t bytes, const std::nothrow_t &) noexcept {
    if (FailNextKeyArray) { FailNextKeyArray = false; return nullptr; }
    try { return ::operator new[](bytes); }
    catch (const std::bad_alloc &) { return nullptr; }
}

void operator delete[](void *pointer) noexcept {
    if (pointer && pointer == ObservedKeys) ++KeyReleases;
    if (pointer && pointer == ObservedTangents) ++TangentReleases;
    ::operator delete(pointer);
}

void operator delete[](void *pointer, size_t) noexcept { ::operator delete[](pointer); }

TEST_F(TCBPositionLifecycleTest, TenthVirtualSlotCustomizesTangentsThroughEvaluateAndConcreteBase) {
    CustomTCBPosition controller; CKAnimController *api = &controller; Author(*api);
    VxVector output; ASSERT_TRUE(api->Evaluate(3.0f, &output));
    EXPECT_EQ(1, controller.Calls); Vec(output, VxVector(2.875f, 4.4375f, 5.5625f));
    ASSERT_TRUE(api->Evaluate(3.0f, &output)); EXPECT_EQ(1, controller.Calls);
    RCKTCBPositionController *concreteBase = &controller;
    concreteBase->ComputeTangents(); EXPECT_EQ(2, controller.Calls);
    ASSERT_TRUE(api->Evaluate(3.0f, &output)); Vec(output, VxVector(2.875f, 4.4375f, 5.5625f));
}
