#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK2dEntity.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
CK2dEntity *API(RCK2dEntity *entity) { return reinterpret_cast<CK2dEntity *>(entity); }

class Entity2dLayoutTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *View = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        View = new RCKRenderContext(Context);
        Context->m_BehaviorContext.CurrentRenderContext = View;
        View->m_ViewportData.ViewX = 20;
        View->m_ViewportData.ViewY = 30;
        View->m_ViewportData.ViewWidth = 400;
        View->m_ViewportData.ViewHeight = 200;
        View->m_Settings.m_Rect.left = 7;
        View->m_Settings.m_Rect.top = 11;
        View->m_Settings.m_Rect.right = 800;
        View->m_Settings.m_Rect.bottom = 600;
    }
    void TearDown() override {
        if (Context) Context->m_BehaviorContext.CurrentRenderContext = nullptr;
        delete View;
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    RCK2dEntity *Entity() {
        return static_cast<RCK2dEntity *>(Context->CreateObject(CKCID_2DENTITY));
    }
    static void Rect(const VxRect &rect, float left, float top, float right, float bottom) {
        EXPECT_NEAR(left, rect.left, .0001f); EXPECT_NEAR(top, rect.top, .0001f);
        EXPECT_NEAR(right, rect.right, .0001f); EXPECT_NEAR(bottom, rect.bottom, .0001f);
    }
    static void PixelRect(RCK2dEntity *entity, float left, float top, float right, float bottom) {
        VxRect rect; entity->GetRect(rect); Rect(rect, left, top, right, bottom);
    }
    static void HomRect(RCK2dEntity *entity, float left, float top, float right, float bottom) {
        VxRect rect; ASSERT_EQ(CK_OK, entity->GetHomogeneousRect(rect));
        Rect(rect, left, top, right, bottom);
    }
};

TEST_F(Entity2dLayoutTest, AbsolutePositionAndSizeUseOptionalReferenceAndPreserveOtherDimensions) {
    RCK2dEntity *entity = Entity(), *reference = Entity();
    entity->SetRect(VxRect(30, 50, 70, 80)); reference->SetRect(VxRect(10, 20, 210, 120));
    Vx2DVector result;
    ASSERT_EQ(CK_OK, entity->GetPosition(result, FALSE, API(reference)));
    EXPECT_FLOAT_EQ(20, result.x); EXPECT_FLOAT_EQ(30, result.y);
    entity->SetPosition(Vx2DVector(5, 8), FALSE, FALSE, API(reference));
    PixelRect(entity, 15, 28, 55, 58);
    ASSERT_EQ(CK_OK, entity->GetSize(result)); EXPECT_FLOAT_EQ(40, result.x); EXPECT_FLOAT_EQ(30, result.y);
    entity->SetSize(Vx2DVector(100, 60)); PixelRect(entity, 15, 28, 115, 88);
    ASSERT_EQ(CK_OK, entity->GetPosition(result)); EXPECT_FLOAT_EQ(15, result.x); EXPECT_FLOAT_EQ(28, result.y);
}

TEST_F(Entity2dLayoutTest, DisabledHomogeneousAccessReportsErrorAndDoesNotModifyOutputsOrPixels) {
    RCK2dEntity *entity = Entity(); entity->SetRect(VxRect(10, 20, 40, 60));
    Vx2DVector result(91, 92); VxRect rect(81, 82, 83, 84);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, entity->GetPosition(result, TRUE));
    EXPECT_FLOAT_EQ(91, result.x); EXPECT_FLOAT_EQ(92, result.y);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, entity->GetSize(result, TRUE));
    EXPECT_FLOAT_EQ(91, result.x); EXPECT_FLOAT_EQ(92, result.y);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, entity->GetHomogeneousRect(rect)); Rect(rect, 81, 82, 83, 84);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, entity->SetHomogeneousRect(VxRect(.1f, .2f, .3f, .4f)));
    entity->SetPosition(Vx2DVector(.2f, .3f), TRUE); entity->SetSize(Vx2DVector(.4f, .5f), TRUE);
    PixelRect(entity, 10, 20, 40, 60);
}

