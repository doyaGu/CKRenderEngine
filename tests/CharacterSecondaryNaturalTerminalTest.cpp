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
CK_CLASSID NaturalTerminalDerivedID = 0;
class NaturalTerminalDerived : public RCKKeyedAnimation {
public:
    explicit NaturalTerminalDerived(CKContext *context) : RCKKeyedAnimation(context) {}
    CK_CLASSID GetClassID() override { return NaturalTerminalDerivedID; }
};
class NaturalTerminalCharacter : public RCKCharacter {
public:
    explicit NaturalTerminalCharacter(CKContext *context) : RCKCharacter(context) {}
    const CKSecondaryAnimation &Slot() const { return m_SecondaryAnimations[0]; }
    void Tick(float milliseconds) {
        SetMoveableFlags(GetMoveableFlags() | VX_MOVEABLE_CHARACTERRENDERED);
        ProcessAnimation(milliseconds);
    }
};
static CK3dEntity *TerminalEntity(RCK3dEntity &entity) { return reinterpret_cast<CK3dEntity *>(&entity); }
class NaturalTerminalRig {
public:
    NaturalTerminalCharacter Character;
    RCKBodyPart Root, Limb;
    RCKObjectAnimation SourceRoot, SourceLimb, TargetRoot, TargetLimb, DerivedSourceRoot, DerivedSourceLimb,
        DerivedTargetRoot, DerivedTargetLimb, SecondaryLimb;
    RCKKeyedAnimation SourceExact, TargetExact, Secondary;
    NaturalTerminalDerived SourceDerived, TargetDerived;
    int Types;
    explicit NaturalTerminalRig(CKContext *context, int types)
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
        VxVector zero(0, 0, 0); Root.SetPosition(&zero, TerminalEntity(Character));
        EXPECT_TRUE(Limb.SetParent(TerminalEntity(Root), TRUE));
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
    ~NaturalTerminalRig() {
        Character.FlushSecondaryAnimations(); Limb.SetParent(nullptr, TRUE); Root.SetParent(nullptr, TRUE);
    }
    RCKKeyedAnimation &Source() { return Types == 2 ? static_cast<RCKKeyedAnimation &>(SourceDerived) : SourceExact; }
    RCKKeyedAnimation &Target() { return Types == 1 ? static_cast<RCKKeyedAnimation &>(TargetDerived) : TargetExact; }
    RCKObjectAnimation &SourceChannel() { return Types == 2 ? DerivedSourceRoot : SourceRoot; }
    RCKObjectAnimation &TargetChannel() { return Types == 1 ? DerivedTargetRoot : TargetRoot; }
private:
    void Curve(RCKKeyedAnimation &group, RCKObjectAnimation &channel, RCKBodyPart &part,
               float length, float first, float last) {
        channel.Set3dEntity(TerminalEntity(part)); VxVector a(first, 0, 0), b(last, 0, 0);
        channel.AddPositionKey(0, &a); channel.AddPositionKey(length, &b); EXPECT_EQ(CK_OK, group.AddAnimation(&channel));
    }
};
class CharacterSecondaryNaturalTerminalTest : public ::testing::Test {
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
        NaturalTerminalDerivedID = CKClassGetNewIdentifier();
        CKClassRegister(NaturalTerminalDerivedID, CKCID_KEYEDANIMATION, nullptr, nullptr, nullptr, nullptr, nullptr);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Pose(NaturalTerminalRig &rig, float world, float limb) {
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
        EXPECT_EQ(TerminalEntity(rig.Character), rig.Root.GetParent()); EXPECT_EQ(TerminalEntity(rig.Root), rig.Limb.GetParent());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Root.GetCharacter());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), rig.Limb.GetCharacter());
        EXPECT_EQ(nullptr, rig.Root.GetExclusiveAnimation());
    }
    static void State(NaturalTerminalRig &rig, CKAnimation *active, CKAnimation *queue,
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
    static void Prepare(NaturalTerminalRig &rig, float warm, CKBOOL sourceLinked, float sourceFPS,
                        CKBOOL targetLinked, float targetFPS, bool reverseSecondary = true, float requested = 2) {
        const float duration = requested < 1 ? 1 : requested;
        const CK_SECONDARYANIMATION_FLAGS flags = static_cast<CK_SECONDARYANIMATION_FLAGS>(
            CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_DOWARP);
        ASSERT_EQ(CK_OK, rig.Character.PlaySecondaryAnimation(&rig.Secondary, reverseSecondary ? 0 : 8, flags, requested));
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        RCKKeyedAnimation *starting = rig.Character.Slot().Animation; ASSERT_NE(nullptr, starting);
        ASSERT_EQ(duration, starting->GetNextFrame(duration * .9765625f)); rig.Character.Tick(duration * .9765625f);
        ASSERT_GT(starting->GetNextFrame(.9765625f), duration); rig.Character.Tick(.9765625f);
        ASSERT_EQ(CKSECONDARYANIMATIONRUNTIME_PLAYING, rig.Character.Slot().Mode);
        EXPECT_FLOAT_EQ(reverseSecondary ? 1 : 9, rig.Secondary.GetFrame());
        rig.Secondary.LinkToFrameRate(TRUE, 0); rig.Source().LinkToFrameRate(TRUE, 1024);
        ASSERT_NEAR(warm, rig.Source().GetNextFrame(warm * .9765625f), .00001f); rig.Character.Tick(warm * .9765625f);
        State(rig, &rig.Source(), nullptr, warm, 0, -warm, -50); Pose(rig, 100 + warm, reverseSecondary ? 110 : 190);
        rig.Source().LinkToFrameRate(sourceLinked, sourceFPS); rig.Target().LinkToFrameRate(targetLinked, targetFPS);
        ASSERT_EQ(CK_OK, rig.Character.SetNextActiveAnimation(&rig.Target(), CK_TRANSITION_TOSTART));
        State(rig, &rig.Source(), &rig.Target(), warm, 0, -warm, -50);
        rig.Secondary.LinkToFrameRate(TRUE, reverseSecondary ? -1024 : 1024);
        ASSERT_EQ(reverseSecondary ? -1.0f : 11.0f, rig.Secondary.GetNextFrame(1.953125f));
    }
    static void Curve(NaturalTerminalRig &rig, RCKKeyedAnimation &warp, float duration, float first, float last) {
        EXPECT_FLOAT_EQ(duration, warp.GetLength()); EXPECT_EQ(2, warp.GetAnimationCount());
        EXPECT_EQ(nullptr, warp.GetAnimation(TerminalEntity(rig.Root)));
        int detached = 0, mapped = 0;
        for (int i = 0; i < warp.GetAnimationCount(); ++i) {
            CKObjectAnimation *child = warp.GetAnimation(i); EXPECT_NE(nullptr, child); if (!child) continue;
            if (!child->Get3dEntity()) {
                ++detached; EXPECT_FALSE(child->HasPositionInfo()); EXPECT_FALSE(child->HasRotationInfo());
                EXPECT_FALSE(child->HasScaleInfo()); EXPECT_FALSE(child->HasScaleAxisInfo()); EXPECT_FALSE(child->HasMorphInfo());
            } else { ++mapped; EXPECT_EQ(TerminalEntity(rig.Limb), child->Get3dEntity()); }
        }
        EXPECT_EQ(1, detached); EXPECT_EQ(1, mapped);
        CKObjectAnimation *channel = warp.GetAnimation(TerminalEntity(rig.Limb)); EXPECT_NE(nullptr, channel);
        if (channel) {
            const float times[] = {0, duration / 2, duration}, values[] = {first, (first + last) / 2, last};
            for (int i = 0; i < 3; ++i) {
                VxVector value; EXPECT_TRUE(channel->EvaluatePosition(times[i], value)); EXPECT_NEAR(values[i], value.x, .0005f);
                EXPECT_FLOAT_EQ(0, value.y); EXPECT_FLOAT_EQ(0, value.z);
            }
        }
    }
    static RCKKeyedAnimation *Return(NaturalTerminalRig &rig, RCKKeyedAnimation *retained,
                                    CKBOOL linked, float rate, float duration, float first, float last) {
        EXPECT_EQ(1, rig.Character.GetSecondaryAnimationsCount()); if (rig.Character.GetSecondaryAnimationsCount() != 1) return nullptr;
        EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, rig.Character.Slot().Mode);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; EXPECT_EQ(retained, warp); if (!warp) return nullptr;
        EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0)); EXPECT_FLOAT_EQ(0, warp->GetFrame());
        EXPECT_EQ(reinterpret_cast<CKCharacter *>(&rig.Character), warp->GetCharacter());
        EXPECT_EQ(static_cast<CKAnimation *>(warp), rig.Limb.GetExclusiveAnimation());
        EXPECT_EQ(linked != FALSE, warp->IsLinkedToFrameRate() != FALSE); EXPECT_FLOAT_EQ(rate, warp->GetLinkedFrameRate());
        Curve(rig, *warp, duration, first, last); return warp;
    }
    static void Gate(NaturalTerminalRig &rig, RCKKeyedAnimation &warp, float frame) {
        ASSERT_EQ(1, rig.Character.GetSecondaryAnimationsCount());
        EXPECT_EQ(&warp, rig.Character.Slot().Animation); EXPECT_EQ(CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP, rig.Character.Slot().Mode);
        EXPECT_EQ(&rig.Secondary, rig.Character.GetSecondaryAnimation(0));
        EXPECT_EQ(static_cast<CKAnimation *>(&warp), rig.Limb.GetExclusiveAnimation()); EXPECT_NEAR(frame, warp.GetFrame(), .00001f);
    }
    static void Released(NaturalTerminalRig &rig) {
        EXPECT_EQ(0, rig.Character.GetSecondaryAnimationsCount()); EXPECT_EQ(nullptr, rig.Limb.GetExclusiveAnimation());
    }
};


