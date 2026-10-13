#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKTargetCamera.h"
#include "RCKRenderObject.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "RCK2dEntity.h"
#include "RCKMaterial.h"
#include "CKRenderedScene.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class TargetCameraStateProbe : public RCKTargetCamera {
public:
    explicit TargetCameraStateProbe(CKContext *context) : RCKTargetCamera(context) {}
    CK_ID TargetID() const { return m_Target; }
};
class TargetCameraViewProbe : public RCKRenderContext {
public:
    explicit TargetCameraViewProbe(CKContext *context) : RCKRenderContext(context) {}
    CKRenderedScene &Scene() { return *m_RenderedScene; }
};
class TargetCameraStateTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKTargetCamera, CKCID_CAMERA);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CK3dEntity *API(RCK3dEntity &e) { return reinterpret_cast<CK3dEntity *>(&e); }
    static CKDWORD TargetFlags(CKDWORD flags) { return (flags | CK_3DENTITY_TARGETCAMERA) & ~CK_3DENTITY_FRAME; }
    static CKDWORD ReleasedFlags(CKDWORD flags) { return (flags & ~CK_3DENTITY_TARGETCAMERA) | CK_3DENTITY_FRAME; }
    static void Expect(const VxVector &v, float x, float y, float z) {
        EXPECT_NEAR(x, v.x, 1e-5f); EXPECT_NEAR(y, v.y, 1e-5f); EXPECT_NEAR(z, v.z, 1e-5f);
    }
    static void LoadPending(TargetCameraStateProbe &camera, CK_ID id) {
        CKStateChunk chunk(CKCID_TARGETCAMERA, nullptr); chunk.StartWrite(); chunk.SetDataVersion(8);
        chunk.WriteIdentifier(0x10000000); chunk.WriteObjectID(id); chunk.CloseChunk();
        ASSERT_EQ(CK_OK, camera.Load(&chunk, nullptr)); EXPECT_EQ(id, camera.TargetID());
    }
};
}

