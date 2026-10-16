#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include <new>
#include "CKAll.h"
#include "RCKKeyframeData.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKObjectAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
thread_local bool FailNextFactoryAllocation = false;
int DeletedCategories[5] = {};

class DeletedPosition : public RCKLinearPositionController { public: ~DeletedPosition() override { ++DeletedCategories[0]; } };
class DeletedScale : public RCKLinearScaleController { public: ~DeletedScale() override { ++DeletedCategories[1]; } };
class DeletedRotation : public RCKLinearRotationController { public: ~DeletedRotation() override { ++DeletedCategories[2]; } };
class DeletedAxis : public RCKLinearScaleAxisController { public: ~DeletedAxis() override { ++DeletedCategories[3]; } };
class DeletedMorph : public RCKMorphController { public: ~DeletedMorph() override { ++DeletedCategories[4]; } };

class FlagsLayout : public CKBezierKeyFlags {
public:
    size_t Offset() const { return reinterpret_cast<const char *>(&m_Flags) - reinterpret_cast<const char *>(this); }
};

template<class Controller>
class EditingBezier : public Controller {
public:
    enum Action { RemoveLast, RemoveAll, AppendAndLength };
    explicit EditingBezier(Action action) : Edit(action) {}
    Action Edit;
    bool Edited = false;
    int Calls = 0;
    int Indices[8] = {};
    int AddResult = -1;
    bool Ready() const { return this->m_TangentsComputed != FALSE; }

    // No override specifier: the same regression also builds with the old
    // header whose missing indexed virtual slot is part of the baseline.
    void ComputeBezierPts(int index) {
        if (Calls < 8) Indices[Calls] = index;
        ++Calls;
        Controller::ComputeBezierPts(index);
        if (Edited || index != 0) return;
        Edited = true;
        if (Edit == RemoveLast) {
            this->RemoveKey(this->GetKeyCount() - 1);
        } else if (Edit == RemoveAll) {
            while (this->GetKeyCount() > 0) this->RemoveKey(this->GetKeyCount() - 1);
        } else {
            this->SetLength(20);
            VxVector position(16, 32, -16);
            CKBezierKeyFlags flags;
            flags.SetInTangentMode(BEZIER_KEY_LINEAR);
            flags.SetOutTangentMode(BEZIER_KEY_FAST);
            CKBezierPositionKey key(8, position, flags);
            AddResult = this->AddKey(&key);
        }
    }
};

