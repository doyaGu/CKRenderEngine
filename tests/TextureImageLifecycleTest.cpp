#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include "CKAll.h"
#include "CKPluginManager.h"
#include "CKBitmapReader.h"
#include "RCKTexture.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
CKPluginInfo Infos[2];
int ReadersReleased = 0, MovieReads = 0;
CKDWORD FilePixels[6] = {0xff123456u, 0xffabcdefu, 0xff112233u, 0xff445566u, 0xff778899u, 0xff010203u};
VxImageDescEx Description(CKBYTE *pixels) {
    VxImageDescEx d; d.Width = 3; d.Height = 2; d.BytesPerLine = 12; d.BitsPerPixel = 32;
    d.RedMask = R_MASK; d.GreenMask = G_MASK; d.BlueMask = B_MASK; d.AlphaMask = A_MASK; d.Image = pixels; return d;
}
class TextureFileReader : public CKBitmapReader {
    CKBitmapProperties Properties;
public:
    TextureFileReader() { Properties.m_Ext = CKFileExtension("tx9"); Properties.m_ReaderGuid = Infos[0].m_GUID; }
    void Release() override { ++ReadersReleased; delete this; }
    CKPluginInfo *GetReaderInfo() override { return &Infos[0]; }
    int GetOptionsCount() override { return 0; }
    CKSTRING GetOptionDescription(int) override { return nullptr; }
    void GetBitmapDefaultProperties(CKBitmapProperties **p) override { *p = &Properties; }
    void SetBitmapDefaultProperties(CKBitmapProperties *) override {}
    int ReadFile(CKSTRING name, CKBitmapProperties **p) override {
        FILE *file = std::fopen(name, "rb"); if (!file) return CKBITMAPERROR_READERROR;
        CKDWORD pixels[6]; const size_t count = std::fread(pixels, sizeof(CKDWORD), 6, file); std::fclose(file);
        if (count != 6) return CKBITMAPERROR_FILECORRUPTED;
        Properties.m_Data = new CKBYTE[24]; std::memcpy(Properties.m_Data, pixels, 24);
        Properties.m_Format = Description(static_cast<CKBYTE *>(Properties.m_Data)); *p = &Properties; return 0;
    }
    int ReadMemory(void *, int, CKBitmapProperties **) override { return 1; }
    int ReadASynchronousFile(CKSTRING, CKBitmapProperties **) override { return 1; }
    int SaveFile(CKSTRING name, CKBitmapProperties *p) override {
        FILE *file = std::fopen(name, "wb"); if (!file) return 0;
        int count = 0;
        for (int y = 0; y < p->m_Format.Height; ++y)
            count += static_cast<int>(std::fwrite(static_cast<CKBYTE *>(p->m_Data) + y * p->m_Format.BytesPerLine,
                sizeof(CKDWORD), p->m_Format.Width, file));
        std::fclose(file); return count * 4;
    }
    int SaveMemory(void **p, CKBitmapProperties *) override { *p = nullptr; return 0; }
    void ReleaseMemory(void *p) override { delete[] static_cast<CKBYTE *>(p); }
};
class TextureMovieReader : public CKMovieReader {
    CKMovieProperties Properties;
public:
    void Release() override { ++ReadersReleased; delete this; }
    CKPluginInfo *GetReaderInfo() override { return &Infos[1]; }
    int GetOptionsCount() override { return 0; }
    CKSTRING GetOptionDescription(int) override { return nullptr; }
    int GetMovieFrameCount() override { return 2; }
    int GetMovieLength() override { return 100; }
    CKERROR OpenFile(CKSTRING name) override { FILE *f = std::fopen(name, "rb"); if (!f) return CKMOVIEERROR_READERROR; std::fclose(f); return CK_OK; }
    CKERROR OpenMemory(CKSTRING) override { return CKMOVIEERROR_READERROR; }
    CKERROR OpenAsynchronousFile(CKSTRING) override { return CKMOVIEERROR_READERROR; }
    CKERROR ReadFrame(int frame, CKMovieProperties **p) override {
        if (frame < 0 || frame >= 2) return CKMOVIEERROR_READERROR;
        ++MovieReads; static CKDWORD framePixels[6];
        // Native SetCurrentSlot flips codec rows; independent expected pixels remain top-down.
        for (int i = 0; i < 6; ++i) framePixels[i] = frame == 0 ? FilePixels[(1 - i / 3) * 3 + i % 3] : 0xff718293u;
        Properties.m_Format = Description(reinterpret_cast<CKBYTE *>(framePixels));
        Properties.m_Data = framePixels; *p = &Properties; return CK_OK;
    }
};
int ReaderCount() { return 2; }
CKPluginInfo *ReaderInfo(int index) { return index >= 0 && index < 2 ? &Infos[index] : nullptr; }
CKDataReader *ReaderFactory(int index) { return index == 0 ? static_cast<CKDataReader *>(new TextureFileReader) : static_cast<CKDataReader *>(new TextureMovieReader); }
class TextureImageProbe : public RCKTexture {
public:
    explicit TextureImageProbe(CKContext *context) : RCKTexture(context) {}
    int FreeCalls = 0; CKBOOL FreeResult = TRUE;
    CKBOOL FreeVideoMemory() override { ++FreeCalls; if (FreeResult) m_InVideoMemory = FALSE; return FreeResult; }
    void Resident() { m_InVideoMemory = TRUE; }
};
class TextureImageLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr; char Path[1024] = {}, MoviePath[1024] = {};
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
        ReadersReleased = MovieReads = 0;
        Infos[0].m_GUID = CKGUID(0x89000001, 1); Infos[0].m_Extension = CKFileExtension("tx9"); Infos[0].m_Type = CKPLUGIN_BITMAP_READER;
        Infos[1].m_GUID = CKGUID(0x89000001, 2); Infos[1].m_Extension = CKFileExtension("tm9"); Infos[1].m_Type = CKPLUGIN_MOVIE_READER;
        ASSERT_EQ(CK_OK, CKGetPluginManager()->RegisterStaticPlugin("Pass89 texture fixture codec", ReaderCount, ReaderInfo, ReaderFactory));
        std::snprintf(Path, sizeof(Path), "%stexture-pass89-%llu.tx9", ::testing::TempDir().c_str(), static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this)));
        std::snprintf(MoviePath, sizeof(MoviePath), "%stexture-pass89-%llu.tm9", ::testing::TempDir().c_str(), static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this)));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); std::remove(Path); std::remove(MoviePath); }
    void WriteFile(const char *name) { FILE *f = std::fopen(name, "wb"); ASSERT_NE(nullptr, f); ASSERT_EQ(6u, std::fwrite(FilePixels, 4, 6, f)); ASSERT_EQ(0, std::fclose(f)); }
};
}

