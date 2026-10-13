#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKCharacter.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class DependenciesProbe : public CKDependenciesContext {
public:
    explicit DependenciesProbe(CKContext *context) : CKDependenciesContext(context) {}
    void Map(CKObject &from, CKObject &to) { m_MapID.Insert(from.GetID(), to.GetID()); }
};

class DependencyAnimation : public RCKObjectAnimation {
public:
    explicit DependencyAnimation(CKContext *context) : RCKObjectAnimation(context, nullptr) {}
    RCKKeyedAnimation *Parent() const { return m_ParentKeyedAnimation; }
    CKERROR PrepareDependencies(CKDependenciesContext &context) override {
        ++PrepareCalls;
        return RCKObjectAnimation::PrepareDependencies(context);
    }
    int PrepareCalls = 0;
};

class KeyedDependenciesProbe : public RCKKeyedAnimation {
public:
    explicit KeyedDependenciesProbe(CKContext *context) : RCKKeyedAnimation(context, nullptr) {}
    void State(RCK3dEntity *root, RCKCharacter *character, CKBOOL merged,
               float factor, const VxVector &vector) {
        m_RootEntity = root;
        m_Character = character;
        m_Merged = merged;
        m_MergeFactor = factor;
        m_Vector = vector;
    }
};

class KeyedAnimationDependenciesTest : public ::testing::Test {
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
    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static void None(CKDependencies &options, CKDependenciesContext &context) {
        options.m_Flags = CK_DEPENDENCIES_NONE;
        context.StartDependencies(&options);
    }
};

TEST_F(KeyedAnimationDependenciesTest, PrepareCollectsChildrenWithoutClassMaskAndBalancesFinishStack) {
    DependencyAnimation first(Context), second(Context);
    KeyedDependenciesProbe keyed(Context);
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&first));
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&second));
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&first));
    const CK_DEPENDENCIES_OPMODE modes[] = {
        CK_DEPENDENCIES_BUILD, CK_DEPENDENCIES_COPY, CK_DEPENDENCIES_DELETE, CK_DEPENDENCIES_SAVE
    };
    for (int index = 0; index < 4; ++index) {
        SCOPED_TRACE(index);
        CKDependencies options;
        DependenciesProbe dependencies(Context);
        dependencies.SetOperationMode(modes[index]);
        None(options, dependencies);
        ASSERT_EQ(CK_OK, keyed.PrepareDependencies(dependencies));
        EXPECT_EQ(3, dependencies.GetDependenciesMap().Size());
        EXPECT_TRUE(dependencies.IsDependenciesHere(keyed.GetID()));
        EXPECT_TRUE(dependencies.IsDependenciesHere(first.GetID()));
        EXPECT_TRUE(dependencies.IsDependenciesHere(second.GetID()));
        EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty());
        EXPECT_EQ((index + 1) * 2, first.PrepareCalls);
        EXPECT_EQ(index + 1, second.PrepareCalls);
        EXPECT_EQ(CKERR_ALREADYPRESENT, keyed.PrepareDependencies(dependencies));
        EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty());
        EXPECT_EQ((index + 1) * 2, first.PrepareCalls);
        dependencies.StopDependencies();
    }
}

TEST_F(KeyedAnimationDependenciesTest, SavePrepareRejectsExcludedParentBeforeTraversingChildren) {
    DependencyAnimation child(Context);
    KeyedDependenciesProbe keyed(Context);
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&child));
    keyed.ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, 0);
    DependenciesProbe dependencies(Context);
    dependencies.SetOperationMode(CK_DEPENDENCIES_SAVE);
    EXPECT_EQ(CKERR_INCOMPATIBLEPARAMETERS, keyed.PrepareDependencies(dependencies));
    EXPECT_EQ(0, child.PrepareCalls);
    EXPECT_EQ(0, dependencies.GetDependenciesMap().Size());
    EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty());
}

TEST_F(KeyedAnimationDependenciesTest, DynamicPrepareCollectsDynamicChildAndSkipsStaticChildAsNative) {
    DependencyAnimation dynamicChild(Context), staticChild(Context);
    KeyedDependenciesProbe keyed(Context);
    keyed.ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0);
    dynamicChild.ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0);
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&staticChild));
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&dynamicChild));
    DependenciesProbe dependencies(Context);
    ASSERT_EQ(CK_OK, keyed.PrepareDependencies(dependencies));
    EXPECT_EQ(2, dependencies.GetDependenciesMap().Size());
    EXPECT_TRUE(dependencies.IsDependenciesHere(keyed.GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(dynamicChild.GetID()));
    EXPECT_FALSE(dependencies.IsDependenciesHere(staticChild.GetID()));
    EXPECT_EQ(1, dynamicChild.PrepareCalls);
    EXPECT_EQ(1, staticChild.PrepareCalls);
    EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty());
}

