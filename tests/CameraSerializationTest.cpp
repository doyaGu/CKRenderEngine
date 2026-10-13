#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKCamera.h"
#include "RCKRenderObject.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class DerivedSerializationCamera : public RCKCamera {
public:
    explicit DerivedSerializationCamera(CKContext *context) : RCKCamera(context) {}
    CK_CLASSID GetClassID() override { return CKCID_TARGETCAMERA; }
};

class CameraSerializationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static void Configure(RCKCamera &camera) {
        camera.SetProjectionType(7);
        camera.SetFov(.75f);
        camera.SetOrthographicZoom(12.5f);
        camera.SetAspectRatio(1920, 1080);
        camera.SetFrontPlane(.25f);
        camera.SetBackPlane(8192);
    }
    static void ExpectState(RCKCamera &camera, int projection, float fov, float zoom,
                            int width, int height, float front, float back) {
        EXPECT_EQ(projection, camera.GetProjectionType());
        EXPECT_FLOAT_EQ(fov, camera.GetFov());
        EXPECT_FLOAT_EQ(zoom, camera.GetOrthographicZoom());
        int actualWidth = 0, actualHeight = 0;
        camera.GetAspectRatio(actualWidth, actualHeight);
        EXPECT_EQ(width, actualWidth);
        EXPECT_EQ(height, actualHeight);
        EXPECT_FLOAT_EQ(front, camera.GetFrontPlane());
        EXPECT_FLOAT_EQ(back, camera.GetBackPlane());
    }
    static void CurrentWire(CKStateChunk &chunk, CKDWORD dimensions = 0x1234abcd) {
        chunk.WriteIdentifier(0x0fc00000);
        chunk.WriteInt(-7);
        chunk.WriteFloat(1.25f);
        chunk.WriteFloat(17.5f);
        chunk.WriteDword(dimensions);
        chunk.WriteFloat(.125f);
        chunk.WriteFloat(16384);
    }
    static void LegacyWire(CKStateChunk &chunk) {
        chunk.WriteIdentifier(0x00400000); chunk.WriteFloat(1.25f);
        chunk.WriteIdentifier(0x00800000); chunk.WriteInt(-7);
        chunk.WriteIdentifier(0x01000000); chunk.WriteFloat(17.5f);
        chunk.WriteIdentifier(0x02000000); chunk.WriteInt(-70123); chunk.WriteInt(190001);
        chunk.WriteIdentifier(0x04000000); chunk.WriteFloat(.125f); chunk.WriteFloat(16384);
    }
    static CKERROR Load(RCKCamera &camera, CKStateChunk &chunk, CKFile *file = nullptr) {
        chunk.CloseChunk();
        return camera.Load(&chunk, file);
    }
    static void ExpectPayload(CKStateChunk &chunk, CKDWORD dimensions) {
        ASSERT_TRUE(chunk.SeekIdentifier(0x0fc00000));
        EXPECT_EQ(7, chunk.ReadInt());
        EXPECT_FLOAT_EQ(.75f, chunk.ReadFloat());
        EXPECT_FLOAT_EQ(12.5f, chunk.ReadFloat());
        EXPECT_EQ(dimensions, chunk.ReadDword());
        EXPECT_FLOAT_EQ(.25f, chunk.ReadFloat());
        EXPECT_FLOAT_EQ(8192, chunk.ReadFloat());
    }
};
}

TEST_F(CameraSerializationTest, NullLoadPreservesCameraValuesAndModificationFlag) {
    RCKCamera camera(Context); Configure(camera);
    camera.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, camera.Load(nullptr, nullptr));
    ExpectState(camera, 7, .75f, 12.5f, 1920, 1080, .25f, 8192);
    EXPECT_TRUE(camera.GetObjectFlags() & CK_OBJECT_UPTODATE);
}

TEST_F(CameraSerializationTest, CurrentIndependentWireUsesUnsignedLowAndHighWords) {
    RCKCamera camera(Context);
    CKStateChunk chunk(CKCID_CAMERA, nullptr); chunk.StartWrite(); chunk.SetDataVersion(5);
    CurrentWire(chunk); camera.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    ASSERT_EQ(CK_OK, Load(camera, chunk));
    ExpectState(camera, -7, 1.25f, 17.5f, 43981, 4660, .125f, 16384);
    EXPECT_FALSE(camera.GetObjectFlags() & CK_OBJECT_UPTODATE);
}

