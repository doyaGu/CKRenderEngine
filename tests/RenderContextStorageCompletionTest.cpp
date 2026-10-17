#include "TextureTestObserver.h"
#include "RCK2dEntity.h"
#include "RCKMaterial.h"
#include "RCKCamera.h"

namespace {
class RenderContextStorageCompletionTest : public TextureTestWorld {};

class StorageFrameContext : public RCKRenderContext {
public:
    StorageFrameContext(CKContext *context, CKRasterizerContext *boundary) : RCKRenderContext(context) {
        m_RasterizerContext = boundary;
        m_Active = TRUE;
        m_ViewportData.ViewX = 7;
        m_ViewportData.ViewY = 11;
        m_ViewportData.ViewWidth = 100;
        m_ViewportData.ViewHeight = 80;
    }
    ~StorageFrameContext() override { m_RasterizerContext = nullptr; }
    void PrepareCameras(CK_RENDER_FLAGS) override {}
    CKERROR Clear(CK_RENDER_FLAGS, CKDWORD) override { return CK_OK; }
    CKERROR DrawScene(CK_RENDER_FLAGS) override { return CK_OK; }
    CKERROR BackToFront(CK_RENDER_FLAGS) override { return CK_OK; }
};

static void Matrix(const VxMatrix &actual, const VxMatrix &expected) {
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            EXPECT_FLOAT_EQ(expected[row][column], actual[row][column]);
}
}

TEST_F(RenderContextStorageCompletionTest, ObjectExtentsOwnTheirRectanglesButBorrowThePublishedObjects) {
    RCK3dEntity *first = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    RCK3dEntity *last = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, last);
    VxRect source(1, 2, 3, 4);
    Device->AddExtents2D(source, first);
    for (int i = 0; i < 63; ++i)
        Device->AddExtents2D(VxRect(float(i), 5, float(i + 20), 25), last);
    source = VxRect(100, 200, 300, 400);
    ASSERT_EQ(64, Device->m_ObjectExtents.Size());
    const CKObjectExtents &oldest = Device->m_ObjectExtents[0];
    EXPECT_FLOAT_EQ(1, oldest.m_Rect.left);
    EXPECT_FLOAT_EQ(4, oldest.m_Rect.bottom);
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(first), oldest.m_Entity);
    EXPECT_EQ(0u, oldest.m_Camera);
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(last), Device->m_ObjectExtents.Back().m_Entity);
    EXPECT_FLOAT_EQ(82, Device->m_ObjectExtents.Back().m_Rect.right);
    const CK_ID firstId = first->GetID(), lastId = last->GetID();
    Device->DetachAll();
    EXPECT_EQ(0, Device->m_ObjectExtents.Size());
    EXPECT_EQ(first, Context->GetObject(firstId));
    EXPECT_EQ(last, Context->GetObject(lastId));
}

TEST_F(RenderContextStorageCompletionTest, RenderExtentsCaptureFlagsAndCameraIdsByValueAcrossFrames) {
    CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
    CKBuildClassHierarchyTable();
    RCKCamera *first = static_cast<RCKCamera *>(Context->CreateObject(CKCID_CAMERA));
    RCKCamera *second = static_cast<RCKCamera *>(Context->CreateObject(CKCID_CAMERA));
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);
    StorageFrameContext frame(Context, &State);
    Context->GetTimeManager()->ChangeLimitOptions(CK_FRAMERATE_FREE);
    frame.AttachViewpointToCamera(reinterpret_cast<CKCamera *>(first));
    const CK_RENDER_FLAGS flags = static_cast<CK_RENDER_FLAGS>(
        CK_RENDER_DONOTUPDATEEXTENTS | CK_RENDER_DOBACKTOFRONT | CK_RENDER_CLEARBACK | CK_RENDER_PLAYERCONTEXT);
    ASSERT_EQ(CK_OK, frame.Render(flags));
    frame.m_ViewportData.ViewX = 19;
    frame.m_ViewportData.ViewY = 23;
    frame.m_ViewportData.ViewWidth = 20;
    frame.m_ViewportData.ViewHeight = 30;
    frame.AttachViewpointToCamera(reinterpret_cast<CKCamera *>(second));
    const CK_RENDER_FLAGS next = static_cast<CK_RENDER_FLAGS>(CK_RENDER_DONOTUPDATEEXTENTS | CK_RENDER_DOBACKTOFRONT);
    ASSERT_EQ(CK_OK, frame.Render(next));
    ASSERT_EQ(2, frame.m_Extents.Size());
    EXPECT_EQ(CKDWORD(flags), frame.m_Extents[0].m_Flags);
    EXPECT_EQ(first->GetID(), frame.m_Extents[0].m_Camera);
    EXPECT_FLOAT_EQ(7, frame.m_Extents[0].m_Rect.left);
    EXPECT_FLOAT_EQ(91, frame.m_Extents[0].m_Rect.bottom);
    EXPECT_EQ(CKDWORD(next), frame.m_Extents[1].m_Flags);
    EXPECT_EQ(second->GetID(), frame.m_Extents[1].m_Camera);
    EXPECT_FLOAT_EQ(19, frame.m_Extents[1].m_Rect.left);
    EXPECT_FLOAT_EQ(53, frame.m_Extents[1].m_Rect.bottom);
}