TEST_F(Entity2dLayoutTest, EnablingHomogeneousCoordinatesConvertsCurrentPixelsWithoutMovingEntity) {
    RCK2dEntity *entity = Entity(); entity->SetRect(VxRect(40, 50, 120, 150));
    entity->SetHomogeneousCoordinates(TRUE);
    EXPECT_TRUE(entity->IsHomogeneousCoordinates()); HomRect(entity, .1f, .25f, .3f, .75f);
    PixelRect(entity, 40, 50, 120, 150);
    EXPECT_NE(0u, entity->GetFlags() & CK_2DENTITY_UPDATEHOMOGENEOUSCOORD);
    entity->SetHomogeneousCoordinates(TRUE); HomRect(entity, .1f, .25f, .3f, .75f);
}

TEST_F(Entity2dLayoutTest, ParentReferenceIncludesParentOriginAndCoordinateDisableUsesCurrentReference) {
    RCK2dEntity *parent = Entity(), *entity = Entity();
    parent->SetRect(VxRect(100, 200, 300, 300)); entity->SetParent(API(parent));
    entity->SetRect(VxRect(120, 225, 160, 275)); entity->SetHomogeneousCoordinates(TRUE);
    HomRect(entity, .1f, .25f, .3f, .75f);
    VxRect relative; entity->GetHomogeneousRelativeRect(relative); Rect(relative, 100, 200, 300, 300);
    parent->SetRect(VxRect(200, 300, 600, 500), TRUE);
    entity->SetHomogeneousCoordinates(FALSE);
    EXPECT_FALSE(entity->IsHomogeneousCoordinates()); PixelRect(entity, 240, 350, 320, 450);
}

TEST_F(Entity2dLayoutTest, RootRelativeRectUsesViewportOrWindowDimensionsAtZeroOrigin) {
    RCK2dEntity *entity = Entity(); VxRect relative;
    entity->EnableRatioOffset(TRUE); entity->GetHomogeneousRelativeRect(relative); Rect(relative, 0, 0, 400, 200);
    entity->EnableRatioOffset(FALSE); entity->GetHomogeneousRelativeRect(relative); Rect(relative, 0, 0, 800, 600);
    entity->SetRect(VxRect(80, 120, 400, 300)); entity->SetHomogeneousCoordinates(TRUE);
    HomRect(entity, .1f, .2f, .5f, .5f);
    ASSERT_EQ(CK_OK, entity->SetHomogeneousRect(VxRect(.25f, .5f, .5f, .75f)));
    PixelRect(entity, 200, 300, 400, 450);
}

TEST_F(Entity2dLayoutTest, HomogeneousPositionAndSizeUpdatePixelsAndIgnorePixelReference) {
    RCK2dEntity *entity = Entity(), *reference = Entity();
    reference->SetRect(VxRect(500, 600, 900, 1000)); entity->SetHomogeneousCoordinates(TRUE);
    ASSERT_EQ(CK_OK, entity->SetHomogeneousRect(VxRect(.1f, .2f, .3f, .6f)));
    entity->SetPosition(Vx2DVector(.25f, .5f), TRUE, FALSE, API(reference));
    HomRect(entity, .25f, .5f, .45f, .9f); PixelRect(entity, 100, 100, 180, 180);
    entity->SetSize(Vx2DVector(.5f, .25f), TRUE);
    HomRect(entity, .25f, .5f, .75f, .75f); PixelRect(entity, 100, 100, 300, 150);
    Vx2DVector result; ASSERT_EQ(CK_OK, entity->GetPosition(result, TRUE, API(reference)));
    EXPECT_FLOAT_EQ(.25f, result.x); EXPECT_FLOAT_EQ(.5f, result.y);
    ASSERT_EQ(CK_OK, entity->GetSize(result, TRUE)); EXPECT_FLOAT_EQ(.5f, result.x); EXPECT_FLOAT_EQ(.25f, result.y);
}

