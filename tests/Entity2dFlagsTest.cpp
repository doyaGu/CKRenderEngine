#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK2dEntity.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class Entity2dFlagsTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *View = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context); View = new RCKRenderContext(Context);
        Context->m_BehaviorContext.CurrentRenderContext = View;
        View->m_ViewportData.ViewWidth = 400; View->m_ViewportData.ViewHeight = 200;
    }
    void TearDown() override {
        if (Context) Context->m_BehaviorContext.CurrentRenderContext = nullptr;
        delete View; if (Context) CKCloseContext(Context); CKShutdown();
    }
    RCK2dEntity *Entity() { return static_cast<RCK2dEntity *>(Context->CreateObject(CKCID_2DENTITY)); }
};

TEST_F(Entity2dFlagsTest, DefaultFlagsExposeRatioCameraClippingAndPickability) {
    RCK2dEntity *entity = Entity();
    EXPECT_TRUE(entity->IsRatioOffset()); EXPECT_TRUE(entity->IsClippedToCamera()); EXPECT_TRUE(entity->IsPickable());
    EXPECT_FALSE(entity->IsClipToParent()); EXPECT_FALSE(entity->IsUsingSourceRect()); EXPECT_FALSE(entity->IsHomogeneousCoordinates());
    EXPECT_NE(0u, entity->GetFlags() & CK_2DENTITY_STICKLEFT); EXPECT_NE(0u, entity->GetFlags() & CK_2DENTITY_STICKTOP);
}
TEST_F(Entity2dFlagsTest, SetFlagsReplacesFlagsAndModifyFlagsRemovalWinsOverlap) {
    RCK2dEntity *entity = Entity(); CKDWORD initial = CK_2DENTITY_RESERVED3 | CK_2DENTITY_CLIPTOPARENT | CK_2DENTITY_STICKBOTTOM;
    entity->SetFlags(initial); EXPECT_EQ(initial, entity->GetFlags());
    entity->ModifyFlags(CK_2DENTITY_RATIOOFFSET | CK_2DENTITY_CLIPTOPARENT, CK_2DENTITY_CLIPTOPARENT | CK_2DENTITY_STICKBOTTOM);
    EXPECT_EQ(CKDWORD(CK_2DENTITY_RESERVED3 | CK_2DENTITY_RATIOOFFSET), entity->GetFlags());
}
TEST_F(Entity2dFlagsTest, SourceRectToggleOnlyChangesItsOwnFlagAndUsesNonzeroBoolean) {
    RCK2dEntity *entity = Entity(); const CKDWORD initial = entity->GetFlags();
    entity->UseSourceRect(7); EXPECT_TRUE(entity->IsUsingSourceRect()); EXPECT_EQ(initial | CK_2DENTITY_USESRCRECT, entity->GetFlags());
    entity->UseSourceRect(FALSE); EXPECT_FALSE(entity->IsUsingSourceRect()); EXPECT_EQ(initial, entity->GetFlags());
}
TEST_F(Entity2dFlagsTest, PickabilityToggleUsesInverseNotPickableFlag) {
    RCK2dEntity *entity = Entity(); const CKDWORD initial = entity->GetFlags();
    entity->SetPickable(FALSE); EXPECT_FALSE(entity->IsPickable()); EXPECT_EQ(initial | CK_2DENTITY_NOTPICKABLE, entity->GetFlags());
    entity->SetPickable(7); EXPECT_TRUE(entity->IsPickable()); EXPECT_EQ(initial, entity->GetFlags());
}
TEST_F(Entity2dFlagsTest, RatioOffsetTogglePreservesAllOtherFlags) {
    RCK2dEntity *entity = Entity(); const CKDWORD initial = entity->GetFlags();
    entity->EnableRatioOffset(FALSE); EXPECT_FALSE(entity->IsRatioOffset()); EXPECT_EQ(initial & ~CK_2DENTITY_RATIOOFFSET, entity->GetFlags());
    entity->EnableRatioOffset(7); EXPECT_TRUE(entity->IsRatioOffset()); EXPECT_EQ(initial, entity->GetFlags());
}
TEST_F(Entity2dFlagsTest, ParentClippingTogglePreservesAllOtherFlags) {
    RCK2dEntity *entity = Entity(); const CKDWORD initial = entity->GetFlags();
    entity->SetClipToParent(7); EXPECT_TRUE(entity->IsClipToParent()); EXPECT_EQ(initial | CK_2DENTITY_CLIPTOPARENT, entity->GetFlags());
    entity->SetClipToParent(FALSE); EXPECT_FALSE(entity->IsClipToParent()); EXPECT_EQ(initial, entity->GetFlags());
}
TEST_F(Entity2dFlagsTest, CameraClippingTogglePreservesAllOtherFlags) {
    RCK2dEntity *entity = Entity(); const CKDWORD initial = entity->GetFlags();
    entity->EnableClipToCamera(FALSE); EXPECT_FALSE(entity->IsClippedToCamera()); EXPECT_EQ(initial & ~CK_2DENTITY_CLIPTOCAMERAVIEW, entity->GetFlags());
    entity->EnableClipToCamera(7); EXPECT_TRUE(entity->IsClippedToCamera()); EXPECT_EQ(initial, entity->GetFlags());
}
TEST_F(Entity2dFlagsTest, DirectFlagUpdatesKeepHomogeneousStorageUsableAndScreenLayoutUnchanged) {
    RCK2dEntity *entity = Entity(); entity->SetRect(VxRect(40, 50, 120, 150));
    CKDWORD initial = entity->GetFlags(); entity->SetFlags(initial | CK_2DENTITY_USEHOMOGENEOUSCOORD);
    EXPECT_TRUE(entity->IsHomogeneousCoordinates());
    ASSERT_EQ(CK_OK, entity->SetHomogeneousRect(VxRect(.1f, .25f, .3f, .75f)));
    VxRect screen; entity->GetRect(screen); EXPECT_FLOAT_EQ(40, screen.left); EXPECT_FLOAT_EQ(150, screen.bottom);
    entity->ModifyFlags(CK_2DENTITY_CLIPTOPARENT, CK_2DENTITY_USEHOMOGENEOUSCOORD);
    EXPECT_FALSE(entity->IsHomogeneousCoordinates()); EXPECT_TRUE(entity->IsClipToParent());
    entity->GetRect(screen); EXPECT_FLOAT_EQ(40, screen.left); EXPECT_FLOAT_EQ(150, screen.bottom);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, entity->SetHomogeneousRect(VxRect(.2f, .3f, .4f, .5f)));
    entity->ModifyFlags(CK_2DENTITY_USEHOMOGENEOUSCOORD, 0);
    ASSERT_EQ(CK_OK, entity->SetHomogeneousRect(VxRect(.2f, .3f, .4f, .5f)));
    entity->GetRect(screen); EXPECT_FLOAT_EQ(80, screen.left); EXPECT_FLOAT_EQ(100, screen.bottom);
}
}
