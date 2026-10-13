#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include "CKAll.h"
#include "CKPluginManager.h"
#include "CKBitmapReader.h"
#include "RCKSprite.h"
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
class SpriteFileReader : public CKBitmapReader {
    CKBitmapProperties Properties;
public:
    SpriteFileReader() { Properties.m_Ext = CKFileExtension("sp8"); Properties.m_ReaderGuid = Infos[0].m_GUID; }
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
class SpriteMovieReader : public CKMovieReader {
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
        for (int i = 0; i < 6; ++i) framePixels[i] = frame == 0 ? FilePixels[i] : 0xff718293u;
        Properties.m_Format = Description(reinterpret_cast<CKBYTE *>(framePixels));
        Properties.m_Data = framePixels; *p = &Properties; return CK_OK;
    }
};
int ReaderCount() { return 2; }
CKPluginInfo *ReaderInfo(int index) { return index >= 0 && index < 2 ? &Infos[index] : nullptr; }
CKDataReader *ReaderFactory(int index) { return index == 0 ? static_cast<CKDataReader *>(new SpriteFileReader) : static_cast<CKDataReader *>(new SpriteMovieReader); }
class SpriteLifecycleProbe : public RCKSprite {
public:
    static void RecordLayout(RCKSprite &o) {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(&o);
        std::size_t previous = sizeof(RCK2dEntity);
#define FIELD(field) { auto m = &SpriteLifecycleProbe::field; const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&(o.*m)); \
        const std::size_t offset = address - base; EXPECT_GE(offset, previous); EXPECT_LE(offset + sizeof(o.*m), sizeof(RCKSprite)); \
        EXPECT_EQ(0u, address % alignof(decltype(o.*m))); ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(offset)); \
        ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(o.*m))); previous = offset + sizeof(o.*m); }
        FIELD(m_BitmapData); FIELD(m_VideoFormat); FIELD(m_VideoFormatDesc); FIELD(m_RasterizerContext); FIELD(m_ObjectIndex); FIELD(m_InVideoMemory);
