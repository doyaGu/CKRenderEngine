#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK3dObject.h"
#include "RCKBodyPart.h"
#include "RCKCharacter.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();
namespace {
CK_CLASSID NaturalDistinctDerivedID = 0;
class NaturalDistinctDerived : public RCKKeyedAnimation {
public:
    explicit NaturalDistinctDerived(CKContext *context) : RCKKeyedAnimation(context) {}
    CK_CLASSID GetClassID() override { return NaturalDistinctDerivedID; }
};
class NaturalDistinctCharacter : public RCKCharacter {
public:
    explicit NaturalDistinctCharacter(CKContext *context) : RCKCharacter(context) {}
    const CKSecondaryAnimation &Slot() const { return m_SecondaryAnimations[0]; }
    void Tick(float milliseconds) {
        SetMoveableFlags(GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        ProcessAnimation(milliseconds);
    }
};
static CK3dEntity *DistinctEntity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
class NaturalDistinctRig {
public:
    NaturalDistinctCharacter Character;
    RCKBodyPart Root, Limb;
    RCKObjectAnimation SourceRoot, SourceLimb, TargetRoot, TargetLimb, DerivedSourceRoot, DerivedSourceLimb,
        DerivedTargetRoot, DerivedTargetLimb, SecondaryLimb;
    RCKKeyedAnimation SourceExact, TargetExact, Secondary;
    NaturalDistinctDerived SourceDerived, TargetDerived;
    int Types;
    explicit NaturalDistinctRig(CKContext *context, int types)
        : Character(context), Root(context), Limb(context), SourceRoot(context), SourceLimb(context),
          TargetRoot(context), TargetLimb(context), DerivedSourceRoot(context), DerivedSourceLimb(context),
          DerivedTargetRoot(context), DerivedTargetLimb(context), SecondaryLimb(context), SourceExact(context),
          TargetExact(context), Secondary(context), SourceDerived(context), TargetDerived(context), Types(types) {
        Character.SetMoveableFlags((Character.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED) | VX_MOVEABLE_CHARACTERRENDERED);
        Root.SetMoveableFlags(Root.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Limb.SetMoveableFlags(Limb.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        Character.SetAnimationLevelOfDetail(1);
        VxMatrix world; world.SetIdentity(); world[3][0] = 100; Character.SetWorldMatrix(world);
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Root)));
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, DistinctEntity(Character));
        EXPECT_TRUE(Limb.SetParent(DistinctEntity(Root), TRUE));
        EXPECT_EQ(CK_OK, Character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&Limb)));
        Curve(SourceExact, SourceRoot, Root, 10, 0, 10); Curve(SourceExact, SourceLimb, Limb, 10, 10, 20);
        Curve(SourceDerived, DerivedSourceRoot, Root, 10, 0, 10); Curve(SourceDerived, DerivedSourceLimb, Limb, 10, 10, 20);
        Curve(TargetExact, TargetRoot, Root, 20, 50, 90); Curve(TargetExact, TargetLimb, Limb, 20, 200, 260);
        Curve(TargetDerived, DerivedTargetRoot, Root, 20, 50, 90); Curve(TargetDerived, DerivedTargetLimb, Limb, 20, 200, 260);
        Curve(Secondary, SecondaryLimb, Limb, 10, 100, 200);
        SourceExact.SetLength(10); SourceDerived.SetLength(10); TargetExact.SetLength(20); TargetDerived.SetLength(20);
        Secondary.SetLength(10);
        EXPECT_EQ(CK_OK, Character.AddAnimation(&SourceExact)); EXPECT_EQ(CK_OK, Character.AddAnimation(&SourceDerived));
        EXPECT_EQ(CK_OK, Character.AddAnimation(&TargetExact)); EXPECT_EQ(CK_OK, Character.AddAnimation(&TargetDerived));
        EXPECT_EQ(CK_OK, Character.AddAnimation(&Secondary));
        Source().LinkToFrameRate(TRUE, 0); Source().SetCanBeInterrupt(FALSE); Target().SetCanBeInterrupt(FALSE);
        Secondary.LinkToFrameRate(TRUE, 1024); Source().SetFrame(0);
        EXPECT_EQ(CK_OK, Character.SetActiveAnimation(&Source()));
    }
    ~NaturalDistinctRig() {
        Character.FlushSecondaryAnimations(); Limb.SetParent(nullptr, TRUE); Root.SetParent(nullptr, TRUE);
    }
    RCKKeyedAnimation &Source() { return Types == 2 ? static_cast<RCKKeyedAnimation &>(SourceDerived) : SourceExact; }
    RCKKeyedAnimation &Target() { return Types == 1 ? static_cast<RCKKeyedAnimation &>(TargetDerived) : TargetExact; }
    RCKObjectAnimation &SourceChannel() { return Types == 2 ? DerivedSourceRoot : SourceRoot; }
    RCKObjectAnimation &TargetChannel() { return Types == 1 ? DerivedTargetRoot : TargetRoot; }
