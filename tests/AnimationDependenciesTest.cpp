#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKAnimation.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKCharacter.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class AnimationDependencyProbe : public RCKAnimation {
public:
    AnimationDependencyProbe(CKContext *context, CKSTRING name)
        : RCKAnimation(context, name) {}
    void References(RCK3dEntity *root, RCKCharacter *character) {
        m_RootEntity = root;
        m_Character = character;
    }
};

class AnimationRemapContext : public CKDependenciesContext {
public:
    explicit AnimationRemapContext(CKContext *context) : CKDependenciesContext(context) {}
    void Map(CKObject *original, CKObject *replacement) {
        m_MapID.Insert(original->GetID(), replacement ? replacement->GetID() : 0);
    }
};

class AnimationDependenciesTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override {
        if (Context)
            CKCloseContext(Context);
        CKShutdown();
    }
    static void Settings(RCKAnimation &animation) {
        animation.SetFlags(0x00120515u);
        animation.LinkToFrameRate(TRUE, 23.0f);
        animation.SetLength(51.0f);
        animation.SetStep(0.375f);
    }
    static void ExpectSettings(RCKAnimation &animation) {
        EXPECT_EQ(0x00120515u, animation.GetFlags());
        EXPECT_FLOAT_EQ(23.0f, animation.GetLinkedFrameRate());
        EXPECT_FLOAT_EQ(51.0f, animation.GetLength());
        EXPECT_FLOAT_EQ(0.375f, animation.GetStep());
    }
};

TEST_F(AnimationDependenciesTest, CopyRestoresFieldsAndObjectStateWithoutImplicitlyRemappingReferences) {
    RCK3dEntity root(Context), replacementRoot(Context);
    RCKCharacter character(Context), replacementCharacter(Context);
    int appData = 17;
    AnimationDependencyProbe source(Context, "Source"), destination(Context, "Destination");
    source.References(&root, &character);
    Settings(source);
    source.Show(CKHIERARCHICALHIDE);
    source.SetAppData(&appData);
    destination.ModifyObjectFlags(CK_OBJECT_PRIVATE | CK_OBJECT_UPTODATE, 0);
    AnimationRemapContext dependencies(Context);
    dependencies.SetOperationMode(CK_DEPENDENCIES_COPY);
    dependencies.Map(&root, &replacementRoot);
    dependencies.Map(&character, &replacementCharacter);
    CKDependencies options;
    CKCopyDefaultClassDependencies(options, CK_DEPENDENCIES_COPY);
    options.m_Flags = CK_DEPENDENCIES_CUSTOM;
    options.ModifyOptions(CKCID_OBJECT, CK_DEPENDENCIES_COPY_OBJECT_NAME,
                         CK_DEPENDENCIES_COPY_OBJECT_UNIQUENAME);
    dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
    dependencies.StopDependencies();
    ExpectSettings(destination);
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&root), destination.GetRootEntity());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), destination.GetCharacter());
    EXPECT_STREQ("Source", destination.GetName());
    EXPECT_EQ(&appData, destination.GetAppData());
    EXPECT_FALSE(destination.IsVisible());
    EXPECT_NE(0u, destination.GetObjectFlags() & CK_OBJECT_HIERACHICALHIDE);
    EXPECT_EQ(CK_OBJECT_PRIVATE | CK_OBJECT_UPTODATE,
              destination.GetObjectFlags() & (CK_OBJECT_PRIVATE | CK_OBJECT_UPTODATE));
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&root), source.GetRootEntity());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), source.GetCharacter());
}

TEST_F(AnimationDependenciesTest, CopyWithNoNameDependencyKeepsDestinationNameAndClearsOldReferences) {
    RCK3dEntity root(Context);
    RCKCharacter character(Context);
    AnimationDependencyProbe source(Context, "Source"), destination(Context, "Destination");
    Settings(source);
    source.Show(CKHIDE);
    destination.References(&root, &character);
    CKDependenciesContext dependencies(Context);
    CKDependencies options;
    options.m_Flags = CK_DEPENDENCIES_NONE;
    dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
    dependencies.StopDependencies();
    ExpectSettings(destination);
    EXPECT_STREQ("Destination", destination.GetName());
    EXPECT_EQ(nullptr, destination.GetRootEntity());
    EXPECT_EQ(nullptr, destination.GetCharacter());
    EXPECT_FALSE(destination.IsVisible());
    EXPECT_EQ(&root, Context->GetObject(root.GetID()));
    EXPECT_EQ(&character, Context->GetObject(character.GetID()));
}

