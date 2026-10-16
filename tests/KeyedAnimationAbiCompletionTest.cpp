#include <gtest/gtest.h>
#include <cstddef>
#include <type_traits>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKCharacter.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class KeyedLayoutProbe : public RCKKeyedAnimation {
public:
    explicit KeyedLayoutProbe(CKContext *context) : RCKKeyedAnimation(context) {}
    std::size_t Offset(int field) const {
        const void *addresses[] = {&m_Animations, &m_Merged, &m_MergeFactor, &m_RootAnimation, &m_Vector};
        return static_cast<const unsigned char *>(addresses[field]) - reinterpret_cast<const unsigned char *>(this);
    }
};
struct Destructions {
    int Group = 0;
    int Child = 0;
    int Root = 0;
    bool ChildHadParent = false;
};
class ObservedGroup : public RCKKeyedAnimation {
public:
    ObservedGroup(CKContext *context, Destructions &counts) : RCKKeyedAnimation(context), Counts(counts) {}
    ~ObservedGroup() override { ++Counts.Group; }
private:
    Destructions &Counts;
};
class ObservedChild : public RCKObjectAnimation {
public:
    ObservedChild(CKContext *context, Destructions &counts) : RCKObjectAnimation(context), Counts(counts) {}
    ~ObservedChild() override { ++Counts.Child; Counts.ChildHadParent = m_ParentKeyedAnimation != nullptr; }
private:
    Destructions &Counts;
};
class ObservedRoot : public RCK3dEntity {
public:
    ObservedRoot(CKContext *context, Destructions &counts) : RCK3dEntity(context), Counts(counts) {}
    ~ObservedRoot() override { ++Counts.Root; }
private:
    Destructions &Counts;
};

class KeyedAnimationAbiCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_SCENEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    CKKeyedAnimation *Group(const char *name = nullptr) {
        return CKKeyedAnimation::Cast(Context->CreateObject(CKCID_KEYEDANIMATION, const_cast<char *>(name)));
    }
    RCK3dEntity *Entity() { return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY)); }
    CKObjectAnimation *Curve(RCK3dEntity *entity, float first, float last) {
        CKObjectAnimation *child = CKObjectAnimation::Cast(Context->CreateObject(CKCID_OBJECTANIMATION));
        if (!child) return nullptr;
        child->Set3dEntity(reinterpret_cast<CK3dEntity *>(entity));
        VxVector a(first, 0, 0), b(last, 0, 0);
        child->AddPositionKey(0, &a); child->AddPositionKey(10, &b); child->SetLength(10);
        return child;
    }
    static void LoadRoot(CKObject &object, CKObject *root) {
        CKStateChunk chunk(CKCID_KEYEDANIMATION, nullptr);
        chunk.StartWrite();
        chunk.WriteIdentifier(CK_STATESAVE_ANIMATIONBODYPARTS);
        chunk.WriteObjectArray(nullptr, 0); chunk.WriteObject(root); chunk.CloseChunk();
        ASSERT_EQ(CK_OK, object.Load(&chunk, nullptr));
    }
    static void OriginalLinearWire(CKObjectAnimation &child) {
        const CKDWORD expected[] = {2, 0, 0, 0, 0, 0x41200000, 0x41200000, 0, 0};
        CKDWORD actual[10]; for (int i = 0; i < 10; ++i) actual[i] = 0xa5a5a5a5;
        CKAnimController *controller = child.GetPositionController();
        ASSERT_NE(nullptr, controller);
        ASSERT_EQ(36, controller->DumpKeysTo(nullptr)); ASSERT_EQ(36, controller->DumpKeysTo(actual));
        for (int i = 0; i < 9; ++i) EXPECT_EQ(expected[i], actual[i]) << "wire DWORD " << i;
        EXPECT_EQ(0xa5a5a5a5u, actual[9]);
    }
};