private:
    void Curve(RCKKeyedAnimation &group, RCKObjectAnimation &channel, RCKBodyPart &part,
               float length, float first, float last) {
        channel.Set3dEntity(DistinctEntity(part)); VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(length, &b); EXPECT_EQ(CK_OK, group.AddAnimation(&channel));
    }
};
class CharacterSecondaryNaturalDistinctTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK3dObject, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKBodyPart, CKCID_3DOBJECT);
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_SCENEOBJECT);
        NaturalDistinctDerivedID = CKClassGetNewIdentifier();
        CKClassRegister(NaturalDistinctDerivedID, CKCID_KEYEDANIMATION, nullptr, nullptr, nullptr, nullptr, nullptr);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Pose(NaturalDistinctRig &rig, float world, float limb) {
        const VxMatrix *matrices[] = {&rig.Character.GetWorldMatrix(), &rig.Character.GetLocalMatrix(),
            &rig.Root.GetWorldMatrix(), &rig.Root.GetLocalMatrix(), &rig.Limb.GetWorldMatrix(), &rig.Limb.GetLocalMatrix()};
        const float translations[] = {world, world, world, 0, world + limb, limb};
        for (int which = 0; which < 6; ++which)
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 4; ++column) {
                    float expected = row == column ? 1.0f : 0.0f;
                    if (row == 3 && column == 0) expected = translations[which];
                    EXPECT_NEAR(expected, (*matrices[which])[row][column], .0006f)
                        << "matrix " << which << " element " << row << ',' << column;
                }
        EXPECT_EQ(DistinctEntity(rig.Character), rig.Root.GetParent()); EXPECT_EQ(DistinctEntity(rig.Root), rig.Limb.GetParent());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Root.GetCharacter());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Limb.GetCharacter());
        EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation());
    }
    static void State(NaturalDistinctRig &rig, CKAnimation *active, CKAnimation *queue,
                      float sourceFrame, float targetFrame, float sourceCenter, float targetCenter) {
        EXPECT_EQ(active, rig.Character.GetActiveAnimation()); EXPECT_EQ(queue, rig.Character.GetNextActiveAnimation());
        EXPECT_NEAR(sourceFrame, rig.Source().GetFrame(), .0003f); EXPECT_NEAR(targetFrame, rig.Target().GetFrame(), .0003f);
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Source().GetCharacter());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Target().GetCharacter());
        EXPECT_EQ(&rig.SourceChannel(), rig.Source().GetRootAnimationInternal());
        EXPECT_EQ(&rig.TargetChannel(), rig.Target().GetRootAnimationInternal());
        EXPECT_NEAR(sourceCenter, rig.Source().GetRootVectorInternal().x, .0003f);
        EXPECT_NEAR(targetCenter, rig.Target().GetRootVectorInternal().x, .0003f);
        EXPECT_FLOAT_EQ(0, rig.Source().GetRootVectorInternal().y); EXPECT_FLOAT_EQ(0, rig.Source().GetRootVectorInternal().z);
        EXPECT_FLOAT_EQ(0, rig.Target().GetRootVectorInternal().y); EXPECT_FLOAT_EQ(0, rig.Target().GetRootVectorInternal().z);
        CKAnimation *destination = nullptr; float entry = -1;
        rig.Character.GetWarperParameters(nullptr, nullptr, nullptr, &destination, &entry);
        EXPECT_EQ(queue, destination); EXPECT_FLOAT_EQ(0, entry);
    }
    static void Prepare(NaturalDistinctRig &rig, float warm, float sourceFPS, float targetFPS,
                        bool queued = true, bool reverseSecondary = true, float requested = 2) {
        const float duration = requested < 1 ? 1 : requested;
        const CK_SECONDARYANIMATION_FLAGS flags = static_cast<CK_SECONDARYANIMATION_FLAGS>(
            CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_DOWARP);
        ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, reverseSecondary ? 0 : 8, flags, requested));
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        RCKKeyedAnimation *starting = rig.Character.Slot().Animation; ASSERT_NE(nullptr, starting);
        ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_STARTINGWARP, rig.Character.Slot().Mode);
        ASSERT_EQ(duration, starting->GetNextFrame(duration * .9765625f)); rig.Character.Tick(duration * .9765625f);
        ASSERT_GT(starting->GetNextFrame(.9765625f), duration); rig.Character.Tick(.9765625f);
        ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, rig.Character.Slot().Mode);
        EXPECT_FLOAT_EQ(reverseSecondary ? 1 : 9, rig.Secondary.GetFrame());
        rig.Secondary.LinkToFrameRate(TRUE, 0); rig.Source().LinkToFrameRate(TRUE, 1024);
        ASSERT_NEAR(warm, rig.Source().GetNextFrame(warm * .9765625f), .00001f); rig.Character.Tick(warm * .9765625f);
        State(rig, &rig.Source(), nullptr, warm, 0, -warm, -50); Pose(rig, 100 + warm, reverseSecondary ? 110 : 190);
        rig.Source().LinkToFrameRate(TRUE, sourceFPS); rig.Target().LinkToFrameRate(TRUE, targetFPS);
        if (queued) {
            ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Target(), CK_TRANSITION_TOSTART));
            State(rig, &rig.Source(), &rig.Target(), warm, 0, -warm, -50);
        }
        rig.Secondary.LinkToFrameRate(TRUE, reverseSecondary ? -1024 : 1024);
        ASSERT_EQ(reverseSecondary ? -1.0f : 11.0f, rig.Secondary.GetNextFrame(1.953125f));
    }
    static RCKKeyedAnimation *Return(NaturalDistinctRig &rig, RCKKeyedAnimation *retained,
                                    float duration, float first, float last) {
        EXPECT_EQ(1, rig.Character.GetSecondaryAnimationsCount()); if (rig.Character.GetSecondaryAnimationsCount() != 1) return nullptr;
        const CKSecondaryAnimation &slot = rig.Character.Slot(); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, slot.Mode);
        RCKKeyedAnimation *warp = slot.Animation; EXPECT_EQ(retained, warp); if (!warp) return nullptr;
        EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0));
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), warp->GetCharacter());
        EXPECT_EQ(static_cast<CKAnimation *>(warp), rig.Limb.GetExclusiveAnimation());
        EXPECT_FLOAT_EQ(duration, warp->GetLength()); EXPECT_FLOAT_EQ(0, warp->GetFrame());
        EXPECT_TRUE(warp->IsLinkedToFrameRate()); EXPECT_FLOAT_EQ(1024, warp->GetLinkedFrameRate());
        EXPECT_EQ(2, warp->GetAnimationCount()); EXPECT_EQ(nullptr, warp->GetAnimation(DistinctEntity(rig.Root)));
        int detached = 0, mapped = 0;
        for (int i = 0; i < warp->GetAnimationCount(); ++i) {
            CKObjectAnimation *child = warp->GetAnimation(i); EXPECT_NE(nullptr, child); if (!child) continue;
            if (!child->Get3dEntity()) {
                ++detached; EXPECT_FALSE(child->HasPositionInfo()); EXPECT_FALSE(child->HasRotationInfo());
                EXPECT_FALSE(child->HasScaleInfo()); EXPECT_FALSE(child->HasScaleAxisInfo()); EXPECT_FALSE(child->HasMorphInfo());
            } else { ++mapped; EXPECT_EQ(DistinctEntity(rig.Limb), child->Get3dEntity()); }
        }
        EXPECT_EQ(1, detached); EXPECT_EQ(1, mapped);
        CKObjectAnimation *channel = warp->GetAnimation(DistinctEntity(rig.Limb)); EXPECT_NE(nullptr, channel);
        if (channel) {
            const float times[] = {0, duration / 2, duration}, values[] = {first, (first + last) / 2, last};
            for (int i = 0; i < 3; ++i) {
                VxVector value; EXPECT_TRUE(channel->EvaluatePosition(times[i], value)); EXPECT_NEAR(values[i], value.x, .0004f);
                EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(0, value.z);
            }
        }
        return warp;
    }
    static void TargetFinish(NaturalDistinctRig &rig, RCKKeyedAnimation &warp, float duration, float last,
                             float sourceFrame, float sourceCenter, float frame, float world,
                             float releaseFrame, float releaseWorld, bool targetActiveAtRelease, float releasedLimb) {
        ASSERT_EQ(duration, warp.GetNextFrame(duration * .9765625f)); rig.Character.Tick(duration * .9765625f);
        State(rig, &rig.Target(), nullptr, sourceFrame, frame, sourceCenter, -(50 + 2 * frame));
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount()); EXPECT_FLOAT_EQ(duration, warp.GetFrame());
        EXPECT_EQ(static_cast<CKAnimation *>(&warp), rig.Limb.GetExclusiveAnimation()); Pose(rig, world, last);
        ASSERT_EQ(duration, warp.GetNextFrame(0)); rig.Character.Tick(0);
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount()); Pose(rig, world, last);
        ASSERT_GT(warp.GetNextFrame(.9765625f), duration); rig.Character.Tick(.9765625f);
        EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation());
        State(rig, targetActiveAtRelease ? &rig.Target() : nullptr, nullptr,
            sourceFrame, releaseFrame, sourceCenter, -(50 + 2 * releaseFrame)); Pose(rig, releaseWorld, last);
        rig.Character.Tick(0); Pose(rig, releaseWorld, releasedLimb);
    }
};