TEST_F(AnimationDependenciesTest, SelfCopyPreservesStateNameAndApplicationData) {
    RCK3dEntity root(Context);
    RCKCharacter character(Context);
    int appData = 31;
    AnimationDependencyProbe animation(Context, "Self");
    animation.References(&root, &character);
    Settings(animation);
    animation.Show(CKHIDE);
    animation.SetAppData(&appData);
    CKDependenciesContext dependencies(Context);
    dependencies.SetOperationMode(CK_DEPENDENCIES_COPY);
    ASSERT_EQ(CK_OK, animation.Copy(animation, dependencies));
    ExpectSettings(animation);
    EXPECT_STREQ("Self", animation.GetName());
    EXPECT_EQ(&appData, animation.GetAppData());
    EXPECT_FALSE(animation.IsVisible());
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&root), animation.GetRootEntity());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), animation.GetCharacter());
}

TEST_F(AnimationDependenciesTest, RemapReplacesBothReferencesThroughActualContextObjects) {
    RCK3dEntity root(Context), replacementRoot(Context);
    RCKCharacter character(Context), replacementCharacter(Context);
    AnimationDependencyProbe animation(Context, "Remap");
    animation.References(&root, &character);
    Settings(animation);
    AnimationRemapContext dependencies(Context);
    dependencies.Map(&root, &replacementRoot);
    dependencies.Map(&character, &replacementCharacter);
    ASSERT_EQ(CK_OK, animation.RemapDependencies(dependencies));
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&replacementRoot), animation.GetRootEntity());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&replacementCharacter), animation.GetCharacter());
    ExpectSettings(animation);
    EXPECT_EQ(&root, Context->GetObject(root.GetID()));
    EXPECT_EQ(&character, Context->GetObject(character.GetID()));
}

TEST_F(AnimationDependenciesTest, RemapKeepsUnmappedAndNullReferences) {
    RCK3dEntity root(Context);
    RCKCharacter character(Context);
    AnimationDependencyProbe animation(Context, "Unmapped");
    animation.References(&root, &character);
    Settings(animation);
    CKDependenciesContext dependencies(Context);
    ASSERT_EQ(CK_OK, animation.RemapDependencies(dependencies));
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&root), animation.GetRootEntity());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), animation.GetCharacter());
    animation.References(nullptr, nullptr);
    ASSERT_EQ(CK_OK, animation.RemapDependencies(dependencies));
    EXPECT_EQ(nullptr, animation.GetRootEntity());
    EXPECT_EQ(nullptr, animation.GetCharacter());
    ExpectSettings(animation);
}

TEST_F(AnimationDependenciesTest, ExplicitZeroRemapsClearReferencesWithoutDestroyingTheirTargets) {
    RCK3dEntity root(Context);
    RCKCharacter character(Context);
    AnimationDependencyProbe animation(Context, "RemovedReferences");
    animation.References(&root, &character);
    Settings(animation);
    AnimationRemapContext dependencies(Context);
    dependencies.Map(&root, nullptr);
    dependencies.Map(&character, nullptr);
    ASSERT_EQ(CK_OK, animation.RemapDependencies(dependencies));
    EXPECT_EQ(nullptr, animation.GetRootEntity());
    EXPECT_EQ(nullptr, animation.GetCharacter());
    ExpectSettings(animation);
    EXPECT_EQ(&root, Context->GetObject(root.GetID()));
    EXPECT_EQ(&character, Context->GetObject(character.GetID()));
}

TEST_F(AnimationDependenciesTest, CheckPreDeletionClearsOnlyReferencesMarkedForDeletion) {
    RCK3dEntity root(Context);
    RCKCharacter character(Context);
    AnimationDependencyProbe animation(Context, "DeletionMarks");
    Settings(animation);
    for (int mask = 0; mask < 4; ++mask) {
        SCOPED_TRACE(mask);
        root.ModifyObjectFlags(CK_OBJECT_PRIVATE, CK_OBJECT_TOBEDELETED);
        character.ModifyObjectFlags(CK_OBJECT_UPTODATE, CK_OBJECT_TOBEDELETED);
        if (mask & 1)
            root.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
        if (mask & 2)
            character.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
        animation.References(&root, &character);
        animation.CheckPreDeletion();
        EXPECT_EQ((mask & 1) ? nullptr : reinterpret_cast<CK3dEntity *>(&root), animation.GetRootEntity());
        EXPECT_EQ((mask & 2) ? nullptr : reinterpret_cast<CKCharacter *>(&character), animation.GetCharacter());
        ExpectSettings(animation);
    }
    root.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
    character.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
    animation.References(nullptr, nullptr);
    animation.CheckPreDeletion();
    EXPECT_EQ(nullptr, animation.GetRootEntity());
    EXPECT_EQ(nullptr, animation.GetCharacter());
}