class ControllerContractsTest : public ::testing::Test {
protected:
    void SetUp() override { SetProcessorSpecific_FunctionsPtr(); }
    void TearDown() override { FailNextFactoryAllocation = false; }
    static void Vec(const VxVector &actual, const VxVector &expected) {
        EXPECT_FLOAT_EQ(expected.x, actual.x); EXPECT_FLOAT_EQ(expected.y, actual.y); EXPECT_FLOAT_EQ(expected.z, actual.z);
    }
    static void Quat(const VxQuaternion &actual, const VxQuaternion &expected) {
        EXPECT_FLOAT_EQ(expected.x, actual.x); EXPECT_FLOAT_EQ(expected.y, actual.y);
        EXPECT_FLOAT_EQ(expected.z, actual.z); EXPECT_FLOAT_EQ(expected.w, actual.w);
    }
    template<class T> static void Layout(const char *name, const T &value, size_t size, size_t alignment) {
        RecordProperty((std::string(name) + "_size").c_str(), static_cast<int>(sizeof(T)));
        RecordProperty((std::string(name) + "_alignment").c_str(), static_cast<int>(alignof(T)));
        EXPECT_EQ(size, sizeof(value)); EXPECT_EQ(alignment, alignof(T));
    }
    template<class T, class F> static void Field(const char *name, const T &owner, const F &field, size_t offset) {
        const size_t actual = reinterpret_cast<const char *>(&field) - reinterpret_cast<const char *>(&owner);
        RecordProperty((std::string(name) + "_offset").c_str(), static_cast<int>(actual));
        RecordProperty((std::string(name) + "_size").c_str(), static_cast<int>(sizeof(field)));
        EXPECT_EQ(offset, actual);
    }
    static void AddAutoKey(CKAnimController &controller, float time, float value, int index) {
        VxVector position(value, value * 2, -value);
        CKBezierKeyFlags flags;
        flags.SetInTangentMode(BEZIER_KEY_AUTOSMOOTH);
        flags.SetOutTangentMode(BEZIER_KEY_AUTOSMOOTH);
        CKBezierPositionKey key(time, position, flags);
        ASSERT_EQ(index, controller.AddKey(&key));
    }
    static void GuardedDump(CKAnimController &controller, const CKDWORD *expected, int count) {
        CKDWORD words[18];
        for (int i = 0; i < 18; ++i) words[i] = 0xa5a5a5a5;
        ASSERT_LE(count, 17);
        ASSERT_EQ(count * 4, controller.DumpKeysTo(nullptr));
        ASSERT_EQ(count * 4, controller.DumpKeysTo(words));
        for (int i = 0; i < count; ++i) EXPECT_EQ(expected[i], words[i]) << "wire DWORD " << i;
        EXPECT_EQ(0xa5a5a5a5u, words[count]);
    }
    template<class Controller> static void RemoveFromIndexedHook(bool clear) {
        typedef EditingBezier<Controller> Editable;
        Editable controller(clear ? Editable::RemoveAll : Editable::RemoveLast);
        controller.SetLength(12);
        AddAutoKey(controller, 0, 0, 0);
        AddAutoKey(controller, 4, 8, 1);
        AddAutoKey(controller, 8, 16, 2);
        CKAnimController &base = controller;
        struct { VxVector Value; CKDWORD Guard; } output = {VxVector(71, 83, 97), 0x12345678};
        const CKBOOL evaluated = base.Evaluate(2, &output.Value);
        ASSERT_TRUE(controller.Edited);
        ASSERT_EQ(clear ? 0 : 2, controller.GetKeyCount());
        EXPECT_TRUE(controller.Ready());
        EXPECT_EQ(clear ? 1 : 2, controller.Calls);
        EXPECT_EQ(0, controller.Indices[0]);
        EXPECT_EQ(0x12345678u, output.Guard);
        if (clear) {
            EXPECT_FALSE(evaluated);
            Vec(output.Value, VxVector(71, 83, 97));
            EXPECT_EQ(nullptr, controller.GetKey(0));
            const CKDWORD empty[] = {0};
            GuardedDump(base, empty, 1);
        } else {
            EXPECT_TRUE(evaluated);
            EXPECT_EQ(1, controller.Indices[1]);
            Vec(output.Value, VxVector(4, 8, -4));
            const CKDWORD remaining[] = {
                2, 0, 0, 0, 0x80000000, 0x00010001,
                0x40800000, 0x41000000, 0x41800000, 0xc1000000, 0x00010001
            };
            GuardedDump(base, remaining, 11);
        }
    }
    template<class Controller> static void AppendFromIndexedHook() {
        typedef EditingBezier<Controller> Editable;
        Editable controller(Editable::AppendAndLength);
        controller.SetLength(12);
        AddAutoKey(controller, 0, 0, 0);
        AddAutoKey(controller, 4, 8, 1);
        CKAnimController &base = controller;
        struct { VxVector Value; CKDWORD Guard; } output = {VxVector(71, 83, 97), 0x12345678};
        const CKBOOL evaluated = base.Evaluate(6, &output.Value);
        ASSERT_TRUE(controller.Edited);
        ASSERT_EQ(3, controller.GetKeyCount());
        EXPECT_TRUE(evaluated);
        EXPECT_TRUE(controller.Ready());
        EXPECT_EQ(2, controller.AddResult);
        EXPECT_FLOAT_EQ(20, controller.GetLength());
        EXPECT_EQ(2, controller.Calls);
        EXPECT_EQ(0, controller.Indices[0]);
        EXPECT_EQ(1, controller.Indices[1]);
        EXPECT_EQ(0x12345678u, output.Guard);
        Vec(output.Value, VxVector(12, 24, -12));
        const CKBezierPositionKey *last = static_cast<CKBezierPositionKey *>(controller.GetKey(2));
        ASSERT_NE(nullptr, last);
        Vec(last->In, VxVector(-2, -4, 2));
        Vec(last->Out, VxVector(-8.0f / 3, -16.0f / 3, 8.0f / 3));
        const CKDWORD appended[] = {
            3, 0, 0, 0, 0x80000000, 0x00010001,
            0x40800000, 0x41000000, 0x41800000, 0xc1000000, 0x00010001,
            0x41000000, 0x41800000, 0x42000000, 0xc1800000, 0x00080002
        };
        GuardedDump(base, appended, 16);
    }
};
}

void *operator new(size_t bytes, const std::nothrow_t &) noexcept {
    if (FailNextFactoryAllocation) { FailNextFactoryAllocation = false; return nullptr; }
    try { return ::operator new(bytes); } catch (const std::bad_alloc &) { return nullptr; }
}

TEST_F(ControllerContractsTest, KeyTimeConstructionAndMutationPreserveFloatBits) {
    CKKey zero; EXPECT_FLOAT_EQ(0, zero.GetTime());
    CKKey key(-3.5f); EXPECT_FLOAT_EQ(-3.5f, key.GetTime());
    key.SetTime(-0.0f); EXPECT_TRUE(std::signbit(key.GetTime()));
    key.SetTime(17.25f); EXPECT_FLOAT_EQ(17.25f, key.TimeStep);
}

TEST_F(ControllerContractsTest, LinearKeysExposeTheirOwnBorrowedPayloadAndBaseTime) {
    VxVector p(2, -4, 6); VxQuaternion q(0, 1, 0, 0);
    CKPositionKey position(3, p); CKRotationKey rotation(7, q);
    EXPECT_FLOAT_EQ(3, position.GetTime()); EXPECT_FLOAT_EQ(7, rotation.GetTime());
    EXPECT_EQ(&position.Pos, &position.GetPosition()); EXPECT_EQ(&rotation.Rot, &rotation.GetRotation());
    Vec(position.GetPosition(), p); Quat(rotation.GetRotation(), q);
    p.Set(-9, 8, 7); q = VxQuaternion(1, 0, 0, 0);
    position.SetPosition(p); rotation.SetRotation(q); Vec(position.Pos, p); Quat(rotation.Rot, q);
    EXPECT_FLOAT_EQ(3, position.TimeStep); EXPECT_FLOAT_EQ(7, rotation.TimeStep);
}

