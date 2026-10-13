#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKBitmapReader.h"
#include "CKPluginManager.h"
#include "CKDependencies.h"
#include "CKStateChunk.h"
#include "RCKTexture.h"
#include "RCKRenderManager.h"
#include "TextureTestObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
CKPluginInfo CopyMovieInfo;
int CopyMovieReadersReleased = 0;
class CopyMovieReader : public CKMovieReader {
    CKMovieProperties Properties;
    // Bottom-up codec rows; the copied image's logical top-left is independently 0xffabcdef.
    CKDWORD Pixels[4] = {0xff405060, 0xff708090, 0xffabcdef, 0xff102030};
public:
    void Release() override { ++CopyMovieReadersReleased; delete this; }
    CKPluginInfo *GetReaderInfo() override { return &CopyMovieInfo; }
    int GetOptionsCount() override { return 0; }
    CKSTRING GetOptionDescription(int) override { return nullptr; }
    int GetMovieFrameCount() override { return 2; }
    int GetMovieLength() override { return 100; }
    CKERROR OpenFile(CKSTRING name) override { return name ? CK_OK : CKMOVIEERROR_READERROR; }
    CKERROR OpenMemory(CKSTRING) override { return CKMOVIEERROR_READERROR; }
    CKERROR OpenAsynchronousFile(CKSTRING) override { return CKMOVIEERROR_READERROR; }
    CKERROR ReadFrame(int frame, CKMovieProperties **properties) override {
        if (frame < 0 || frame >= 2) return CKMOVIEERROR_READERROR;
        VxPixelFormat2ImageDesc(_32_ARGB8888, Properties.m_Format); Properties.m_Format.Width = 2; Properties.m_Format.Height = 2;
        Properties.m_Format.BytesPerLine = 8; Properties.m_Format.Image = reinterpret_cast<CKBYTE *>(Pixels); Properties.m_Data = Pixels;
        *properties = &Properties; return CK_OK;
    }
};
int CopyMovieReaderCount() { return 1; }
CKPluginInfo *CopyMovieReaderInfo(int index) { return index == 0 ? &CopyMovieInfo : nullptr; }
CKDataReader *CopyMovieReaderFactory(int index) { return index == 0 ? new CopyMovieReader : nullptr; }
class CopiedTexture : public RCKTexture {
public:
    explicit CopiedTexture(CKContext *context) : RCKTexture(context) {}
    void MipCount(CKDWORD value) { m_MipMapLevel = value; }
    int StorageSlotCount() const { return m_Slots.Size(); }
    CKDWORD Flags() const { return m_BitmapFlags; }
};
class CopyMovieTexture : public CopiedTexture {
public:
    explicit CopyMovieTexture(CKContext *context) : CopiedTexture(context) {}
    XString MovieName;
    int MovieCalls = 0;
    CKBOOL LoadMovie(CKSTRING name) override { ++MovieCalls; MovieName = name; return FALSE; }
};
class TextureCopyVideoTest : public TextureTestWorld {};
class TextureCopyTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override { SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp()); CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
        CopyMovieReadersReleased = 0; CopyMovieInfo.m_GUID = CKGUID(0x89000301, 1); CopyMovieInfo.m_Extension = CKFileExtension("tc9"); CopyMovieInfo.m_Type = CKPLUGIN_MOVIE_READER;
        ASSERT_EQ(CK_OK, CKGetPluginManager()->RegisterStaticPlugin("Pass89 copy fixture movie codec", CopyMovieReaderCount, CopyMovieReaderInfo, CopyMovieReaderFactory)); }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Image(CopiedTexture &texture, int slots = 1, int width = 2, int height = 2) {
        ASSERT_TRUE(texture.SetSlotCount(slots)); for (int i = 0; i < slots; ++i) ASSERT_TRUE(texture.CreateImage(width, height, 32, i));
    }
    CKERROR Copy(CopiedTexture &dest, CopiedTexture &source) { CKDependenciesContext dependencies(Context); return dest.Copy(source, dependencies); }
};
}

