#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKTexture.h"
#include "RCKRenderManager.h"
#include "TextureTestObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class TextureStateProbe : public RCKTexture {
public:
    explicit TextureStateProbe(CKContext *context) : RCKTexture(context) {}
    int FreeCalls = 0;
    CKBOOL FreeResult = TRUE;
    CKBOOL FreeVideoMemory() override {
        ++FreeCalls;
        if (FreeResult) m_InVideoMemory = FALSE;
        return FreeResult;
    }
    void Resident() { m_InVideoMemory = TRUE; }
    void Handle(CKDWORD index) { m_ObjectIndex = index; }
};
class TextureStateTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
};
}

TEST_F(TextureStateTest, AutomaticMipModeDoesNotEvictRepeatedEquivalentRequest) {
    TextureStateProbe texture(Context);
    EXPECT_TRUE(texture.UseMipmap(TRUE)); EXPECT_EQ(-1, texture.GetMipmapCount());
    texture.Resident();
    EXPECT_TRUE(texture.UseMipmap(-1)); EXPECT_TRUE(texture.UseMipmap(TRUE));
    EXPECT_EQ(0, texture.FreeCalls);
    EXPECT_TRUE(texture.UseMipmap(FALSE)); EXPECT_EQ(0, texture.GetMipmapCount());
    EXPECT_EQ(1, texture.FreeCalls);
}
TEST_F(TextureStateTest, ExplicitMipCountAndFailedEvictionPreserveConfiguration) {
    TextureStateProbe texture(Context);
    ASSERT_TRUE(texture.UseMipmap(3)); EXPECT_EQ(3, texture.GetMipmapCount());
    texture.Resident(); texture.FreeResult = FALSE;
    EXPECT_FALSE(texture.UseMipmap(0)); EXPECT_EQ(3, texture.GetMipmapCount());
    EXPECT_EQ(1, texture.FreeCalls);
    texture.FreeResult = TRUE; EXPECT_TRUE(texture.UseMipmap(0));
    EXPECT_EQ(0, texture.GetMipmapCount()); EXPECT_EQ(2, texture.FreeCalls);
}
TEST_F(TextureStateTest, DesiredFormatChangeEvictsOnceAndFailureKeepsOldFormat) {
    TextureStateProbe texture(Context);
    texture.SetDesiredVideoFormat(_16_RGB565); texture.FreeCalls = 0; texture.Resident();
    texture.SetDesiredVideoFormat(_16_RGB565); EXPECT_EQ(0, texture.FreeCalls);
    texture.FreeResult = FALSE; texture.SetDesiredVideoFormat(_16_ARGB4444);
    EXPECT_EQ(_16_RGB565, texture.GetDesiredVideoFormat()); EXPECT_EQ(1, texture.FreeCalls);
    texture.FreeResult = TRUE; texture.SetDesiredVideoFormat(_16_ARGB4444);
    EXPECT_EQ(_16_ARGB4444, texture.GetDesiredVideoFormat()); EXPECT_EQ(2, texture.FreeCalls);
}
TEST_F(TextureStateTest, UnresidentVideoQueriesFailWithoutChangingOutput) {
    TextureStateProbe texture(Context); VxImageDescEx desc;
    desc.Width = 67; desc.Height = 43;
    EXPECT_FALSE(texture.GetVideoTextureDesc(desc)); EXPECT_EQ(67, desc.Width); EXPECT_EQ(43, desc.Height);
    EXPECT_EQ(UNKNOWN_PF, texture.GetVideoPixelFormat());
    texture.Handle(917); EXPECT_EQ(917, texture.GetRstTextureIndex());
}
TEST_F(TextureStateTest, SystemDescriptorReportsFormatWithoutLockingSurface) {
    RCKTexture texture(Context); ASSERT_TRUE(texture.Create(4, 2, 32, 0));
    CKDWORD *pixels = reinterpret_cast<CKDWORD *>(texture.LockSurfacePtr()); ASSERT_NE(nullptr, pixels);
    pixels[0] = 0x7f123456u; pixels[7] = 0xffabcdefu; texture.ReleaseSurfacePtr();
    VxImageDescEx desc; ASSERT_TRUE(texture.GetSystemTextureDesc(desc));
    EXPECT_EQ(4, desc.Width); EXPECT_EQ(2, desc.Height); EXPECT_EQ(16, desc.BytesPerLine);
    EXPECT_EQ(_32_ARGB8888, VxImageDesc2PixelFormat(desc));
    EXPECT_EQ(nullptr, desc.Image); EXPECT_EQ(0x7f123456u, texture.GetPixel(0, 0, 0));
    EXPECT_EQ(0xffabcdefu, texture.GetPixel(3, 1, 0));
}
TEST_F(TextureStateTest, RectangularUserMipsReachOnePixelWithoutZeroDimensions) {
    RCKTexture texture(Context); ASSERT_TRUE(texture.Create(8, 2, 32, 0));
    ASSERT_TRUE(texture.SetUserMipMapMode(TRUE)); EXPECT_EQ(-1, texture.GetMipmapCount());
    const int widths[] = {4, 2, 1};
    for (int i = 0; i < 3; ++i) {
        VxImageDescEx desc; ASSERT_TRUE(texture.GetUserMipMapLevel(i, desc));
        EXPECT_EQ(widths[i], desc.Width); EXPECT_EQ(1, desc.Height); EXPECT_EQ(widths[i] * 4, desc.BytesPerLine);
        EXPECT_EQ(_32_ARGB8888, VxImageDesc2PixelFormat(desc)); ASSERT_NE(nullptr, desc.Image);
        for (int x = 0; x < desc.Width; ++x) EXPECT_EQ(0u, reinterpret_cast<CKDWORD *>(desc.Image)[x]);
        reinterpret_cast<CKDWORD *>(desc.Image)[0] = 0xff000001u + i;
        VxImageDescEx again; ASSERT_TRUE(texture.GetUserMipMapLevel(i, again));
        EXPECT_EQ(desc.Image, again.Image); EXPECT_EQ(0xff000001u + i, reinterpret_cast<CKDWORD *>(again.Image)[0]);
    }
    VxImageDescEx untouched; untouched.Width = 91;
    EXPECT_FALSE(texture.GetUserMipMapLevel(-1, untouched)); EXPECT_FALSE(texture.GetUserMipMapLevel(3, untouched));
    EXPECT_EQ(91, untouched.Width);
}
TEST_F(TextureStateTest, UserModeIsIdempotentAndDisablingKeepsAutomaticMode) {
    RCKTexture texture(Context); ASSERT_TRUE(texture.Create(4, 4, 32, 0));
    ASSERT_TRUE(texture.SetUserMipMapMode(TRUE)); VxImageDescEx first;
    ASSERT_TRUE(texture.GetUserMipMapLevel(0, first)); reinterpret_cast<CKDWORD *>(first.Image)[0] = 0x55667788u;
    ASSERT_TRUE(texture.SetUserMipMapMode(TRUE)); VxImageDescEx second;
    ASSERT_TRUE(texture.GetUserMipMapLevel(0, second)); EXPECT_EQ(first.Image, second.Image);
    EXPECT_EQ(0x55667788u, reinterpret_cast<CKDWORD *>(second.Image)[0]);
    EXPECT_TRUE(texture.SetUserMipMapMode(FALSE)); EXPECT_FALSE(texture.GetUserMipMapLevel(0, second));
    EXPECT_EQ(-1, texture.GetMipmapCount()); EXPECT_TRUE(texture.SetUserMipMapMode(FALSE));
}
TEST_F(TextureStateTest, OnePixelTextureHasNoAdditionalMipLevels) {
    RCKTexture texture(Context); ASSERT_TRUE(texture.Create(1, 1, 32, 0));
    EXPECT_TRUE(texture.SetUserMipMapMode(TRUE)); VxImageDescEx desc;
    EXPECT_FALSE(texture.GetUserMipMapLevel(0, desc)); EXPECT_EQ(-1, texture.GetMipmapCount());
}
TEST_F(TextureStateTest, InvalidDimensionsAndMultipleSlotsDoNotEnableUserMode) {
    RCKTexture texture(Context); EXPECT_FALSE(texture.SetUserMipMapMode(TRUE));
    ASSERT_TRUE(texture.Create(3, 4, 32, 0)); EXPECT_FALSE(texture.SetUserMipMapMode(TRUE));
    ASSERT_TRUE(texture.Create(4, 3, 32, 0)); EXPECT_FALSE(texture.SetUserMipMapMode(TRUE));
    ASSERT_TRUE(texture.Create(4, 4, 32, 0)); ASSERT_TRUE(texture.SetSlotCount(2));
    EXPECT_FALSE(texture.SetUserMipMapMode(TRUE)); EXPECT_EQ(0, texture.GetMipmapCount());
}
TEST_F(TextureStateTest, UserMipEvictionFailureRetainsLivePixels) {
    TextureStateProbe texture(Context); ASSERT_TRUE(texture.Create(4, 4, 32, 0));
    ASSERT_TRUE(texture.SetUserMipMapMode(TRUE)); VxImageDescEx first;
    ASSERT_TRUE(texture.GetUserMipMapLevel(0, first)); reinterpret_cast<CKDWORD *>(first.Image)[0] = 0x12345678u;
    texture.Resident(); texture.FreeResult = FALSE;
    EXPECT_FALSE(texture.SetUserMipMapMode(FALSE)); VxImageDescEx after;
    ASSERT_TRUE(texture.GetUserMipMapLevel(0, after)); EXPECT_EQ(first.Image, after.Image);
    EXPECT_EQ(0x12345678u, reinterpret_cast<CKDWORD *>(after.Image)[0]);
    texture.FreeResult = TRUE; EXPECT_TRUE(texture.SetUserMipMapMode(FALSE));
    EXPECT_FALSE(texture.GetUserMipMapLevel(0, after)); EXPECT_EQ(2, texture.FreeCalls);
}

