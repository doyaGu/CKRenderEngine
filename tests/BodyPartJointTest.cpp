#include <gtest/gtest.h>
#include <cmath>
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
class JointBodyProbe : public RCKBodyPart {
public:
    explicit JointBodyProbe(CKContext *context, CKSTRING name = nullptr) : RCKBodyPart(context, name) {}
    CKIkJoint &Joint() { return m_RotationJoint; }
    void Character(RCKCharacter *character) { m_Character = character; }
    void Exclusive(RCKAnimation *animation) { m_ExclusiveAnimation = animation; }
};

class JointRemapContext : public CKDependenciesContext {
public:
    explicit JointRemapContext(CKContext *context) : CKDependenciesContext(context) {}
    void Map(CKObject *source, CKObject *destination) {
        m_MapID.Insert(source->GetID(), destination ? destination->GetID() : 0);
    }
};

class BodyPartJointTest : public ::testing::Test {
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
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Pose(RCK3dEntity &body, float x, float y, float z) {
        Vx3DMatrixFromEulerAngles(body.m_LocalMatrix, x, y, z); body.LocalMatrixChanged(FALSE, TRUE);
    }
    static void Limits(JointBodyProbe &body, CKDWORD flags) {
        body.Joint().m_Flags = flags; body.Joint().m_Min.Set(-.2f, -.3f, -.4f);
        body.Joint().m_Max.Set(.2f, .3f, .4f); body.Joint().m_Damping.Set(.1f, .2f, .3f);
    }
    static void ExpectAngles(RCK3dEntity &body, float x, float y, float z) {
        float actualX, actualY, actualZ;
        Vx3DMatrixToEulerAngles(body.GetLocalMatrix(), &actualX, &actualY, &actualZ);
        EXPECT_NEAR(x, actualX, 1e-5f); EXPECT_NEAR(y, actualY, 1e-5f); EXPECT_NEAR(z, actualZ, 1e-5f);
    }
    static void PositionCurve(RCKObjectAnimation &channel, JointBodyProbe &body) {
        channel.Set3dEntity(reinterpret_cast<CK3dEntity *>(&body));
        VxVector first(0, 0, 0), last(10, 0, 0); channel.AddPositionKey(0, &first); channel.AddPositionKey(10, &last);
        channel.SetLength(10);
    }
};

TEST_F(BodyPartJointTest, ConstructorCreatesActiveZeroJointAndNullNonowningReferences) {
    JointBodyProbe body(Context); EXPECT_EQ(7u, body.Joint().m_Flags);
    EXPECT_EQ(nullptr, body.GetCharacter()); EXPECT_EQ(nullptr, body.GetExclusiveAnimation());
    for (int i = 0; i < 3; ++i) {
        EXPECT_FLOAT_EQ(0, (&body.Joint().m_Min.x)[i]); EXPECT_FLOAT_EQ(0, (&body.Joint().m_Max.x)[i]);
        EXPECT_FLOAT_EQ(0, (&body.Joint().m_Damping.x)[i]);
    }
    EXPECT_EQ(&body, Context->GetObject(body.GetID()));
}

TEST_F(BodyPartJointTest, XLimitClampsBothBoundsAndDoesNotLimitY) {
    JointBodyProbe body(Context); Limits(body, CK_IKJOINT_LIMIT_X); Pose(body, .8f, .6f, 0);
    ASSERT_EQ(CK_OK, body.FitToJoint()); ExpectAngles(body, .2f, .6f, 0);
    Pose(body, -.8f, -.6f, 0); ASSERT_EQ(CK_OK, body.FitToJoint()); ExpectAngles(body, -.2f, -.6f, 0);
}

TEST_F(BodyPartJointTest, YLimitClampsBothBoundsAndDoesNotLimitZ) {
    JointBodyProbe body(Context); Limits(body, CK_IKJOINT_LIMIT_Y); Pose(body, 0, .7f, .8f);
    ASSERT_EQ(CK_OK, body.FitToJoint()); ExpectAngles(body, 0, .3f, .8f);
    Pose(body, 0, -.7f, -.8f); ASSERT_EQ(CK_OK, body.FitToJoint()); ExpectAngles(body, 0, -.3f, -.8f);
}

TEST_F(BodyPartJointTest, ZLimitClampsBothBoundsWithNamedZFlag) {
    JointBodyProbe body(Context); Limits(body, CK_IKJOINT_LIMIT_Z); Pose(body, .5f, 0, .8f);
    ASSERT_EQ(CK_OK, body.FitToJoint()); ExpectAngles(body, .5f, 0, .4f);
    Pose(body, -.5f, 0, -.8f); ASSERT_EQ(CK_OK, body.FitToJoint()); ExpectAngles(body, -.5f, 0, -.4f);
}