TEST_F(Entity2dLayoutTest, PixelPositionAndSizeRecomputeHomogeneousRectangle) {
    RCK2dEntity *entity = Entity(); entity->SetHomogeneousCoordinates(TRUE);
    ASSERT_EQ(CK_OK, entity->SetHomogeneousRect(VxRect(.1f, .2f, .3f, .6f)));
    entity->SetPosition(Vx2DVector(100, 50)); HomRect(entity, .25f, .25f, .45f, .65f);
    entity->SetSize(Vx2DVector(200, 100)); HomRect(entity, .25f, .25f, .75f, .75f);
    EXPECT_NE(0u, entity->GetFlags() & CK_2DENTITY_UPDATEHOMOGENEOUSCOORD);
    PixelRect(entity, 100, 50, 300, 150);
}

TEST_F(Entity2dLayoutTest, HomogeneousChildUsesNewParentSizeAndOriginWhenParentRectangleChanges) {
    RCK2dEntity *parent = Entity(), *child = Entity();
    parent->SetRect(VxRect(100, 200, 300, 300)); child->SetParent(API(parent));
    child->SetHomogeneousCoordinates(TRUE);
    ASSERT_EQ(CK_OK, child->SetHomogeneousRect(VxRect(.1f, .25f, .3f, .75f)));
    parent->SetRect(VxRect(200, 300, 600, 500));
    PixelRect(child, 240, 350, 320, 450); HomRect(child, .1f, .25f, .3f, .75f);
}

TEST_F(Entity2dLayoutTest, ParentResizePropagatesHomogeneousLayoutThroughGrandchildren) {
    RCK2dEntity *parent = Entity(), *child = Entity(), *grandchild = Entity();
    parent->SetRect(VxRect(100, 200, 300, 300)); child->SetParent(API(parent));
    child->SetHomogeneousCoordinates(TRUE); child->SetHomogeneousRect(VxRect(.1f, .2f, .6f, .8f));
    grandchild->SetParent(API(child)); grandchild->SetHomogeneousCoordinates(TRUE);
    grandchild->SetHomogeneousRect(VxRect(.2f, .25f, .8f, .75f));
    parent->SetRect(VxRect(200, 300, 600, 500));
    PixelRect(child, 240, 340, 440, 460); PixelRect(grandchild, 280, 370, 400, 430);
    HomRect(child, .1f, .2f, .6f, .8f); HomRect(grandchild, .2f, .25f, .8f, .75f);
}

TEST_F(Entity2dLayoutTest, SceneRootResizeKeepsViewportAndWindowHomogeneousReferencesDistinct) {
    RCK2dEntity *root = reinterpret_cast<RCK2dEntity *>(View->Get2dRoot(FALSE));
    ASSERT_NE(nullptr, root); root->SetRect(VxRect(0, 0, 800, 600));
    RCK2dEntity *ratio = Entity(), *window = Entity();
    ASSERT_TRUE(ratio->SetParent(API(root))); ASSERT_TRUE(window->SetParent(API(root)));
    EXPECT_EQ(nullptr, ratio->GetParent()); EXPECT_EQ(nullptr, window->GetParent());
    ratio->EnableRatioOffset(TRUE); window->EnableRatioOffset(FALSE);
    ratio->SetHomogeneousCoordinates(TRUE); window->SetHomogeneousCoordinates(TRUE);
    ASSERT_EQ(CK_OK, ratio->SetHomogeneousRect(VxRect(.25f, .25f, .75f, .75f)));
    ASSERT_EQ(CK_OK, window->SetHomogeneousRect(VxRect(.25f, .25f, .75f, .75f)));
    PixelRect(ratio, 100, 50, 300, 150); PixelRect(window, 200, 150, 600, 450);
    View->m_ViewportData.ViewWidth = 500; View->m_ViewportData.ViewHeight = 300;
    View->m_Settings.m_Rect.right = 1000; View->m_Settings.m_Rect.bottom = 800;
    root->SetRect(VxRect(0, 0, 1000, 800));
    PixelRect(ratio, 125, 75, 375, 225); PixelRect(window, 250, 200, 750, 600);
    HomRect(ratio, .25f, .25f, .75f, .75f); HomRect(window, .25f, .25f, .75f, .75f);
}

