#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

class LifetimeRemap : public CKDependenciesContext {
public:
    explicit LifetimeRemap(CKContext *context) : CKDependenciesContext(context) {}
    void Forget(CKObject &object) { m_MapID.Insert(object.GetID(), 0); }
};

class LifetimeChild : public RCKObjectAnimation {
public:
    explicit LifetimeChild(CKContext *context) : RCKObjectAnimation(context) {}
    RCKKeyedAnimation *Parent() const { return m_ParentKeyedAnimation; }
};

class KeyedAnimationLifetimeCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_SCENEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }

    void TearDown() override {
        if (Context)
            CKCloseContext(Context);
        CKShutdown();
    }

    int PartialLoadAfterNullRemap(bool fileLoad) {
        RCK3dEntity root(Context);
        LifetimeChild removed(Context), survivor(Context);
        RCKKeyedAnimation keyed(Context);
        removed.Set3dEntity(reinterpret_cast<CK3dEntity *>(&root));
        survivor.Set3dEntity(reinterpret_cast<CK3dEntity *>(&root));
        VxVector a(20, 0, 0), b(7, 8, 9), c(17, 18, 19);
        removed.AddPositionKey(0, &a);
        survivor.AddPositionKey(0, &b);
        survivor.AddPositionKey(10, &c);
        if (keyed.AddAnimation(&removed) != CK_OK || keyed.AddAnimation(&survivor) != CK_OK)
            return 11;
        keyed.SetLength(10);
        keyed.UpdateRootEntity();
        keyed.CenterAnimation(0);
        if (keyed.GetRootAnimationInternal() != &removed)
            return 12;

        LifetimeRemap remap(Context);
        remap.Forget(removed);
        if (remap.Remap(&removed) != nullptr || keyed.RemapDependencies(remap) != CK_OK)
            return 13;
        if (keyed.GetAnimationCount() != 2 || keyed.GetAnimation(0) != nullptr ||
            keyed.GetAnimation(1) != &survivor || removed.Parent() != nullptr ||
            survivor.Parent() != &keyed)
            return 14;

        // Literal chunk7/class18/data10 wire: merge-only, no list identifier.
        // It cannot accidentally replace or prune the slot produced by Remap.
        CKDWORD wire[] = {0x0007120Au, 4, 0x00100000u, 0, 7, 0x3E800000u};
        CKStateChunk chunk(CKCID_KEYEDANIMATION, nullptr);
        if (!chunk.ConvertFromBuffer(wire))
            return 15;
        survivor.SetAppData(new VxVector(2, -3, 4));
        CKFile file(Context);
        if (keyed.Load(&chunk, fileLoad ? &file : nullptr) != CK_OK)
            return 16;
        if (keyed.GetAnimationCount() != 2 || keyed.GetAnimation(0) != nullptr ||
            keyed.GetAnimation(1) != &survivor || keyed.IsMerged() != 7 ||
            keyed.GetMergeFactor() != 0.25f || survivor.GetMergeFactor() != 0.5f)
            return 17;
        if (survivor.Parent() != &keyed || removed.Parent() != nullptr ||
            survivor.GetAppData() != nullptr || Context->GetObject(removed.GetID()) != &removed)
            return 18;
        const VxVector &offset = keyed.GetRootVectorInternal();
        if (offset.x != 2 || offset.y != -3 || offset.z != 4)
            return 19;
        if (keyed.GetRootEntity() != reinterpret_cast<CK3dEntity *>(&root) ||
            keyed.GetAnimation(reinterpret_cast<CK3dEntity *>(&root)) != &survivor)
            return 20;

        VxVector sample;
        if (!survivor.EvaluatePosition(5, sample) || sample.x != 12 || sample.y != 13 || sample.z != 14)
            return 21;
        keyed.SetStep(0.5f);
        const VxMatrix &pose = root.GetLocalMatrix();
        if (pose[3][0] != 14 || pose[3][1] != 10 || pose[3][2] != 18 || keyed.GetStep() != 0.5f)
            return 22;
        return 0;
    }

    int ContextDeletionAfterNullRemap() {
        RCK3dEntity root(Context);
        LifetimeChild removed(Context), survivor(Context);
        RCKKeyedAnimation *keyed = static_cast<RCKKeyedAnimation *>(
            Context->CreateObject(CKCID_KEYEDANIMATION));
        RCKObjectAnimation *deleted = static_cast<RCKObjectAnimation *>(
            Context->CreateObject(CKCID_OBJECTANIMATION));
        CKObject *unrelated = Context->CreateObject(CKCID_3DENTITY);
        if (!keyed || !deleted || !unrelated)
            return 31;
        removed.Set3dEntity(reinterpret_cast<CK3dEntity *>(&root));
        survivor.Set3dEntity(reinterpret_cast<CK3dEntity *>(&root));
        deleted->Set3dEntity(reinterpret_cast<CK3dEntity *>(&root));
        VxVector a(20, 0, 0), b(7, 8, 9), c(17, 18, 19), d(30, 0, 0);
        removed.AddPositionKey(0, &a);
        survivor.AddPositionKey(0, &b);
        survivor.AddPositionKey(10, &c);
        deleted->AddPositionKey(0, &d);
        if (keyed->AddAnimation(&removed) != CK_OK || keyed->AddAnimation(deleted) != CK_OK ||
            keyed->AddAnimation(&survivor) != CK_OK || keyed->AddAnimation(&survivor) != CK_OK)
            return 32;
        keyed->SetLength(10);
        keyed->UpdateRootEntity();
        keyed->CenterAnimation(0);
        LifetimeRemap remap(Context);
        remap.Forget(removed);
        if (keyed->RemapDependencies(remap) != CK_OK || keyed->GetAnimation(0) != nullptr ||
            keyed->GetAnimationCount() != 4 || removed.Parent() != nullptr)
            return 33;
        if (keyed->GetAnimation(reinterpret_cast<CK3dEntity *>(&root)) != deleted)
            return 34;

        // Real factory registration is needed for Context's class notification
        // walk; a stack-constructed group has only an object ID registration.
        CKDependencies none;
        none.m_Flags = CK_DEPENDENCIES_NONE;
        const CK_ID unrelatedID = unrelated->GetID(), deletedID = deleted->GetID();
        if (Context->DestroyObject(unrelated, 0, &none) != CK_OK || Context->GetObject(unrelatedID))
            return 35;
        if (keyed->GetAnimationCount() != 3 || keyed->GetAnimation(0) != deleted ||
            keyed->GetAnimation(1) != &survivor || keyed->GetAnimation(2) != &survivor ||
            survivor.Parent() != keyed || keyed->GetRootAnimationInternal() != nullptr ||
            !keyed->IsUpToDate())
            return 36;
        if (keyed->GetAnimation(reinterpret_cast<CK3dEntity *>(&root)) != deleted)
            return 37;
        if (Context->DestroyObject(deleted, 0, &none) != CK_OK || Context->GetObject(deletedID))
            return 38;
        if (keyed->GetAnimationCount() != 2 || keyed->GetAnimation(0) != &survivor ||
            keyed->GetAnimation(1) != &survivor || survivor.Parent() != keyed ||
            keyed->GetRootAnimationInternal() != nullptr ||
            keyed->GetRootEntity() != reinterpret_cast<CK3dEntity *>(&root))
            return 39;
        if (keyed->GetAnimation(reinterpret_cast<CK3dEntity *>(&root)) != &survivor)
            return 40;
        keyed->SetStep(0.5f);
        const VxMatrix &pose = root.GetLocalMatrix();
        if (pose[3][0] != -8 || pose[3][1] != 13 || pose[3][2] != 14 ||
            Context->GetObject(removed.GetID()) != &removed || removed.Parent() != nullptr)
            return 41;
        const CK_ID keyedID = keyed->GetID();
        delete keyed;
        if (Context->GetObject(keyedID) || survivor.Parent() != nullptr ||
            Context->GetObject(survivor.GetID()) != &survivor)
            return 42;
        if (survivor.SetFrame(5) != CK_OK || root.GetLocalMatrix()[3][0] != 12)
            return 43;
        return 0;
    }
};