TEST_F(KeyedAnimationDependenciesTest, CopyKeepsShallowSourceReferencesAndDetachesOverwrittenOwnedChild) {
    RCK3dEntity root(Context);
    RCKCharacter character(Context);
    DependencyAnimation sourceChild(Context), oldChild(Context);
    KeyedDependenciesProbe source(Context), destination(Context);
    sourceChild.Set3dEntity(reinterpret_cast<CK3dEntity *>(&root));
    ASSERT_EQ(CK_OK, source.AddAnimation(&sourceChild));
    ASSERT_EQ(CK_OK, destination.AddAnimation(&oldChild));
    source.State(&root, &character, TRUE, 0.375f, VxVector(2, -3, 4));
    source.SetLength(18);
    source.SetStep(0.25f);
    ASSERT_EQ(&sourceChild, source.GetAnimation(reinterpret_cast<CK3dEntity *>(&root)));
    DependenciesProbe dependencies(Context);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
    ASSERT_EQ(1, destination.GetAnimationCount());
    EXPECT_EQ(&sourceChild, destination.GetAnimation(0));
    EXPECT_EQ(&source, sourceChild.Parent());
    EXPECT_EQ(nullptr, oldChild.Parent());
    EXPECT_EQ(nullptr, destination.GetRootAnimationInternal());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&root), destination.GetRootEntity());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), destination.GetCharacter());
    EXPECT_FLOAT_EQ(18, destination.GetLength());
    EXPECT_FLOAT_EQ(0.25f, destination.GetStep());
    EXPECT_TRUE(destination.IsMerged());
    EXPECT_FLOAT_EQ(0.375f, destination.GetMergeFactor());
    EXPECT_FLOAT_EQ(2, destination.GetRootVectorInternal().x);
    EXPECT_FLOAT_EQ(-3, destination.GetRootVectorInternal().y);
    EXPECT_FLOAT_EQ(4, destination.GetRootVectorInternal().z);
    // The array storage is copied, while child objects are references until
    // the dependency remap stage replaces them with created copies.
    ASSERT_EQ(CK_OK, destination.RemoveAnimation(&sourceChild));
    EXPECT_EQ(1, source.GetAnimationCount());
    EXPECT_EQ(&source, sourceChild.Parent());
}

TEST_F(KeyedAnimationDependenciesTest, CopyPreservesOtherParentOfAnOverwrittenChild) {
    DependencyAnimation oldChild(Context), sourceChild(Context);
    KeyedDependenciesProbe source(Context), destination(Context), other(Context);
    ASSERT_EQ(CK_OK, source.AddAnimation(&sourceChild));
    ASSERT_EQ(CK_OK, destination.AddAnimation(&oldChild));
    ASSERT_EQ(CK_OK, other.AddAnimation(&oldChild));
    DependenciesProbe dependencies(Context);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
    EXPECT_EQ(&other, oldChild.Parent());
    EXPECT_EQ(&source, sourceChild.Parent());
}