TEST_F(ControllerContractsTest, DefaultLinearKeyInitializationHasExplicitPortableValues) {
    CKPositionKey position; CKRotationKey rotation;
    EXPECT_FLOAT_EQ(0, position.TimeStep); Vec(position.Pos, VxVector(0, 0, 0));
    EXPECT_FLOAT_EQ(0, rotation.TimeStep); Quat(rotation.Rot, VxQuaternion(0, 0, 0, 1));
}

TEST_F(ControllerContractsTest, PositionComparisonUsesInclusiveThresholdForEveryStoredComponent) {
    VxVector p(1, 2, 3), q(1.25f, 2.25f, 3.25f); CKPositionKey a(4, p), b(4.25f, q);
    EXPECT_TRUE(a.Compare(b, .25f)); EXPECT_TRUE(b.Compare(a, .25f));
    EXPECT_FALSE(a.Compare(b, std::nextafter(.25f, 0.0f)));
    float *fields[] = {&b.TimeStep, &b.Pos.x, &b.Pos.y, &b.Pos.z};
    for (float *f : fields) { const float saved = *f; *f += .25f; EXPECT_FALSE(a.Compare(b, .25f)); *f = saved; }
}

TEST_F(ControllerContractsTest, RotationComparisonChecksRawSignsAndAllComponentsInclusively) {
    VxQuaternion p(1, 2, 3, 4), q(1.25f, 2.25f, 3.25f, 4.25f); CKRotationKey a(4, p), b(4.25f, q);
    EXPECT_TRUE(a.Compare(b, .25f)); EXPECT_TRUE(b.Compare(a, .25f));
    EXPECT_FALSE(a.Compare(b, std::nextafter(.25f, 0.0f)));
    float *fields[] = {&b.TimeStep, &b.Rot.x, &b.Rot.y, &b.Rot.z, &b.Rot.w};
    for (float *f : fields) { const float saved = *f; *f += .25f; EXPECT_FALSE(a.Compare(b, .25f)); *f = saved; }
    b = a; b.Rot = -a.Rot; EXPECT_FALSE(a.Compare(b, 0));
}

TEST_F(ControllerContractsTest, TCBConstructorsStoreAllFiveCurveParametersInDeclaredOrder) {
    VxVector p(2, 4, 6); VxQuaternion q(0, 0, 1, 0);
    CKTCBPositionKey a(3, p, .1f, .2f, .3f, .4f, .5f);
    CKTCBRotationKey b(7, q, -.1f, -.2f, -.3f, -.4f, -.5f);
    Vec(a.Pos, p); Quat(b.Rot, q); EXPECT_FLOAT_EQ(3, a.TimeStep); EXPECT_FLOAT_EQ(7, b.TimeStep);
    const float x[] = {a.tension,a.continuity,a.bias,a.easeto,a.easefrom};
    const float y[] = {b.tension,b.continuity,b.bias,b.easeto,b.easefrom};
    for (int i = 0; i < 5; ++i) { EXPECT_FLOAT_EQ((i+1)*.1f, x[i]); EXPECT_FLOAT_EQ(-(i+1)*.1f, y[i]); }
}

TEST_F(ControllerContractsTest, DefaultTCBKeysAcceptCallerInitializedCurveParameters) {
    CKTCBPositionKey a; CKTCBRotationKey b;
    a.tension=0; a.continuity=0; a.bias=0; a.easeto=0; a.easefrom=0;
    b.tension=0; b.continuity=0; b.bias=0; b.easeto=0; b.easefrom=0;
    VxVector p(1,2,3); VxQuaternion q(0,1,0,0); a.SetPosition(p); b.SetRotation(q);
    CKTCBPositionKey expectedP(0,p); CKTCBRotationKey expectedR(0,q);
    EXPECT_TRUE(a.Compare(expectedP,0)); EXPECT_TRUE(b.Compare(expectedR,0));
}

TEST_F(ControllerContractsTest, TCBComparisonsRequireExactCurveParametersDespiteLargeThreshold) {
    VxVector p(2,4,6); VxQuaternion q(0,0,1,0);
    CKTCBPositionKey a(3,p,.1f,.2f,.3f,.4f,.5f), b=a;
    CKTCBRotationKey c(3,q,.1f,.2f,.3f,.4f,.5f), d=c;
    float *left[] = {&b.tension,&b.continuity,&b.bias,&b.easeto,&b.easefrom};
    float *right[] = {&d.tension,&d.continuity,&d.bias,&d.easeto,&d.easefrom};
    for (int i=0;i<5;++i) { const float x=*left[i],y=*right[i]; *left[i]=std::nextafter(x,1.0f); *right[i]=std::nextafter(y,1.0f);
        EXPECT_FALSE(a.Compare(b,100)); EXPECT_FALSE(c.Compare(d,100)); *left[i]=x; *right[i]=y; }
    b.TimeStep+=.25f; d.Rot.w+=.25f; EXPECT_TRUE(a.Compare(b,.25f)); EXPECT_TRUE(c.Compare(d,.25f));
}