TEST_F(Entity2dLayoutTest, KeepChildrenPreservesAbsoluteRectanglesInBothCoordinateModes) {
    RCK2dEntity *parent = Entity(), *pixel = Entity(), *hom = Entity();
    parent->SetRect(VxRect(100, 200, 300, 300)); pixel->SetParent(API(parent)); hom->SetParent(API(parent));
    pixel->SetRect(VxRect(110, 220, 140, 240)); hom->SetHomogeneousCoordinates(TRUE);
    hom->SetHomogeneousRect(VxRect(.1f, .2f, .3f, .6f));
    parent->SetPosition(Vx2DVector(200, 300), FALSE, TRUE);
    parent->SetSize(Vx2DVector(400, 200), FALSE, TRUE);
    PixelRect(parent, 200, 300, 600, 500); PixelRect(pixel, 110, 220, 140, 240); PixelRect(hom, 120, 220, 160, 260);
}

TEST_F(Entity2dLayoutTest, LeftTopAnchorsMoveChildrenAndGrandchildrenWithParent) {
    RCK2dEntity *parent = Entity(), *child = Entity(), *grandchild = Entity();
    parent->SetRect(VxRect(100, 200, 300, 300)); child->SetParent(API(parent));
    child->SetRect(VxRect(110, 220, 150, 250)); grandchild->SetParent(API(child)); grandchild->SetRect(VxRect(115, 225, 125, 235));
    parent->SetRect(VxRect(200, 300, 600, 500));
    PixelRect(child, 210, 320, 250, 350); PixelRect(grandchild, 215, 325, 225, 335);
}

TEST_F(Entity2dLayoutTest, OppositeAnchorsStretchWhileSingleRightBottomAnchorsPreserveSize) {
    RCK2dEntity *parent = Entity(), *stretch = Entity(), *corner = Entity();
    parent->SetRect(VxRect(100, 200, 300, 300)); stretch->SetParent(API(parent)); corner->SetParent(API(parent));
    stretch->SetRect(VxRect(110, 220, 280, 270)); corner->SetRect(VxRect(250, 250, 280, 270));
    stretch->ModifyFlags(CK_2DENTITY_STICKRIGHT | CK_2DENTITY_STICKBOTTOM, 0);
    corner->ModifyFlags(CK_2DENTITY_STICKRIGHT | CK_2DENTITY_STICKBOTTOM, CK_2DENTITY_STICKLEFT | CK_2DENTITY_STICKTOP);
    parent->SetRect(VxRect(200, 300, 600, 500));
    PixelRect(stretch, 210, 320, 580, 470); PixelRect(corner, 550, 450, 580, 470);
}

TEST_F(Entity2dLayoutTest, UnanchoredChildrenPreserveRelativeCenterAndPixelSize) {
    RCK2dEntity *parent = Entity(), *child = Entity();
    parent->SetRect(VxRect(100, 200, 300, 300)); child->SetParent(API(parent));
    child->SetRect(VxRect(130, 240, 170, 260));
    child->ModifyFlags(0, CK_2DENTITY_STICKLEFT | CK_2DENTITY_STICKRIGHT | CK_2DENTITY_STICKTOP | CK_2DENTITY_STICKBOTTOM);
    parent->SetRect(VxRect(200, 300, 600, 500)); PixelRect(child, 280, 390, 320, 410);
}