TEST_F(RenderContextStorageCompletionTest, QueuedRequestsKeepIndependentTargetsFacesAndDestinationSnapshots) {
    TextureTestProbe cube(Context), ordinary(Context);
    cube.SetCubeMap(TRUE);
    CKDWORD initial[16] = {};
    for (int face = 0; face < 6; ++face)
        Pixels(cube, initial, 4, 4, face);
    Pixels(ordinary, initial, 2, 2);
    ASSERT_TRUE(cube.SystemToVideoMemory(Device, FALSE));
    ASSERT_TRUE(ordinary.SystemToVideoMemory(Device, FALSE));
    const CKDWORD cubeHandle = cube.GetRstTextureIndex(), ordinaryHandle = ordinary.GetRstTextureIndex();
    VxRect source(2, 3, 4, 5), destination(1, 1, 3, 3);
    ASSERT_TRUE(cube.CopyContext(Device, &source, &destination, CKRST_CUBEFACE_ZNEG));
    CKReadbackCallback firstCallback = State.Pending;
    void *firstRequest = State.PendingUser;
    State.Pending = nullptr;
    State.PendingUser = nullptr;
    ASSERT_TRUE(ordinary.CopyContext(Device, nullptr, nullptr, 0));
    destination = VxRect(9, 9, 10, 10);
    source = VxRect(20, 30, 40, 50);
    const int initialUploads = State.Loads;
    CKDWORD secondPixels[4] = {11, 12, 13, 14};
    VxImageDescEx secondImage = Image(secondPixels, 2, 2);
    State.Complete(&secondImage);
    CKDWORD firstPixels[4] = {21, 22, 23, 24};
    VxImageDescEx firstImage = Image(firstPixels, 2, 2);
    firstCallback(firstRequest, nullptr, VXBUFFER_BACKBUFFER, &firstImage, TRUE);
    ASSERT_EQ(initialUploads + 2, State.Loads);
    EXPECT_EQ(ordinaryHandle, State.Uploads[initialUploads].Handle);
    EXPECT_FALSE(State.Uploads[initialUploads].HasRegion);
    EXPECT_EQ(14u, Pixel(State.Uploads[initialUploads], 3));
    const TextureTestObserver::Upload &cubeUpload = State.Uploads[initialUploads + 1];
    EXPECT_EQ(cubeHandle, cubeUpload.Handle);
    EXPECT_EQ(CKRST_CUBEFACE_ZNEG, cubeUpload.Face);
    EXPECT_TRUE(cubeUpload.HasRegion);
    EXPECT_EQ(1, cubeUpload.Region.left);
    EXPECT_EQ(3, cubeUpload.Region.right);
    EXPECT_EQ(24u, Pixel(cubeUpload, 3));
    EXPECT_EQ(nullptr, Device->m_Readbacks);
}

