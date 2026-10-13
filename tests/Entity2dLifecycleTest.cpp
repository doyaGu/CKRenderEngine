#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKRenderObject.h"
#include "RCK2dEntity.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class Entity2dLifecycleProbe : public RCK2dEntity {
public:
    static VxRect *Homogeneous(RCK2dEntity &o) { return o.*(&Entity2dLifecycleProbe::m_HomogeneousRect); }
    static VxRect Vertices(RCK2dEntity &o) { return o.*(&Entity2dLifecycleProbe::m_VtxPos); }
    static VxRect ClippedSource(RCK2dEntity &o) { return o.*(&Entity2dLifecycleProbe::m_SrcRect); }
    static void RecordLayout(RCK2dEntity &o) {
        const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(&o);
        std::size_t previousEnd = sizeof(RCKRenderObject);
#define RECORD_FIELD(field) \
        { auto member = &Entity2dLifecycleProbe::field; \
          const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&(o.*member)); \
          const std::size_t offset = static_cast<std::size_t>(address - begin); \
          EXPECT_GE(offset, previousEnd) << #field; \
          EXPECT_EQ(0u, address % alignof(decltype(o.*member))) << #field; \
          EXPECT_LE(offset + sizeof(o.*member), sizeof(RCK2dEntity)) << #field; \
          ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(offset)); \
          ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(o.*member))); \
          previousEnd = offset + sizeof(o.*member); }
        RECORD_FIELD(m_Rect); RECORD_FIELD(m_HomogeneousRect); RECORD_FIELD(m_VtxPos);
        RECORD_FIELD(m_SrcRect); RECORD_FIELD(m_Flags); RECORD_FIELD(m_Parent);
        RECORD_FIELD(m_Material); RECORD_FIELD(m_Children); RECORD_FIELD(m_SourceRect);
        RECORD_FIELD(m_ZOrder);
#undef RECORD_FIELD
        ::testing::Test::RecordProperty("m_ClassID_value", static_cast<int>(m_ClassID));
        ::testing::Test::RecordProperty("m_ClassID_size", static_cast<int>(sizeof(m_ClassID)));
        ::testing::Test::RecordProperty("SDL2dEntitySize", static_cast<int>(sizeof(RCK2dEntity)));
        ::testing::Test::RecordProperty("SDL2dEntityAlignment", static_cast<int>(alignof(RCK2dEntity)));
        ::testing::Test::RecordProperty("SDLRenderObjectSize", static_cast<int>(sizeof(RCKRenderObject)));
        ::testing::Test::RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
    }
};

void ExpectRect(const VxRect &r, float l, float t, float right, float bottom) {
    EXPECT_FLOAT_EQ(l, r.left); EXPECT_FLOAT_EQ(t, r.top);
    EXPECT_FLOAT_EQ(right, r.right); EXPECT_FLOAT_EQ(bottom, r.bottom);
}

class Entity2dLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    RCK2dEntity *Entity() { return static_cast<RCK2dEntity *>(Context->CreateObject(CKCID_2DENTITY)); }
};

TEST_F(Entity2dLifecycleTest, RegisteredIdentityDeterministicDefaultRectanglesAndActualLayout) {
    RCK2dEntity *e = Entity(); ASSERT_NE(nullptr, e);
    CKObject *base = e;
    EXPECT_EQ(static_cast<void *>(e), static_cast<void *>(base));
    EXPECT_EQ(27, base->GetClassID()); EXPECT_EQ(CKCID_2DENTITY, RCK2dEntity::m_ClassID);
    EXPECT_EQ(base, Context->GetObject(base->GetID()));
    EXPECT_TRUE(CKIsChildClassOf(base, CKCID_RENDEROBJECT));
    EXPECT_STREQ("2dEntity", RCK2dEntity::GetClassName());
    EXPECT_EQ(0x280501u, e->GetFlags());
    EXPECT_EQ(nullptr, e->GetParent()); EXPECT_EQ(nullptr, e->GetMaterial());
    EXPECT_EQ(nullptr, Entity2dLifecycleProbe::Homogeneous(*e));
    EXPECT_EQ(0, e->GetChildrenCount()); EXPECT_EQ(0, e->GetZOrder());
    EXPECT_TRUE(e->IsRootObject());
    VxRect r; e->GetRect(r); ExpectRect(r, 0, 0, 0, 0);
    e->GetSourceRect(r); ExpectRect(r, 0, 0, 1, 1);
    ExpectRect(Entity2dLifecycleProbe::Vertices(*e), 0, 0, 0, 0);
    ExpectRect(Entity2dLifecycleProbe::ClippedSource(*e), 0, 0, 0, 0);
    Entity2dLifecycleProbe::RecordLayout(*e);
}

TEST_F(Entity2dLifecycleTest, DirectFactoryAndVirtualDestructionReleaseOwnedHomogeneousStorage) {
    char name[] = "2D owned rectangle";
    RCK2dEntity *named = new RCK2dEntity(Context, name);
    RCK2dEntity *factory = RCK2dEntity::CreateInstance(Context);
    ASSERT_NE(nullptr, factory); name[0] = 'X';
    EXPECT_STREQ("2D owned rectangle", named->GetName()); EXPECT_EQ(nullptr, factory->GetName());
    RCK2dEntity *parent = Entity(); ASSERT_TRUE(named->SetParent(reinterpret_cast<CK2dEntity *>(parent)));
    parent->SetRect(VxRect(0, 0, 200, 100));
    named->SetRect(VxRect(10, 20, 40, 60)); named->SetHomogeneousCoordinates(TRUE);
    ASSERT_NE(nullptr, Entity2dLifecycleProbe::Homogeneous(*named));
    const CK_ID id = named->GetID(), factoryID = factory->GetID();
    ASSERT_TRUE(named->SetParent(nullptr));
    delete static_cast<CKObject *>(named);
    EXPECT_EQ(nullptr, Context->GetObject(id)); EXPECT_EQ(factory, Context->GetObject(factoryID));
    delete static_cast<CKObject *>(factory); EXPECT_EQ(nullptr, Context->GetObject(factoryID));
}

