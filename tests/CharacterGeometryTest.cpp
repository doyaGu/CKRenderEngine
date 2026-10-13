#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK3dObject.h"
#include "RCKBodyPart.h"
#include "RCKCharacter.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKMesh.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class VisiblePart : public RCKBodyPart {
public:
    explicit VisiblePart(CKContext *context) : RCKBodyPart(context), Calls(0), Last(CKSHOW) {}
    void Show(CK_OBJECT_SHOWOPTION show) override {
        ++Calls;
        Last = show;
        RCKBodyPart::Show(show);
    }
    int Calls;
    CK_OBJECT_SHOWOPTION Last;
};

class HierarchyCharacter : public RCKCharacter {
public:
    explicit HierarchyCharacter(CKContext *context) : RCKCharacter(context), Calls(0), Last(FALSE) {}
    const VxBbox &GetHierarchicalBox(CKBOOL local) override {
        ++Calls;
        Last = local;
        return RCK3dEntity::GetHierarchicalBox(local);
    }
    int Calls;
    CKBOOL Last;
};

class CharacterGeometryTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK3dObject, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKBodyPart, CKCID_3DOBJECT);
        CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
        CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
        CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMesh, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context); // CKContext owns it; hierarchy queries require real scene nodes.
        ASSERT_NE(nullptr, Context->GetRenderManager());
    }
    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static CK3dEntity *API(RCK3dEntity *entity) { return reinterpret_cast<CK3dEntity *>(entity); }
    static CKBodyPart *PartAPI(RCKBodyPart *part) { return reinterpret_cast<CKBodyPart *>(part); }
    RCKCharacter *Character() { return static_cast<RCKCharacter *>(Context->CreateObject(CKCID_CHARACTER)); }
    RCKBodyPart *Part() { return static_cast<RCKBodyPart *>(Context->CreateObject(CKCID_BODYPART)); }
    RCK3dEntity *Entity() { return static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY)); }
    static void Position(RCK3dEntity &entity, float x, float y, float z, CK3dEntity *reference = nullptr) {
        entity.SetMoveableFlags(entity.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
        VxVector position(x, y, z);
        entity.SetPosition(&position, reference);
    }
    void Mesh(RCK3dEntity &entity, float ax, float ay, float az, float bx, float by, float bz) {
        RCKMesh *mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
        ASSERT_NE(nullptr, mesh);
        ASSERT_TRUE(mesh->SetVertexCount(3));
        VxVector positions[] = {VxVector(ax, ay, az), VxVector(bx, by, bz), VxVector(ax, ay, az)};
        for (int i = 0; i < 3; ++i) mesh->SetVertexPosition(i, &positions[i]);
        mesh->VertexMove();
        entity.SetCurrentMesh(reinterpret_cast<CKMesh *>(mesh));
        ASSERT_EQ(reinterpret_cast<CKMesh *>(mesh), entity.GetCurrentMesh());
    }
    static void Vector(const VxVector &actual, float x, float y, float z) {
        EXPECT_NEAR(x, actual.x, 0.0001f);
        EXPECT_NEAR(y, actual.y, 0.0001f);
        EXPECT_NEAR(z, actual.z, 0.0001f);
    }
    static void Box(const VxBbox &actual, float ax, float ay, float az, float bx, float by, float bz) {
        Vector(actual.Min, ax, ay, az);
        Vector(actual.Max, bx, by, bz);
    }
    void Aggregate(RCKCharacter &character, RCKBodyPart *&part, RCK3dEntity *&child) {
        Position(character, 10, 20, 30);
        part = Part(); child = Entity();
        ASSERT_NE(nullptr, part); ASSERT_NE(nullptr, child);
        Position(*part, 12, 23, 34);
        Mesh(*part, -1, -2, -3, 2, 4, 5);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        ASSERT_EQ(CK_OK, character.AddBodyPart(PartAPI(part)));
        Position(*child, 8, 19, 31);
        Mesh(*child, -4, -1, -2, 1, 2, 1);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        ASSERT_TRUE(child->SetParent(API(&character)));
        ASSERT_EQ(1, character.GetBodyPartCount());
    }
};

TEST_F(CharacterGeometryTest, ShowVirtuallyUpdatesListedPartsEvenWhenTheirParentIsExternal) {
    RCKCharacter character(Context);
    VisiblePart listed(Context);
    RCK3dEntity external(Context), unlisted(Context);
    ASSERT_TRUE(listed.SetParent(API(&external)));
    ASSERT_EQ(CK_OK, character.AddBodyPart(PartAPI(&listed)));
    ASSERT_TRUE(unlisted.SetParent(API(&character)));
    RCK3dEntity *base = &character;
    base->Show(CKHIDE);
    EXPECT_FALSE(character.IsVisible()); EXPECT_FALSE(listed.IsVisible());
    EXPECT_TRUE(unlisted.IsVisible()); EXPECT_FALSE(unlisted.IsHiddenByParent());
    EXPECT_EQ(1, listed.Calls); EXPECT_EQ(CKHIDE, listed.Last);
    base->Show(CKSHOW);
    EXPECT_TRUE(character.IsVisible()); EXPECT_TRUE(listed.IsVisible());
    EXPECT_EQ(2, listed.Calls); EXPECT_EQ(CKSHOW, listed.Last);
    EXPECT_EQ(API(&external), listed.GetParent());
    EXPECT_EQ(reinterpret_cast<CKCharacter *>(&character), listed.GetCharacter());
    listed.SetParent(nullptr); unlisted.SetParent(nullptr);
}