TEST_F(CameraSerializationTest, CurrentWireRemainsTheSameForSupportedLaterChunkVersions) {
    const int versions[] = {5, 6, 8};
    for (int version : versions) {
        RCKCamera camera(Context);
        CKStateChunk chunk(CKCID_CAMERA, nullptr); chunk.StartWrite(); chunk.SetDataVersion(version);
        CurrentWire(chunk, 0xfedc8000); ASSERT_EQ(CK_OK, Load(camera, chunk));
        ExpectState(camera, -7, 1.25f, 17.5f, 32768, 65244, .125f, 16384);
    }
}

TEST_F(CameraSerializationTest, CurrentFileContextLoadsLiteralCameraPayload) {
    RCKCamera camera(Context); CKFile file(Context);
    CKStateChunk chunk(CKCID_CAMERA, nullptr); chunk.StartWrite(); chunk.SetDataVersion(5);
    CurrentWire(chunk, 0x00000000); ASSERT_EQ(CK_OK, Load(camera, chunk, &file));
    ExpectState(camera, -7, 1.25f, 17.5f, 0, 0, .125f, 16384);
}

TEST_F(CameraSerializationTest, LegacyIndependentWirePreservesSignedFullWidthDimensions) {
    const int versions[] = {0, 4};
    for (int version : versions) {
        RCKCamera camera(Context);
        CKStateChunk chunk(CKCID_CAMERA, nullptr); chunk.StartWrite(); chunk.SetDataVersion(version);
        LegacyWire(chunk); ASSERT_EQ(CK_OK, Load(camera, chunk));
        ExpectState(camera, -7, 1.25f, 17.5f, -70123, 190001, .125f, 16384);
    }
}

TEST_F(CameraSerializationTest, LegacyFovOnlyPreservesEveryOtherCameraProperty) {
    RCKCamera camera(Context); Configure(camera);
    CKStateChunk chunk(CKCID_CAMERA, nullptr); chunk.StartWrite(); chunk.SetDataVersion(4);
    chunk.WriteIdentifier(0x00400000); chunk.WriteFloat(1.25f);
    ASSERT_EQ(CK_OK, Load(camera, chunk));
    ExpectState(camera, 7, 1.25f, 12.5f, 1920, 1080, .25f, 8192);
}

TEST_F(CameraSerializationTest, LegacyProjectionOnlyPreservesEveryOtherCameraProperty) {
    RCKCamera camera(Context); Configure(camera);
    CKStateChunk chunk(CKCID_CAMERA, nullptr); chunk.StartWrite(); chunk.SetDataVersion(4);
    chunk.WriteIdentifier(0x00800000); chunk.WriteInt(-7);
    ASSERT_EQ(CK_OK, Load(camera, chunk));
    ExpectState(camera, -7, .75f, 12.5f, 1920, 1080, .25f, 8192);
}

TEST_F(CameraSerializationTest, LegacyZoomOnlyPreservesEveryOtherCameraProperty) {
    RCKCamera camera(Context); Configure(camera);
    CKStateChunk chunk(CKCID_CAMERA, nullptr); chunk.StartWrite(); chunk.SetDataVersion(4);
    chunk.WriteIdentifier(0x01000000); chunk.WriteFloat(-17.5f);
    ASSERT_EQ(CK_OK, Load(camera, chunk));
    ExpectState(camera, 7, .75f, -17.5f, 1920, 1080, .25f, 8192);
}

TEST_F(CameraSerializationTest, LegacyAspectOnlyPreservesEveryOtherCameraProperty) {
    RCKCamera camera(Context); Configure(camera);
    CKStateChunk chunk(CKCID_CAMERA, nullptr); chunk.StartWrite(); chunk.SetDataVersion(4);
    chunk.WriteIdentifier(0x02000000); chunk.WriteInt(-70123); chunk.WriteInt(190001);
    ASSERT_EQ(CK_OK, Load(camera, chunk));
    ExpectState(camera, 7, .75f, 12.5f, -70123, 190001, .25f, 8192);
}