TEST_F(BodyPartJointTest, CombinedLimitBitsApplyThreeDistinctIntervals) {
    JointBodyProbe body(Context); Limits(body, CK_IKJOINT_LIMIT); Pose(body, .8f, -.7f, .9f);
    ASSERT_EQ(CK_OK, body.FitToJoint()); ExpectAngles(body, .2f, -.3f, .4f);
}

TEST_F(BodyPartJointTest, ActiveAndEaseBitsDoNotActAsLimitBits) {
    JointBodyProbe body(Context); Limits(body, CK_IKJOINT_ACTIVE | CK_IKJOINT_EASE); Pose(body, .5f, -.6f, .7f);
    ASSERT_EQ(CK_OK, body.FitToJoint()); ExpectAngles(body, .5f, -.6f, .7f);
}

TEST_F(BodyPartJointTest, AnglesAlreadyInsideBoundsRemainUnchanged) {
    JointBodyProbe body(Context); Limits(body, CK_IKJOINT_LIMIT); Pose(body, .1f, -.2f, .3f);
    ASSERT_EQ(CK_OK, body.FitToJoint()); ExpectAngles(body, .1f, -.2f, .3f);
}

TEST_F(BodyPartJointTest, FitUpdatesWorldRotationAndDescendantPositionThroughRealHierarchy) {
    RCK3dEntity parent(Context), descendant(Context); JointBodyProbe body(Context);
    parent.m_LocalMatrix[3][0] = 10; parent.LocalMatrixChanged(FALSE, TRUE);
    body.SetParent(reinterpret_cast<CK3dEntity *>(&parent));
    descendant.SetParent(reinterpret_cast<CK3dEntity *>(&body)); descendant.m_LocalMatrix.SetIdentity();
    descendant.m_LocalMatrix[3][0] = 2; descendant.LocalMatrixChanged(FALSE, TRUE);
    Limits(body, CK_IKJOINT_LIMIT_Z); body.Joint().m_Max.z = .25f; Pose(body, 0, 0, .8f);
    ASSERT_EQ(CK_OK, body.FitToJoint());
    const float cosine = std::cos(.25f), sine = std::sin(.25f);
    EXPECT_NEAR(cosine, body.GetLocalMatrix()[0][0], 1e-5f); EXPECT_NEAR(sine, body.GetWorldMatrix()[0][1], 1e-5f);
    EXPECT_FLOAT_EQ(10, body.GetWorldMatrix()[3][0]);
    EXPECT_NEAR(10 + 2 * cosine, descendant.GetWorldMatrix()[3][0], 1e-5f);
    EXPECT_NEAR(2 * sine, descendant.GetWorldMatrix()[3][1], 1e-5f);
}

TEST_F(BodyPartJointTest, RotationJointGetterReturnsIndependentCompleteValueCopy) {
    JointBodyProbe body(Context); Limits(body, 0x321); CKIkJoint output;
    body.GetRotationJoint(&output); EXPECT_EQ(0x321u, output.m_Flags);
    EXPECT_FLOAT_EQ(-.2f, output.m_Min.x); EXPECT_FLOAT_EQ(.3f, output.m_Max.y);
    EXPECT_FLOAT_EQ(.3f, output.m_Damping.z); output.m_Min.x = -9; output.m_Flags = 0;
    // Getter output mutation cannot alter the body value. Inspect actual Save's independent wire.
    body.SetFlags(body.GetFlags() | CK_3DENTITY_IKJOINTVALID);
    CKStateChunk *saved = body.Save(nullptr, CK_STATESAVE_BODYPARTCHARACTER); ASSERT_NE(nullptr, saved);
    saved->StartRead(); ASSERT_TRUE(saved->SeekIdentifier(CK_STATESAVE_BODYPARTCHARACTER)); saved->ReadObjectID();
    EXPECT_EQ(0x321u, saved->ReadDword()); EXPECT_FLOAT_EQ(-.2f, saved->ReadFloat()); DeleteCKStateChunk(saved);
}

TEST_F(BodyPartJointTest, RotationJointSetterCopiesValuesUsedByFitWithoutRetainingInputPointer) {
    JointBodyProbe body(Context); CKIkJoint joint;
    joint.m_Flags = CK_IKJOINT_LIMIT_Z; joint.m_Min.Set(-1, -1, -.25f); joint.m_Max.Set(1, 1, .25f);
    joint.m_Damping.Set(.1f, .2f, .3f); body.SetRotationJoint(&joint);
    joint.m_Flags = 0; joint.m_Max.z = .9f; EXPECT_EQ(CK_IKJOINT_LIMIT_Z, body.Joint().m_Flags);
    EXPECT_FLOAT_EQ(.25f, body.Joint().m_Max.z); Pose(body, 0, 0, .8f);
    ASSERT_EQ(CK_OK, body.FitToJoint()); ExpectAngles(body, 0, 0, .25f);
}