TEST_F(Entity2dLifecycleTest, RegisterPublishesGuidMaterialNotificationAndDependencyModes) {
    CKClassDesc *desc = CKGetClassDesc(CKCID_2DENTITY); ASSERT_NE(nullptr, desc);
    const CKDWORD deleteMask = desc->DefaultDeleteDependencies;
    desc->ToBeNotify.Clear(); desc->Parameter = CKGUID(); RCK2dEntity::Register();
    EXPECT_EQ(CKPGUID_2DENTITY, desc->Parameter);
    EXPECT_EQ(404124131u, desc->Parameter.d1); EXPECT_EQ(467408131u, desc->Parameter.d2);
    EXPECT_TRUE(desc->ToBeNotify.IsSet(CKCID_MATERIAL));
    EXPECT_EQ(3u, desc->DefaultCopyDependencies); EXPECT_EQ(3u, desc->DefaultSaveDependencies);
    EXPECT_EQ(deleteMask, desc->DefaultDeleteDependencies);
    const int modes[] = {-1, 0, 1, 2, 3, 4, 5};
    for (int mode : modes) {
        EXPECT_EQ(mode == 1 || mode == 2 || mode == 4 ? 2 : 0, RCK2dEntity::GetDependenciesCount(mode));
        EXPECT_STREQ("Material", RCK2dEntity::GetDependencies(0, mode));
        EXPECT_STREQ("Children", RCK2dEntity::GetDependencies(1, mode));
        EXPECT_EQ(nullptr, RCK2dEntity::GetDependencies(-1, mode));
        EXPECT_EQ(nullptr, RCK2dEntity::GetDependencies(2, mode));
    }
}

TEST_F(Entity2dLifecycleTest, MemoryOccupationUsesActualPlatformClassDelta) {
    RCK2dEntity *e = Entity(); ASSERT_NE(nullptr, e);
    const int base = e->RCKRenderObject::GetMemoryOccupation();
    EXPECT_EQ(base + static_cast<int>(sizeof(RCK2dEntity) - sizeof(RCKRenderObject)), e->GetMemoryOccupation());
    for (int i = 0; i < 8; ++i) ASSERT_TRUE(Entity()->SetParent(reinterpret_cast<CK2dEntity *>(e)));
    EXPECT_EQ(base + static_cast<int>(sizeof(RCK2dEntity) - sizeof(RCKRenderObject)), e->GetMemoryOccupation());
}

TEST_F(Entity2dLifecycleTest, RenderContextMembershipIntersectsActualContextMasks) {
    new RCKRenderManager(Context);
    RCKRenderContext *a = new RCKRenderContext(Context), *b = new RCKRenderContext(Context);
    a->m_MaskFree = 1; b->m_MaskFree = 8;
    RCK2dEntity *e = Entity(); ASSERT_NE(nullptr, e);
    EXPECT_FALSE(e->IsInRenderContext(a)); EXPECT_FALSE(e->IsInRenderContext(b));
    e->AddToRenderContext(a); EXPECT_TRUE(e->IsInRenderContext(a)); EXPECT_FALSE(e->IsInRenderContext(b));
    e->AddToRenderContext(b); EXPECT_TRUE(e->IsInRenderContext(a)); EXPECT_TRUE(e->IsInRenderContext(b));
    e->RemoveFromRenderContext(a); EXPECT_FALSE(e->IsInRenderContext(a)); EXPECT_TRUE(e->IsInRenderContext(b));
    e->RemoveFromRenderContext(b); EXPECT_FALSE(e->IsInRenderContext(b));
    delete a; delete b;
}

TEST_F(Entity2dLifecycleTest, RenderEligibilityRemainsTrueWhileVisibilityChanges) {
    RCK2dEntity *e = Entity(); ASSERT_NE(nullptr, e);
    EXPECT_TRUE(e->IsToBeRendered()); e->Show(CKHIDE);
    EXPECT_FALSE(e->IsVisible()); EXPECT_TRUE(e->IsToBeRendered());
    e->Show(CKHIERARCHICALHIDE); EXPECT_TRUE(e->IsToBeRendered());
    e->Show(CKSHOW); EXPECT_TRUE(e->IsVisible()); EXPECT_TRUE(e->IsToBeRendered());
}

TEST_F(Entity2dLifecycleTest, CheckPreDeletionClearsOnlyDoomedBorrowedMaterial) {
    RCK2dEntity *e = Entity(); RCKMaterial *m = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
    ASSERT_NE(nullptr, e); ASSERT_NE(nullptr, m);
    e->SetMaterial(reinterpret_cast<CKMaterial *>(m)); e->CheckPreDeletion();
    EXPECT_EQ(reinterpret_cast<CKMaterial *>(m), e->GetMaterial());
    m->ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); e->CheckPreDeletion();
    EXPECT_EQ(nullptr, e->GetMaterial()); m->ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
    e->CheckPreDeletion(); EXPECT_EQ(nullptr, e->GetMaterial());
}
}
