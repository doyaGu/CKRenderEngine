#include "TextureTestObserver.h"

class TextureUploadTest : public TextureTestWorld {};

TEST_F(TextureUploadTest, DesiredRGB565ControlsAllocationAndConvertsLiteralPrimaryColors) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF};
    Pixels(texture, pixels, 2, 2); texture.SetDesiredVideoFormat(_16_RGB565);
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
    EXPECT_EQ(_16_RGB565, VxImageDesc2PixelFormat(State.Created[0].Format));
    ASSERT_EQ(1, State.Loads); EXPECT_EQ(16, State.Uploads[0].Image.BitsPerPixel);
    const CKWORD expected[] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    EXPECT_EQ(0, memcmp(expected, State.Uploads[0].Bytes, sizeof(expected)));
}
TEST_F(TextureUploadTest, DesiredARGB1555PreservesTransparentAndOpaqueBits) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0x00FF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF};
    Pixels(texture, pixels, 2, 2); texture.SetDesiredVideoFormat(_16_ARGB1555);
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
    EXPECT_EQ(_16_ARGB1555, VxImageDesc2PixelFormat(State.Created[0].Format));
    const CKWORD expected[] = {0x7C00, 0x83E0, 0x801F, 0xFFFF};
    EXPECT_EQ(0, memcmp(expected, State.Uploads[0].Bytes, sizeof(expected)));
}
TEST_F(TextureUploadTest, CompressedFormatsPassActualARGBPixelsToRasterizerForEncoding) {
    const VX_PIXELFORMAT formats[] = {_DXT1, _DXT3, _DXT5};
    const CKDWORD pixels[] = {0x12224466,0x4488AACC,0x66CCEE22,0x88AABBCC};
    for (int i = 0; i < 3; ++i) {
        TextureTestProbe texture(Context); Pixels(texture, pixels, 2, 2); texture.SetDesiredVideoFormat(formats[i]);
        ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
        EXPECT_EQ(formats[i], VxImageDesc2PixelFormat(State.Created[i].Format));
        EXPECT_EQ(0, State.Created[i].Format.BitsPerPixel); EXPECT_TRUE(State.Created[i].Flags & CKRST_TEXTURE_ALPHA);
        EXPECT_EQ(32, State.Uploads[i].Image.BitsPerPixel); EXPECT_EQ(0, memcmp(pixels, State.Uploads[i].Bytes, sizeof(pixels)));
    }
}
TEST_F(TextureUploadTest, CompressedManualMipmapPassesEachRealLevelWithoutInvalidCPUCompression) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xFF112233,0xFF445566,0xFF778899,0xFFAABBCC};
    Pixels(texture, pixels, 2, 2); texture.SetDesiredVideoFormat(_DXT5); ASSERT_TRUE(texture.SetUserMipMapMode(TRUE));
    VxImageDescEx mip; ASSERT_TRUE(texture.GetUserMipMapLevel(0, mip)); ASSERT_NE(nullptr, mip.Image);
    const CKDWORD mipPixel = 0x88112233; memcpy(mip.Image, &mipPixel, sizeof(mipPixel));
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE)); ASSERT_EQ(2, State.Loads);
    EXPECT_EQ(_DXT5, VxImageDesc2PixelFormat(State.Created[0].Format)); EXPECT_EQ(2u, State.Created[0].MipMapCount);
    EXPECT_EQ(0, State.Uploads[0].Level); EXPECT_EQ(1, State.Uploads[1].Level); EXPECT_EQ(32, State.Uploads[1].Image.BitsPerPixel);
    EXPECT_EQ(mipPixel, Pixel(State.Uploads[1], 0));
}
TEST_F(TextureUploadTest, EqualFormatUploadsExactPixelsWithoutColorConversion) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0x12345678, 0x90ABCDEF, 0x11223344, 0x55667788};
    Pixels(texture, pixels, 2, 2); texture.SetDesiredVideoFormat(_32_ARGB8888);
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
    EXPECT_EQ(0, memcmp(pixels, State.Uploads[0].Bytes, sizeof(pixels)));
    EXPECT_TRUE(State.Created[0].Flags & CKRST_TEXTURE_ALPHA);
}
TEST_F(TextureUploadTest, ActualNegotiatedFormatControlsUploadWithoutPrematureColorQuantization) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0x7F123456,0x89ABCDEF};
    Pixels(texture, pixels, 2, 1); texture.SetDesiredVideoFormat(_16_RGB565); State.ActualFormat = _32_ARGB8888;
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE)); EXPECT_EQ(_16_RGB565, VxImageDesc2PixelFormat(State.Created[0].Format));
    EXPECT_EQ(_32_ARGB8888, texture.GetVideoPixelFormat()); ASSERT_EQ(1, State.Loads);
    EXPECT_EQ(32, State.Uploads[0].Image.BitsPerPixel); EXPECT_EQ(0, memcmp(pixels, State.Uploads[0].Bytes, sizeof(pixels)));
    EXPECT_TRUE(texture.TextureFlags() & CKRST_TEXTURE_ALPHA);
}
TEST_F(TextureUploadTest, CreatedResourceDescriptorFailureCleansUpAndDoesNotUpload) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xFF112233}; Pixels(texture, pixels, 1, 1); State.QueryResult = FALSE;
    EXPECT_FALSE(texture.SystemToVideoMemory(Device, FALSE)); EXPECT_EQ(1, State.Creates); EXPECT_EQ(1, State.Deletes);
    EXPECT_FALSE(State.Live[1]); EXPECT_EQ(0, State.Loads); EXPECT_EQ(0, texture.GetRstTextureIndex());
}
TEST_F(TextureUploadTest, TransparentColorPromotesNonAlphaFormatAndChangesActualUploadedAlpha) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xFF224466, 0xFF88AACC};
    Pixels(texture, pixels, 2, 1); texture.SetDesiredVideoFormat(_16_RGB565);
    texture.SetTransparentColor(0x00224466); texture.SetTransparent(TRUE);
    ASSERT_EQ(2, texture.SetAsCurrent(Device, FALSE, 3));
    EXPECT_TRUE(State.Created[0].Format.AlphaMask != 0);
    ASSERT_EQ(32, State.Uploads[0].Image.BitsPerPixel);
    EXPECT_EQ(0x00224466u, Pixel(State.Uploads[0], 0)); EXPECT_EQ(0xFF88AACCu, Pixel(State.Uploads[0], 1));
    EXPECT_EQ(TRUE, State.States[VXRENDERSTATE_ALPHATESTENABLE]); EXPECT_EQ(0u, State.States[VXRENDERSTATE_ALPHAREF]);
    EXPECT_EQ(VXCMP_GREATER, State.States[VXRENDERSTATE_ALPHAFUNC]); EXPECT_EQ(texture.GetRstTextureIndex(), State.Textures[3]);
}
TEST_F(TextureUploadTest, ClampClearsOnlyBoundaryAlphaAndRetainsInteriorPixel) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xFF224466,0xFF224466,0xFF224466,0xFF224466,0xFF88AACC,0xFF224466,0xFF224466,0xFF224466,0xFF224466};
    Pixels(texture, pixels, 3, 3); ASSERT_EQ(2, texture.SetAsCurrent(Device, TRUE, 1));
    for (int i = 0; i < 9; ++i) EXPECT_EQ(i == 4 ? 0xFF88AACCu : 0x00224466u, Pixel(State.Uploads[0], i));
    EXPECT_TRUE(texture.BitmapFlags() & CKBITMAPDATA_CLAMPUPTODATE);
}
TEST_F(TextureUploadTest, UnsupportedClampCapabilityLeavesPixelsAndAlphaTestDisabled) {
    Manager->m_Drivers[0].Caps3D.CKRasterizerSpecificCaps = 0;
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xFF224466}; Pixels(texture, pixels, 1, 1);
    ASSERT_EQ(1, texture.SetAsCurrent(Device, TRUE, 2));
    EXPECT_EQ(0xFF224466u, Pixel(State.Uploads[0], 0)); EXPECT_EQ(FALSE, State.States[VXRENDERSTATE_ALPHATESTENABLE]);
}
TEST_F(TextureUploadTest, UploadCreationFailureDoesNotCreateResidencyAndResetsOnlyRequestedStage) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xFFFFFFFF}; Pixels(texture, pixels, 1, 1);
    State.CreateResult = FALSE; State.Textures[0] = 47; State.Textures[2] = 49;
    EXPECT_FALSE(texture.SetAsCurrent(Device, FALSE, 2)); EXPECT_FALSE(texture.IsInVideoMemory());
    EXPECT_EQ(0, State.Loads); EXPECT_EQ(47u, State.Textures[0]); EXPECT_EQ(0u, State.Textures[2]);
}
TEST_F(TextureUploadTest, UploadFailureDeletesCreatedResourceAndNextCallRetriesRealPixels) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xFFAABBCC}; Pixels(texture, pixels, 1, 1);
    State.FailLoad = 0; EXPECT_FALSE(texture.SystemToVideoMemory(Device, FALSE));
    EXPECT_FALSE(texture.IsInVideoMemory()); EXPECT_EQ(1, State.Deletes); EXPECT_FALSE(State.Live[1]);
    State.FailLoad = -1; ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
    EXPECT_EQ(2, State.Creates); EXPECT_EQ(0xFFAABBCCu, Pixel(State.Uploads[1], 0));
}
TEST_F(TextureUploadTest, FailedBindingPropagatesFailureAndUnbindsRequestedStage) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xFFFFFFFF}; Pixels(texture, pixels, 1, 1);
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE)); State.BindResult = FALSE; State.Textures[2] = 55;
    EXPECT_FALSE(texture.SetAsCurrent(Device, FALSE, 2)); EXPECT_EQ(0u, State.Textures[2]); EXPECT_TRUE(texture.IsInVideoMemory());
}
TEST_F(TextureUploadTest, ResidencyQueriesActualResourceRatherThanStaleCachedBoolean) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xFFFFFFFF}; Pixels(texture, pixels, 1, 1);
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE)); EXPECT_TRUE(texture.IsInVideoMemory());
    State.Live[texture.GetRstTextureIndex()] = FALSE; EXPECT_FALSE(texture.IsInVideoMemory());
    EXPECT_TRUE(texture.SetAsCurrent(Device, FALSE, 0)); EXPECT_TRUE(texture.IsInVideoMemory());
    EXPECT_EQ(2, State.Creates); EXPECT_EQ(2, State.Loads); EXPECT_EQ(0, State.Deletes);
    EXPECT_EQ(0xFFFFFFFFu, Pixel(State.Uploads[1], 0));
    EXPECT_EQ(texture.GetRstTextureIndex(), State.Textures[0]);
}
TEST_F(TextureUploadTest, ContextSwitchReusesIndependentResourcesAndPropagatesPixelChangesToBoth) {
    TextureTestProbe texture(Context); const CKDWORD initial[] = {0xFF112233}; Pixels(texture, initial, 1, 1);
    ASSERT_TRUE(texture.SetAsCurrent(Device, FALSE, 1)); const int firstHandle = texture.GetRstTextureIndex();
    ASSERT_TRUE(texture.SetAsCurrent(OtherDevice, FALSE, 1)); const int secondHandle = texture.GetRstTextureIndex();
    ASSERT_TRUE(texture.SetAsCurrent(Device, FALSE, 1)); EXPECT_EQ(firstHandle, texture.GetRstTextureIndex());
    EXPECT_EQ(1, State.Creates); EXPECT_EQ(1, OtherState.Creates); EXPECT_EQ(1, State.Loads);
    CKDWORD *pixel = reinterpret_cast<CKDWORD *>(texture.LockSurfacePtr()); ASSERT_NE(nullptr, pixel); *pixel = 0xFF445566; texture.ReleaseSurfacePtr();
    ASSERT_TRUE(texture.SetAsCurrent(Device, FALSE, 1)); ASSERT_TRUE(texture.SetAsCurrent(OtherDevice, FALSE, 1));
    EXPECT_EQ(secondHandle, texture.GetRstTextureIndex()); EXPECT_EQ(2, State.Loads); EXPECT_EQ(2, OtherState.Loads);
    EXPECT_EQ(0xFF445566u, Pixel(State.Uploads[1], 0)); EXPECT_EQ(0xFF445566u, Pixel(OtherState.Uploads[1], 0));
}
TEST_F(TextureUploadTest, FreeVideoMemoryReleasesAllContextsAndSecondReleaseReportsNoResource) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xFF112233}; Pixels(texture, pixels, 1, 1);
    ASSERT_TRUE(texture.SetAsCurrent(Device, FALSE, 0)); ASSERT_TRUE(texture.SetAsCurrent(OtherDevice, FALSE, 0));
    ASSERT_TRUE(texture.FreeVideoMemory()); EXPECT_EQ(1, State.Deletes); EXPECT_EQ(1, OtherState.Deletes);
    EXPECT_FALSE(texture.IsInVideoMemory()); EXPECT_EQ(0, texture.GetRstTextureIndex()); EXPECT_FALSE(texture.FreeVideoMemory());
}
TEST_F(TextureUploadTest, PartialReleaseFailureRetainsOnlyFailedContextAndCanRetry) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xFF112233}; Pixels(texture, pixels, 1, 1);
    ASSERT_TRUE(texture.SetAsCurrent(Device, FALSE, 0)); ASSERT_TRUE(texture.SetAsCurrent(OtherDevice, FALSE, 0));
    OtherState.DeleteResult = FALSE; EXPECT_FALSE(texture.FreeVideoMemory()); EXPECT_TRUE(texture.IsInVideoMemory());
    EXPECT_EQ(&OtherState, texture.SelectedContext()); EXPECT_EQ(1, State.Deletes); EXPECT_EQ(1, OtherState.Deletes);
    OtherState.DeleteResult = TRUE; EXPECT_TRUE(texture.FreeVideoMemory()); EXPECT_FALSE(texture.IsInVideoMemory());
}
TEST_F(TextureUploadTest, CubeUploadPreservesDistinctPixelsAndFaceOrderForAllSixFaces) {
    TextureTestProbe texture(Context); texture.SetCubeMap(TRUE);
    for (int face = 0; face < 6; ++face) { const CKDWORD pixel = 0xFF223340u + face; Pixels(texture, &pixel, 1, 1, face); }
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE)); EXPECT_TRUE(State.Created[0].Flags & CKRST_TEXTURE_CUBEMAP);
    ASSERT_EQ(6, State.Loads);
    for (int face = 0; face < 6; ++face) { EXPECT_EQ(face, State.Uploads[face].Face); EXPECT_EQ(0xFF223340u + face, Pixel(State.Uploads[face], 0)); }
}
TEST_F(TextureUploadTest, SampledCubeShapeTransitionsRecreateAllocationAndUploadEveryRequiredFace) {
    TextureTestProbe texture(Context); const CKDWORD initial = 0xFF112233; Pixels(texture, &initial, 1, 1);
    ASSERT_TRUE(texture.SetAsCurrent(Device, FALSE, 0)); ASSERT_EQ(1, State.Creates); ASSERT_EQ(1, State.Loads);
    texture.SetCubeMap(TRUE);
    for (int face = 0; face < 6; ++face) { const CKDWORD pixel = 0xFF223340u + face; Pixels(texture, &pixel, 1, 1, face); }
    ASSERT_TRUE(texture.SetAsCurrent(Device, FALSE, 0)); ASSERT_EQ(2, State.Creates); ASSERT_EQ(7, State.Loads); EXPECT_EQ(1, State.Deletes);
    EXPECT_TRUE(State.Created[1].Flags & CKRST_TEXTURE_CUBEMAP);
    for (int face = 0; face < 6; ++face) { EXPECT_EQ(face, State.Uploads[face + 1].Face); EXPECT_EQ(0xFF223340u + face, Pixel(State.Uploads[face + 1], 0)); }
    texture.SetCubeMap(FALSE); ASSERT_TRUE(texture.SetAsCurrent(Device, FALSE, 0));
    ASSERT_EQ(3, State.Creates); ASSERT_EQ(8, State.Loads); EXPECT_EQ(2, State.Deletes); EXPECT_FALSE(State.Created[2].Flags & CKRST_TEXTURE_CUBEMAP);
    EXPECT_EQ(CKRST_CUBEFACE_XPOS, State.Uploads[7].Face); EXPECT_EQ(0xFF223340u, Pixel(State.Uploads[7], 0));
}
TEST_F(TextureUploadTest, BumpFormatsAllocateRequestedEncodingAndPackIndependentChannelBits) {
    const VX_PIXELFORMAT formats[] = {_16_V8U8, _32_V16U16, _16_L6V5U5, _32_X8L8V8U8};
    // Expected packed bytes follow each public descriptor's RG[B] bit widths;
    // this checks uploads, not the excluded backend's bump shading semantics.
    const CKDWORD expected[] = {0x3412,0x34341212,0x54C2,0x00563412};
    const CKDWORD pixel = 0xFF123456;
    for (int i = 0; i < 4; ++i) {
        TextureTestProbe texture(Context); Pixels(texture, &pixel, 1, 1); texture.SetDesiredVideoFormat(formats[i]);
        ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
        EXPECT_EQ((CKDWORD)formats[i], State.Created[i].Format.Flags);
        EXPECT_EQ(i == 3 ? _32_BGR888 : formats[i], VxImageDesc2PixelFormat(State.Created[i].Format));
        EXPECT_TRUE(State.Created[i].Flags & CKRST_TEXTURE_BUMPDUDV);
        EXPECT_EQ(i >= 2, (State.Created[i].Flags & CKRST_TEXTURE_BUMPLUMINANCE) != 0);
        CKDWORD actual = 0; memcpy(&actual, State.Uploads[i].Bytes, State.Uploads[i].Image.BitsPerPixel / 8);
        EXPECT_EQ(expected[i], actual);
    }
}
TEST_F(TextureUploadTest, FallbackBumpEncodingSurvivesItsCanonicalBgrDetection) {
    TextureTestProbe texture(Context); const CKDWORD pixel = 0xFF123456;
    Pixels(texture, &pixel, 1, 1); texture.SetDesiredVideoFormat(UNKNOWN_PF);
    Manager->m_TextureVideoFormat.Value = _32_X8L8V8U8;
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
    EXPECT_EQ((CKDWORD)_32_X8L8V8U8, State.Created[0].Format.Flags);
    EXPECT_EQ(_32_BGR888, VxImageDesc2PixelFormat(State.Created[0].Format));
    EXPECT_TRUE(State.Created[0].Flags & CKRST_TEXTURE_BUMPDUDV);
    EXPECT_TRUE(State.Created[0].Flags & CKRST_TEXTURE_BUMPLUMINANCE);
    EXPECT_EQ(_32_BGR888, texture.GetVideoPixelFormat());
    EXPECT_EQ(0x00563412u, Pixel(State.Uploads[0], 0));
}
TEST_F(TextureUploadTest, CubeFaceFailurePropagatesAndReleasesPartialAllocation) {
    TextureTestProbe texture(Context); texture.SetCubeMap(TRUE);
    for (int face = 0; face < 6; ++face) { const CKDWORD pixel = 0xFF223340u + face; Pixels(texture, &pixel, 1, 1, face); }
    State.FailLoad = 3; EXPECT_FALSE(texture.SystemToVideoMemory(Device, FALSE));
    EXPECT_EQ(4, State.Loads); EXPECT_EQ(1, State.Deletes); EXPECT_FALSE(texture.IsInVideoMemory());
}
TEST_F(TextureUploadTest, FailedRestoreKeepsDirtyStateAndRetryUploadsAgain) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xFF112233}; Pixels(texture, pixels, 1, 1);
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE)); CKDWORD *pixel = reinterpret_cast<CKDWORD *>(texture.LockSurfacePtr());
    *pixel = 0xFF445566; texture.ReleaseSurfacePtr(); State.FailLoad = State.Loads;
    EXPECT_FALSE(texture.Restore(FALSE)); EXPECT_TRUE(texture.BitmapFlags() & CKBITMAPDATA_FORCERESTORE);
    State.FailLoad = -1; ASSERT_TRUE(texture.Restore(FALSE)); EXPECT_FALSE(texture.BitmapFlags() & CKBITMAPDATA_FORCERESTORE);
    EXPECT_EQ(0xFF445566u, Pixel(State.Uploads[State.Loads - 1], 0));
}
TEST_F(TextureUploadTest, RenderTargetAllocationDoesNotUploadCPUImageOrOverrideExistingAlphaState) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xFFFFFFFF}; Pixels(texture, pixels, 1, 1);
    ASSERT_TRUE(texture.EnsureRenderTarget(Device)); EXPECT_EQ(0, State.Loads); EXPECT_TRUE(State.Created[0].Flags & CKRST_TEXTURE_RENDERTARGET);
    State.States[VXRENDERSTATE_ALPHATESTENABLE] = 73; ASSERT_EQ(1, texture.SetAsCurrent(Device, TRUE, 2));
    EXPECT_EQ(73u, State.States[VXRENDERSTATE_ALPHATESTENABLE]); EXPECT_EQ(0, State.Loads);
}
TEST_F(TextureUploadTest, ExistingSampledTextureIsReplacedByRenderTargetAndTargetIsReused) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xFFFFFFFF}; Pixels(texture, pixels, 1, 1);
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE)); ASSERT_TRUE(texture.EnsureRenderTarget(Device));
    EXPECT_EQ(2, State.Creates); EXPECT_EQ(1, State.Deletes); ASSERT_TRUE(texture.EnsureRenderTarget(Device)); EXPECT_EQ(2, State.Creates);
}
TEST_F(TextureUploadTest, CubeToOrdinaryTargetTransitionRecreatesShapeInsteadOfRetainingCubeHandle) {
    TextureTestProbe texture(Context); texture.SetCubeMap(TRUE);
    for (int face = 0; face < 6; ++face) { const CKDWORD pixel = 0xFFFFFFFF; Pixels(texture, &pixel, 1, 1, face); }
    ASSERT_TRUE(texture.EnsureRenderTarget(Device)); texture.SetCubeMap(FALSE); ASSERT_TRUE(texture.EnsureRenderTarget(Device));
    EXPECT_EQ(2, State.Creates); EXPECT_EQ(1, State.Deletes); EXPECT_FALSE(State.Created[1].Flags & CKRST_TEXTURE_CUBEMAP);
}
TEST_F(TextureUploadTest, RenderTargetCreationFailureIsReportedWithoutResidency) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xFFFFFFFF}; Pixels(texture, pixels, 1, 1); State.CreateResult = FALSE;
    EXPECT_FALSE(texture.EnsureRenderTarget(Device)); EXPECT_FALSE(texture.IsInVideoMemory()); EXPECT_EQ(0, State.Loads);
}
TEST_F(TextureUploadTest, RenderTargetCachesActualNegotiatedFormatAndFlagsAndReusesItsHandle) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xFFFFFFFF}; Pixels(texture, pixels, 1, 1);
    texture.SetDesiredVideoFormat(_32_ARGB8888); State.ActualFormat = _16_RGB565; ASSERT_TRUE(texture.EnsureRenderTarget(Device));
    EXPECT_EQ(_32_ARGB8888, VxImageDesc2PixelFormat(State.Created[0].Format)); EXPECT_EQ(_16_RGB565, texture.GetVideoPixelFormat());
    EXPECT_FALSE(texture.TextureFlags() & CKRST_TEXTURE_ALPHA); EXPECT_TRUE(texture.TextureFlags() & CKRST_TEXTURE_RENDERTARGET);
    ASSERT_TRUE(texture.EnsureRenderTarget(Device)); EXPECT_EQ(1, State.Creates); EXPECT_EQ(0, State.Loads);
}
TEST_F(TextureUploadTest, RenderTargetDescriptorFailureCleansCreatedHandleAndReportsFailure) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xFFFFFFFF}; Pixels(texture, pixels, 1, 1); State.QueryResult = FALSE;
    EXPECT_FALSE(texture.EnsureRenderTarget(Device)); EXPECT_EQ(1, State.Creates); EXPECT_EQ(1, State.Deletes);
    EXPECT_FALSE(State.Live[1]); EXPECT_EQ(0, texture.GetRstTextureIndex());
}