TEST_F(CharacterGeometryTest, HierarchicalHideAffectsUnlistedDescendantsAndShowRestoresThem) {
    RCKCharacter *character = Character();
    RCKBodyPart *part = Part(); RCK3dEntity *child = Entity();
    ASSERT_NE(nullptr, character); ASSERT_NE(nullptr, part); ASSERT_NE(nullptr, child);
    ASSERT_EQ(CK_OK, character->AddBodyPart(PartAPI(part)));
    ASSERT_TRUE(child->SetParent(API(part)));
    character->Show(CKHIERARCHICALHIDE);
    EXPECT_FALSE(character->IsVisible()); EXPECT_FALSE(part->IsVisible()); EXPECT_FALSE(child->IsVisible());
    EXPECT_TRUE(child->IsHiddenByParent());
    character->Show(CKSHOW);
    EXPECT_TRUE(character->IsVisible()); EXPECT_TRUE(part->IsVisible()); EXPECT_TRUE(child->IsVisible());
    EXPECT_FALSE(child->IsHiddenByParent());
    EXPECT_EQ(1, character->GetBodyPartCount());
}

TEST_F(CharacterGeometryTest, BoundsAggregateRealHierarchyRatherThanCharacterMembership) {
    RCKCharacter *character = Character(); ASSERT_NE(nullptr, character);
    RCKBodyPart *part = nullptr; RCK3dEntity *child = nullptr;
    Aggregate(*character, part, child); ASSERT_FALSE(::testing::Test::HasFatalFailure());
    RCKBodyPart *externalPart = Part(); RCK3dEntity *external = Entity();
    ASSERT_NE(nullptr, externalPart); ASSERT_NE(nullptr, external);
    Position(*externalPart, 100, 100, 100);
    Mesh(*externalPart, -20, -20, -20, 20, 20, 20); ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_TRUE(externalPart->SetParent(API(external)));
    ASSERT_EQ(CK_OK, character->AddBodyPart(PartAPI(externalPart)));
    RCK3dEntity *base = character;
    Box(base->GetBoundingBox(FALSE), 4, 18, 29, 14, 27, 39);
    Box(base->GetBoundingBox(TRUE), -6, -2, -1, 4, 7, 9);
    EXPECT_EQ(2, character->GetBodyPartCount()); EXPECT_EQ(API(external), externalPart->GetParent());
    child->Show(CKHIDE);
    Box(base->GetBoundingBox(FALSE), 4, 18, 29, 14, 27, 39);
}

TEST_F(CharacterGeometryTest, CachedBoundsRefreshAfterRealChildMotionAndDetachment) {
    RCKCharacter *character = Character(); ASSERT_NE(nullptr, character);
    RCKBodyPart *part = nullptr; RCK3dEntity *child = nullptr;
    Aggregate(*character, part, child); ASSERT_FALSE(::testing::Test::HasFatalFailure());
    Box(character->GetBoundingBox(FALSE), 4, 18, 29, 14, 27, 39);
    Position(*part, 22, 23, 34);
    Box(character->GetBoundingBox(FALSE), 4, 18, 29, 24, 27, 39);
    EXPECT_NEAR(10, character->GetRadius(), 0.0001f);
    ASSERT_TRUE(child->SetParent(nullptr));
    Box(character->GetBoundingBox(FALSE), 21, 21, 31, 24, 27, 39);
    Box(character->GetBoundingBox(TRUE), 11, 1, 1, 14, 7, 9);
    EXPECT_NEAR(4, character->GetRadius(), 0.0001f);
}