TEST_F(AnimationDependenciesTest, ActualContextDeletionNotifiesAnimationBeforeTargetDestruction) {
    CKObject *root = Context->CreateObject(CKCID_3DENTITY, "Root");
    CKObject *character = Context->CreateObject(CKCID_CHARACTER, "Character");
    ASSERT_NE(nullptr, root);
    ASSERT_NE(nullptr, character);
    const CK_ID rootId = root->GetID(), characterId = character->GetID();
    // Factory creation finishes the class-list registration used by deletion
    // notifications; a stack-constructed object has only its context ID.
    RCKAnimation *animation = static_cast<RCKAnimation *>(Context->CreateObject(CKCID_ANIMATION, "LiveAnimation"));
    ASSERT_NE(nullptr, animation);
    CKStateChunk references(CKCID_ANIMATION, nullptr);
    references.StartWrite();
    references.WriteIdentifier(CK_STATESAVE_ANIMATIONBODYPARTS);
    references.WriteInt(0);
    references.WriteObject(root);
    references.WriteIdentifier(CK_STATESAVE_ANIMATIONCHARACTER);
    references.WriteObject(character);
    references.CloseChunk();
    ASSERT_EQ(CK_OK, animation->Load(&references, nullptr));
    Settings(*animation);
    ASSERT_EQ(CK_OK, Context->DestroyObject(root));
    EXPECT_EQ(nullptr, Context->GetObject(rootId));
    EXPECT_EQ(nullptr, animation->GetRootEntity());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(character), animation->GetCharacter());
    ASSERT_EQ(CK_OK, Context->DestroyObject(character));
    EXPECT_EQ(nullptr, Context->GetObject(characterId));
    EXPECT_EQ(nullptr, animation->GetCharacter());
    ExpectSettings(*animation);
}

TEST_F(AnimationDependenciesTest, AnimationRegistersNoOwnDependencyOptionsWhileObjectOptionsRemainAvailable) {
    CKClassDesc *description = CKGetClassDesc(CKCID_ANIMATION);
    ASSERT_NE(nullptr, description);
    ASSERT_NE(nullptr, description->DependsCountFct);
    ASSERT_NE(nullptr, description->DependsFct);
    EXPECT_EQ(CKCID_SCENEOBJECT, description->Parent);
    const int modes[] = {CK_DEPENDENCIES_COPY, CK_DEPENDENCIES_DELETE, CK_DEPENDENCIES_REPLACE,
                         CK_DEPENDENCIES_SAVE, CK_DEPENDENCIES_BUILD};
    for (int mode : modes) {
        SCOPED_TRACE(mode);
        EXPECT_EQ(0, RCKAnimation::GetDependenciesCount(mode));
        EXPECT_EQ(0, description->DependsCountFct(mode));
        EXPECT_EQ(nullptr, RCKAnimation::GetDependencies(0, mode));
        EXPECT_EQ(nullptr, description->DependsFct(0, mode));
    }
    EXPECT_EQ(2, CKObject::GetDependenciesCount(CK_DEPENDENCIES_COPY));
    EXPECT_EQ(1, CKObject::GetDependenciesCount(CK_DEPENDENCIES_SAVE));
    EXPECT_STREQ("Name", CKObject::GetDependencies(0, CK_DEPENDENCIES_COPY));
}

TEST_F(AnimationDependenciesTest, InheritedObjectPrepareCollectsSelfAndHonorsSaveExclusion) {
    RCK3dEntity root(Context);
    RCKCharacter character(Context);
    AnimationDependencyProbe animation(Context, "Prepare");
    animation.References(&root, &character);
    CKDependenciesContext dependencies(Context);
    dependencies.SetOperationMode(CK_DEPENDENCIES_COPY);
    ASSERT_EQ(CK_OK, animation.PrepareDependencies(dependencies));
    EXPECT_TRUE(dependencies.IsDependenciesHere(animation.GetID()));
    EXPECT_FALSE(dependencies.IsDependenciesHere(root.GetID()));
    EXPECT_FALSE(dependencies.IsDependenciesHere(character.GetID()));
    // There is no Animation Prepare override. Its registered zero own-option
    // count does not suppress the ordinary Object save exclusion contract.
    animation.ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, 0);
    CKDependenciesContext saving(Context);
    saving.SetOperationMode(CK_DEPENDENCIES_SAVE);
    EXPECT_EQ(CKERR_INCOMPATIBLEPARAMETERS, animation.PrepareDependencies(saving));
    EXPECT_FALSE(saving.IsDependenciesHere(animation.GetID()));
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&root), animation.GetRootEntity());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), animation.GetCharacter());
}

} // namespace
