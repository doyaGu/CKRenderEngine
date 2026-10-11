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

class CKFileAnimationBindingTest : public ::testing::Test {
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
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context,nullptr,0,0));
    }
    void TearDown() override { if(Context) CKCloseContext(Context); CKShutdown(); }
    void AddObjectAnimation(CKFile &file, CKObjectAnimation *animation) {
        const int index=file.m_FileObjects.Size(); file.m_FileObjects.Resize(index+1);
        file.m_FileObjects[index].ObjPtr=animation; file.m_FileObjects[index].ObjectCid=CKCID_OBJECTANIMATION;
        file.m_IndexByClassId[CKCID_OBJECTANIMATION].PushBack(index);
    }
};

TEST_F(CKFileAnimationBindingTest, UnboundAnimationFindsActualEntityByExactContextName) {
    RCK3dEntity *entity = reinterpret_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY,"ExactEntity"));
    ASSERT_NE(nullptr, entity); RCKObjectAnimation animation(Context,"ExactEntity");
    CKFile file(Context); file.m_IndexByClassId.Resize(80); AddObjectAnimation(file,&animation);
    file.UpdateAndApplyAnimationsTo(nullptr); EXPECT_EQ(reinterpret_cast<CK3dEntity *>(entity),animation.Get3dEntity());
    ASSERT_EQ(1,entity->GetObjectAnimationCount()); EXPECT_EQ(&animation,entity->GetObjectAnimation(0));
}

TEST_F(CKFileAnimationBindingTest, ExistingTargetIsPreservedWhenAnotherEntityHasMatchingName) {
    RCK3dEntity existing(Context,"Existing"),other(Context,"AnimationName"); RCKObjectAnimation animation(Context,"AnimationName");
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&existing)); CKFile file(Context); file.m_IndexByClassId.Resize(80); AddObjectAnimation(file,&animation);
    file.UpdateAndApplyAnimationsTo(nullptr); EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&existing),animation.Get3dEntity()); EXPECT_EQ(0,other.GetObjectAnimationCount());
}

TEST_F(CKFileAnimationBindingTest, ExactCharacterBodyPartWinsOverGlobalName) {
    RCK3dEntity other(Context,"Arm"); RCKBodyPart body(Context,"Arm"); RCKCharacter character(Context,"Character");
    ASSERT_EQ(CK_OK,character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&body))); RCKObjectAnimation animation(Context,"Arm");
    CKFile file(Context); file.m_IndexByClassId.Resize(80); file.m_Flags=CK_LOAD_ANIMATION; AddObjectAnimation(file,&animation);
    file.UpdateAndApplyAnimationsTo(reinterpret_cast<CKCharacter *>(&character)); EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&body),animation.Get3dEntity());
    ASSERT_EQ(1,body.GetObjectAnimationCount()); EXPECT_EQ(&animation,body.GetObjectAnimation(0));
}

TEST_F(CKFileAnimationBindingTest, CharacterBodyPartWithNumericRenameSuffixIsMatched) {
    RCKBodyPart body(Context,"Arm001"); RCKCharacter character(Context,"Character");
    ASSERT_EQ(CK_OK,character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&body))); RCKObjectAnimation animation(Context,"Arm");
    CKFile file(Context); file.m_IndexByClassId.Resize(80); file.m_Flags=CK_LOAD_ANIMATION; AddObjectAnimation(file,&animation);
    file.UpdateAndApplyAnimationsTo(reinterpret_cast<CKCharacter *>(&character)); EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&body),animation.Get3dEntity());
    EXPECT_EQ(1,body.GetObjectAnimationCount());
}

TEST_F(CKFileAnimationBindingTest, NonnumericRenameSuffixDoesNotBindAnUnrelatedBodyPart) {
    RCKBodyPart body(Context,"Arm00x"); RCKCharacter character(Context,"Character");
    ASSERT_EQ(CK_OK,character.AddBodyPart(reinterpret_cast<CKBodyPart *>(&body))); RCKObjectAnimation animation(Context,"Arm");
    CKFile file(Context); file.m_IndexByClassId.Resize(80); file.m_Flags=CK_LOAD_ANIMATION; AddObjectAnimation(file,&animation);
    file.UpdateAndApplyAnimationsTo(reinterpret_cast<CKCharacter *>(&character)); EXPECT_EQ(nullptr,animation.Get3dEntity()); EXPECT_EQ(0,body.GetObjectAnimationCount());
}

TEST_F(CKFileAnimationBindingTest, UnnamedAndUnmatchedAnimationsStayUnbound) {
    RCK3dEntity entity(Context,"Different"); RCKObjectAnimation unnamed(Context,nullptr),unmatched(Context,"Absent");
    CKFile file(Context); file.m_IndexByClassId.Resize(80); AddObjectAnimation(file,&unnamed); AddObjectAnimation(file,&unmatched);
    file.UpdateAndApplyAnimationsTo(nullptr); EXPECT_EQ(nullptr,unnamed.Get3dEntity()); EXPECT_EQ(nullptr,unmatched.Get3dEntity());
    EXPECT_EQ(0,entity.GetObjectAnimationCount());
}

TEST_F(CKFileAnimationBindingTest, LoadedKeyedAnimationIsAttachedToTheRequestedCharacter) {
    RCKCharacter character(Context,"Character"); RCKKeyedAnimation animation(Context,"Walk");
    CKFile file(Context); file.m_IndexByClassId.Resize(80); file.m_Flags=CK_LOAD_ANIMATION; file.m_FileObjects.Resize(1);
    file.m_FileObjects[0].ObjPtr=&animation; file.m_FileObjects[0].ObjectCid=CKCID_KEYEDANIMATION;
    file.m_IndexByClassId[CKCID_KEYEDANIMATION].PushBack(0);
    file.UpdateAndApplyAnimationsTo(reinterpret_cast<CKCharacter *>(&character)); ASSERT_EQ(1,character.GetAnimationCount());
    EXPECT_EQ(reinterpret_cast<CKAnimation *>(&animation),character.GetAnimation(0));
}

TEST_F(CKFileAnimationBindingTest, CharacterAttachmentsAreSkippedWhenAnimationFlagIsAbsent) {
    RCKCharacter character(Context,"Character"); RCKKeyedAnimation animation(Context,"Walk");
    CKFile file(Context); file.m_IndexByClassId.Resize(80); file.m_Flags=CK_LOAD_GEOMETRY; file.m_FileObjects.Resize(1);
    file.m_FileObjects[0].ObjPtr=&animation; file.m_FileObjects[0].ObjectCid=CKCID_KEYEDANIMATION;
    file.m_IndexByClassId[CKCID_KEYEDANIMATION].PushBack(0);
    file.UpdateAndApplyAnimationsTo(reinterpret_cast<CKCharacter *>(&character)); EXPECT_EQ(0,character.GetAnimationCount());
}

} // namespace