TEST_F(ControllerContractsTest, BezierFlagHalvesRetainIndependentModesAndFriendInequality) {
    CKBezierKeyFlags flags, same; EXPECT_EQ(0, flags.GetInTangentMode()); EXPECT_EQ(0, flags.GetOutTangentMode());
    EXPECT_FALSE(flags != same); flags.SetInTangentMode(BEZIER_KEY_FAST); flags.SetOutTangentMode(BEZIER_KEY_TANGENTS);
    EXPECT_EQ(BEZIER_KEY_FAST, flags.GetInTangentMode()); EXPECT_EQ(BEZIER_KEY_TANGENTS, flags.GetOutTangentMode());
    EXPECT_TRUE(flags != same); same.SetInTangentMode(BEZIER_KEY_FAST); same.SetOutTangentMode(BEZIER_KEY_TANGENTS); EXPECT_FALSE(flags != same);
    flags.SetInTangentMode(BEZIER_KEY_SLOW); EXPECT_EQ(BEZIER_KEY_TANGENTS, flags.GetOutTangentMode());
    flags.SetOutTangentMode(BEZIER_KEY_STEP); EXPECT_EQ(BEZIER_KEY_SLOW, flags.GetInTangentMode());
}

TEST_F(ControllerContractsTest, BezierFlagMasksPreserveEveryBitInTheOppositeHalf) {
    CKBezierKeyFlags flags;
    flags.SetInTangentMode(static_cast<CKBEZIERKEY_FLAGS>(0x7fff)); flags.SetOutTangentMode(static_cast<CKBEZIERKEY_FLAGS>(0x1234));
    EXPECT_EQ(0x7fff, flags.GetInTangentMode()); EXPECT_EQ(0x1234, flags.GetOutTangentMode());
    flags.SetInTangentMode(static_cast<CKBEZIERKEY_FLAGS>(0x12340020));
    EXPECT_EQ(BEZIER_KEY_TANGENTS, flags.GetInTangentMode()); EXPECT_EQ(0x1234, flags.GetOutTangentMode());
}

TEST_F(ControllerContractsTest, BezierConstructorsPreservePayloadAndSetTheSpecifiedModes) {
    VxVector p(2,4,6), in(1,3,5), out(7,9,11); CKBezierKeyFlags flags;
    flags.SetInTangentMode(BEZIER_KEY_STEP); flags.SetOutTangentMode(BEZIER_KEY_LINEAR);
    CKBezierPositionKey zero, all(3,p,flags,in,out), modes(7,p,flags), explicitTangents(9,p,in,out);
    EXPECT_EQ(BEZIER_KEY_AUTOSMOOTH,zero.Flags.GetInTangentMode()); EXPECT_EQ(BEZIER_KEY_AUTOSMOOTH,zero.Flags.GetOutTangentMode());
    Vec(all.Pos,p); Vec(all.In,in); Vec(all.Out,out); EXPECT_FLOAT_EQ(3,all.TimeStep); EXPECT_FALSE(all.Flags!=flags);
    EXPECT_FLOAT_EQ(7,modes.TimeStep); Vec(modes.Pos,p); EXPECT_FALSE(modes.Flags!=flags);
    EXPECT_EQ(BEZIER_KEY_TANGENTS,explicitTangents.Flags.GetInTangentMode()); EXPECT_EQ(BEZIER_KEY_TANGENTS,explicitTangents.Flags.GetOutTangentMode());
    Vec(explicitTangents.In,in); Vec(explicitTangents.Out,out); EXPECT_EQ(&all.Flags,&all.GetFlags());
    all.GetFlags().SetInTangentMode(BEZIER_KEY_FAST); EXPECT_EQ(BEZIER_KEY_FAST,all.Flags.GetInTangentMode());
}

TEST_F(ControllerContractsTest, BezierComparisonOnlyUsesExactTangentsForExplicitModes) {
    VxVector p(2,4,6), in(1,3,5), out(7,9,11); CKBezierPositionKey a(3,p,in,out), b=a;
    b.In.x+=.0001f; EXPECT_FALSE(a.Compare(b,100)); b=a; b.Out.z+=.0001f; EXPECT_FALSE(a.Compare(b,100));
    a.Flags.SetInTangentMode(BEZIER_KEY_LINEAR); a.Flags.SetOutTangentMode(BEZIER_KEY_STEP); b=a;
    b.In.Set(99,88,77); b.Out.Set(-99,-88,-77); EXPECT_TRUE(a.Compare(b,0));
    b.Flags.SetOutTangentMode(BEZIER_KEY_SLOW); EXPECT_FALSE(a.Compare(b,100));
    b=a; b.Pos.x+=.25f; EXPECT_TRUE(a.Compare(b,.25f)); EXPECT_FALSE(a.Compare(b,std::nextafter(.25f,0.0f)));
}

TEST_F(ControllerContractsTest, MorphKeyPresenceMustBeSymmetricIncludingZeroVertexKeys) {
    VxVector pos(2,4,6); VxCompressedVector normal; normal.xa=7; normal.ya=-9;
    for (int count=0; count<2; ++count) {
        CKMorphKey empty, populated; empty.TimeStep=populated.TimeStep=3;
        empty.PosArray=nullptr; empty.NormArray=nullptr; populated.PosArray=&pos; populated.NormArray=nullptr;
        EXPECT_FALSE(empty.Compare(populated,count,0)); EXPECT_FALSE(populated.Compare(empty,count,0));
        populated.PosArray=nullptr; populated.NormArray=&normal;
        EXPECT_FALSE(empty.Compare(populated,count,0)); EXPECT_FALSE(populated.Compare(empty,count,0));
    }
}