TEST_F(CharacterSecondaryNaturalTerminalTest, PositiveLinkedTargetStopsBeforeReturnAndRetainsItsPreviousCursor) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalTerminalRig rig(Context, types);
        Prepare(rig, 8.5f, FALSE, -2048, TRUE, 512, true, 4); rig.Target().SetCanBeInterrupt(TRUE);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(rig, warp, FALSE, 2048, 4, 100, 19.5f));
        ASSERT_EQ(1.0f, warp->GetNextFrame(7.8125f)); rig.Character.Tick(7.8125f);
        State(rig, &rig.Target(), nullptr, 9.5f, 2, -10, -54); Pose(rig, 114, 126.5f); Curve(rig, *warp, 4, 100, 206);
        ASSERT_GT(rig.Target().GetNextFrame(62.5f), 20); ASSERT_EQ(2.0f, warp->GetNextFrame(62.5f)); rig.Character.Tick(62.5f);
        State(rig, nullptr, nullptr, 9.5f, 2, -10, -90); Pose(rig, 150, 180); Gate(rig, *warp, 2); Curve(rig, *warp, 4, 100, 260);
        rig.Character.Tick(0); State(rig, nullptr, nullptr, 9.5f, 2, -10, -90); Pose(rig, 150, 220); Gate(rig, *warp, 3);
        ASSERT_EQ(4.0f, warp->GetNextFrame(0)); rig.Character.Tick(0); Pose(rig, 150, 260); Gate(rig, *warp, 4);
        ASSERT_GT(warp->GetNextFrame(0), 4); rig.Character.Tick(0); Released(rig); Pose(rig, 150, 260);
        rig.Character.Tick(0); State(rig, nullptr, nullptr, 9.5f, 2, -10, -90); Pose(rig, 150, 260);
    }
}
TEST_F(CharacterSecondaryNaturalTerminalTest, PositiveLinkedTargetStopsInTheExactReturnEndpointCall) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalTerminalRig rig(Context, types);
        Prepare(rig, 8.5f, FALSE, 0, TRUE, 512); rig.Target().SetCanBeInterrupt(TRUE);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(rig, warp, FALSE, 0, 2, 100, 19.5f));
        rig.Character.Tick(7.8125f); State(rig, &rig.Target(), nullptr, 9.5f, 2, -10, -54); Pose(rig, 114, 153);
        ASSERT_EQ(2.0f, warp->GetNextFrame(62.5f)); ASSERT_GT(rig.Target().GetNextFrame(62.5f), 20); rig.Character.Tick(62.5f);
        State(rig, nullptr, nullptr, 9.5f, 2, -10, -90); Pose(rig, 150, 260); Gate(rig, *warp, 2); Curve(rig, *warp, 2, 100, 260);
        rig.Character.Tick(0); Released(rig); Pose(rig, 150, 260); rig.Character.Tick(0); Pose(rig, 150, 260);
    }
}
TEST_F(CharacterSecondaryNaturalTerminalTest, PositiveLinkedTargetStopsOnTheStrictReleaseCall) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalTerminalRig rig(Context, types);
        Prepare(rig, 8.5f, FALSE, 4096, TRUE, 512); rig.Target().SetCanBeInterrupt(TRUE);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(rig, warp, FALSE, 4096, 2, 100, 19.5f));
        rig.Character.Tick(7.8125f); Pose(rig, 114, 153);
        ASSERT_EQ(2.0f, warp->GetNextFrame(0)); ASSERT_EQ(2.0f, rig.Target().GetNextFrame(0)); rig.Character.Tick(0);
        State(rig, &rig.Target(), nullptr, 9.5f, 2, -10, -54); Pose(rig, 114, 206); Gate(rig, *warp, 2); Curve(rig, *warp, 2, 100, 206);
        ASSERT_GT(warp->GetNextFrame(62.5f), 2); ASSERT_GT(rig.Target().GetNextFrame(62.5f), 20); rig.Character.Tick(62.5f);
        Released(rig); State(rig, nullptr, nullptr, 9.5f, 2, -10, -90); Pose(rig, 150, 260);
        rig.Character.Tick(0); Pose(rig, 150, 260);
    }
}
TEST_F(CharacterSecondaryNaturalTerminalTest, NonInterruptibleLinkedTargetDisplaysLengthBeforeTheHeldBranchStopsIt) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalTerminalRig rig(Context, types);
        Prepare(rig, 8.5f, FALSE, -2048, TRUE, 512, true, 4);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(rig, warp, FALSE, 2048, 4, 100, 19.5f));
        rig.Character.Tick(7.8125f); Pose(rig, 114, 126.5f);
        ASSERT_GT(rig.Target().GetNextFrame(62.5f), 20); rig.Character.Tick(62.5f);
        State(rig, &rig.Target(), nullptr, 9.5f, 20, -10, -90); Pose(rig, 150, 180); Gate(rig, *warp, 2); Curve(rig, *warp, 4, 100, 260);
        ASSERT_EQ(20.0f, rig.Target().GetNextFrame(0)); rig.Character.Tick(0);
        State(rig, nullptr, nullptr, 9.5f, 20, -10, -90); Pose(rig, 150, 220); Gate(rig, *warp, 3);
        rig.Character.Tick(0); Pose(rig, 150, 260); Gate(rig, *warp, 4);
        rig.Character.Tick(0); Released(rig); State(rig, nullptr, nullptr, 9.5f, 20, -10, -90); Pose(rig, 150, 260);
    }
}
TEST_F(CharacterSecondaryNaturalTerminalTest, NegativeStoredUnlinkedTargetStopsBeforeReturnAndHeldZeroKeepsLength) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalTerminalRig rig(Context, types); rig.Target().SetLength(2);
        Prepare(rig, 3, TRUE, -1024, FALSE, -2048, true, 4); rig.Target().SetCanBeInterrupt(TRUE);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 4, 100, 11));
        ASSERT_EQ(0.0f, rig.Source().GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        State(rig, &rig.Source(), &rig.Target(), 0, 0, 0, -50); Pose(rig, 100, 77.5f); Curve(rig, *warp, 4, 100, 10);
        rig.Character.Tick(.9765625f); State(rig, &rig.Target(), nullptr, 0, 1, 0, -52); Pose(rig, 102, 151.5f);
        ASSERT_EQ(2.0f, rig.Target().GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        State(rig, nullptr, nullptr, 0, 1, 0, -54); Pose(rig, 104, 179.5f); Gate(rig, *warp, 3); Curve(rig, *warp, 4, 100, 206);
        ASSERT_EQ(4.0f, warp->GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f); Pose(rig, 104, 206); Gate(rig, *warp, 4);
        ASSERT_EQ(4.0f, warp->GetNextFrame(0)); rig.Character.Tick(0); State(rig, nullptr, nullptr, 0, 1, 0, -54); Pose(rig, 104, 206); Gate(rig, *warp, 4);
        ASSERT_GT(warp->GetNextFrame(.9765625f), 4); rig.Character.Tick(.9765625f); Released(rig); Pose(rig, 104, 206);
        rig.Character.Tick(0); Pose(rig, 104, 206);
    }
}
TEST_F(CharacterSecondaryNaturalTerminalTest, UnlinkedTargetStopsInTheExactReturnEndpointCallWithItsOldCursor) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalTerminalRig rig(Context, types); rig.Target().SetLength(1);
        Prepare(rig, 2.5f, TRUE, -1024, FALSE, 0); rig.Target().SetCanBeInterrupt(TRUE);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 100, 10.5f));
        rig.Character.Tick(.9765625f); State(rig, &rig.Target(), nullptr, .5f, .5f, 0, -51); Pose(rig, 101, 150.75f);
        ASSERT_EQ(2.0f, warp->GetNextFrame(.9765625f)); ASSERT_GT(rig.Target().GetNextFrame(.9765625f), 1); rig.Character.Tick(.9765625f);
        State(rig, nullptr, nullptr, .5f, .5f, 0, -52); Pose(rig, 102, 203); Gate(rig, *warp, 2); Curve(rig, *warp, 2, 100, 203);
        rig.Character.Tick(0); State(rig, nullptr, nullptr, .5f, .5f, 0, -52); Pose(rig, 102, 203); Gate(rig, *warp, 2);
        rig.Character.Tick(.9765625f); Released(rig); Pose(rig, 102, 203); rig.Character.Tick(0); Pose(rig, 102, 203);
    }
}
TEST_F(CharacterSecondaryNaturalTerminalTest, UnlinkedTargetStoppingOnStrictReleaseAppliesLengthBeforeClearingItsGate) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalTerminalRig rig(Context, types); rig.Target().SetLength(2.5f);
        Prepare(rig, 2.5f, TRUE, -1024, FALSE, 4096); rig.Target().SetCanBeInterrupt(TRUE);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 2, 100, 10.5f));
        rig.Character.Tick(.9765625f); Pose(rig, 101, 150.75f);
        rig.Character.Tick(.9765625f); State(rig, &rig.Target(), nullptr, .5f, 1.5f, 0, -53); Pose(rig, 103, 204.5f); Gate(rig, *warp, 2); Curve(rig, *warp, 2, 100, 204.5f);
        ASSERT_EQ(2.5f, rig.Target().GetNextFrame(.9765625f)); ASSERT_GT(warp->GetNextFrame(.9765625f), 2); rig.Character.Tick(.9765625f);
        Released(rig); State(rig, nullptr, nullptr, .5f, 1.5f, 0, -55); Pose(rig, 105, 207.5f);
        rig.Character.Tick(0); Pose(rig, 105, 207.5f);
    }
}
TEST_F(CharacterSecondaryNaturalTerminalTest, NonInterruptibleUnlinkedLengthHoldsThenStopsOnZeroTimeWithoutAdvancingReturn) {
    for (int types = 0; types < 3; ++types) {
        SCOPED_TRACE(types); NaturalTerminalRig rig(Context, types); rig.Target().SetLength(2);
        Prepare(rig, 3, TRUE, -1024, FALSE, -2048, true, 4);
        RCKKeyedAnimation *warp = rig.Character.Slot().Animation; rig.Character.Tick(1.953125f);
        ASSERT_NE(nullptr, Return(rig, warp, TRUE, 1024, 4, 100, 11));
        rig.Character.Tick(.9765625f); rig.Character.Tick(.9765625f); Pose(rig, 102, 151.5f);
        ASSERT_EQ(2.0f, rig.Target().GetNextFrame(.9765625f)); rig.Character.Tick(.9765625f);
        State(rig, &rig.Target(), nullptr, 0, 2, 0, -54); Pose(rig, 104, 179.5f); Gate(rig, *warp, 3); Curve(rig, *warp, 4, 100, 206);
        ASSERT_EQ(3.0f, rig.Target().GetNextFrame(0)); ASSERT_EQ(3.0f, warp->GetNextFrame(0)); rig.Character.Tick(0);
        State(rig, nullptr, nullptr, 0, 2, 0, -54); Pose(rig, 104, 179.5f); Gate(rig, *warp, 3); Curve(rig, *warp, 4, 100, 206);
        rig.Character.Tick(.9765625f); Pose(rig, 104, 206); Gate(rig, *warp, 4);
        rig.Character.Tick(0); Pose(rig, 104, 206); Gate(rig, *warp, 4);
        rig.Character.Tick(.9765625f); Released(rig); State(rig, nullptr, nullptr, 0, 2, 0, -54); Pose(rig, 104, 206);
    }
}
} // namespace