TEST_F(KeyedAnimationAbiCompletionTest, FactorySdkBridgeDispatchesBothLookupOverloadsAndInheritedClockToDistinctChannels) {
    CKKeyedAnimation *group = Group("SDK keyed");
    RCK3dEntity *firstEntity = Entity(), *secondEntity = Entity();
    ASSERT_NE(nullptr, group); ASSERT_NE(nullptr, firstEntity); ASSERT_NE(nullptr, secondEntity);
    CKObjectAnimation *first = Curve(firstEntity, 2, 10), *second = Curve(secondEntity, 10, 30);
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    ASSERT_EQ(CK_OK, group->AddAnimation(first)); ASSERT_EQ(CK_OK, group->AddAnimation(second));
    EXPECT_EQ(2, group->GetAnimationCount());
    EXPECT_EQ(first, group->GetAnimation(0)); EXPECT_EQ(second, group->GetAnimation(1));
    EXPECT_EQ(first, group->GetAnimation(reinterpret_cast<CK3dEntity *>(firstEntity)));
    EXPECT_EQ(second, group->GetAnimation(reinterpret_cast<CK3dEntity *>(secondEntity)));
    CKObject *registered = Context->GetObject(group->GetID());
    ASSERT_NE(nullptr, registered);
    RCKKeyedAnimation *implementation = static_cast<RCKKeyedAnimation *>(registered);
    CKAnimation *base = implementation;
    EXPECT_EQ(static_cast<void *>(group), static_cast<void *>(base));
    EXPECT_EQ(CKCID_KEYEDANIMATION, base->GetClassID()); EXPECT_STREQ("SDK keyed", base->GetName());
    base->SetLength(20); EXPECT_FLOAT_EQ(20, first->GetLength()); EXPECT_FLOAT_EQ(20, second->GetLength());
    base->SetFrame(5);
    EXPECT_FLOAT_EQ(.25f, base->GetStep());
    EXPECT_FLOAT_EQ(6, firstEntity->GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(20, secondEntity->GetLocalMatrix()[3][0]);
    base->SetCurrentStep(.75f);
    EXPECT_FLOAT_EQ(15, base->GetFrame());
    EXPECT_FLOAT_EQ(6, firstEntity->GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(20, secondEntity->GetLocalMatrix()[3][0]);
    base->LinkToFrameRate(TRUE, 8); EXPECT_FLOAT_EQ(16, base->GetNextFrame(125));
    base->SetMergeFactor(.25f); EXPECT_FLOAT_EQ(.25f, base->GetMergeFactor()); EXPECT_FALSE(base->IsMerged());
    ASSERT_EQ(CK_OK, group->RemoveAnimation(first)); EXPECT_EQ(1, group->GetAnimationCount());
    EXPECT_EQ(second, group->GetAnimation(0)); EXPECT_EQ(nullptr, group->GetAnimation(reinterpret_cast<CK3dEntity *>(firstEntity)));
    group->Clear(); EXPECT_EQ(0, group->GetAnimationCount());
    EXPECT_EQ(second, Context->GetObject(second->GetID()));
}

TEST_F(KeyedAnimationAbiCompletionTest, VirtualGroupDeletionDetachesDuplicateChildrenWithoutChangingTheirControllerWire) {
    CKKeyedAnimation *group = Group("Temporary group");
    RCK3dEntity *root = Entity(), *other = Entity();
    ASSERT_NE(nullptr, group); ASSERT_NE(nullptr, root); ASSERT_NE(nullptr, other);
    CKObjectAnimation *child = Curve(root, 0, 10), *second = Curve(other, 20, 40);
    ASSERT_NE(nullptr, child); ASSERT_NE(nullptr, second);
    ASSERT_EQ(CK_OK, group->AddAnimation(child)); ASSERT_EQ(CK_OK, group->AddAnimation(child));
    ASSERT_EQ(CK_OK, group->AddAnimation(second));
    CKObject *registered = Context->GetObject(group->GetID()); ASSERT_NE(nullptr, registered);
    LoadRoot(*registered, root);
    CKAnimation *base = static_cast<RCKKeyedAnimation *>(registered);
    base->SetLength(10); base->CenterAnimation(2); base->SetFrame(5);
    EXPECT_FLOAT_EQ(3, root->GetLocalMatrix()[3][0]); EXPECT_FLOAT_EQ(30, other->GetLocalMatrix()[3][0]);
    OriginalLinearWire(*child);
    const CK_ID groupId = registered->GetID(), childId = child->GetID(), secondId = second->GetID();
    delete base; EXPECT_EQ(nullptr, Context->GetObject(groupId));
    EXPECT_EQ(child, Context->GetObject(childId)); EXPECT_EQ(second, Context->GetObject(secondId));
    OriginalLinearWire(*child);
    ASSERT_EQ(CK_OK, child->SetFrame(5)); EXPECT_FLOAT_EQ(5, root->GetLocalMatrix()[3][0]);
    CKKeyedAnimation *replacement = Group("Replacement"); ASSERT_NE(nullptr, replacement);
    ASSERT_EQ(CK_OK, replacement->AddAnimation(child)); replacement->SetLength(10); replacement->SetFrame(7);
    EXPECT_FLOAT_EQ(7, root->GetLocalMatrix()[3][0]); OriginalLinearWire(*child);
    ASSERT_EQ(CK_OK, second->SetFrame(5)); EXPECT_FLOAT_EQ(30, other->GetLocalMatrix()[3][0]);
}

TEST_F(KeyedAnimationAbiCompletionTest, ContextCloseHandlesBothGroupChildDestructionOrdersExactlyOnce) {
    for (int groupFirst = 0; groupFirst < 2; ++groupFirst) {
        if (!Context) ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Destructions counts;
        ObservedGroup *group = nullptr;
        if (groupFirst) group = new ObservedGroup(Context, counts);
        ObservedRoot *root = new ObservedRoot(Context, counts);
        ObservedChild *child = new ObservedChild(Context, counts);
        if (!groupFirst) group = new ObservedGroup(Context, counts);
        child->Set3dEntity(reinterpret_cast<CK3dEntity *>(root)); VxVector position(3, 5, 7); child->AddPositionKey(0, &position); child->SetLength(10);
        ASSERT_EQ(CK_OK, group->AddAnimation(child)); ASSERT_EQ(CK_OK, group->AddAnimation(child));
        group->SetLength(10); group->SetStep(.5f);
        EXPECT_FLOAT_EQ(3, root->GetLocalMatrix()[3][0]);
        CKContext *closing = Context; Context = nullptr;
        EXPECT_EQ(CK_OK, CKCloseContext(closing));
        EXPECT_EQ(1, counts.Group); EXPECT_EQ(1, counts.Child); EXPECT_EQ(1, counts.Root);
        // Context destroys newest first. A surviving child is detached when
        // its group goes first; a child going first merely holds a borrow.
        EXPECT_EQ(groupFirst != 0, counts.ChildHadParent);
    }
}

TEST_F(KeyedAnimationAbiCompletionTest, CurrentCompilerLayoutRecordsOwnedArrayAndNonowningCacheWithoutNativePluginClaim) {
    KeyedLayoutProbe group(Context);
    const bool wide = sizeof(void *) == 8;
    const std::size_t expected[] = {wide ? 88u : 52u, wide ? 104u : 60u, wide ? 108u : 64u,
                                    wide ? 112u : 68u, wide ? 120u : 72u};
    const char *names[] = {"m_Animations", "m_Merged", "m_MergeFactor", "m_RootAnimation", "m_Vector"};
    RecordProperty("pointer_size", static_cast<int>(sizeof(void *)));
    RecordProperty("RCKKeyedAnimation_size", static_cast<int>(sizeof(RCKKeyedAnimation)));
    RecordProperty("RCKKeyedAnimation_alignment", static_cast<int>(alignof(RCKKeyedAnimation)));
    RecordProperty("RCKAnimation_size", static_cast<int>(sizeof(RCKAnimation)));
    RecordProperty("XSObjectPointerArray_size", static_cast<int>(sizeof(XSObjectPointerArray)));
    RecordProperty("XSObjectPointerArray_alignment", static_cast<int>(alignof(XSObjectPointerArray)));
    EXPECT_EQ(wide ? 136u : 84u, sizeof(RCKKeyedAnimation)); EXPECT_EQ(wide ? 8u : 4u, alignof(RCKKeyedAnimation));
    EXPECT_EQ(wide ? 88u : 52u, sizeof(RCKAnimation)); EXPECT_EQ(2 * sizeof(void *), sizeof(XSObjectPointerArray));
    EXPECT_EQ(alignof(void *), alignof(XSObjectPointerArray)); EXPECT_EQ(sizeof(RCKKeyedAnimation), sizeof(KeyedLayoutProbe));
    for (int field = 0; field < 5; ++field) {
        EXPECT_EQ(expected[field], group.Offset(field)) << names[field];
        RecordProperty(names[field], static_cast<int>(group.Offset(field)));
    }
    EXPECT_EQ(static_cast<void *>(&group), static_cast<void *>(static_cast<RCKAnimation *>(&group)));
    EXPECT_EQ(static_cast<void *>(&group), static_cast<void *>(static_cast<CKAnimation *>(&group)));
    EXPECT_EQ(static_cast<void *>(&group), static_cast<void *>(static_cast<CKObject *>(&group)));
    EXPECT_EQ(4u, sizeof(RCKKeyedAnimation::m_ClassID));
    EXPECT_FALSE((std::is_base_of<CKKeyedAnimation, RCKKeyedAnimation>::value));
    EXPECT_FALSE(std::is_copy_constructible<RCKKeyedAnimation>::value);
    EXPECT_FALSE(std::is_copy_assignable<RCKKeyedAnimation>::value);
    EXPECT_FALSE(std::is_move_constructible<RCKKeyedAnimation>::value);
    EXPECT_FALSE(std::is_move_assignable<RCKKeyedAnimation>::value);
}
} // namespace