TEST_F(CharacterGeometryTest, LocalBoundsUndoQuarterTurnAndNonuniformWorldScale) {
    RCKCharacter *character = Character(); RCKBodyPart *part = Part();
    ASSERT_NE(nullptr, character); ASSERT_NE(nullptr, part);
    VxMatrix world;
    world.SetIdentity();
    world[0] = VxVector(0, 0, 2); world[1] = VxVector(0, 3, 0);
    world[2] = VxVector(-4, 0, 0); world[3] = VxVector(10, 20, 30);
    character->SetMoveableFlags(character->GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
    character->SetWorldMatrix(world);
    ASSERT_EQ(CK_OK, character->AddBodyPart(PartAPI(part)));
    VxMatrix local; local.SetIdentity(); local[3] = VxVector(1, 2, 3);
    part->SetMoveableFlags(part->GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
    part->SetLocalMatrix(local);
    Mesh(*part, -1, -2, -3, 2, 4, 5); ASSERT_FALSE(::testing::Test::HasFatalFailure());
    // Independent row-coordinate map: world=(10-4*z,20+3*y,30+2*x).
    Box(character->GetBoundingBox(FALSE), -22, 20, 30, 10, 38, 36);
    Box(character->GetBoundingBox(TRUE), 0, 0, 0, 3, 6, 8);
    EXPECT_NEAR(16, character->GetRadius(), 0.0001f);
    VxVector center; ASSERT_TRUE(character->GetBaryCenter(&center)); Vector(center, -6, 29, 33);
}

TEST_F(CharacterGeometryTest, RadiusIsHalfTheLongestWorldAxisRatherThanDiagonalOrMeshSphere) {
    RCKCharacter *character = Character(); ASSERT_NE(nullptr, character);
    const float extents[][3] = {{12, 4, 6}, {2, 14, 8}, {4, 6, 18}, {8, 8, 8}};
    const float radii[] = {6, 7, 9, 4};
    for (int i = 0; i < 4; ++i) {
        VxBbox box; box.Min = VxVector(-3, 2, 5);
        box.Max = VxVector(-3 + extents[i][0], 2 + extents[i][1], 5 + extents[i][2]);
        ASSERT_TRUE(character->SetBoundingBox(&box, FALSE));
        EXPECT_NEAR(radii[i], static_cast<RCK3dEntity *>(character)->GetRadius(), 0.0001f);
    }
}

TEST_F(CharacterGeometryTest, BaryCenterUsesWorldHierarchyBoxAndNullOutputStillSucceeds) {
    RCKCharacter *character = Character(); ASSERT_NE(nullptr, character);
    RCKBodyPart *part = nullptr; RCK3dEntity *child = nullptr;
    Aggregate(*character, part, child); ASSERT_FALSE(::testing::Test::HasFatalFailure());
    VxVector center(-999, -999, -999);
    ASSERT_TRUE(static_cast<RCK3dEntity *>(character)->GetBaryCenter(&center));
    Vector(center, 9, 22.5f, 34);
    EXPECT_TRUE(character->GetBaryCenter(nullptr));
    // The six actual vertices average (8.833333,21,31.833333), not this AABB midpoint.
    EXPECT_NEAR(9, center.x, 0.0001f); EXPECT_NEAR(22.5f, center.y, 0.0001f);
}

TEST_F(CharacterGeometryTest, GeometryMethodsKeepVirtualHierarchyDispatchAndForwardLocalArgument) {
    HierarchyCharacter character(Context);
    RCKBodyPart *part = nullptr; RCK3dEntity *child = nullptr;
    Aggregate(character, part, child); ASSERT_FALSE(::testing::Test::HasFatalFailure());
    RCK3dEntity *base = &character;
    Box(base->GetBoundingBox(TRUE), -6, -2, -1, 4, 7, 9);
    EXPECT_EQ(1, character.Calls); EXPECT_EQ(TRUE, character.Last);
    EXPECT_NEAR(5, base->GetRadius(), 0.0001f);
    EXPECT_EQ(2, character.Calls); EXPECT_EQ(FALSE, character.Last);
    VxVector center; ASSERT_TRUE(base->GetBaryCenter(&center)); Vector(center, 9, 22.5f, 34);
    EXPECT_EQ(3, character.Calls); EXPECT_EQ(FALSE, character.Last);
    EXPECT_TRUE(base->GetBaryCenter(nullptr)); EXPECT_EQ(3, character.Calls);
    part->SetParent(nullptr); child->SetParent(nullptr);
}

TEST_F(CharacterGeometryTest, HeadlessUserBoxQueriesUseOwnBaseGeometryWithoutVirtualRecursion) {
    // An independent legitimate SDL context with no RenderManager exercises only own-box fallback.
    CKContext headless(nullptr, 0, 0);
    ASSERT_EQ(nullptr, headless.GetRenderManager());
    RCKCharacter *character = static_cast<RCKCharacter *>(headless.CreateObject(CKCID_CHARACTER));
    ASSERT_NE(nullptr, character);
    Position(*character, 10, 20, 30);
    VxBbox local; local.Min = VxVector(-2, -4, -6); local.Max = VxVector(4, 6, 8);
    ASSERT_TRUE(character->SetBoundingBox(&local, TRUE));
    RCK3dEntity *base = character;
    Box(base->GetBoundingBox(FALSE), 8, 16, 24, 14, 26, 38);
    Box(base->GetBoundingBox(TRUE), -2, -4, -6, 4, 6, 8);
    EXPECT_NEAR(7, base->GetRadius(), 0.0001f);
    VxVector center; ASSERT_TRUE(base->GetBaryCenter(&center)); Vector(center, 11, 21, 31);
    EXPECT_TRUE(base->GetBaryCenter(nullptr));
    // No children: this test does not introduce a headless aggregation contract.
    EXPECT_EQ(0, character->GetChildrenCount()); EXPECT_EQ(0, character->GetBodyPartCount());
}
} // namespace