TEST_F(ControllerContractsTest, MorphKeyTimeUsesThresholdButPayloadAndEncodedNormalsAreExact) {
    VxVector aPos[2]={VxVector(1,2,3),VxVector(4,5,6)}, bPos[2]={aPos[0],aPos[1]};
    VxCompressedVector aNorm[2], bNorm[2]; aNorm[0].xa=10;aNorm[0].ya=20;aNorm[1].xa=30;aNorm[1].ya=40;
    std::memcpy(bNorm,aNorm,sizeof(aNorm)); CKMorphKey a,b; a.TimeStep=3;b.TimeStep=3.25f;
    a.PosArray=aPos;b.PosArray=bPos;a.NormArray=aNorm;b.NormArray=bNorm;
    EXPECT_TRUE(a.Compare(b,2,.25f)); EXPECT_TRUE(b.Compare(a,2,.25f)); EXPECT_FALSE(a.Compare(b,2,std::nextafter(.25f,0.0f)));
    bPos[1].z+=.001f; EXPECT_FALSE(a.Compare(b,2,100)); bPos[1]=aPos[1];
    ++bNorm[1].xa; EXPECT_FALSE(a.Compare(b,2,100)); --bNorm[1].xa;
    ++bNorm[1].ya; EXPECT_FALSE(a.Compare(b,2,100)); --bNorm[1].ya;
    EXPECT_TRUE(a.Compare(b,2,100));
}

TEST_F(ControllerContractsTest, BaseCloneCopiesCountAndLengthWithoutChangingTypeOrOwnedKeys) {
    RCKLinearPositionController source; RCKLinearScaleController destination;
    VxVector a(1,2,3),b(7,8,9); CKPositionKey ka(3,a); CKScaleKey kb(3,b);
    ASSERT_EQ(0,source.AddKey(&ka));ASSERT_EQ(0,destination.AddKey(&kb)); source.SetLength(19);destination.SetLength(7);
    CKKey *owned=destination.GetKey(0); ASSERT_TRUE(destination.CKAnimController::Clone(&source));
    EXPECT_EQ(CKANIMATION_LINSCL_CONTROL,destination.GetType());EXPECT_EQ(owned,destination.GetKey(0));
    EXPECT_EQ(1,destination.GetKeyCount());EXPECT_FLOAT_EQ(19,destination.GetLength());
    VxVector result;ASSERT_TRUE(destination.Evaluate(3,&result));Vec(result,b);
}

TEST_F(ControllerContractsTest, BaseMetadataCanBeSetBeforeConsumersReadLength) {
    RCKLinearPositionController concrete; CKAnimController &base=concrete;
    EXPECT_EQ(0,base.GetKeyCount());EXPECT_EQ(CKANIMATION_LINPOS_CONTROL,base.GetType());
    base.SetLength(37.25f);EXPECT_FLOAT_EQ(37.25f,base.GetLength());EXPECT_FLOAT_EQ(37.25f,base.m_Length);
}

TEST_F(ControllerContractsTest, MorphBaseForwarderRequestsNormalsAndFullEvaluateUsesCallerStride) {
    RCKMorphController concrete;CKMorphController &base=concrete;base.SetMorphVertexCount(1);base.SetLength(7);
    VxVector pos(2,4,6);VxCompressedVector normal;normal.xa=70;normal.ya=-90;
    CKMorphKey key;key.TimeStep=3;key.PosArray=&pos;key.NormArray=&normal;
    ASSERT_EQ(0,base.CKMorphController::AddKey(&key));
    CKMorphKey *stored=static_cast<CKMorphKey *>(base.GetKey(0));ASSERT_NE(nullptr,stored);ASSERT_NE(nullptr,stored->NormArray);
    struct Vertex { VxVector Pos; int Guard; } output;output.Guard=123;VxCompressedVector outNorm;
    ASSERT_TRUE(base.Evaluate(3,1,&output.Pos,sizeof(output),&outNorm));Vec(output.Pos,pos);EXPECT_EQ(123,output.Guard);
    EXPECT_EQ(70,outNorm.xa);EXPECT_EQ(-90,outNorm.ya);VxVector untouched(8,9,10);EXPECT_FALSE(base.Evaluate(3,&untouched));Vec(untouched,VxVector(8,9,10));
    base.RemoveKey(0);EXPECT_EQ(0,base.GetKeyCount());
}