TEST_F(KeyedAnimationDependenciesTest, ActualDependencyCopyBindsClonedChildAndPreservesCenteredPose) {
    RCK3dEntity *entity = reinterpret_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY, "Target"));
    RCKObjectAnimation *child = reinterpret_cast<RCKObjectAnimation *>(Context->CreateObject(CKCID_OBJECTANIMATION, "Child"));
    RCKKeyedAnimation *source = reinterpret_cast<RCKKeyedAnimation *>(Context->CreateObject(CKCID_KEYEDANIMATION, "Source"));
    ASSERT_NE(nullptr, entity);
    ASSERT_NE(nullptr, child);
    ASSERT_NE(nullptr, source);
    VxVector position(7, 8, 9);
    child->Set3dEntity(reinterpret_cast<CK3dEntity *>(entity));
    child->AddPositionKey(5, &position);
    ASSERT_EQ(CK_OK, source->AddAnimation(child));
    source->UpdateRootEntity();
    source->CenterAnimation(5);
    ASSERT_EQ(child, source->GetRootAnimationInternal());
    CKDependencies options;
    DependenciesProbe dependencies(Context);
    None(options, dependencies);
    CK_ID sourceId = source->GetID();
    dependencies.AddObjects(&sourceId, 1);
    dependencies.Copy();
    RCKKeyedAnimation *copy = static_cast<RCKKeyedAnimation *>(dependencies.Remap(source));
    RCKObjectAnimation *copiedChild = static_cast<RCKObjectAnimation *>(dependencies.Remap(child));
    ASSERT_NE(nullptr, copy);
    ASSERT_NE(nullptr, copiedChild);
    ASSERT_NE(source, copy);
    ASSERT_NE(child, copiedChild);
    ASSERT_EQ(1, copy->GetAnimationCount());
    EXPECT_EQ(copiedChild, copy->GetAnimation(0));
    EXPECT_EQ(copiedChild, copy->GetAnimation(reinterpret_cast<CK3dEntity *>(entity)));
    ASSERT_EQ(CK_OK, copiedChild->SetFrame(5, reinterpret_cast<CKKeyedAnimation *>(copy)));
    EXPECT_FLOAT_EQ(0, entity->GetLocalMatrix()[3][0]);
    EXPECT_FLOAT_EQ(0, entity->GetLocalMatrix()[3][1]);
    EXPECT_FLOAT_EQ(0, entity->GetLocalMatrix()[3][2]);
    // Remapping the clone must leave the original channel attached to Source.
    ASSERT_EQ(CK_OK, child->SetFrame(5, reinterpret_cast<CKKeyedAnimation *>(source)));
    EXPECT_FLOAT_EQ(0, entity->GetLocalMatrix()[3][0]);
    EXPECT_FLOAT_EQ(0, entity->GetLocalMatrix()[3][1]);
    EXPECT_FLOAT_EQ(0, entity->GetLocalMatrix()[3][2]);
    dependencies.StopDependencies();
}

TEST_F(KeyedAnimationDependenciesTest, RemapReplacesRootCharacterAndChildAndInvalidatesCachedChannel) {
    RCK3dEntity oldRoot(Context), newRoot(Context);
    RCKCharacter oldCharacter(Context), newCharacter(Context);
    DependencyAnimation oldChild(Context), newChild(Context);
    KeyedDependenciesProbe keyed(Context);
    oldChild.Set3dEntity(reinterpret_cast<CK3dEntity *>(&oldRoot));
    newChild.Set3dEntity(reinterpret_cast<CK3dEntity *>(&newRoot));
    VxVector position(7, 8, 9);
    newChild.AddPositionKey(5, &position);
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&oldChild));
    keyed.State(&oldRoot, &oldCharacter, FALSE, 0.25f, VxVector(2, -3, 4));
    ASSERT_EQ(&oldChild, keyed.GetAnimation(reinterpret_cast<CK3dEntity *>(&oldRoot)));
    DependenciesProbe dependencies(Context);
    dependencies.Map(oldRoot, newRoot);
    dependencies.Map(oldCharacter, newCharacter);
    dependencies.Map(oldChild, newChild);
    ASSERT_EQ(CK_OK, keyed.RemapDependencies(dependencies));
    EXPECT_EQ(nullptr, oldChild.Parent());
    EXPECT_EQ(&keyed, newChild.Parent());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&newRoot), keyed.GetRootEntity());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&newCharacter), keyed.GetCharacter());
    EXPECT_EQ(&newChild, keyed.GetAnimation(0));
    EXPECT_EQ(&newChild, keyed.GetAnimation(reinterpret_cast<CK3dEntity *>(&newRoot)));
    ASSERT_EQ(CK_OK, newChild.SetFrame(5, reinterpret_cast<CKKeyedAnimation *>(&keyed)));
    EXPECT_FLOAT_EQ(9, newRoot.GetLocalMatrix()[3][0]);
    EXPECT_FLOAT_EQ(5, newRoot.GetLocalMatrix()[3][1]);
    EXPECT_FLOAT_EQ(13, newRoot.GetLocalMatrix()[3][2]);
}