TEST_F(TextureImageLifecycleTest, LoadImageReadsActualFileIntoIndependentArgbPixelsAndRetainsFilename) {
    WriteFile(Path); RCKTexture texture(Context); ASSERT_TRUE(texture.LoadImage(Path, 0));
    EXPECT_EQ(3, texture.GetWidth()); EXPECT_EQ(2, texture.GetHeight()); VxImageDescEx desc; ASSERT_TRUE(texture.GetSystemTextureDesc(desc)); EXPECT_EQ(32, desc.BitsPerPixel);
    for (int i = 0; i < 6; ++i) EXPECT_EQ(FilePixels[i], texture.GetPixel(i % 3, i / 3, 0));
    EXPECT_STREQ(Path, texture.GetSlotFileName(0)); EXPECT_GT(ReadersReleased, 0);
    std::remove(Path); EXPECT_EQ(FilePixels[4], texture.GetPixel(1, 1, 0));
}
TEST_F(TextureImageLifecycleTest, LoadingAdditionalSlotPreservesOtherPixelsAndCurrentSlot) {
    WriteFile(Path); RCKTexture texture(Context); ASSERT_TRUE(texture.Create(3, 2, 32, 0));
    ASSERT_TRUE(texture.SetPixel(1, 1, 0xff102030u, 0)); ASSERT_TRUE(texture.LoadImage(Path, 1));
    EXPECT_EQ(2, texture.GetSlotCount()); EXPECT_EQ(0, texture.GetCurrentSlot());
    EXPECT_EQ(0xff102030u, texture.GetPixel(1, 1, 0)); EXPECT_EQ(FilePixels[4], texture.GetPixel(1, 1, 1));
    EXPECT_STREQ(Path, texture.GetSlotFileName(1));
}
TEST_F(TextureImageLifecycleTest, MissingImageKeepsExistingPixelsAndRecordsRequestedFilename) {
    RCKTexture texture(Context); ASSERT_TRUE(texture.Create(3, 2, 32, 0)); ASSERT_TRUE(texture.SetPixel(0, 0, 0xff345678u));
    EXPECT_FALSE(texture.LoadImage(Path, 0)); EXPECT_STREQ(Path, texture.GetSlotFileName(0));
    EXPECT_EQ(0xff345678u, texture.GetPixel(0, 0)); EXPECT_EQ(3, texture.GetWidth()); EXPECT_EQ(2, texture.GetHeight());
}
TEST_F(TextureImageLifecycleTest, NullImageLeavesActualMipmapPixelsAndVideoObjectUntouched) {
    TextureImageProbe texture(Context); ASSERT_TRUE(texture.Create(4, 4, 32, 0)); ASSERT_TRUE(texture.SetUserMipMapMode(TRUE));
    VxImageDescEx first; ASSERT_TRUE(texture.GetUserMipMapLevel(0, first)); first.Image[0] = 79; texture.Resident();
    EXPECT_FALSE(texture.LoadImage(nullptr, 0)); EXPECT_EQ(0, texture.FreeCalls);
    VxImageDescEx after; ASSERT_TRUE(texture.GetUserMipMapLevel(0, after)); EXPECT_EQ(first.Image, after.Image); EXPECT_EQ(79u, after.Image[0]);
}
TEST_F(TextureImageLifecycleTest, NonNullImageRequestDisablesUserMipmapsEvenIfFileMissing) {
    RCKTexture texture(Context); ASSERT_TRUE(texture.Create(4, 4, 32, 0)); ASSERT_TRUE(texture.SetUserMipMapMode(TRUE));
    VxImageDescEx desc; ASSERT_TRUE(texture.GetUserMipMapLevel(0, desc)); EXPECT_FALSE(texture.LoadImage(Path, 0));
    EXPECT_FALSE(texture.GetUserMipMapLevel(0, desc)); EXPECT_EQ(4, texture.GetWidth()); EXPECT_STREQ(Path, texture.GetSlotFileName(0));
}
TEST_F(TextureImageLifecycleTest, ResizeImagePropagatesVirtualVideoReleaseFailureWithDecodedCpuPixels) {
    WriteFile(Path); TextureImageProbe texture(Context); ASSERT_TRUE(texture.Create(5, 4, 32, 0)); texture.Resident(); texture.FreeResult = FALSE;
    EXPECT_FALSE(texture.LoadImage(Path, 0)); EXPECT_EQ(1, texture.FreeCalls); EXPECT_EQ(3, texture.GetWidth()); EXPECT_EQ(2, texture.GetHeight());
    EXPECT_EQ(FilePixels[5], texture.GetPixel(2, 1));
}
TEST_F(TextureImageLifecycleTest, FailedMipmapReleaseStopsImageReadAndPreservesCpuAndMipPixels) {
    WriteFile(Path); TextureImageProbe texture(Context); ASSERT_TRUE(texture.Create(4, 4, 32, 0)); ASSERT_TRUE(texture.SetPixel(0, 0, 0xff213243u));
    ASSERT_TRUE(texture.SetUserMipMapMode(TRUE)); VxImageDescEx before; ASSERT_TRUE(texture.GetUserMipMapLevel(0, before)); before.Image[0] = 193;
    texture.Resident(); texture.FreeResult = FALSE; EXPECT_FALSE(texture.LoadImage(Path, 0)); EXPECT_EQ(1, texture.FreeCalls);
    EXPECT_EQ(4, texture.GetWidth()); EXPECT_EQ(4, texture.GetHeight()); EXPECT_EQ(0xff213243u, texture.GetPixel(0, 0));
    VxImageDescEx after; ASSERT_TRUE(texture.GetUserMipMapLevel(0, after)); EXPECT_EQ(before.Image, after.Image); EXPECT_EQ(193u, after.Image[0]);
}
TEST_F(TextureImageLifecycleTest, LoadMovieUsesDecodedDimensionsAndIndependentReaderFrames) {
    WriteFile(MoviePath); RCKTexture texture(Context); ASSERT_TRUE(texture.Create(7, 5, 32, 0)); ASSERT_TRUE(texture.LoadMovie(MoviePath));
    EXPECT_EQ(3, texture.GetWidth()); EXPECT_EQ(2, texture.GetHeight()); ASSERT_NE(nullptr, texture.GetMovieReader()); EXPECT_STREQ(MoviePath, texture.GetMovieFileName());
    EXPECT_EQ(0, texture.GetCurrentSlot()); EXPECT_EQ(FilePixels[0], texture.GetPixel(0, 0));
    ASSERT_TRUE(texture.SetCurrentSlot(1)); EXPECT_EQ(0xff718293u, texture.GetPixel(2, 1)); EXPECT_GT(MovieReads, 0);
}
TEST_F(TextureImageLifecycleTest, NullMovieDisablesUserMipmapsButKeepsExistingCpuPixels) {
    RCKTexture texture(Context); ASSERT_TRUE(texture.Create(4, 4, 32, 0)); ASSERT_TRUE(texture.SetPixel(0, 0, 0xff123456u)); ASSERT_TRUE(texture.SetUserMipMapMode(TRUE));
    EXPECT_FALSE(texture.LoadMovie(nullptr)); VxImageDescEx desc; EXPECT_FALSE(texture.GetUserMipMapLevel(0, desc)); EXPECT_EQ(0xff123456u, texture.GetPixel(0, 0));
}
TEST_F(TextureImageLifecycleTest, FailedVideoReleaseStopsMovieOpenAndKeepsExistingCpuImage) {
    WriteFile(MoviePath); TextureImageProbe texture(Context); ASSERT_TRUE(texture.Create(5, 4, 32, 0)); ASSERT_TRUE(texture.SetPixel(0, 0, 0xff432165u));
    texture.Resident(); texture.FreeResult = FALSE; EXPECT_FALSE(texture.LoadMovie(MoviePath)); EXPECT_EQ(1, texture.FreeCalls); EXPECT_EQ(0, MovieReads);
    EXPECT_EQ(5, texture.GetWidth()); EXPECT_EQ(4, texture.GetHeight()); EXPECT_EQ(0xff432165u, texture.GetPixel(0, 0)); EXPECT_EQ(nullptr, texture.GetMovieReader());
}
TEST_F(TextureImageLifecycleTest, FailedMipmapReleaseStopsMovieOpenAndKeepsActualMipPixels) {
    WriteFile(MoviePath); TextureImageProbe texture(Context); ASSERT_TRUE(texture.Create(4, 4, 32, 0)); ASSERT_TRUE(texture.SetUserMipMapMode(TRUE));
    VxImageDescEx before; ASSERT_TRUE(texture.GetUserMipMapLevel(0, before)); before.Image[0] = 163; texture.Resident(); texture.FreeResult = FALSE;
    EXPECT_FALSE(texture.LoadMovie(MoviePath)); EXPECT_EQ(1, texture.FreeCalls); EXPECT_EQ(0, MovieReads); EXPECT_EQ(4, texture.GetWidth());
    VxImageDescEx after; ASSERT_TRUE(texture.GetUserMipMapLevel(0, after)); EXPECT_EQ(before.Image, after.Image); EXPECT_EQ(163u, after.Image[0]);
}
TEST_F(TextureImageLifecycleTest, MissingMovieReturnsFailurePreservesPixelsAndHasNoReader) {
    RCKTexture texture(Context); ASSERT_TRUE(texture.Create(3, 2, 32, 0)); ASSERT_TRUE(texture.SetPixel(0, 0, 0xff617283u));
    EXPECT_FALSE(texture.LoadMovie(MoviePath)); EXPECT_EQ(nullptr, texture.GetMovieReader()); EXPECT_EQ(0xff617283u, texture.GetPixel(0, 0));
}