TEST_F(ControllerContractsTest, KeyframeFactoryCreatesEveryConcreteTypeAndDoesNotCommitSlots) {
    const CKANIMATION_CONTROLLER types[]={CKANIMATION_LINPOS_CONTROL,CKANIMATION_LINROT_CONTROL,CKANIMATION_LINSCL_CONTROL,CKANIMATION_LINSCLAXIS_CONTROL,
        CKANIMATION_TCBPOS_CONTROL,CKANIMATION_TCBROT_CONTROL,CKANIMATION_TCBSCL_CONTROL,CKANIMATION_TCBSCLAXIS_CONTROL,
        CKANIMATION_BEZIERPOS_CONTROL,CKANIMATION_BEZIERSCL_CONTROL,CKANIMATION_MORPH_CONTROL};
    CKKeyframeData owner;owner.m_Length=43;
    for (int i=0;i<11;++i) {
        SCOPED_TRACE(i);CKAnimController *controller=owner.CreateController(types[i]);ASSERT_NE(nullptr,controller);
        EXPECT_EQ(types[i],controller->GetType());EXPECT_EQ(0,controller->GetKeyCount());EXPECT_FLOAT_EQ(43,controller->GetLength());
        EXPECT_EQ(nullptr,owner.m_PositionController);EXPECT_EQ(nullptr,owner.m_ScaleController);EXPECT_EQ(nullptr,owner.m_RotationController);
        EXPECT_EQ(nullptr,owner.m_ScaleAxisController);EXPECT_EQ(nullptr,owner.m_MorphController);delete controller;
    }
}

TEST_F(ControllerContractsTest, FactoryBaseAliasesAndUnknownFullTypesFollowTheNativeAcceptedSet) {
    const CKANIMATION_CONTROLLER aliases[]={CKANIMATION_CONTROLLER_POS,CKANIMATION_CONTROLLER_ROT,CKANIMATION_CONTROLLER_SCL,CKANIMATION_CONTROLLER_MORPH};
    const CKANIMATION_CONTROLLER full[]={CKANIMATION_LINPOS_CONTROL,CKANIMATION_LINROT_CONTROL,CKANIMATION_LINSCL_CONTROL,CKANIMATION_MORPH_CONTROL};
    CKKeyframeData owner;
    for(int i=0;i<4;++i){CKAnimController *c=owner.CreateController(aliases[i]);ASSERT_NE(nullptr,c);EXPECT_EQ(full[i],c->GetType());delete c;}
    EXPECT_EQ(nullptr,owner.CreateController(CKANIMATION_CONTROLLER_SCLAXIS));
    EXPECT_EQ(nullptr,owner.CreateController(static_cast<CKANIMATION_CONTROLLER>(0x12345601)));
    EXPECT_EQ(nullptr,owner.CreateController(static_cast<CKANIMATION_CONTROLLER>(0)));
}

TEST_F(ControllerContractsTest, EveryFactoryTypeComposesTheRealBaseVirtualContract) {
    const CKANIMATION_CONTROLLER types[]={CKANIMATION_LINPOS_CONTROL,CKANIMATION_LINROT_CONTROL,CKANIMATION_LINSCL_CONTROL,CKANIMATION_LINSCLAXIS_CONTROL,
        CKANIMATION_TCBPOS_CONTROL,CKANIMATION_TCBROT_CONTROL,CKANIMATION_TCBSCL_CONTROL,CKANIMATION_TCBSCLAXIS_CONTROL,
        CKANIMATION_BEZIERPOS_CONTROL,CKANIMATION_BEZIERSCL_CONTROL,CKANIMATION_MORPH_CONTROL};
    VxVector position(2,4,6), tangent(0,0,0);VxQuaternion rotation(0,1,0,0);VxCompressedVector normal;normal.xa=70;normal.ya=-90;
    CKPositionKey linear(3,position);CKRotationKey linearRot(3,rotation);CKTCBPositionKey tcb(3,position);CKTCBRotationKey tcbRot(3,rotation);
    CKBezierPositionKey bezier(3,position,tangent,tangent);CKMorphKey morph;morph.TimeStep=3;morph.PosArray=&position;morph.NormArray=&normal;
    CKKey *keys[]={&linear,&linearRot,&linear,&linearRot,&tcb,&tcbRot,&tcb,&tcbRot,&bezier,&bezier,&morph};
    const int keyBytes[]={16,20,16,20,36,40,36,40,44,44,0};CKKeyframeData owner;
    for(int i=0;i<11;++i) {
        SCOPED_TRACE(i);CKAnimController *api=owner.CreateController(types[i]);CKAnimController *copy=owner.CreateController(types[i]);
        ASSERT_NE(nullptr,api);ASSERT_NE(nullptr,copy);if(i==10)static_cast<CKMorphController *>(api)->SetMorphVertexCount(1);
        ASSERT_EQ(0,api->AddKey(keys[i]));EXPECT_EQ(1,api->GetKeyCount());ASSERT_NE(nullptr,api->GetKey(0));EXPECT_FLOAT_EQ(3,api->GetKey(0)->GetTime());
        unsigned char wire[64]={};int bytes=0;
        if(i==10) {
            const int header[]={1,1,1};std::memcpy(wire,header,12);std::memcpy(wire+12,&morph.TimeStep,4);
            std::memcpy(wire+16,&position,12);std::memcpy(wire+28,&normal,4);bytes=32;
        } else {
            const int count=1;std::memcpy(wire,&count,4);std::memcpy(wire+4,keys[i],keyBytes[i]);bytes=4+keyBytes[i];
        }
        EXPECT_EQ(bytes,api->DumpKeysTo(nullptr));unsigned char actual[68];std::memset(actual,0xa5,sizeof(actual));
        ASSERT_EQ(bytes,api->DumpKeysTo(actual));EXPECT_EQ(0,std::memcmp(wire,actual,bytes));EXPECT_EQ(0xa5,actual[bytes]);
        ASSERT_TRUE(copy->Clone(api));EXPECT_TRUE(api->Compare(copy,0));EXPECT_TRUE(copy->Compare(api,0));
        api->RemoveKey(0);EXPECT_EQ(0,api->GetKeyCount());ASSERT_EQ(bytes,api->ReadKeysFrom(wire));
        if(i==10) {VxVector value;VxCompressedVector n;ASSERT_TRUE(static_cast<CKMorphController *>(api)->Evaluate(3,1,&value,sizeof(value),&n));Vec(value,position);EXPECT_EQ(70,n.xa);EXPECT_EQ(-90,n.ya);}
        else if((types[i]&CKANIMATION_CONTROLLER_MASK)==CKANIMATION_CONTROLLER_ROT || (types[i]&CKANIMATION_CONTROLLER_MASK)==CKANIMATION_CONTROLLER_SCLAXIS) {VxQuaternion value;ASSERT_TRUE(api->Evaluate(3,&value));Quat(value,rotation);}
        else {VxVector value;ASSERT_TRUE(api->Evaluate(3,&value));Vec(value,position);}
        delete api;delete copy;
    }
}