TEST_F(CameraSerializationTest, LegacyPlanesOnlyPreservesEveryOtherCameraProperty) {
    RCKCamera camera(Context); Configure(camera);
    CKStateChunk chunk(CKCID_CAMERA, nullptr); chunk.StartWrite(); chunk.SetDataVersion(4);
    chunk.WriteIdentifier(0x04000000); chunk.WriteFloat(-.125f); chunk.WriteFloat(16384);
    ASSERT_EQ(CK_OK, Load(camera, chunk));
    ExpectState(camera, 7, .75f, 12.5f, 1920, 1080, -.125f, 16384);
}

TEST_F(CameraSerializationTest, VersionBoundaryIgnoresTheOtherCameraWireFormat) {
    for (int version : {4, 5}) {
        RCKCamera camera(Context); Configure(camera);
        CKStateChunk chunk(CKCID_CAMERA, nullptr); chunk.StartWrite(); chunk.SetDataVersion(version);
        if (version < 5) CurrentWire(chunk); else LegacyWire(chunk);
        camera.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
        ASSERT_EQ(CK_OK, Load(camera, chunk));
        ExpectState(camera, 7, .75f, 12.5f, 1920, 1080, .25f, 8192);
        EXPECT_FALSE(camera.GetObjectFlags() & CK_OBJECT_UPTODATE);
    }
}

TEST_F(CameraSerializationTest, MissingCameraIdentifiersPreserveValuesAndClearModificationFlag) {
    for (int version : {4, 5}) {
        RCKCamera camera(Context); Configure(camera);
        CKStateChunk chunk(CKCID_CAMERA, nullptr); chunk.StartWrite(); chunk.SetDataVersion(version);
        chunk.WriteIdentifier(0x76543210); chunk.WriteInt(73);
        camera.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
        ASSERT_EQ(CK_OK, Load(camera, chunk));
        ExpectState(camera, 7, .75f, 12.5f, 1920, 1080, .25f, 8192);
        EXPECT_FALSE(camera.GetObjectFlags() & CK_OBJECT_UPTODATE);
    }
}

TEST_F(CameraSerializationTest, CurrentCameraLoadAlsoAppliesIndependentAncestorPose) {
    RCKCamera camera(Context); CKFile file(Context);
    CKStateChunk chunk(CKCID_CAMERA, nullptr); chunk.StartWrite(); chunk.SetDataVersion(6);
    chunk.WriteIdentifier(CK_STATESAVE_3DENTITYNDATA); chunk.WriteDword(0); chunk.WriteDword(VX_MOVEABLE_VISIBLE);
    chunk.WriteVector(VxVector(1, 0, 0)); chunk.WriteVector(VxVector(0, 1, 0));
    chunk.WriteVector(VxVector(0, 0, 1)); chunk.WriteVector(VxVector(7, 11, 13));
    CurrentWire(chunk); ASSERT_EQ(CK_OK, Load(camera, chunk, &file));
    ExpectState(camera, -7, 1.25f, 17.5f, 43981, 4660, .125f, 16384);
    EXPECT_FLOAT_EQ(7, camera.GetWorldMatrix()[3][0]);
    EXPECT_FLOAT_EQ(11, camera.GetWorldMatrix()[3][1]);
    EXPECT_FLOAT_EQ(13, camera.GetWorldMatrix()[3][2]);
}

TEST_F(CameraSerializationTest, FilelessZeroMaskDelegatesWithoutEmittingCameraPayload) {
    RCKCamera camera(Context); Configure(camera);
    CKStateChunk *chunk = camera.Save(nullptr, 0); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    EXPECT_NE(CKCID_CAMERA, chunk->GetChunkClassID());
    EXPECT_FALSE(chunk->SeekIdentifier(0x0fc00000));
    DeleteCKStateChunk(chunk);
}

TEST_F(CameraSerializationTest, FilelessUnrelatedMaskRetainsEntityPayloadWithoutCameraPayload) {
    RCKCamera camera(Context); Configure(camera);
    CKStateChunk *chunk = camera.Save(nullptr, CK_STATESAVE_3DENTITYMATRIX);
    ASSERT_NE(nullptr, chunk); EXPECT_EQ(CKCID_3DENTITY, chunk->GetChunkClassID()); chunk->StartRead();
    EXPECT_TRUE(chunk->SeekIdentifier(CK_STATESAVE_3DENTITYNDATA));
    EXPECT_FALSE(chunk->SeekIdentifier(0x0fc00000)); DeleteCKStateChunk(chunk);
}