class TextureVideoStateTest : public TextureTestWorld {};
TEST_F(TextureVideoStateTest, VideoQueriesUseActualDriverDescriptionAndNotCreationCache) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xff123456u, 0xffabcdefu};
    Pixels(texture, pixels, 2, 1); texture.SetDesiredVideoFormat(_32_ARGB8888);
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE)); const CKDWORD handle = texture.GetRstTextureIndex();
    ASSERT_GT(handle, 0u); VxImageDescEx desc; ASSERT_TRUE(texture.GetVideoTextureDesc(desc));
    EXPECT_EQ(2, desc.Width); EXPECT_EQ(_32_ARGB8888, texture.GetVideoPixelFormat());
    // A driver can choose a supported format different from the requested one.
    VxPixelFormat2ImageDesc(_16_RGB565, State.Resource[handle].Format);
    State.Resource[handle].Format.Width = 2; State.Resource[handle].Format.Height = 1;
    EXPECT_TRUE(texture.GetVideoTextureDesc(desc)); EXPECT_EQ(16, desc.BitsPerPixel);
    EXPECT_EQ(_16_RGB565, texture.GetVideoPixelFormat());
}
TEST_F(TextureVideoStateTest, ExternallyDeletedHandleIsNotReportedAsResident) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xff123456u}; Pixels(texture, pixels, 1, 1);
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE)); ASSERT_TRUE(texture.IsInVideoMemory());
    ASSERT_TRUE(State.DeleteObject(texture.GetRstTextureIndex(), CKRST_OBJ_TEXTURE));
    EXPECT_FALSE(texture.IsInVideoMemory()); VxImageDescEx desc; desc.Width = 97;
    EXPECT_FALSE(texture.GetVideoTextureDesc(desc)); EXPECT_EQ(97, desc.Width);
    EXPECT_EQ(UNKNOWN_PF, texture.GetVideoPixelFormat());
}
TEST_F(TextureVideoStateTest, FormatChangeReleasesAllContextsAndLeavesSystemPixelsIntact) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0x7f123456u}; Pixels(texture, pixels, 1, 1);
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE)); ASSERT_TRUE(texture.SystemToVideoMemory(OtherDevice, FALSE));
    texture.SetDesiredVideoFormat(_16_RGB565); EXPECT_EQ(1, State.Deletes); EXPECT_EQ(1, OtherState.Deletes);
    EXPECT_FALSE(texture.IsInVideoMemory()); EXPECT_EQ(_16_RGB565, texture.GetDesiredVideoFormat());
    VxImageDescEx desc; ASSERT_TRUE(texture.GetSystemTextureDesc(desc));
    EXPECT_EQ(nullptr, desc.Image); EXPECT_EQ(0x7f123456u, texture.GetPixel(0, 0, 0));
}
TEST_F(TextureVideoStateTest, FormatChangeEvictsAnotherContextEvenWhenSelectedContextHasNoHandle) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xff123456u}; Pixels(texture, pixels, 1, 1);
    ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE));
    EXPECT_FALSE(Manager->SelectTextureObject(&texture, &OtherState)); EXPECT_FALSE(texture.IsInVideoMemory());
    texture.SetDesiredVideoFormat(_16_RGB565); EXPECT_EQ(1, State.Deletes);
    EXPECT_EQ(_16_RGB565, texture.GetDesiredVideoFormat());
    ASSERT_TRUE(texture.SetAsCurrent(Device, FALSE, 0)); EXPECT_EQ(2, State.Creates);
    EXPECT_EQ(_16_RGB565, texture.GetVideoPixelFormat());
}