TEST_F(ControllerContractsTest, FactoryAllocationFailureLeavesAllOwnedStateUntouched) {
    CKKeyframeData owner;owner.m_PositionController=new RCKLinearPositionController;owner.m_Length=51;
    VxVector pos(2,4,6);CKPositionKey key(3,pos);ASSERT_EQ(0,owner.m_PositionController->AddKey(&key));
    CKAnimController *before=owner.m_PositionController;CKKey *borrowed=before->GetKey(0);
    FailNextFactoryAllocation=true;EXPECT_EQ(nullptr,owner.CreateController(CKANIMATION_TCBPOS_CONTROL));EXPECT_FALSE(FailNextFactoryAllocation);
    EXPECT_EQ(before,owner.m_PositionController);EXPECT_EQ(borrowed,before->GetKey(0));EXPECT_FLOAT_EQ(51,owner.m_Length);
    VxVector output;ASSERT_TRUE(before->Evaluate(3,&output));Vec(output,pos);
}

TEST_F(ControllerContractsTest, KeyframeDefaultsAndOwnedFiveCategoryDestructionAreObservable) {
    for(int i=0;i<5;++i)DeletedCategories[i]=0;
    { CKKeyframeData owner;EXPECT_FLOAT_EQ(100,owner.m_Length);EXPECT_EQ(1,owner.m_RefCount);EXPECT_EQ(nullptr,owner.m_ObjectAnimation);
      EXPECT_EQ(nullptr,owner.m_PositionController);EXPECT_EQ(nullptr,owner.m_ScaleController);EXPECT_EQ(nullptr,owner.m_RotationController);
      EXPECT_EQ(nullptr,owner.m_ScaleAxisController);EXPECT_EQ(nullptr,owner.m_MorphController);
      owner.m_PositionController=new DeletedPosition;owner.m_ScaleController=new DeletedScale;owner.m_RotationController=new DeletedRotation;
      owner.m_ScaleAxisController=new DeletedAxis;owner.m_MorphController=new DeletedMorph; }
    for(int i=0;i<5;++i)EXPECT_EQ(1,DeletedCategories[i]);
}

TEST_F(ControllerContractsTest, AllPublicKeyLayoutsReportActualFieldsWithoutReadingUninitializedValues) {
    CKKey key;CKPositionKey pos;CKRotationKey rot;CKTCBPositionKey tcbPos;CKTCBRotationKey tcbRot;FlagsLayout flags;CKBezierPositionKey bez;CKMorphKey morph;
    Layout("CKKey",key,4,4);Field("CKKey_TimeStep",key,key.TimeStep,0);
    Layout("CKPositionKey",pos,16,4);Field("CKPositionKey_Pos",pos,pos.Pos,4);
    Layout("CKRotationKey",rot,20,4);Field("CKRotationKey_Rot",rot,rot.Rot,4);
    Layout("CKTCBPositionKey",tcbPos,36,4);Layout("CKTCBRotationKey",tcbRot,40,4);
#define FIELD_TCB(value,name,offset) Field(#value "_" #name,value,value.name,offset)
    FIELD_TCB(tcbPos,tension,16);FIELD_TCB(tcbPos,continuity,20);FIELD_TCB(tcbPos,bias,24);FIELD_TCB(tcbPos,easeto,28);FIELD_TCB(tcbPos,easefrom,32);
    FIELD_TCB(tcbRot,tension,20);FIELD_TCB(tcbRot,continuity,24);FIELD_TCB(tcbRot,bias,28);FIELD_TCB(tcbRot,easeto,32);FIELD_TCB(tcbRot,easefrom,36);
#undef FIELD_TCB
    Layout("CKBezierKeyFlags",flags,4,4);EXPECT_EQ(0,flags.Offset());RecordProperty("CKBezierKeyFlags_m_Flags_offset",static_cast<int>(flags.Offset()));RecordProperty("CKBezierKeyFlags_m_Flags_size",4);
    Layout("CKBezierPositionKey",bez,44,4);Field("CKBezierPositionKey_Flags",bez,bez.Flags,16);Field("CKBezierPositionKey_In",bez,bez.In,20);Field("CKBezierPositionKey_Out",bez,bez.Out,32);
    Layout("CKMorphKey",morph,sizeof(void*)==8?24:12,sizeof(void*));Field("CKMorphKey_PosArray",morph,morph.PosArray,sizeof(void*)==8?8:4);Field("CKMorphKey_NormArray",morph,morph.NormArray,sizeof(void*)==8?16:8);
}