TEST_F(Entity2dLayoutTest, ZeroSizedParentUsesFiniteCenterForUnanchoredChild) {
    RCK2dEntity *parent = Entity(), *child = Entity();
    parent->SetRect(VxRect(100, 200, 100, 200)); child->SetParent(API(parent)); child->SetRect(VxRect(90, 190, 110, 210));
    child->ModifyFlags(0, CK_2DENTITY_STICKLEFT | CK_2DENTITY_STICKRIGHT | CK_2DENTITY_STICKTOP | CK_2DENTITY_STICKBOTTOM);
    parent->SetRect(VxRect(200, 300, 600, 500)); PixelRect(child, 390, 390, 410, 410);
}

TEST_F(Entity2dLayoutTest, SourceRectangleRemainsIndependentOfEntityPositionAndSize) {
    RCK2dEntity *entity = Entity(); VxRect source(3, 4, 35, 44), result;
    entity->SetSourceRect(source); entity->SetRect(VxRect(10, 20, 40, 60));
    entity->GetSourceRect(result); Rect(result, 3, 4, 35, 44);
    source = VxRect(100, 200, 300, 400); entity->GetSourceRect(result); Rect(result, 3, 4, 35, 44);
    entity->SetPosition(Vx2DVector(50, 60)); entity->SetSize(Vx2DVector(70, 80));
    entity->GetSourceRect(result); Rect(result, 3, 4, 35, 44); PixelRect(entity, 50, 60, 120, 140);
}

TEST_F(Entity2dLayoutTest, SetExtentsUpdatesRenderedRectanglesWithoutChangingLayoutOrSourceRectangle) {
    RCK2dEntity *entity = Entity(); entity->SetRect(VxRect(10, 20, 100, 120));
    VxRect original(2, 3, 4, 5); entity->SetSourceRect(original);
    entity->SetExtents(VxRect(.1f, .2f, .6f, .8f), VxRect(25, 35, 65, 75));
    VxRect source, pixels; entity->GetExtents(source, pixels);
    Rect(source, .1f, .2f, .6f, .8f); Rect(pixels, 25, 35, 65, 75);
    entity->GetSourceRect(source); Rect(source, 2, 3, 4, 5); PixelRect(entity, 10, 20, 100, 120);
}

TEST_F(Entity2dLayoutTest, GetExtentsReturnsActualViewportClippingAndCroppedSourceCoordinates) {
    RCK2dEntity *entity = Entity(); entity->EnableRatioOffset(FALSE);
    entity->SetRect(VxRect(0, 10, 100, 110));
    ASSERT_TRUE(entity->UpdateExtents(View));
    VxRect source, pixels; entity->GetExtents(source, pixels);
    Rect(pixels, 20, 30, 100, 110); Rect(source, .2f, .2f, 1, 1);
    PixelRect(entity, 0, 10, 100, 110); entity->GetSourceRect(source); Rect(source, 0, 0, 1, 1);
}

TEST_F(Entity2dLayoutTest, RestoreInitialSizeIsNoOpForBaseEntityAndLeavesRenderedExtentsIntact) {
    RCK2dEntity *entity = Entity(); entity->SetRect(VxRect(10, 20, 100, 120));
    VxRect original(2, 3, 4, 5); entity->SetSourceRect(original);
    entity->SetExtents(VxRect(.1f, .2f, .6f, .8f), VxRect(25, 35, 65, 75));
    entity->RestoreInitialSize(); VxRect source, pixels; entity->GetExtents(source, pixels);
    Rect(source, .1f, .2f, .6f, .8f); Rect(pixels, 25, 35, 65, 75);
    entity->GetSourceRect(source); Rect(source, 2, 3, 4, 5); PixelRect(entity, 10, 20, 100, 120);
}
}
