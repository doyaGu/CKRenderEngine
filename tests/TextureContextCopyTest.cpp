#include "TextureTestObserver.h"

class TextureContextCopyTest : public TextureTestWorld {};

TEST_F(TextureContextCopyTest, MatchingWholeImageUploadsExactPixelsWithoutDestinationRegion) {
    TextureTestProbe texture(Context); CKDWORD original[] = {0,0,0,0}; Pixels(texture, original, 2, 2);
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE)); CKDWORD pixels[] = {0xFF112233,0xFF445566,0xFF778899,0xFFAABBCC};
    ASSERT_TRUE(texture.ApplyContextCopy(Device, Image(pixels, 2, 2), nullptr, 0));
    ASSERT_EQ(2, State.Loads); EXPECT_FALSE(State.Uploads[1].HasRegion);
    EXPECT_EQ(0, memcmp(pixels, State.Uploads[1].Bytes, sizeof(pixels)));
}
TEST_F(TextureContextCopyTest, PositionedCopyPreservesOutsideByPassingExactDestinationRegion) {
    TextureTestProbe texture(Context); CKDWORD original[16] = {}; Pixels(texture, original, 4, 4); ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
    CKDWORD pixels[] = {0xFF112233,0xFF445566,0xFF778899,0xFFAABBCC}; VxRect destination(1,1,3,3);
    ASSERT_TRUE(texture.ApplyContextCopy(Device, Image(pixels, 2, 2), &destination, 0));
    const auto &upload = State.Uploads[1]; ASSERT_TRUE(upload.HasRegion);
    EXPECT_EQ(1, upload.Region.left); EXPECT_EQ(1, upload.Region.top); EXPECT_EQ(3, upload.Region.right); EXPECT_EQ(3, upload.Region.bottom);
    EXPECT_EQ(0, memcmp(pixels, upload.Bytes, sizeof(pixels)));
}
TEST_F(TextureContextCopyTest, NegativeDestinationClipsSourceOriginInBothAxes) {
    TextureTestProbe texture(Context); CKDWORD original[4] = {}; Pixels(texture, original, 2, 2); ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
    CKDWORD pixels[] = {1,2,3,4,5,6,7,8,9}; VxRect destination(-1,-1,2,2);
    ASSERT_TRUE(texture.ApplyContextCopy(Device, Image(pixels, 3, 3), &destination, 0));
    const auto &upload = State.Uploads[1]; EXPECT_EQ(2, upload.Image.Width); EXPECT_EQ(2, upload.Image.Height);
    const CKDWORD expected[] = {5,6,8,9}; EXPECT_EQ(0, memcmp(expected, upload.Bytes, sizeof(expected)));
    EXPECT_EQ(0, upload.Region.left); EXPECT_EQ(0, upload.Region.top); EXPECT_EQ(2, upload.Region.right); EXPECT_EQ(2, upload.Region.bottom);
}
TEST_F(TextureContextCopyTest, RightBottomClipRetainsSourcePitchAcrossRows) {
    TextureTestProbe texture(Context); CKDWORD original[9] = {}; Pixels(texture, original, 3, 3); ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
    CKDWORD pixels[] = {1,2,3,4,5,6,7,8,9}; VxRect destination(1,1,4,4);
    ASSERT_TRUE(texture.ApplyContextCopy(Device, Image(pixels, 3, 3), &destination, 0));
    const CKDWORD expected[] = {1,2,4,5}; EXPECT_EQ(0, memcmp(expected, State.Uploads[1].Bytes, sizeof(expected)));
    EXPECT_EQ(3, State.Uploads[1].Region.right); EXPECT_EQ(3, State.Uploads[1].Region.bottom);
}
TEST_F(TextureContextCopyTest, SourcePaddingIsSkippedWhileCopyingActualPixelRows) {
    TextureTestProbe texture(Context); CKDWORD original[6] = {}; Pixels(texture, original, 3, 2); ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
    CKDWORD padded[] = {1,2,3,0xDEADBEEF,4,5,6,0xBADCAFE};
    ASSERT_TRUE(texture.ApplyContextCopy(Device, Image(padded, 3, 2, 16), nullptr, 0));
    const CKDWORD expected[] = {1,2,3,4,5,6}; EXPECT_EQ(0, memcmp(expected, State.Uploads[1].Bytes, sizeof(expected)));
}
TEST_F(TextureContextCopyTest, EnlargedDestinationUsesPointSamplesAtPixelCenters) {
    TextureTestProbe texture(Context); CKDWORD original[16] = {}; Pixels(texture, original, 4, 4); ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
    CKDWORD pixels[] = {1,2,3,4}; VxRect destination(0,0,4,4);
    ASSERT_TRUE(texture.ApplyContextCopy(Device, Image(pixels, 2, 2), &destination, 0));
    const auto &upload = State.Uploads[1]; ASSERT_EQ(4, upload.Image.Width); ASSERT_EQ(4, upload.Image.Height);
    const CKDWORD expected[] = {1,1,2,2,1,1,2,2,3,3,4,4,3,3,4,4}; EXPECT_EQ(0, memcmp(expected, upload.Bytes, sizeof(expected)));
}
TEST_F(TextureContextCopyTest, ReducedDestinationUsesCenterSamplesRatherThanTopLeftCrop) {
    TextureTestProbe texture(Context); CKDWORD original[4] = {}; Pixels(texture, original, 2, 2); ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
    CKDWORD pixels[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}; VxRect destination(0,0,2,2);
    ASSERT_TRUE(texture.ApplyContextCopy(Device, Image(pixels, 4, 4), &destination, 0));
    const CKDWORD expected[] = {6,8,14,16}; EXPECT_EQ(0, memcmp(expected, State.Uploads[1].Bytes, sizeof(expected)));
}
TEST_F(TextureContextCopyTest, WholeTargetWithoutDestinationScalesDifferentSizeReadback) {
    TextureTestProbe texture(Context); CKDWORD original[4] = {}; Pixels(texture, original, 2, 2); ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
    CKDWORD pixels[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    ASSERT_TRUE(texture.ApplyContextCopy(Device, Image(pixels, 4, 4), nullptr, 0));
    EXPECT_EQ(2, State.Uploads[1].Image.Width); EXPECT_EQ(2, State.Uploads[1].Image.Height);
    const CKDWORD expected[] = {6,8,14,16}; EXPECT_EQ(0, memcmp(expected, State.Uploads[1].Bytes, sizeof(expected)));
}
TEST_F(TextureContextCopyTest, ClippingAfterScalingUsesOriginalDestinationCoordinates) {
    TextureTestProbe texture(Context); CKDWORD original[8] = {}; Pixels(texture, original, 2, 4); ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
    CKDWORD pixels[] = {1,2,3,4}; VxRect destination(-2,0,2,4);
    ASSERT_TRUE(texture.ApplyContextCopy(Device, Image(pixels, 2, 2), &destination, 0));
    ASSERT_EQ(2, State.Uploads[1].Image.Width); ASSERT_EQ(4, State.Uploads[1].Image.Height);
    const CKDWORD expected[] = {2,2,2,2,4,4,4,4}; EXPECT_EQ(0, memcmp(expected, State.Uploads[1].Bytes, sizeof(expected)));
}
TEST_F(TextureContextCopyTest, DifferentTargetFormatConvertsBeforeClippingRealPixels) {
    TextureTestProbe texture(Context); CKDWORD original[4] = {}; Pixels(texture, original, 2, 2); texture.SetDesiredVideoFormat(_16_RGB565);
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE)); CKDWORD pixels[] = {0xFFFF0000,0xFF00FF00,0xFF0000FF,0xFFFFFFFF}; VxRect destination(-1,0,1,2);
    ASSERT_TRUE(texture.ApplyContextCopy(Device, Image(pixels, 2, 2), &destination, 0));
    ASSERT_EQ(16, State.Uploads[1].Image.BitsPerPixel); const CKWORD expected[] = {0x07E0,0xFFFF};
    EXPECT_EQ(0, memcmp(expected, State.Uploads[1].Bytes, sizeof(expected))); EXPECT_EQ(1, State.Uploads[1].Region.right);
}
TEST_F(TextureContextCopyTest, NaturalUploadFailureIsPropagatedWithoutDestroyingDestinationTexture) {
    TextureTestProbe texture(Context); CKDWORD pixels[] = {0xFFFFFFFF}; Pixels(texture, pixels, 1, 1);
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE)); State.FailLoad = State.Loads;
    EXPECT_FALSE(texture.ApplyContextCopy(Device, Image(pixels, 1, 1), nullptr, 0)); EXPECT_TRUE(texture.IsInVideoMemory()); EXPECT_EQ(0, State.Deletes);
}
TEST_F(TextureContextCopyTest, CompressedDestinationReceivesARGBReadbackAndExactPixelRegion) {
    TextureTestProbe texture(Context); CKDWORD original[4] = {}; Pixels(texture, original, 2, 2); texture.SetDesiredVideoFormat(_DXT3);
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE)); CKDWORD pixels[] = {0x12224466,0x4488AACC,0x66CCEE22,0x88AABBCC}; VxRect destination(-1,0,1,2);
    ASSERT_TRUE(texture.ApplyContextCopy(Device, Image(pixels, 2, 2), &destination, 0));
    ASSERT_EQ(2, State.Loads); EXPECT_EQ(_DXT3, VxImageDesc2PixelFormat(State.Created[0].Format));
    EXPECT_EQ(32, State.Uploads[1].Image.BitsPerPixel); EXPECT_EQ(1, State.Uploads[1].Image.Width);
    const CKDWORD expected[] = {0x4488AACC,0x88AABBCC}; EXPECT_EQ(0, memcmp(expected, State.Uploads[1].Bytes, sizeof(expected)));
    EXPECT_EQ(0, State.Uploads[1].Region.left); EXPECT_EQ(1, State.Uploads[1].Region.right);
}
TEST_F(TextureContextCopyTest, EmptyOutsideRegionsAndUnsupportedFaceDoNotUpload) {
    TextureTestProbe texture(Context); CKDWORD pixels[] = {0xFFFFFFFF}; Pixels(texture, pixels, 1, 1); ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
    const VxRect destinations[] = {VxRect(0,0,0,1),VxRect(0,0,1,0),VxRect(2,2,3,3),VxRect(-2,-2,-1,-1)};
    for (const VxRect &destination : destinations) EXPECT_FALSE(texture.ApplyContextCopy(Device, Image(pixels, 1, 1), &destination, 0));
    EXPECT_FALSE(texture.ApplyContextCopy(Device, Image(pixels, 1, 1), nullptr, 1)); EXPECT_EQ(1, State.Loads);
}
TEST_F(TextureContextCopyTest, CubeReadbackUploadsRequestedFaceAndNoOtherFace) {
    TextureTestProbe texture(Context); texture.SetCubeMap(TRUE);
    for (int face = 0; face < 6; ++face) { const CKDWORD pixel = 0xFF223340u + face; Pixels(texture, &pixel, 1, 1, face); }
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE)); CKDWORD pixel = 0xFFAABBCC;
    ASSERT_TRUE(texture.ApplyContextCopy(Device, Image(&pixel, 1, 1), nullptr, CKRST_CUBEFACE_ZNEG));
    ASSERT_EQ(7, State.Loads); EXPECT_EQ(CKRST_CUBEFACE_ZNEG, State.Uploads[6].Face); EXPECT_EQ(pixel, Pixel(State.Uploads[6], 0));
}
TEST_F(TextureContextCopyTest, QueueCapturesRectanglesAndDeliversPixelsAtCompletion) {
    TextureTestProbe texture(Context); CKDWORD original[16] = {}; Pixels(texture, original, 4, 4); ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
    VxRect source(2,3,4,5), destination(1,1,3,3); ASSERT_TRUE(texture.CopyContext(Device, &source, &destination, 0));
    ASSERT_EQ(1, State.Readbacks); EXPECT_EQ(2, State.ReadbackRegion.left); EXPECT_EQ(3, State.ReadbackRegion.top);
    EXPECT_EQ(4, State.ReadbackRegion.right); EXPECT_EQ(5, State.ReadbackRegion.bottom); EXPECT_EQ(VXBUFFER_BACKBUFFER, State.ReadbackBuffer);
    destination = VxRect(9,9,10,10); EXPECT_EQ(1, State.Loads); CKDWORD pixels[] = {1,2,3,4}; VxImageDescEx image = Image(pixels, 2, 2); State.Complete(&image);
    ASSERT_EQ(2, State.Loads); EXPECT_EQ(1, State.Uploads[1].Region.left); EXPECT_EQ(3, State.Uploads[1].Region.right); EXPECT_EQ(0, memcmp(pixels, State.Uploads[1].Bytes, sizeof(pixels)));
}
TEST_F(TextureContextCopyTest, RequestFailureAndCompletionFailureDoNotClaimPixelUploads) {
    TextureTestProbe texture(Context); CKDWORD original[] = {0xFFFFFFFF}; Pixels(texture, original, 1, 1); ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
    State.ReadbackResult = FALSE; EXPECT_FALSE(texture.CopyContext(Device, nullptr, nullptr, 0)); EXPECT_EQ(nullptr, State.Pending);
    State.ReadbackResult = TRUE; ASSERT_TRUE(texture.CopyContext(Device, nullptr, nullptr, 0)); State.Complete(nullptr, FALSE); EXPECT_EQ(1, State.Loads);
}
TEST_F(TextureContextCopyTest, DeletedQueuedTargetIsNotDereferencedAtCompletion) {
    TextureTestProbe *texture = new TextureTestProbe(Context); CKDWORD pixel = 0xFFFFFFFF; Pixels(*texture, &pixel, 1, 1);
    ASSERT_TRUE(texture->SystemToVideoMemory(Device, FALSE)); ASSERT_TRUE(texture->CopyContext(Device, nullptr, nullptr, 0));
    delete texture; VxImageDescEx image = Image(&pixel, 1, 1); State.Complete(&image); EXPECT_EQ(1, State.Loads); EXPECT_EQ(1, State.Deletes);
}
TEST_F(TextureContextCopyTest, CopyRejectsMissingDeviceResidencyAndInvalidCubeFaceWithoutReadback) {
    TextureTestProbe texture(Context); CKDWORD pixel = 0xFFFFFFFF; Pixels(texture, &pixel, 1, 1);
    EXPECT_FALSE(texture.CopyContext(nullptr, nullptr, nullptr, 0)); EXPECT_FALSE(texture.CopyContext(Device, nullptr, nullptr, 0));
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE)); EXPECT_FALSE(texture.CopyContext(Device, nullptr, nullptr, -1));
    EXPECT_FALSE(texture.CopyContext(Device, nullptr, nullptr, 6)); EXPECT_FALSE(texture.CopyContext(Device, nullptr, nullptr, 1)); EXPECT_EQ(0, State.Readbacks);
}