TEST(RenderContextStorageGuardTest, EachStageAndTransformRestoresItsOwnSnapshotAfterIndependentMutation) {
    MaterialRenderObserver state;
    VxMatrix world = VxMatrix::Identity(), view = world, projection = world;
    world[3].x = 3;
    view[3].y = 5;
    projection[2].z = 7;
    state.SetTransformMatrix(VXMATRIX_WORLD, world);
    state.SetTransformMatrix(VXMATRIX_VIEW, view);
    state.SetTransformMatrix(VXMATRIX_PROJECTION, projection);
    for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) {
        state.SetTexture(100 + stage, stage);
        state.SetTextureStageState(stage, CKRST_TSS_MINFILTER, stage + 10);
        VxMatrix texture = VxMatrix::Identity();
        texture[3].Set(float(stage + 20), float(stage + 30), float(stage + 40), 1);
        state.SetTransformMatrix(static_cast<VXMATRIX_TYPE>(VXMATRIX_TEXTURE0 + stage), texture);
    }
    CKRenderContextStateGuard guard(&state);
    VxMatrix zero = VxMatrix::Identity();
    zero[3].Set(71, 73, 79, 1);
    state.SetTransformMatrix(VXMATRIX_WORLD, zero);
    state.SetTransformMatrix(VXMATRIX_VIEW, zero);
    state.SetTransformMatrix(VXMATRIX_PROJECTION, zero);
    for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) {
        state.SetTexture(200 + stage, stage);
        state.SetTextureStageState(stage, CKRST_TSS_MINFILTER, 50 + stage);
        state.SetTransformMatrix(static_cast<VXMATRIX_TYPE>(VXMATRIX_TEXTURE0 + stage), zero);
    }
    ASSERT_TRUE(guard.Restore());
    Matrix(state.Matrices[VXMATRIX_WORLD], world);
    Matrix(state.Matrices[VXMATRIX_VIEW], view);
    Matrix(state.Matrices[VXMATRIX_PROJECTION], projection);
    for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) {
        EXPECT_EQ(CKDWORD(100 + stage), state.Textures[stage]);
        EXPECT_EQ(CKDWORD(stage + 10), state.StageStates[stage][CKRST_TSS_MINFILTER]);
        EXPECT_FLOAT_EQ(float(stage + 20), state.Matrices[VXMATRIX_TEXTURE0 + stage][3].x);
        EXPECT_FLOAT_EQ(float(stage + 30), state.Matrices[VXMATRIX_TEXTURE0 + stage][3].y);
        EXPECT_FLOAT_EQ(float(stage + 40), state.Matrices[VXMATRIX_TEXTURE0 + stage][3].z);
    }
}

TEST(RenderContextStorageScratchTest, IndependentOwnersNeverAliasCachesOrIndexAndVertexAllocations) {
    UserDrawPrimitiveDataClass first, second;
    VxDrawPrimitiveData *one = first.GetStructure(CKRST_DP_TRANSFORM, 5);
    VxDrawPrimitiveData *two = second.GetStructure(CKRST_DP_TRANSFORM, 9);
    ASSERT_NE(nullptr, one->PositionPtr);
    ASSERT_NE(nullptr, two->PositionPtr);
    EXPECT_NE(one->PositionPtr, two->PositionPtr);
    CKWORD *firstIndices = first.GetIndices(12), *secondIndices = second.GetIndices(24);
    ASSERT_NE(nullptr, firstIndices);
    ASSERT_NE(nullptr, secondIndices);
    EXPECT_NE(firstIndices, secondIndices);
    static_cast<VxVector4 *>(one->PositionPtr)[4].Set(3, 5, 7, 1);
    static_cast<VxVector4 *>(two->PositionPtr)[8].Set(11, 13, 17, 1);
    firstIndices[11] = 4;
    secondIndices[23] = 8;
    second.GetStructure(CKRST_DP_DIFFUSE, 3);
    one = first.GetStructure(CKRST_DP_TRANSFORM, 5);
    EXPECT_FLOAT_EQ(7, static_cast<VxVector4 *>(one->PositionPtr)[4].z);
    EXPECT_EQ(4, first.GetIndices(1)[11]);
    EXPECT_EQ(8, second.GetIndices(1)[23]);
    second.ClearStructure();
    one = first.GetStructure(CKRST_DP_TRANSFORM, 5);
    EXPECT_FLOAT_EQ(7, static_cast<VxVector4 *>(one->PositionPtr)[4].z);
    EXPECT_EQ(5, first.m_MaxVertexCount);
    EXPECT_EQ(12, first.m_MaxIndexCount);
}