TEST_F(CameraSerializationTest, OneCameraFlagEmitsTheEntireCombinedPayload) {
    RCKCamera camera(Context); Configure(camera);
    CKStateChunk *chunk = camera.Save(nullptr, CK_STATESAVE_CAMERAFOV); ASSERT_NE(nullptr, chunk);
    EXPECT_EQ(CKCID_CAMERA, chunk->GetChunkClassID()); EXPECT_EQ(-1, chunk->GetCurrentPos()); chunk->StartRead();
    ExpectPayload(*chunk, 0x04380780); DeleteCKStateChunk(chunk);
}

TEST_F(CameraSerializationTest, FileZeroMaskWritesCombinedCameraAndAncestorPose) {
    RCKCamera camera(Context); Configure(camera); CKFile file(Context);
    VxMatrix pose = VxMatrix::Identity(); pose[3][0] = 23; pose[3][1] = -17;
    camera.SetWorldMatrix(pose, TRUE);
    CKStateChunk *chunk = camera.Save(&file, 0); ASSERT_NE(nullptr, chunk);
    EXPECT_EQ(CKCID_CAMERA, chunk->GetChunkClassID()); EXPECT_EQ(-1, chunk->GetCurrentPos()); chunk->StartRead();
    ExpectPayload(*chunk, 0x04380780);
    ASSERT_TRUE(chunk->SeekIdentifier(CK_STATESAVE_3DENTITYNDATA)); chunk->ReadDword(); chunk->ReadDword();
    const float expected[] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 23, -17, 0};
    for (float value : expected) EXPECT_FLOAT_EQ(value, chunk->ReadFloat());
    DeleteCKStateChunk(chunk);
}

TEST_F(CameraSerializationTest, SaveMasksLargeDimensionWordsIndependently) {
    RCKCamera camera(Context); Configure(camera); camera.SetAspectRatio(0x1234abcd, 0x45671234);
    CKStateChunk *chunk = camera.Save(nullptr, CK_STATESAVE_CAMERAONLY); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    ExpectPayload(*chunk, 0x1234abcd); DeleteCKStateChunk(chunk);
}

TEST_F(CameraSerializationTest, SaveNegativeWidthDoesNotOverwriteTheHeightWord) {
    RCKCamera camera(Context); Configure(camera); camera.SetAspectRatio(-1, 1080);
    CKStateChunk *chunk = camera.Save(nullptr, CK_STATESAVE_CAMERAONLY); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    ExpectPayload(*chunk, 0x0438ffff); DeleteCKStateChunk(chunk);
}

TEST_F(CameraSerializationTest, SaveNegativeHeightKeepsThePositiveWidthWord) {
    RCKCamera camera(Context); Configure(camera); camera.SetAspectRatio(1920, -2);
    CKStateChunk *chunk = camera.Save(nullptr, CK_STATESAVE_CAMERAONLY); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    ExpectPayload(*chunk, 0xfffe0780); DeleteCKStateChunk(chunk);
}

TEST_F(CameraSerializationTest, DerivedCameraSaveLeavesCombinedPayloadWritableForSubclass) {
    DerivedSerializationCamera camera(Context); Configure(camera);
    CKStateChunk *chunk = camera.Save(nullptr, CK_STATESAVE_CAMERAONLY); ASSERT_NE(nullptr, chunk);
    EXPECT_EQ(CKCID_CAMERA, chunk->GetChunkClassID()); EXPECT_GE(chunk->GetCurrentPos(), 0);
    chunk->WriteIdentifier(0x76543210); chunk->WriteInt(79); chunk->CloseChunk(); chunk->StartRead();
    ExpectPayload(*chunk, 0x04380780);
    ASSERT_TRUE(chunk->SeekIdentifier(0x76543210)); EXPECT_EQ(79, chunk->ReadInt()); DeleteCKStateChunk(chunk);
}