TEST_F(ControllerContractsTest, ControllerAndOwnerLayoutsExposeEveryPublicField) {
    RCKLinearPositionController base;RCKMorphController morph;CKKeyframeData owner;
    RecordProperty("CKAnimController_size",static_cast<int>(sizeof(CKAnimController)));RecordProperty("CKMorphController_size",static_cast<int>(sizeof(CKMorphController)));
    RecordProperty("CKAnimController_alignment",static_cast<int>(alignof(CKAnimController)));RecordProperty("CKMorphController_alignment",static_cast<int>(alignof(CKMorphController)));
    EXPECT_EQ(sizeof(void*)==8?24u:16u,sizeof(CKAnimController));EXPECT_EQ(sizeof(CKAnimController),sizeof(CKMorphController));
    Field("CKAnimController_m_Type",base,base.m_Type,sizeof(void*));Field("CKAnimController_m_NbKeys",base,base.m_NbKeys,sizeof(void*)+4);Field("CKAnimController_m_Length",base,base.m_Length,sizeof(void*)+8);
    Layout("CKKeyframeData",owner,sizeof(void*)==8?56:32,sizeof(void*));
    Field("CKKeyframeData_m_PositionController",owner,owner.m_PositionController,0);Field("CKKeyframeData_m_ScaleController",owner,owner.m_ScaleController,sizeof(void*));
    Field("CKKeyframeData_m_RotationController",owner,owner.m_RotationController,sizeof(void*)*2);Field("CKKeyframeData_m_ScaleAxisController",owner,owner.m_ScaleAxisController,sizeof(void*)*3);
    Field("CKKeyframeData_m_MorphController",owner,owner.m_MorphController,sizeof(void*)*4);Field("CKKeyframeData_m_Length",owner,owner.m_Length,sizeof(void*)*5);
    Field("CKKeyframeData_m_RefCount",owner,owner.m_RefCount,sizeof(void*)*5+4);Field("CKKeyframeData_m_ObjectAnimation",owner,owner.m_ObjectAnimation,sizeof(void*)==8?48:28);
}

TEST_F(ControllerContractsTest, PublicReplacementAndSharedOwnerDeathKeepThePreparedControllerUsable) {
    ASSERT_EQ(CK_OK,CKStartUp());CKCLASSREGISTERCID(RCKRenderObject,CKCID_BEOBJECT);CKCLASSREGISTERCID(RCK3dEntity,CKCID_RENDEROBJECT);
    CKCLASSREGISTERCID(RCKObjectAnimation,CKCID_ANIMATION);CKBuildClassHierarchyTable();CKContext *context=nullptr;ASSERT_EQ(CK_OK,CKCreateContext(&context,nullptr,0,0));
    { RCKObjectAnimation *first=new RCKObjectAnimation(context);RCKObjectAnimation second(context);first->SetLength(17);
      CKAnimController *controller=first->CreateController(CKANIMATION_LINPOS_CONTROL);ASSERT_NE(nullptr,controller);
      VxVector value(2,4,6);CKPositionKey key(3,value);ASSERT_EQ(0,controller->AddKey(&key));
      FailNextFactoryAllocation=true;EXPECT_EQ(nullptr,first->CreateController(CKANIMATION_TCBPOS_CONTROL));EXPECT_FALSE(FailNextFactoryAllocation);
      EXPECT_EQ(controller,first->GetPositionController());EXPECT_FLOAT_EQ(17,controller->GetLength());
      ASSERT_TRUE(second.ShareDataFrom(reinterpret_cast<CKObjectAnimation *>(first)));delete first;
      ASSERT_EQ(controller,second.GetPositionController());VxVector output;ASSERT_TRUE(second.EvaluatePosition(3,output));Vec(output,value);
      EXPECT_TRUE(second.DeleteController(CKANIMATION_CONTROLLER_POS));EXPECT_EQ(nullptr,second.GetPositionController()); }
    CKCloseContext(context);CKShutdown();
}

TEST_F(ControllerContractsTest, IndexedBezierHookCanRemoveKeysAndPublishEmptyOrRemainingState) {
    RemoveFromIndexedHook<RCKBezierPositionController>(false);
    RemoveFromIndexedHook<RCKBezierScaleController>(false);
    RemoveFromIndexedHook<RCKBezierPositionController>(true);
    RemoveFromIndexedHook<RCKBezierScaleController>(true);
}

TEST_F(ControllerContractsTest, IndexedBezierHookCanAppendAndChangeLengthBeforeLaterWrapTangents) {
    AppendFromIndexedHook<RCKBezierPositionController>();
    AppendFromIndexedHook<RCKBezierScaleController>();
}