TEST_F(KeyedAnimationDependenciesTest, RemapWithoutMappingsPreservesSharedParentAndRootCache) {
    RCK3dEntity root(Context);
    DependencyAnimation child(Context);
    KeyedDependenciesProbe source(Context), destination(Context);
    child.Set3dEntity(reinterpret_cast<CK3dEntity *>(&root));
    ASSERT_EQ(CK_OK, source.AddAnimation(&child));
    source.State(&root, nullptr, FALSE, 0.5f, VxVector(2, -3, 4));
    DependenciesProbe dependencies(Context);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
    ASSERT_EQ(&child, destination.GetAnimation(reinterpret_cast<CK3dEntity *>(&root)));
    ASSERT_EQ(CK_OK, destination.RemapDependencies(dependencies));
    EXPECT_EQ(&child, destination.GetRootAnimationInternal());
    EXPECT_EQ(&source, child.Parent());
}

TEST_F(KeyedAnimationDependenciesTest, ReferenceQueryRequiresAnimationClassAndActualListMembership) {
    DependencyAnimation child(Context), other(Context);
    RCK3dEntity root(Context);
    KeyedDependenciesProbe keyed(Context);
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&child));
    keyed.State(&root, nullptr, FALSE, 0.5f, VxVector(0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(keyed.IsObjectUsed(&child, CKCID_OBJECTANIMATION));
    EXPECT_FALSE(keyed.IsObjectUsed(&other, CKCID_OBJECTANIMATION));
    EXPECT_FALSE(keyed.IsObjectUsed(&child, CKCID_OBJECT));
    EXPECT_FALSE(keyed.IsObjectUsed(&root, CKCID_3DENTITY));
}

TEST_F(KeyedAnimationDependenciesTest, DeletionPrunesMarkedChildAndRepairsRootCacheAndBaseReferences) {
    RCK3dEntity oldRoot(Context), keptRoot(Context);
    RCKCharacter character(Context);
    DependencyAnimation deleted(Context), kept(Context);
    KeyedDependenciesProbe keyed(Context);
    deleted.Set3dEntity(reinterpret_cast<CK3dEntity *>(&oldRoot));
    kept.Set3dEntity(reinterpret_cast<CK3dEntity *>(&keptRoot));
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&deleted));
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&kept));
    keyed.State(&oldRoot, &character, FALSE, 0.5f, VxVector(3, 4, 5));
    ASSERT_EQ(&deleted, keyed.GetAnimation(reinterpret_cast<CK3dEntity *>(&oldRoot)));
    keyed.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    deleted.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
    oldRoot.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
    character.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
    keyed.CheckPreDeletion();
    ASSERT_EQ(1, keyed.GetAnimationCount());
    EXPECT_EQ(&kept, keyed.GetAnimation(0));
    EXPECT_TRUE(keyed.IsUpToDate());
    EXPECT_EQ(nullptr, keyed.GetRootAnimationInternal());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&keptRoot), keyed.GetRootEntity());
    EXPECT_EQ(nullptr, keyed.GetCharacter());
    EXPECT_EQ(&kept, keyed.GetAnimation(reinterpret_cast<CK3dEntity *>(&keptRoot)));
    deleted.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
    oldRoot.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
    character.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
}

TEST_F(KeyedAnimationDependenciesTest, DeletionWithoutMarkedChildKeepsPlaybackCacheAndUpToDateFlag) {
    RCK3dEntity root(Context);
    DependencyAnimation child(Context);
    KeyedDependenciesProbe keyed(Context);
    child.Set3dEntity(reinterpret_cast<CK3dEntity *>(&root));
    ASSERT_EQ(CK_OK, keyed.AddAnimation(&child));
    keyed.State(&root, nullptr, FALSE, 0.5f, VxVector(0.0f, 0.0f, 0.0f));
    ASSERT_EQ(&child, keyed.GetAnimation(reinterpret_cast<CK3dEntity *>(&root)));
    keyed.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    keyed.CheckPreDeletion();
    EXPECT_EQ(1, keyed.GetAnimationCount());
    EXPECT_EQ(&child, keyed.GetRootAnimationInternal());
    EXPECT_TRUE(keyed.IsUpToDate());
}

TEST_F(KeyedAnimationDependenciesTest, MetadataDeclaresNoClassOptionsInEveryDependencyMode) {
    for (int mode = CK_DEPENDENCIES_COPY; mode <= CK_DEPENDENCIES_BUILD; ++mode) {
        EXPECT_EQ(0, RCKKeyedAnimation::GetDependenciesCount(mode));
        EXPECT_EQ(nullptr, RCKKeyedAnimation::GetDependencies(0, mode));
    }
}

} // namespace