TEST_F(TextureCopyTest, PopulatedSlotsCopyPixelsAndFilenamesToIndependentStorage) {
    CopiedTexture source(Context), dest(Context); Image(source, 2); source.SetSlotFileName(0, "first.png"); source.SetSlotFileName(1, "second.png");
    source.SetPixel(0, 0, 0xff123456, 0); source.SetPixel(1, 1, 0x7fabcdef, 1); ASSERT_TRUE(source.SetCurrentSlot(1)); ASSERT_EQ(CK_OK, Copy(dest, source));
    EXPECT_EQ(2, dest.GetSlotCount()); EXPECT_EQ(1, dest.GetCurrentSlot()); EXPECT_STREQ("first.png", dest.GetSlotFileName(0)); EXPECT_STREQ("second.png", dest.GetSlotFileName(1));
    EXPECT_EQ(0xff123456u, dest.GetPixel(0, 0, 0)); EXPECT_EQ(0x7fabcdefu, dest.GetPixel(1, 1, 1)); EXPECT_NE(source.LockSurfacePtr(0), dest.LockSurfacePtr(0));
    dest.SetPixel(0, 0, 0xff112233, 0); EXPECT_EQ(0xff123456u, source.GetPixel(0, 0, 0)); source.SetPixel(1, 1, 0xff000000, 1); EXPECT_EQ(0x7fabcdefu, dest.GetPixel(1, 1, 1));
}
TEST_F(TextureCopyTest, FilenameOnlySlotsRemainEmptyAndRetainTheirExternalReference) {
    CopiedTexture source(Context), dest(Context); ASSERT_TRUE(source.SetSlotCount(2)); source.SetSlotFileName(1, "external.tga"); ASSERT_EQ(CK_OK, Copy(dest, source));
    EXPECT_EQ(2, dest.GetSlotCount()); EXPECT_EQ(nullptr, dest.LockSurfacePtr(1)); EXPECT_STREQ("external.tga", dest.GetSlotFileName(1));
}
TEST_F(TextureCopyTest, CopyReplacesDestinationDimensionsSlotsAndOldUserMips) {
    CopiedTexture source(Context), dest(Context); Image(source, 2, 3, 2); Image(dest, 1, 8, 4); ASSERT_TRUE(dest.SetUserMipMapMode(TRUE));
    source.SetPixel(2, 1, 0x80402010, 1); ASSERT_EQ(CK_OK, Copy(dest, source)); EXPECT_EQ(3, dest.GetWidth()); EXPECT_EQ(2, dest.GetHeight()); EXPECT_EQ(2, dest.GetSlotCount());
    EXPECT_EQ(0x80402010u, dest.GetPixel(2, 1, 1)); VxImageDescEx desc; EXPECT_FALSE(dest.GetUserMipMapLevel(0, desc));
}
TEST_F(TextureCopyTest, CopyTransfersOwnAndBitmapSettingsAndMarksTheImageForRestore) {
    CopiedTexture source(Context), dest(Context); Image(source); source.SetTransparentColor(0x12345678); source.SetTransparent(TRUE); source.SetSaveOptions(CKTEXTURE_IMAGEFORMAT);
    source.SetPickThreshold(25); source.SetDesiredVideoFormat(_32_ARGB8888); source.MipCount(static_cast<CKDWORD>(-1)); ASSERT_EQ(CK_OK, Copy(dest, source));
    EXPECT_TRUE(dest.IsTransparent()); EXPECT_EQ(0x12345678u, dest.GetTransparentColor()); EXPECT_EQ(CKTEXTURE_IMAGEFORMAT, dest.GetSaveOptions()); EXPECT_EQ(25, dest.GetPickThreshold());
    EXPECT_EQ(_32_ARGB8888, dest.GetDesiredVideoFormat()); EXPECT_EQ(-1, dest.GetMipmapCount()); EXPECT_NE(0u, dest.Flags() & CKBITMAPDATA_FORCERESTORE);
}
TEST_F(TextureCopyTest, SavePropertiesAreDeepCopiedAndSurviveSourceDestruction) {
    struct Properties : CKBitmapProperties { CKDWORD Marker; } properties; properties.m_Size = sizeof(properties); properties.Marker = 0x98765432;
    CopiedTexture dest(Context); { CopiedTexture source(Context); Image(source); source.SetSaveFormat(&properties); ASSERT_EQ(CK_OK, Copy(dest, source));
        ASSERT_NE(nullptr, dest.GetSaveFormat()); EXPECT_NE(source.GetSaveFormat(), dest.GetSaveFormat()); static_cast<Properties *>(source.GetSaveFormat())->Marker = 1; }
    ASSERT_NE(nullptr, dest.GetSaveFormat()); EXPECT_EQ(0x98765432u, static_cast<Properties *>(dest.GetSaveFormat())->Marker);
}
TEST_F(TextureCopyTest, SourceWithoutSavePropertiesRemovesDestinationProperties) {
    CKBitmapProperties properties; CopiedTexture source(Context), dest(Context); Image(source); dest.SetSaveFormat(&properties); ASSERT_NE(nullptr, dest.GetSaveFormat());
    ASSERT_EQ(CK_OK, Copy(dest, source)); EXPECT_EQ(nullptr, dest.GetSaveFormat());
}
TEST_F(TextureCopyTest, UserMipDescriptorsAndPixelsAreDeepCopiedAndSurviveSourceDestruction) {
    CopiedTexture dest(Context); CKBYTE *sourceAddress = nullptr;
    { CopiedTexture source(Context); Image(source, 1, 4, 2); ASSERT_TRUE(source.SetUserMipMapMode(TRUE)); VxImageDescEx desc;
        ASSERT_TRUE(source.GetUserMipMapLevel(0, desc)); sourceAddress = desc.Image; reinterpret_cast<CKDWORD *>(desc.Image)[0] = 0xff112233; reinterpret_cast<CKDWORD *>(desc.Image)[1] = 0x80445566;
        ASSERT_TRUE(source.GetUserMipMapLevel(1, desc)); reinterpret_cast<CKDWORD *>(desc.Image)[0] = 0x7f778899; ASSERT_EQ(CK_OK, Copy(dest, source));
        VxImageDescEx copied; ASSERT_TRUE(dest.GetUserMipMapLevel(0, copied)); EXPECT_NE(sourceAddress, copied.Image); EXPECT_EQ(2, copied.Width); EXPECT_EQ(1, copied.Height);
        reinterpret_cast<CKDWORD *>(copied.Image)[0] = 0xffabcdef; ASSERT_TRUE(source.GetUserMipMapLevel(0, desc)); EXPECT_EQ(0xff112233u, reinterpret_cast<CKDWORD *>(desc.Image)[0]); }
    VxImageDescEx desc; ASSERT_TRUE(dest.GetUserMipMapLevel(0, desc)); EXPECT_EQ(0xffabcdefu, reinterpret_cast<CKDWORD *>(desc.Image)[0]); EXPECT_EQ(0x80445566u, reinterpret_cast<CKDWORD *>(desc.Image)[1]);
    ASSERT_TRUE(dest.GetUserMipMapLevel(1, desc)); EXPECT_EQ(0x7f778899u, reinterpret_cast<CKDWORD *>(desc.Image)[0]);
}
TEST_F(TextureCopyTest, SelfCopyPreservesPixelsFilenamesPropertiesAndUserMipStorage) {
    CopiedTexture texture(Context); Image(texture, 1, 4, 2); texture.SetPixel(1, 1, 0x12345678); texture.SetSlotFileName(0, "self.png"); texture.SetPickThreshold(29);
    CKBitmapProperties properties; texture.SetSaveFormat(&properties); ASSERT_TRUE(texture.SetUserMipMapMode(TRUE)); VxImageDescEx desc; ASSERT_TRUE(texture.GetUserMipMapLevel(0, desc));
    reinterpret_cast<CKDWORD *>(desc.Image)[0] = 0xff102030; CKBYTE *mip = desc.Image; CKBYTE *surface = texture.LockSurfacePtr(); CKBitmapProperties *saved = texture.GetSaveFormat();
    ASSERT_EQ(CK_OK, Copy(texture, texture)); EXPECT_EQ(surface, texture.LockSurfacePtr()); EXPECT_EQ(0x12345678u, texture.GetPixel(1, 1)); EXPECT_STREQ("self.png", texture.GetSlotFileName(0));
    EXPECT_EQ(29, texture.GetPickThreshold()); EXPECT_EQ(saved, texture.GetSaveFormat()); ASSERT_TRUE(texture.GetUserMipMapLevel(0, desc)); EXPECT_EQ(mip, desc.Image); EXPECT_EQ(0xff102030u, reinterpret_cast<CKDWORD *>(desc.Image)[0]);
}
TEST_F(TextureCopyTest, MovieCopyLoadsTheSourceFilenameAndFailureCopiesOnlyExistingDecodedStorage) {
    CopiedTexture source(Context); CopyMovieTexture dest(Context); ASSERT_TRUE(source.LoadMovieFile(XString("source_movie.tc9"))); ASSERT_TRUE(dest.LoadMovieFile(XString("old_destination.tc9")));
    ASSERT_NE(nullptr, source.GetMovieReader()); ASSERT_NE(nullptr, dest.GetMovieReader()); source.SetSlotFileName(0, "decoded.tga"); const int releases = CopyMovieReadersReleased;
    ASSERT_EQ(CK_OK, Copy(dest, source)); EXPECT_EQ(1, dest.MovieCalls); EXPECT_STREQ("source_movie.tc9", dest.MovieName.Str()); EXPECT_EQ(releases + 1, CopyMovieReadersReleased);
    EXPECT_EQ(nullptr, dest.GetMovieFileName()); EXPECT_EQ(1, dest.StorageSlotCount()); EXPECT_EQ(0xffabcdefu, dest.GetPixel(0, 0)); EXPECT_STREQ("decoded.tga", dest.GetSlotFileName(0));
    EXPECT_EQ(2, source.GetSlotCount()); EXPECT_STREQ("source_movie.tc9", source.GetMovieFileName()); EXPECT_NE(nullptr, source.GetMovieReader());
}
TEST_F(TextureCopyTest, LiteralBitmapLoadReleasesExistingMovieReaderAndReplacesFramePixels) {
    CopiedTexture texture(Context); ASSERT_TRUE(texture.LoadMovieFile(XString("old_movie.tc9")));
    ASSERT_NE(nullptr, texture.GetMovieReader()); EXPECT_EQ(0xffabcdefu, texture.GetPixel(0, 0));
    const int releases = CopyMovieReadersReleased;
    CKStateChunk chunk(CKCID_TEXTURE, nullptr); chunk.StartWrite(); chunk.SetDataVersion(10);
    chunk.WriteIdentifier(0x20000); chunk.WriteInt(1);
    chunk.WriteInt(32); chunk.WriteInt(1); chunk.WriteInt(1);
    chunk.WriteDword(0xff000000); chunk.WriteDword(0xff0000); chunk.WriteDword(0xff00); chunk.WriteDword(0xff); chunk.WriteDword(0);
    CKBYTE channels[] = {0x89, 0x67, 0x45, 0xff};
    for (int i = 0; i < 4; ++i) chunk.WriteBuffer_LEndian(1, &channels[i]);
    chunk.CloseChunk(); ASSERT_EQ(CK_OK, texture.Load(&chunk, nullptr));
    EXPECT_EQ(releases + 1, CopyMovieReadersReleased); EXPECT_EQ(nullptr, texture.GetMovieReader());
    EXPECT_EQ(nullptr, texture.GetMovieFileName()); EXPECT_EQ(1, texture.GetSlotCount());
    EXPECT_EQ(1, texture.GetWidth()); EXPECT_EQ(1, texture.GetHeight());
    EXPECT_EQ(0xff456789u, texture.GetPixel(0, 0));
}
TEST_F(TextureCopyTest, RepeatedCopyDoesNotRetainAnEarlierSourcesUserMipOrSaveFormat) {
    CopiedTexture first(Context), second(Context), dest(Context); Image(first, 1, 4, 2); ASSERT_TRUE(first.SetUserMipMapMode(TRUE)); CKBitmapProperties properties; first.SetSaveFormat(&properties);
    Image(second, 2, 3, 2); ASSERT_EQ(CK_OK, Copy(dest, first)); ASSERT_NE(nullptr, dest.GetSaveFormat()); VxImageDescEx desc; ASSERT_TRUE(dest.GetUserMipMapLevel(0, desc));
    ASSERT_EQ(CK_OK, Copy(dest, second)); EXPECT_EQ(nullptr, dest.GetSaveFormat()); EXPECT_FALSE(dest.GetUserMipMapLevel(0, desc)); EXPECT_EQ(2, dest.GetSlotCount()); EXPECT_EQ(3, dest.GetWidth());
}
TEST_F(TextureCopyVideoTest, FailedResidentDeletionReturnsErrorAndPreservesOwnedBitmapAndMipResources) {
    CopiedTexture source(Context), dest(Context); ASSERT_TRUE(source.CreateImage(3, 2, 32, 0)); ASSERT_TRUE(source.CreateImage(3, 2, 32, 1)); ASSERT_TRUE(dest.CreateImage(4, 2));
    dest.SetPixel(1, 1, 0x7f123456); dest.SetSlotFileName(0, "retained.png"); CKBitmapProperties properties; dest.SetSaveFormat(&properties);
    ASSERT_TRUE(dest.SetUserMipMapMode(TRUE)); VxImageDescEx before; ASSERT_TRUE(dest.GetUserMipMapLevel(0, before)); reinterpret_cast<CKDWORD *>(before.Image)[0] = 0xffabcdef;
    dest.SetDesiredVideoFormat(_32_ARGB8888); ASSERT_TRUE(dest.SystemToVideoMemory(Device, FALSE)); ASSERT_TRUE(dest.IsInVideoMemory()); const CKDWORD handle = dest.GetRstTextureIndex(); ASSERT_TRUE(State.Live[handle]);
    CKBYTE *surface = dest.LockSurfacePtr(); CKBitmapProperties *saved = dest.GetSaveFormat(); State.DeleteResult = FALSE; CKDependenciesContext dependencies(Context);
    EXPECT_EQ(CKERR_INVALIDOPERATION, dest.Copy(source, dependencies)); EXPECT_EQ(1, State.Deletes); EXPECT_TRUE(State.Live[handle]); EXPECT_TRUE(dest.IsInVideoMemory()); EXPECT_EQ(handle, dest.GetRstTextureIndex());
    EXPECT_EQ(4, dest.GetWidth()); EXPECT_EQ(2, dest.GetHeight()); EXPECT_EQ(1, dest.GetSlotCount()); EXPECT_EQ(surface, dest.LockSurfacePtr()); EXPECT_EQ(0x7f123456u, dest.GetPixel(1, 1));
    EXPECT_STREQ("retained.png", dest.GetSlotFileName(0)); EXPECT_EQ(saved, dest.GetSaveFormat()); VxImageDescEx after; ASSERT_TRUE(dest.GetUserMipMapLevel(0, after)); EXPECT_EQ(before.Image, after.Image);
    EXPECT_EQ(0xffabcdefu, reinterpret_cast<CKDWORD *>(after.Image)[0]); State.DeleteResult = TRUE; ASSERT_TRUE(dest.FreeVideoMemory()); EXPECT_FALSE(State.Live[handle]);
}
TEST_F(TextureCopyVideoTest, SuccessfulResidentDeletionReplacesPixelsAndLeavesNewImageReadyForRestore) {
    CopiedTexture source(Context), dest(Context); ASSERT_TRUE(source.CreateImage(3, 2, 32, 0)); ASSERT_TRUE(source.CreateImage(3, 2, 32, 1)); ASSERT_TRUE(dest.CreateImage(4, 2)); source.SetPixel(2, 1, 0xff987654, 1); source.SetSlotFileName(1, "new.png");
    ASSERT_TRUE(dest.SetUserMipMapMode(TRUE)); dest.SetDesiredVideoFormat(_32_ARGB8888); ASSERT_TRUE(dest.SystemToVideoMemory(Device, FALSE)); ASSERT_TRUE(dest.IsInVideoMemory()); const CKDWORD handle = dest.GetRstTextureIndex(); ASSERT_TRUE(State.Live[handle]);
    CKDependenciesContext dependencies(Context); ASSERT_EQ(CK_OK, dest.Copy(source, dependencies)); EXPECT_EQ(1, State.Deletes); EXPECT_FALSE(State.Live[handle]); EXPECT_FALSE(dest.IsInVideoMemory()); EXPECT_EQ(0, dest.GetRstTextureIndex());
    EXPECT_EQ(3, dest.GetWidth()); EXPECT_EQ(2, dest.GetHeight()); EXPECT_EQ(2, dest.GetSlotCount()); EXPECT_EQ(0xff987654u, dest.GetPixel(2, 1, 1)); EXPECT_STREQ("new.png", dest.GetSlotFileName(1));
    EXPECT_NE(0u, dest.Flags() & CKBITMAPDATA_FORCERESTORE); VxImageDescEx desc; EXPECT_FALSE(dest.GetUserMipMapLevel(0, desc)); EXPECT_EQ(0xff987654u, source.GetPixel(2, 1, 1));
}