TEST_F(BodyPartJointTest, ExclusiveAnimationSetterControlsActualChannelPlaybackAndNullReleasesIt) {
    JointBodyProbe body(Context); RCKKeyedAnimation allowed(Context), other(Context); RCKObjectAnimation channel(Context);
    PositionCurve(channel, body); body.SetExclusiveAnimation(&allowed);
    ASSERT_EQ(CK_OK, channel.SetFrame(5, reinterpret_cast<CKKeyedAnimation *>(&other)));
    EXPECT_FLOAT_EQ(0, body.GetLocalMatrix()[3][0]);
    ASSERT_EQ(CK_OK, channel.SetFrame(5, reinterpret_cast<CKKeyedAnimation *>(&allowed)));
    EXPECT_FLOAT_EQ(5, body.GetLocalMatrix()[3][0]); body.SetExclusiveAnimation(nullptr);
    ASSERT_EQ(CK_OK, channel.SetFrame(8)); EXPECT_FLOAT_EQ(8, body.GetLocalMatrix()[3][0]);
}

TEST_F(BodyPartJointTest, ExclusiveGetterUsesStoredReferenceAndCharacterGetterUsesIndependentAssociation) {
    RCKCharacter character(Context); JointBodyProbe body(Context); RCKKeyedAnimation exclusive(Context);
    RCKObjectAnimation channel(Context); PositionCurve(channel, body);
    body.Character(&character); body.Exclusive(&exclusive);
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), body.GetCharacter());
    EXPECT_EQ(static_cast<CKAnimation *>(&exclusive), body.GetExclusiveAnimation());
    ASSERT_EQ(CK_OK, channel.SetFrame(7)); EXPECT_FLOAT_EQ(0, body.GetLocalMatrix()[3][0]);
    ASSERT_EQ(CK_OK, channel.SetFrame(7, reinterpret_cast<CKKeyedAnimation *>(&exclusive)));
    EXPECT_FLOAT_EQ(7, body.GetLocalMatrix()[3][0]);
}

TEST_F(BodyPartJointTest, CopyKeepsShallowRelationsAndCopiesJointIndependentlyFromSource) {
    RCKCharacter character(Context), mappedCharacter(Context); RCKKeyedAnimation exclusive(Context), mappedExclusive(Context);
    JointBodyProbe source(Context, "Source"), destination(Context, "Destination");
    source.Character(&character); source.Exclusive(&exclusive); Limits(source, CK_IKJOINT_LIMIT_Z); Pose(source, 0, 0, .8f);
    JointRemapContext dependencies(Context); dependencies.SetOperationMode(CK_DEPENDENCIES_COPY);
    dependencies.Map(&character, &mappedCharacter); dependencies.Map(&exclusive, &mappedExclusive);
    CKDependencies options; options.m_Flags = CK_DEPENDENCIES_NONE; dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); dependencies.StopDependencies();
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), destination.GetCharacter());
    EXPECT_EQ(static_cast<CKAnimation *>(&exclusive), destination.GetExclusiveAnimation());
    EXPECT_STREQ("Destination", destination.GetName()); EXPECT_NE(source.GetID(), destination.GetID());
    source.Joint().m_Max.z = .9f; EXPECT_FLOAT_EQ(.4f, destination.Joint().m_Max.z);
    ASSERT_EQ(CK_OK, destination.FitToJoint()); ExpectAngles(destination, 0, 0, .4f); ExpectAngles(source, 0, 0, .8f);
}

TEST_F(BodyPartJointTest, RemapReplacesCharacterAndExclusiveReferenceAndChangesPlaybackGate) {
    RCKCharacter character(Context), replacement(Context); RCKKeyedAnimation oldAnimation(Context), newAnimation(Context);
    JointBodyProbe body(Context); RCKObjectAnimation channel(Context); PositionCurve(channel, body);
    body.Character(&character); body.Exclusive(&oldAnimation); Limits(body, 0x321);
    JointRemapContext dependencies(Context); dependencies.Map(&character, &replacement);
    dependencies.Map(&oldAnimation, &newAnimation); CKDependencies options; options.m_Flags = CK_DEPENDENCIES_NONE;
    dependencies.StartDependencies(&options); ASSERT_EQ(CK_OK, body.RemapDependencies(dependencies)); dependencies.StopDependencies();
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&replacement), body.GetCharacter());
    EXPECT_EQ(static_cast<CKAnimation *>(&newAnimation), body.GetExclusiveAnimation()); EXPECT_EQ(0x321u, body.Joint().m_Flags);
    ASSERT_EQ(CK_OK, channel.SetFrame(6, reinterpret_cast<CKKeyedAnimation *>(&oldAnimation)));
    EXPECT_FLOAT_EQ(0, body.GetLocalMatrix()[3][0]);
    ASSERT_EQ(CK_OK, channel.SetFrame(6, reinterpret_cast<CKKeyedAnimation *>(&newAnimation)));
    EXPECT_FLOAT_EQ(6, body.GetLocalMatrix()[3][0]);
}