TEST_F(TargetCameraStateTest, GetTargetResolvesTheCurrentContextObjectWithoutChangingFlags) {
    TargetCameraStateProbe camera(Context); RCK3dEntity target(Context); camera.SetTarget(API(target));
    const CKDWORD cameraFlags = camera.GetObjectFlags(), flags = target.GetFlags();
    EXPECT_EQ(API(target), camera.GetTarget()); EXPECT_EQ(target.GetID(), camera.TargetID());
    EXPECT_EQ(cameraFlags, camera.GetObjectFlags()); EXPECT_EQ(flags, target.GetFlags()); camera.SetTarget(nullptr);
}
TEST_F(TargetCameraStateTest, GetTargetReturnsNullForADeferredRawIdentifierWithoutErasingIt) {
    TargetCameraStateProbe camera(Context); LoadPending(camera, 0x7ffabcde);
    EXPECT_EQ(nullptr, camera.GetTarget()); EXPECT_EQ(0x7ffabcdeu, camera.TargetID());
}
TEST_F(TargetCameraStateTest, BindMarksTheTargetAndPreservesUnrelatedFlagsAndOwnership) {
    TargetCameraStateProbe camera(Context); RCK3dEntity target(Context); target.SetFlags(CK_3DENTITY_FRAME | CK_3DENTITY_IGNOREANIMATION);
    const CKDWORD flags = target.GetFlags(); camera.SetTarget(API(target));
    EXPECT_EQ(TargetFlags(flags), target.GetFlags()); EXPECT_EQ(nullptr, target.GetParent());
    EXPECT_EQ(nullptr, camera.GetParent()); EXPECT_EQ(API(target), camera.GetTarget()); camera.SetTarget(nullptr);
}
TEST_F(TargetCameraStateTest, ReplaceReleasesTheOldMarkerAndSetsTheNewMarker) {
    TargetCameraStateProbe camera(Context); RCK3dEntity first(Context), second(Context);
    first.SetFlags(CK_3DENTITY_IGNOREANIMATION); second.SetFlags(CK_3DENTITY_FRAME | CK_3DENTITY_RESERVED0);
    camera.SetTarget(API(first)); const CKDWORD oldFlags = first.GetFlags(), newFlags = second.GetFlags();
    camera.SetTarget(API(second)); EXPECT_EQ(ReleasedFlags(oldFlags), first.GetFlags());
    EXPECT_EQ(TargetFlags(newFlags), second.GetFlags()); EXPECT_EQ(API(second), camera.GetTarget()); camera.SetTarget(nullptr);
}
TEST_F(TargetCameraStateTest, SettingTheSameTargetDoesNotReapplyFlagsOrMoveEitherObject) {
    TargetCameraStateProbe camera(Context); RCK3dEntity target(Context); camera.SetPosition3f(1, 2, 3); target.SetPosition3f(7, 8, 9);
    camera.SetTarget(API(target)); target.SetFlags(CK_3DENTITY_FRAME | CK_3DENTITY_IGNOREANIMATION);
    const CKDWORD flags = target.GetFlags(); camera.SetTarget(API(target)); EXPECT_EQ(flags, target.GetFlags());
    VxVector position; camera.GetPosition(&position); Expect(position, 1, 2, 3);
    target.GetPosition(&position); Expect(position, 7, 8, 9); camera.SetTarget(nullptr);
}
TEST_F(TargetCameraStateTest, SelfTargetIsRejectedWithoutReplacingTheExistingTarget) {
    TargetCameraStateProbe camera(Context); RCK3dEntity target(Context); camera.SetTarget(API(target));
    const CKDWORD flags = target.GetFlags(); camera.SetTarget(API(camera));
    EXPECT_EQ(API(target), camera.GetTarget()); EXPECT_EQ(flags, target.GetFlags()); camera.SetTarget(nullptr);
}
TEST_F(TargetCameraStateTest, ClearResolvedTargetRestoresItsFrameMarkerAndZeroesReference) {
    TargetCameraStateProbe camera(Context); RCK3dEntity target(Context); camera.SetTarget(API(target));
    const CKDWORD flags = target.GetFlags(); camera.SetTarget(nullptr); EXPECT_EQ(ReleasedFlags(flags), target.GetFlags());
    EXPECT_EQ(nullptr, camera.GetTarget()); EXPECT_EQ(0u, camera.TargetID()); camera.SetTarget(nullptr); EXPECT_EQ(0u, camera.TargetID());
}
TEST_F(TargetCameraStateTest, ClearDeferredTargetZeroesTheReferenceDespiteNoResolvedPointer) {
    TargetCameraStateProbe camera(Context); LoadPending(camera, 0x7ffabcde);
    camera.SetTarget(nullptr); EXPECT_EQ(nullptr, camera.GetTarget()); EXPECT_EQ(0u, camera.TargetID());
}
TEST_F(TargetCameraStateTest, SharedTargetFlagsRetainNativeLastMutationSemanticsWhileOtherReferenceSurvives) {
    TargetCameraStateProbe first(Context), second(Context); RCK3dEntity target(Context);
    first.SetTarget(API(target)); second.SetTarget(API(target)); const CKDWORD flags = target.GetFlags();
    first.SetTarget(nullptr); EXPECT_EQ(ReleasedFlags(flags), target.GetFlags()); EXPECT_EQ(API(target), second.GetTarget());
    second.SetTarget(nullptr); EXPECT_EQ(nullptr, second.GetTarget());
}
TEST_F(TargetCameraStateTest, FramePreparationPointsAtTheMovingTargetAndPreservesCameraPosition) {
    TargetCameraViewProbe view(Context); TargetCameraStateProbe camera(Context); RCK3dEntity target(Context);
    camera.SetPosition3f(3, 5, 7); target.SetPosition3f(13, 5, 7); camera.SetTarget(API(target));
    view.Scene().AddObject(reinterpret_cast<CKRenderObject *>(&camera)); view.Scene().PrepareCameras();
    VxVector direction, position; camera.GetOrientation(&direction, nullptr, nullptr); Expect(direction, 1, 0, 0);
    camera.GetPosition(&position); Expect(position, 3, 5, 7);
    target.SetPosition3f(3, 5, 17); view.Scene().PrepareCameras(); camera.GetOrientation(&direction, nullptr, nullptr); Expect(direction, 0, 0, 1);
    camera.GetPosition(&position); Expect(position, 3, 5, 7);
    view.Scene().RemoveObject(reinterpret_cast<CKRenderObject *>(&camera)); camera.SetTarget(nullptr);
}
TEST_F(TargetCameraStateTest, FramePreparationUsesTheParentedTargetsWorldOrigin) {
    TargetCameraViewProbe view(Context); TargetCameraStateProbe camera(Context); RCK3dEntity parent(Context), target(Context);
    VxMatrix parentMatrix = VxMatrix::Identity(); parentMatrix[0].Set(0, 1, 0); parentMatrix[1].Set(-1, 0, 0); parentMatrix[3].Set(20, 30, 40);
    parent.SetWorldMatrix(parentMatrix); target.SetParent(API(parent), FALSE); target.SetPosition3f(3, 4, 5, API(parent));
    camera.SetPosition3f(6, 33, 45); camera.SetTarget(API(target));
    view.Scene().AddObject(reinterpret_cast<CKRenderObject *>(&camera)); view.Scene().PrepareCameras();
    VxVector direction, position; camera.GetOrientation(&direction, nullptr, nullptr); Expect(direction, 1, 0, 0);
    camera.GetPosition(&position); Expect(position, 6, 33, 45);
    view.Scene().RemoveObject(reinterpret_cast<CKRenderObject *>(&camera)); camera.SetTarget(nullptr); target.SetParent(nullptr, TRUE);
}
TEST_F(TargetCameraStateTest, ClearingTheTargetLeavesAFreeCameraPoseUnchangedOnNextPreparation) {
    TargetCameraViewProbe view(Context); TargetCameraStateProbe camera(Context); RCK3dEntity target(Context);
    target.SetPosition3f(10, 0, 0); camera.SetTarget(API(target)); view.Scene().AddObject(reinterpret_cast<CKRenderObject *>(&camera));
    view.Scene().PrepareCameras(); camera.SetTarget(nullptr); camera.Roll(.4f); const VxMatrix expected = camera.GetWorldMatrix();
    view.Scene().PrepareCameras(); const VxMatrix &actual = camera.GetWorldMatrix();
    for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column) EXPECT_FLOAT_EQ(expected[row][column], actual[row][column]);
    view.Scene().RemoveObject(reinterpret_cast<CKRenderObject *>(&camera));
}