#undef FIELD
        ::testing::Test::RecordProperty("SDLSize", static_cast<int>(sizeof(RCKSprite)));
        ::testing::Test::RecordProperty("SDLAlignment", static_cast<int>(alignof(RCKSprite)));
        ::testing::Test::RecordProperty("SDLBaseSize", static_cast<int>(sizeof(RCK2dEntity)));
        ::testing::Test::RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
        ::testing::Test::RecordProperty("m_ClassID_value", static_cast<int>(m_ClassID));
        EXPECT_EQ(nullptr, o.*(&SpriteLifecycleProbe::m_RasterizerContext));
        EXPECT_EQ(0u, o.*(&SpriteLifecycleProbe::m_ObjectIndex)); EXPECT_FALSE(o.*(&SpriteLifecycleProbe::m_InVideoMemory));
    }
};
class SpriteLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr; RCKRenderManager *Manager = nullptr; char Path[1024] = {}, MoviePath[1024] = {};
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKSprite, CKCID_2DENTITY); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); Manager = new RCKRenderManager(Context);
        ReadersReleased = MovieReads = 0;
        Infos[0].m_GUID = CKGUID(0x78000001, 1); Infos[0].m_Extension = CKFileExtension("sp8"); Infos[0].m_Type = CKPLUGIN_BITMAP_READER;
        Infos[1].m_GUID = CKGUID(0x78000001, 2); Infos[1].m_Extension = CKFileExtension("mv8"); Infos[1].m_Type = CKPLUGIN_MOVIE_READER;
        ASSERT_EQ(CK_OK, CKGetPluginManager()->RegisterStaticPlugin("Pass78 sprite fixture codec", ReaderCount, ReaderInfo, ReaderFactory));
        std::snprintf(Path, sizeof(Path), "%ssprite-pass78-%llu.sp8", ::testing::TempDir().c_str(), static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this)));
        std::snprintf(MoviePath, sizeof(MoviePath), "%ssprite-pass78-%llu.mv8", ::testing::TempDir().c_str(), static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this)));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); std::remove(Path); std::remove(MoviePath); }
    void WriteFile(const char *name) { FILE *f = std::fopen(name, "wb"); ASSERT_NE(nullptr, f); ASSERT_EQ(6u, std::fwrite(FilePixels, 4, 6, f)); ASSERT_EQ(0, std::fclose(f)); }
    static void Rect(const VxRect &r, float l, float t, float right, float bottom) {
        EXPECT_FLOAT_EQ(l, r.left); EXPECT_FLOAT_EQ(t, r.top); EXPECT_FLOAT_EQ(right, r.right); EXPECT_FLOAT_EQ(bottom, r.bottom);
    }
};
}
TEST_F(SpriteLifecycleTest, RegisteredIdentityDefaultsAndActualPlatformLayout) {
    Manager->m_SpriteVideoFormat.Value = _16_RGB565;
    RCKSprite *s = static_cast<RCKSprite *>(Context->CreateObject(CKCID_SPRITE)); ASSERT_NE(nullptr, s);
    EXPECT_EQ(28, s->GetClassID()); EXPECT_EQ(s, Context->GetObject(s->GetID())); EXPECT_TRUE(CKIsChildClassOf(s, CKCID_2DENTITY));
    EXPECT_STREQ("Sprite", RCKSprite::GetClassName()); EXPECT_EQ(_16_RGB565, s->GetDesiredVideoFormat());
    EXPECT_EQ(-1, s->GetWidth()); EXPECT_EQ(-1, s->GetHeight()); EXPECT_EQ(0, s->GetSlotCount());
    VxRect r; s->GetSourceRect(r); Rect(r, 0, 0, 0, 0); SpriteLifecycleProbe::RecordLayout(*s);
}
TEST_F(SpriteLifecycleTest, RegisterPublishesSpriteParameterAndNoOwnDependencies) {
    CKClassDesc *d = CKGetClassDesc(CKCID_SPRITE); ASSERT_NE(nullptr, d); d->Parameter = CKGUID(); RCKSprite::Register();
    EXPECT_EQ(CKPGUID_SPRITE, d->Parameter); EXPECT_EQ(1467887272u, d->Parameter.d1); EXPECT_EQ(1536170345u, d->Parameter.d2);
    for (int mode = -1; mode < 6; ++mode) { EXPECT_EQ(0, RCKSprite::GetDependenciesCount(mode)); EXPECT_EQ(nullptr, RCKSprite::GetDependencies(0, mode)); }
}
TEST_F(SpriteLifecycleTest, DirectFactoryAndVirtualDestructorReleaseOwnedBitmapAndObjectRegistration) {
    CKSprite *api = RCKSprite::CreateInstance(Context); ASSERT_NE(nullptr, api); RCKSprite *s = reinterpret_cast<RCKSprite *>(api);
    ASSERT_TRUE(s->Create(3, 2, 24)); ASSERT_TRUE(s->SetPixel(1, 1, 0xffabcdef)); const CK_ID id = s->GetID();
    delete static_cast<CKObject *>(s); EXPECT_EQ(nullptr, Context->GetObject(id));
}
TEST_F(SpriteLifecycleTest, MemoryOccupationIncludesPlatformClassDelta) {
    RCKSprite s(Context); const int base = s.RCK2dEntity::GetMemoryOccupation();
    EXPECT_EQ(base + static_cast<int>(sizeof(RCKSprite) - sizeof(RCK2dEntity)), s.GetMemoryOccupation());
    ASSERT_TRUE(s.Create(3, 2, 32)); EXPECT_EQ(base + static_cast<int>(sizeof(RCKSprite) - sizeof(RCK2dEntity)), s.GetMemoryOccupation());
}
TEST_F(SpriteLifecycleTest, CreateUsesArgbSystemPixelsAndUpdatesPixelSourceAndEntitySize) {
    RCKSprite s(Context); s.SetRect(VxRect(4, 5, 40, 50)); ASSERT_TRUE(s.Create(3, 2, 16));
    EXPECT_EQ(3, s.GetWidth()); EXPECT_EQ(2, s.GetHeight()); EXPECT_EQ(32, s.GetBitsPerPixel());
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 3; ++x) EXPECT_EQ(0xff000000u, s.GetPixel(x, y));
    VxRect r; s.GetRect(r); Rect(r, 4, 5, 7, 7); s.GetSourceRect(r); Rect(r, 0, 0, 3, 2);
}
TEST_F(SpriteLifecycleTest, CreateAdditionalSlotsRejectsDifferentDimensionsWithoutLosingExistingPixels) {
    RCKSprite s(Context); ASSERT_TRUE(s.Create(3, 2, 32)); ASSERT_TRUE(s.SetPixel(1, 1, 0xff123456));
    ASSERT_TRUE(s.Create(3, 2, 32, 1)); EXPECT_FALSE(s.Create(4, 2, 32, 2)); EXPECT_EQ(2, s.GetSlotCount());
    EXPECT_EQ(0xff123456u, s.GetPixel(1, 1, 0)); EXPECT_EQ(3, s.GetWidth());
}
TEST_F(SpriteLifecycleTest, RestoreInitialSizeChangesOnlyScreenDimensionsAndPreservesOriginAndSource) {
    RCKSprite s(Context); ASSERT_TRUE(s.Create(3, 2, 32)); s.SetRect(VxRect(11, 13, 111, 213)); VxRect src(1, 0, 2, 1); s.SetSourceRect(src);
    s.RestoreInitialSize(); VxRect r; s.GetRect(r); Rect(r, 11, 13, 14, 15); s.GetSourceRect(r); Rect(r, 1, 0, 2, 1);
}
TEST_F(SpriteLifecycleTest, LoadImageReadsActualFileIntoIndependentArgbPixelsAndSetsName) {
    WriteFile(Path); RCKSprite s(Context); ASSERT_TRUE(s.LoadImage(Path));
    EXPECT_EQ(3, s.GetWidth()); EXPECT_EQ(2, s.GetHeight()); EXPECT_EQ(FilePixels[4], s.GetPixel(1, 1));
    EXPECT_STREQ(Path, s.GetSlotFileName(0)); VxRect r; s.GetSourceRect(r); Rect(r, 0, 0, 3, 2); EXPECT_GT(ReadersReleased, 0);
}
TEST_F(SpriteLifecycleTest, LoadSecondaryImagePreservesCustomPixelSourceRect) {
    WriteFile(Path); RCKSprite s(Context); ASSERT_TRUE(s.Create(3, 2, 32)); VxRect source(1, .5f, 2, 1.5f); s.SetSourceRect(source);
    ASSERT_TRUE(s.LoadImage(Path, 1)); VxRect r; s.GetSourceRect(r); Rect(r, 1, .5f, 2, 1.5f); EXPECT_EQ(FilePixels[2], s.GetPixel(2, 0, 1));
}
TEST_F(SpriteLifecycleTest, MissingImageRetainsRequestedFilenameAndExistingPixels) {
    RCKSprite s(Context); ASSERT_TRUE(s.Create(3, 2, 32)); ASSERT_TRUE(s.SetPixel(0, 0, 0xff345678));
    EXPECT_FALSE(s.LoadImage(Path)); EXPECT_STREQ(Path, s.GetSlotFileName(0)); EXPECT_EQ(0xff345678u, s.GetPixel(0, 0)); EXPECT_FALSE(s.LoadImage(nullptr));
}
TEST_F(SpriteLifecycleTest, SaveImageWritesActualPixelsAndReturnsReaderFailure) {
    RCKSprite s(Context); ASSERT_TRUE(s.Create(3, 2, 32)); for (int y = 0; y < 2; ++y) for (int x = 0; x < 3; ++x) ASSERT_TRUE(s.SetPixel(x, y, FilePixels[y * 3 + x]));
    ASSERT_TRUE(s.SaveImage(Path, 0, FALSE)); FILE *f = std::fopen(Path, "rb"); ASSERT_NE(nullptr, f); CKDWORD output[6]; EXPECT_EQ(6u, std::fread(output, 4, 6, f)); std::fclose(f);
    for (int i = 0; i < 6; ++i) EXPECT_EQ(FilePixels[i], output[i]); EXPECT_FALSE(s.SaveImage(nullptr));
    char unsupported[] = "sprite-pass78-no-reader.zz8"; EXPECT_FALSE(s.SaveImage(unsupported, 0, FALSE));
}
TEST_F(SpriteLifecycleTest, LoadMovieUsesDecodedDimensionsRatherThanObsoleteRequestedDimensions) {
    WriteFile(MoviePath); RCKSprite s(Context); ASSERT_TRUE(s.LoadMovie(MoviePath, 90, 70, 16));
    EXPECT_EQ(3, s.GetWidth()); EXPECT_EQ(2, s.GetHeight()); EXPECT_NE(nullptr, s.GetMovieReader()); EXPECT_STREQ(MoviePath, s.GetMovieFileName());
    VxRect r; s.GetSourceRect(r); Rect(r, 0, 0, 3, 2); EXPECT_GT(MovieReads, 0); EXPECT_FALSE(s.LoadMovie(nullptr));
}
TEST_F(SpriteLifecycleTest, CopyMovieReopensIndependentReaderAndPreservesSelectedFrameAndPixels) {
    WriteFile(MoviePath); RCKSprite source(Context), destination(Context); ASSERT_TRUE(source.LoadMovie(MoviePath)); ASSERT_TRUE(source.SetCurrentSlot(1));
    EXPECT_EQ(0xff718293u, source.GetPixel(0, 0)); CKMovieReader *reader = source.GetMovieReader(); destination.CopySpriteData(&source);
    EXPECT_STREQ(MoviePath, destination.GetMovieFileName()); ASSERT_NE(nullptr, destination.GetMovieReader()); EXPECT_NE(reader, destination.GetMovieReader());
    EXPECT_EQ(1, destination.GetCurrentSlot()); EXPECT_EQ(0xff718293u, destination.GetPixel(0, 0));
    ASSERT_TRUE(source.SetCurrentSlot(0)); EXPECT_EQ(0xff718293u, destination.GetPixel(0, 0)); EXPECT_EQ(1, destination.GetCurrentSlot());
}
TEST_F(SpriteLifecycleTest, CopyPlainBitmapIntoMovieDestinationReleasesReaderAndReplacesEverySlot) {
    WriteFile(MoviePath); RCKSprite source(Context), destination(Context); ASSERT_TRUE(source.Create(3, 2, 32)); ASSERT_TRUE(source.Create(3, 2, 32, 1));
    ASSERT_TRUE(source.SetPixel(0, 0, 0xff123456, 0)); ASSERT_TRUE(source.SetPixel(2, 1, 0xffabcdef, 1)); ASSERT_TRUE(source.SetCurrentSlot(1));
    ASSERT_TRUE(destination.LoadMovie(MoviePath)); const int released = ReadersReleased; destination.CopySpriteData(&source);
    EXPECT_EQ(nullptr, destination.GetMovieFileName()); EXPECT_EQ(nullptr, destination.GetMovieReader()); EXPECT_GT(ReadersReleased, released);
    EXPECT_EQ(2, destination.GetSlotCount()); EXPECT_EQ(1, destination.GetCurrentSlot()); EXPECT_EQ(0xff123456u, destination.GetPixel(0, 0, 0)); EXPECT_EQ(0xffabcdefu, destination.GetPixel(2, 1, 1));
}