// A subprocess keeps the same final behavioral fixture reportable on the old
// runtime, whose legacy AppData loop faults on the legitimate remapped slot.
// Every semantic check returns a nonzero code, so surviving the Load alone
// cannot make these tests pass.
TEST_F(KeyedAnimationLifetimeCompletionTest, MemoryPartialLoadPreservesNullRemapAndRestoresLiveSiblingPose) {
    ASSERT_EXIT({
        const int result = PartialLoadAfterNullRemap(false);
        if (result)
            std::fprintf(stderr, "memory scenario failed at %d\n", result);
        std::exit(result);
    }, ::testing::ExitedWithCode(0), "");
}

TEST_F(KeyedAnimationLifetimeCompletionTest, FilePartialLoadPreservesNullRemapAndRestoresLiveSiblingPose) {
    ASSERT_EXIT({
        const int result = PartialLoadAfterNullRemap(true);
        if (result)
            std::fprintf(stderr, "file scenario failed at %d\n", result);
        std::exit(result);
    }, ::testing::ExitedWithCode(0), "");
}

TEST_F(KeyedAnimationLifetimeCompletionTest, ContextDeletionPrunesRemappedSlotsAndMarkedChildrenWithoutLosingDuplicates) {
    ASSERT_EXIT({
        const int result = ContextDeletionAfterNullRemap();
        if (result)
            std::fprintf(stderr, "deletion scenario failed at %d\n", result);
        std::exit(result);
    }, ::testing::ExitedWithCode(0), "");
}

} // namespace