TEST_F(CharacterSecondaryNaturalDistinctTest, NegativeSourcePendingPositiveTargetReturnsToFutureTargetCurve) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalDistinctRig rig(Context, types); Prepare(rig, 3, -1024, 1024);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; ASSERT_EQ(1.0f, rig.Source().GetNextFrame(1.953125f));
        rig.Character.Tick(1.953125f); State(rig, &rig.Source(), &rig.Target(), 1, 0, -1, -50); Pose(rig, 101, 100);
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 100, 203); ASSERT_NE(nullptr, warp);
        TargetFinish(rig, *warp, 2, 203, 1, 0, 1, 102, 2, 104, true, 206);
    }
}
TEST_F(CharacterSecondaryNaturalDistinctTest, NegativeSourcePendingNegativeTargetKeepsLiteralZeroEntry) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalDistinctRig rig(Context, types); Prepare(rig, 3, -1024, -1024);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        State(rig, &rig.Source(), &rig.Target(), 1, 0, -1, -50); Pose(rig, 101, 100);
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 100, 200); ASSERT_NE(nullptr, warp);
        TargetFinish(rig, *warp, 2, 200, 1, 0, 0, 100, 0, 100, false, 200);
    }
}
TEST_F(CharacterSecondaryNaturalDistinctTest, HalfRatePositiveTargetDoesNotRetimestampTheActualReturnClock) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalDistinctRig rig(Context, types); Prepare(rig, 3, -1024, 512);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 100, 201.5f); ASSERT_NE(nullptr, warp);
        TargetFinish(rig, *warp, 2, 201.5f, 1, 0, .5f, 101, 1, 102, true, 203);
    }
}
TEST_F(CharacterSecondaryNaturalDistinctTest, HalfRateNegativeTargetClampsLiteralEntryAndPreservesSourceClock) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalDistinctRig rig(Context, types); Prepare(rig, 3, -1024, -512);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 100, 200); ASSERT_NE(nullptr, warp);
        TargetFinish(rig, *warp, 2, 200, 1, 0, 0, 100, 0, 100, false, 200);
    }
}
TEST_F(CharacterSecondaryNaturalDistinctTest, ForwardSourceBoundaryUsesDistinctPositiveTargetAndRetainedSecondaryCursor) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalDistinctRig rig(Context, types); Prepare(rig, 7, 1024, 1024, true, false);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        State(rig, &rig.Source(), &rig.Target(), 9, 0, -9, -50); EXPECT_FLOAT_EQ(9, rig.Secondary.GetFrame()); Pose(rig, 109, 190);
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 190, 203); ASSERT_NE(nullptr, warp);
        TargetFinish(rig, *warp, 2, 203, 9, -10, 1, 112, 2, 114, true, 206);
    }
}
TEST_F(CharacterSecondaryNaturalDistinctTest, ForwardSourceBoundaryIntoNegativeTargetDoesNotInventTargetEnd) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalDistinctRig rig(Context, types); Prepare(rig, 7, 1024, -1024);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 100, 200); ASSERT_NE(nullptr, warp);
        TargetFinish(rig, *warp, 2, 200, 9, -10, 0, 110, 0, 110, false, 200);
    }
}
TEST_F(CharacterSecondaryNaturalDistinctTest, ExactReverseZeroForecastKeepsCurrentSourceAndPendingDistinctTarget) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalDistinctRig rig(Context, types); Prepare(rig, 4, -1024, 1024);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        ASSERT_EQ(0.0f, rig.Source().GetNextFrame(1.953125f));
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 100, 10); ASSERT_NE(nullptr, warp);
        ASSERT_EQ(2.0f, warp->GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        State(rig, &rig.Source(), &rig.Target(), 0, 0, 0, -50); Pose(rig, 100, 10);
        rig.Character.Tick(0); ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount()); Pose(rig, 100, 10);
        ASSERT_LT(rig.Source().GetNextFrame(.9765625f), 0); rig.Character.Tick(.9765625f);
        EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation());
        State(rig, &rig.Target(), nullptr, 0, 1, 0, -52); Pose(rig, 102, 10);
        rig.Character.Tick(0); Pose(rig, 102, 203);
    }
}
TEST_F(CharacterSecondaryNaturalDistinctTest, ExactForwardLengthForecastAlreadyPromotesQueuedTargetAtFrameZero) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalDistinctRig rig(Context, types); Prepare(rig, 6, 1024, 1024);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        ASSERT_EQ(10.0f, rig.Source().GetNextFrame(1.953125f));
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 100, 200); ASSERT_NE(nullptr, warp);
        TargetFinish(rig, *warp, 2, 200, 8, -10, 0, 110, 1, 112, true, 203);
    }
}
TEST_F(CharacterSecondaryNaturalDistinctTest, PendingDistinctTargetDoesNotReplaceAnInteriorCurrentSourceReturn) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalDistinctRig rig(Context, types); Prepare(rig, 6, -1024, 1024);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        State(rig, &rig.Source(), &rig.Target(), 4, 0, -4, -50);
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 100, 12); ASSERT_NE(nullptr, warp);
        ASSERT_EQ(2.0f, warp->GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        State(rig, &rig.Source(), &rig.Target(), 2, 0, -2, -50); Pose(rig, 102, 12);
        rig.Character.Tick(0); ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        rig.Character.Tick(.9765625f); EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation()); State(rig, &rig.Source(), &rig.Target(), 1, 0, -1, -50); Pose(rig, 101, 12);
        rig.Character.Tick(0); Pose(rig, 101, 11);
    }
}
TEST_F(CharacterSecondaryNaturalDistinctTest, ReverseExpiryTickAlreadyPromotesDistinctTargetBeforeReturnIsBuilt) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalDistinctRig rig(Context, types); Prepare(rig, 1, -1024, 1024);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; ASSERT_EQ(-1.0f, rig.Source().GetNextFrame(1.953125f));
        rig.Character.Tick(1.953125f); State(rig, &rig.Target(), nullptr, 1, 1, 0, -52); Pose(rig, 102, 100);
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 100, 209); ASSERT_NE(nullptr, warp);
        TargetFinish(rig, *warp, 2, 209, 1, 0, 3, 106, 4, 108, true, 212);
    }
}
TEST_F(CharacterSecondaryNaturalDistinctTest, ForwardExpiryTickAlreadyPromotesDistinctTargetAndUsesItsCurrentFrame) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalDistinctRig rig(Context, types); Prepare(rig, 9, 1024, 1024);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation;
        ASSERT_GT(rig.Source().GetNextFrame(1.953125f), 10.0f);
        EXPECT_NEAR(11.0f, rig.Source().GetNextFrame(1.953125f), .00001f);
        rig.Character.Tick(1.953125f); State(rig, &rig.Target(), nullptr, 9, 1, -10, -52); Pose(rig, 112, 100);
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 100, 209); ASSERT_NE(nullptr, warp);
        TargetFinish(rig, *warp, 2, 209, 9, -10, 3, 116, 4, 118, true, 212);
    }
}
TEST_F(CharacterSecondaryNaturalDistinctTest, NaturalExpiryAfterMainStoppedUsesRealNoActiveFallback) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalDistinctRig rig(Context, types); Prepare(rig, 1, -1024, 1024, false);
        ASSERT_EQ(-1.0f, rig.Source().GetNextFrame(1.953125f)); rig.Character.Tick(1.953125f);
        State(rig, nullptr, nullptr, 1, 0, 0, -50); EXPECT_FLOAT_EQ(0, rig.Secondary.GetFrame());
        EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation());
        Pose(rig, 100, 100); rig.Character.Tick(0); Pose(rig, 100, 100);
    }
}
TEST_F(CharacterSecondaryNaturalDistinctTest, PendingPausedTargetStillCompletesReturnOnTheTriggeringSourceClock) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalDistinctRig rig(Context, types); Prepare(rig, 3, -1024, 0);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        RCKKeyedAnimation *warp = Return(rig, retained, 2, 100, 200); ASSERT_NE(nullptr, warp);
        TargetFinish(rig, *warp, 2, 200, 1, 0, 0, 100, 0, 100, true, 200);
    }
}
TEST_F(CharacterSecondaryNaturalDistinctTest, MinimumActualDurationForecastsDistinctHalfRateTargetAfterReverseCrossing) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalDistinctRig rig(Context, types); Prepare(rig, 2.5f, -1024, 512, true, true, .5f);
        RCKKeyedAnimation *retained = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        State(rig, &rig.Source(), &rig.Target(), .5f, 0, -.5f, -50); Pose(rig, 100.5f, 100);
        ASSERT_LT(rig.Source().GetNextFrame(.9765625f), 0);
        RCKKeyedAnimation *warp = Return(rig, retained, 1, 100, 200.75f); ASSERT_NE(nullptr, warp);
        TargetFinish(rig, *warp, 1, 200.75f, .5f, 0, .25f, 100.5f, .75f, 101.5f, true, 202.25f);
    }
}
} // namespace