TEST_F(BodyPartJointTest, RemapPreservesUnmappedReferencesAndExplicitZeroMappingClearsThem) {
    RCKCharacter character(Context); RCKKeyedAnimation exclusive(Context); JointBodyProbe body(Context);
    body.Character(&character); body.Exclusive(&exclusive); CKDependencies options; options.m_Flags = CK_DEPENDENCIES_NONE;
    JointRemapContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, body.RemapDependencies(dependencies));
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), body.GetCharacter());
    EXPECT_EQ(static_cast<CKAnimation *>(&exclusive), body.GetExclusiveAnimation());
    dependencies.Map(&character, nullptr); dependencies.Map(&exclusive, nullptr);
    ASSERT_EQ(CK_OK, body.RemapDependencies(dependencies)); dependencies.StopDependencies();
    EXPECT_EQ(nullptr, body.GetCharacter()); EXPECT_EQ(nullptr, body.GetExclusiveAnimation());
    EXPECT_EQ(&character, Context->GetObject(character.GetID())); EXPECT_EQ(&exclusive, Context->GetObject(exclusive.GetID()));
}

TEST_F(BodyPartJointTest, PrepareHonorsInheritedChildrenOptionWithoutOwningCharacterOrExclusive) {
    RCKCharacter character(Context); RCKKeyedAnimation exclusive(Context); JointBodyProbe body(Context); RCK3dEntity child(Context);
    body.Character(&character); body.Exclusive(&exclusive); child.SetParent(reinterpret_cast<CK3dEntity *>(&body));
    CKDependencies none; none.m_Flags = CK_DEPENDENCIES_NONE; CKDependenciesContext first(Context);
    first.SetOperationMode(CK_DEPENDENCIES_COPY); first.StartDependencies(&none);
    ASSERT_EQ(CK_OK, body.PrepareDependencies(first)); first.StopDependencies();
    EXPECT_TRUE(first.IsDependenciesHere(body.GetID())); EXPECT_FALSE(first.IsDependenciesHere(child.GetID()));
    EXPECT_FALSE(first.IsDependenciesHere(character.GetID())); EXPECT_FALSE(first.IsDependenciesHere(exclusive.GetID()));
    CKDependencies selected; CKCopyDefaultClassDependencies(selected, CK_DEPENDENCIES_COPY);
    selected.m_Flags = CK_DEPENDENCIES_CUSTOM; selected.ModifyOptions(CKCID_3DENTITY, 2, 0);
    CKDependenciesContext second(Context); second.SetOperationMode(CK_DEPENDENCIES_COPY); second.StartDependencies(&selected);
    ASSERT_EQ(CK_OK, body.PrepareDependencies(second)); second.StopDependencies();
    EXPECT_TRUE(second.IsDependenciesHere(body.GetID())); EXPECT_TRUE(second.IsDependenciesHere(child.GetID()));
    EXPECT_FALSE(second.IsDependenciesHere(character.GetID())); EXPECT_FALSE(second.IsDependenciesHere(exclusive.GetID()));
}

TEST_F(BodyPartJointTest, PreparePropagatesReachableObjectSaveExclusionWithoutCollectingReferences) {
    RCKCharacter character(Context); RCKKeyedAnimation exclusive(Context); JointBodyProbe body(Context);
    body.Character(&character); body.Exclusive(&exclusive); body.ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, 0);
    CKDependenciesContext dependencies(Context); dependencies.SetOperationMode(CK_DEPENDENCIES_SAVE);
    EXPECT_EQ(CKERR_INCOMPATIBLEPARAMETERS, body.PrepareDependencies(dependencies));
    EXPECT_FALSE(dependencies.IsDependenciesHere(body.GetID())); EXPECT_FALSE(dependencies.IsDependenciesHere(character.GetID()));
    EXPECT_FALSE(dependencies.IsDependenciesHere(exclusive.GetID()));
}

TEST_F(BodyPartJointTest, DirectDestructorUnregistersBodyWithoutDestroyingNonowningReferences) {
    RCKCharacter character(Context); RCKKeyedAnimation exclusive(Context);
    JointBodyProbe *body = new JointBodyProbe(Context); body->Character(&character); body->Exclusive(&exclusive);
    CK_ID id = body->GetID(); delete body; EXPECT_EQ(nullptr, Context->GetObject(id));
    EXPECT_EQ(&character, Context->GetObject(character.GetID())); EXPECT_EQ(&exclusive, Context->GetObject(exclusive.GetID()));
    exclusive.SetLength(27); EXPECT_FLOAT_EQ(27, exclusive.GetLength());
}
} // namespace
