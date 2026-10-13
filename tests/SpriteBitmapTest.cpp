#include <gtest/gtest.h>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include "CKAll.h"
#include "CKBitmapData.h"
#include "CKBitmapReader.h"
#include "CKMovieReader.h"
#include "CKPluginManager.h"
#include "RCKRenderObject.h"
#include "RCK2dEntity.h"
#include "RCKSprite.h"
#include "RCKMaterial.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
CKPluginInfo MovieProviderInfo;
class SpriteBitmapProbe : public RCKSprite {
public:
    static CKBitmapData &Bitmap(RCKSprite &sprite) { return sprite.*(&SpriteBitmapProbe::m_BitmapData); }
};

// A media-provider boundary supplies literal frame pixels. Slot selection,
// image storage and conversion remain the production CKBitmapData code.
class SpriteMovieProvider : public CKMovieReader {
public:
    int Requests = 0;
    int LastFrame = -1;
    CKDWORD Pixels[4] = {};
    CKMovieProperties Properties;
    void Release() override { delete this; }
    CKPluginInfo *GetReaderInfo() override { return &MovieProviderInfo; }
    int GetOptionsCount() override { return 0; }
    CKSTRING GetOptionDescription(int) override { return nullptr; }
    int GetMovieFrameCount() override { return 3; }
    int GetMovieLength() override { return 90; }
    CKERROR OpenFile(CKSTRING name) override {
        FILE *file = std::fopen(name, "rb"); if (!file) return CKMOVIEERROR_READERROR;
        const int marker = std::fgetc(file); std::fclose(file);
        return marker == 0x78 ? CK_OK : CKMOVIEERROR_READERROR;
    }
    CKERROR OpenMemory(CKSTRING) override { return CKMOVIEERROR_READERROR; }
    CKERROR OpenAsynchronousFile(CKSTRING) override { return CKMOVIEERROR_READERROR; }
    CKERROR ReadFrame(int frame, CKMovieProperties **properties) override {
        ++Requests; LastFrame = frame;
        const CKDWORD colors[3] = {0xFF102030u, 0xFF405060u, 0xFF708090u};
        if (frame < 0 || frame > 2) return CKERR_INVALIDPARAMETER;
        for (int i = 0; i < 4; ++i) Pixels[i] = colors[frame];
        Properties.m_Format.Width = 2; Properties.m_Format.Height = 2;
        Properties.m_Format.BitsPerPixel = 32; Properties.m_Format.BytesPerLine = 8;
        Properties.m_Format.RedMask = 0x00FF0000; Properties.m_Format.GreenMask = 0x0000FF00;
        Properties.m_Format.BlueMask = 0x000000FF; Properties.m_Format.AlphaMask = 0xFF000000;
        Properties.m_Data = Pixels; *properties = &Properties; return CK_OK;
    }
};
int MovieProviderCount() { return 1; }
CKPluginInfo *MovieProviderDescription(int index) { return index == 0 ? &MovieProviderInfo : nullptr; }
CKDataReader *CreateMovieProvider(int) { return new SpriteMovieProvider; }

struct SpriteSaveProperties : CKBitmapProperties {
    CKDWORD Quality;
    CKDWORD Marker;
    SpriteSaveProperties() {
        m_Size = sizeof(SpriteSaveProperties); m_ReaderGuid = CKGUID(0x12345678, 0x89ABCDEF);
        m_Ext = CKFileExtension("png"); Quality = 73; Marker = 0x10203040;
    }
};

class SpriteBitmapTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    char MoviePath[1024] = {};
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKSprite, CKCID_2DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        MovieProviderInfo.m_GUID = CKGUID(0x78000002, 1);
        MovieProviderInfo.m_Extension = CKFileExtension("mb8"); MovieProviderInfo.m_Type = CKPLUGIN_MOVIE_READER;
        ASSERT_EQ(CK_OK, CKGetPluginManager()->RegisterStaticPlugin("Pass78 bitmap fixture movie codec",
            MovieProviderCount, MovieProviderDescription, CreateMovieProvider));
        std::snprintf(MoviePath, sizeof(MoviePath), "%ssprite-bitmap-pass78-%llu.mb8", ::testing::TempDir().c_str(),
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this)));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); std::remove(MoviePath); }
    RCKSprite *Sprite() { return static_cast<RCKSprite *>(Context->CreateObject(CKCID_SPRITE)); }
    static void AcknowledgeChanges(RCKSprite &sprite) {
        SpriteBitmapProbe::Bitmap(sprite).m_BitmapFlags &= ~CKBITMAPDATA_FORCERESTORE;
    }
    void WriteMovie() {
        FILE *file = std::fopen(MoviePath, "wb"); ASSERT_NE(nullptr, file);
        EXPECT_EQ(0x78, std::fputc(0x78, file)); EXPECT_EQ(0, std::fclose(file));
    }
};

TEST_F(SpriteBitmapTest, EmptyBitmapReportsNoSlotsImagesOrMovieAndDefaultProperties) {
    RCKSprite *sprite = Sprite(); ASSERT_NE(nullptr, sprite);
    EXPECT_EQ(-1, sprite->GetWidth()); EXPECT_EQ(-1, sprite->GetHeight());
    EXPECT_EQ(0, sprite->GetSlotCount()); EXPECT_EQ(0, sprite->GetCurrentSlot());
    EXPECT_EQ(nullptr, sprite->LockSurfacePtr()); EXPECT_EQ(nullptr, sprite->GetSlotFileName(0));
    EXPECT_EQ(nullptr, sprite->GetMovieFileName()); EXPECT_EQ(nullptr, sprite->GetMovieReader());
    EXPECT_EQ(0, sprite->GetPickThreshold()); EXPECT_EQ(nullptr, sprite->GetSaveFormat());
    EXPECT_FALSE(sprite->IsTransparent()); EXPECT_EQ(0u, sprite->GetTransparentColor());
    EXPECT_FALSE(sprite->SetCurrentSlot(0)); EXPECT_FALSE(sprite->ReleaseSlot(0));
    EXPECT_FALSE(sprite->SetPixel(0, 0, 0x12345678)); EXPECT_EQ(0u, sprite->GetPixel(0, 0));
}

TEST_F(SpriteBitmapTest, SystemDescriptionAndMaskAccessorsDescribeActualArgbPixels) {
    RCKSprite *sprite = Sprite(); ASSERT_TRUE(sprite->Create(3, 2, 16, 0));
    VxImageDescEx description; description.Image = reinterpret_cast<CKBYTE *>(sprite);
    ASSERT_TRUE(sprite->GetSystemTextureDesc(description));
    EXPECT_EQ(3, description.Width); EXPECT_EQ(2, description.Height);
    EXPECT_EQ(32, description.BitsPerPixel); EXPECT_EQ(12, description.BytesPerLine);
    EXPECT_EQ(nullptr, description.Image);
    EXPECT_EQ(3, sprite->GetWidth()); EXPECT_EQ(2, sprite->GetHeight());
    EXPECT_EQ(32, sprite->GetBitsPerPixel()); EXPECT_EQ(12, sprite->GetBytesPerLine());
    EXPECT_EQ(0x00FF0000u, static_cast<CKDWORD>(sprite->GetRedMask()));
    EXPECT_EQ(0x0000FF00u, static_cast<CKDWORD>(sprite->GetGreenMask()));
    EXPECT_EQ(0x000000FFu, static_cast<CKDWORD>(sprite->GetBlueMask()));
    EXPECT_EQ(0xFF000000u, static_cast<CKDWORD>(sprite->GetAlphaMask()));
    ASSERT_TRUE(sprite->SetPixel(2, 1, 0x7F123456u));
    CKBYTE *bytes = sprite->LockSurfacePtr(); ASSERT_NE(nullptr, bytes);
    EXPECT_EQ(0x56, bytes[20]); EXPECT_EQ(0x34, bytes[21]);
    EXPECT_EQ(0x12, bytes[22]); EXPECT_EQ(0x7F, bytes[23]);
}

TEST_F(SpriteBitmapTest, SlotPixelsAndCurrentSlotSelectorsRemainIndependent) {
    RCKSprite *sprite = Sprite(); ASSERT_TRUE(sprite->Create(2, 2, 32, 0));
    ASSERT_TRUE(sprite->Create(2, 2, 32, 1));
    EXPECT_EQ(2, sprite->GetSlotCount()); EXPECT_EQ(0, sprite->GetCurrentSlot());
    EXPECT_EQ(0xFF000000u, sprite->GetPixel(1, 1, 0));
    ASSERT_TRUE(sprite->SetPixel(1, 1, 0xFF010203u, 0));
    ASSERT_TRUE(sprite->SetPixel(1, 1, 0xFF112233u, 1));
    ASSERT_TRUE(sprite->SetCurrentSlot(1));
    EXPECT_EQ(0xFF112233u, sprite->GetPixel(1, 1));
    ASSERT_TRUE(sprite->SetPixel(0, 1, 0xFF445566u));
    EXPECT_EQ(0xFF445566u, sprite->GetPixel(0, 1, 1));
    EXPECT_EQ(0xFF000000u, sprite->GetPixel(0, 1, 0));
    EXPECT_NE(sprite->LockSurfacePtr(0), sprite->LockSurfacePtr(1));
    EXPECT_EQ(sprite->LockSurfacePtr(1), sprite->LockSurfacePtr(-1));
    EXPECT_EQ(sprite->LockSurfacePtr(1), sprite->LockSurfacePtr(99));
    EXPECT_FALSE(sprite->SetCurrentSlot(99)); EXPECT_EQ(1, sprite->GetCurrentSlot());
    EXPECT_FALSE(sprite->SetPixel(0, 0, 0, 99)); EXPECT_EQ(0u, sprite->GetPixel(0, 0, 99));
}

TEST_F(SpriteBitmapTest, SurfaceWriteAndReleasePublishChangedImageWithoutAlteringPixels) {
    RCKSprite *sprite = Sprite(); ASSERT_TRUE(sprite->Create(2, 1));
    CKBYTE *surface = sprite->LockSurfacePtr(); ASSERT_NE(nullptr, surface);
    const CKDWORD colors[2] = {0x10203040u, 0xA0B0C0D0u}; std::memcpy(surface, colors, sizeof(colors));
    AcknowledgeChanges(*sprite); ASSERT_TRUE(sprite->ReleaseSurfacePtr());
    EXPECT_TRUE(SpriteBitmapProbe::Bitmap(*sprite).m_BitmapFlags & CKBITMAPDATA_FORCERESTORE);
    EXPECT_EQ(colors[0], sprite->GetPixel(0, 0)); EXPECT_EQ(colors[1], sprite->GetPixel(1, 0));
}

TEST_F(SpriteBitmapTest, SlotNamesOwnTheirStorageAndRejectInvalidIndices) {
    RCKSprite *sprite = Sprite(); ASSERT_TRUE(sprite->SetSlotCount(2));
    char first[] = "folder/first.png"; char second[] = "second.tga";
    ASSERT_TRUE(sprite->SetSlotFileName(0, first)); ASSERT_TRUE(sprite->SetSlotFileName(1, second));
    first[0] = 'X'; second[0] = 'X';
    EXPECT_STREQ("folder/first.png", sprite->GetSlotFileName(0));
    EXPECT_STREQ("second.tga", sprite->GetSlotFileName(1));
    EXPECT_FALSE(sprite->SetSlotFileName(-1, first)); EXPECT_FALSE(sprite->SetSlotFileName(2, second));
    EXPECT_EQ(nullptr, sprite->GetSlotFileName(-1)); EXPECT_EQ(nullptr, sprite->GetSlotFileName(2));
    ASSERT_TRUE(sprite->SetSlotFileName(0, nullptr)); EXPECT_STREQ("", sprite->GetSlotFileName(0));
}

TEST_F(SpriteBitmapTest, GrowingAndShrinkingSlotsPreservesRetainedImagesAndClampsCurrentSlot) {
    RCKSprite *sprite = Sprite(); ASSERT_TRUE(sprite->Create(2, 1));
    ASSERT_TRUE(sprite->SetPixel(0, 0, 0xFF112233u));
    CKBYTE *original = sprite->LockSurfacePtr(0);
    ASSERT_TRUE(sprite->SetSlotCount(3)); EXPECT_EQ(3, sprite->GetSlotCount());
    EXPECT_EQ(original, sprite->LockSurfacePtr(0)); EXPECT_EQ(nullptr, sprite->LockSurfacePtr(2));
    ASSERT_TRUE(sprite->SetCurrentSlot(2)); ASSERT_TRUE(sprite->SetSlotCount(1));
    EXPECT_EQ(0, sprite->GetCurrentSlot()); EXPECT_EQ(0xFF112233u, sprite->GetPixel(0, 0));
    EXPECT_FALSE(sprite->SetSlotCount(-1)); EXPECT_EQ(1, sprite->GetSlotCount());
    ASSERT_TRUE(sprite->SetSlotCount(0)); EXPECT_EQ(0, sprite->GetSlotCount());
    EXPECT_EQ(-1, sprite->GetCurrentSlot()); EXPECT_EQ(nullptr, sprite->LockSurfacePtr());
}

TEST_F(SpriteBitmapTest, ReleasingSelectedTailSlotClampsToSurvivingImage) {
    RCKSprite *sprite = Sprite(); ASSERT_TRUE(sprite->Create(2, 1, 32, 0));
    ASSERT_TRUE(sprite->Create(2, 1, 32, 1)); ASSERT_TRUE(sprite->Create(2, 1, 32, 2));
    ASSERT_TRUE(sprite->SetPixel(0, 0, 0xFFABCDEFu, 1)); ASSERT_TRUE(sprite->SetCurrentSlot(2));
    ASSERT_TRUE(sprite->ReleaseSlot(2)); EXPECT_EQ(2, sprite->GetSlotCount());
    EXPECT_EQ(1, sprite->GetCurrentSlot()); EXPECT_EQ(0xFFABCDEFu, sprite->GetPixel(0, 0));
    EXPECT_FALSE(sprite->ReleaseSlot(-1)); EXPECT_FALSE(sprite->ReleaseSlot(2));
    ASSERT_TRUE(sprite->ReleaseAllSlots()); EXPECT_EQ(0, sprite->GetSlotCount());
    EXPECT_EQ(-1, sprite->GetCurrentSlot()); EXPECT_EQ(nullptr, sprite->LockSurfacePtr());
    ASSERT_TRUE(sprite->ReleaseAllSlots());
}

TEST_F(SpriteBitmapTest, TransparencyTransitionsAndColorChangesPublishRestoreOnlyWhenNeeded) {
    RCKSprite *sprite = Sprite(); ASSERT_TRUE(sprite->Create(2, 1));
    AcknowledgeChanges(*sprite); sprite->SetTransparentColor(0x00112233u);
    EXPECT_EQ(0x00112233u, sprite->GetTransparentColor());
    EXPECT_FALSE(SpriteBitmapProbe::Bitmap(*sprite).m_BitmapFlags & CKBITMAPDATA_FORCERESTORE);
    sprite->SetTransparent(TRUE); EXPECT_TRUE(sprite->IsTransparent());
    EXPECT_TRUE(SpriteBitmapProbe::Bitmap(*sprite).m_BitmapFlags & CKBITMAPDATA_FORCERESTORE);
    AcknowledgeChanges(*sprite); sprite->SetTransparent(TRUE);
    EXPECT_FALSE(SpriteBitmapProbe::Bitmap(*sprite).m_BitmapFlags & CKBITMAPDATA_FORCERESTORE);
    sprite->SetTransparentColor(0x00112233u);
    EXPECT_FALSE(SpriteBitmapProbe::Bitmap(*sprite).m_BitmapFlags & CKBITMAPDATA_FORCERESTORE);
    sprite->SetTransparentColor(0x00445566u);
    EXPECT_EQ(0x00445566u, sprite->GetTransparentColor());
    EXPECT_TRUE(SpriteBitmapProbe::Bitmap(*sprite).m_BitmapFlags & CKBITMAPDATA_FORCERESTORE);
    AcknowledgeChanges(*sprite); sprite->SetTransparent(FALSE); EXPECT_FALSE(sprite->IsTransparent());
    EXPECT_FALSE(SpriteBitmapProbe::Bitmap(*sprite).m_BitmapFlags & CKBITMAPDATA_FORCERESTORE);
    EXPECT_EQ(0xFF000000u, sprite->GetPixel(0, 0));
}

TEST_F(SpriteBitmapTest, PickThresholdAndSaveOptionsPreserveCallerValues) {
    RCKSprite *sprite = Sprite();
    const int thresholds[] = {0, 1, 127, 255};
    for (int i = 0; i < 4; ++i) { sprite->SetPickThreshold(thresholds[i]); EXPECT_EQ(thresholds[i], sprite->GetPickThreshold()); }
    const CK_BITMAP_SAVEOPTIONS options[] = {CKTEXTURE_RAWDATA, CKTEXTURE_EXTERNAL, CKTEXTURE_IMAGEFORMAT, CKTEXTURE_USEGLOBAL, CKTEXTURE_INCLUDEORIGINALFILE};
    for (int i = 0; i < 5; ++i) { sprite->SetSaveOptions(options[i]); EXPECT_EQ(options[i], sprite->GetSaveOptions()); }
}

TEST_F(SpriteBitmapTest, SaveFormatOwnsCompleteDerivedPropertiesAndCanBeReplacedOrCleared) {
    RCKSprite *sprite = Sprite(); SpriteSaveProperties original;
    sprite->SetSaveFormat(&original);
    SpriteSaveProperties *saved = static_cast<SpriteSaveProperties *>(sprite->GetSaveFormat());
    ASSERT_NE(nullptr, saved); EXPECT_NE(&original, saved); EXPECT_EQ(sizeof(original), saved->m_Size);
    EXPECT_EQ(original.m_ReaderGuid, saved->m_ReaderGuid); EXPECT_STREQ("png", saved->m_Ext.m_Data);
    EXPECT_EQ(73u, saved->Quality); EXPECT_EQ(0x10203040u, saved->Marker);
    original.Quality = 11; original.Marker = 0xAABBCCDD;
    EXPECT_EQ(73u, saved->Quality); EXPECT_EQ(0x10203040u, saved->Marker);
    sprite->SetSaveFormat(&original); saved = static_cast<SpriteSaveProperties *>(sprite->GetSaveFormat());
    ASSERT_NE(nullptr, saved); EXPECT_EQ(11u, saved->Quality); EXPECT_EQ(0xAABBCCDDu, saved->Marker);
    sprite->SetSaveFormat(nullptr); EXPECT_EQ(nullptr, sprite->GetSaveFormat());
}

TEST_F(SpriteBitmapTest, SaveFormatAcceptsItsCurrentOwnedPropertiesWithoutDiscardingThem) {
    RCKSprite *sprite = Sprite(); SpriteSaveProperties properties; sprite->SetSaveFormat(&properties);
    CKBitmapProperties *current = sprite->GetSaveFormat(); ASSERT_NE(nullptr, current);
    sprite->SetSaveFormat(current);
    SpriteSaveProperties *saved = static_cast<SpriteSaveProperties *>(sprite->GetSaveFormat());
    ASSERT_NE(nullptr, saved); EXPECT_EQ(sizeof(properties), saved->m_Size);
    EXPECT_EQ(73u, saved->Quality); EXPECT_EQ(0x10203040u, saved->Marker);
    EXPECT_EQ(CKGUID(0x12345678, 0x89ABCDEF), saved->m_ReaderGuid);
}

TEST_F(SpriteBitmapTest, MovieMetadataReaderIdentityAndFrameCountComeFromActualMovieInfo) {
    RCKSprite *sprite = Sprite(); WriteMovie(); ASSERT_TRUE(sprite->LoadMovie(MoviePath));
    CKMovieReader *provider = sprite->GetMovieReader(); ASSERT_NE(nullptr, provider);
    EXPECT_EQ(&MovieProviderInfo, provider->GetReaderInfo());
    EXPECT_STREQ(MoviePath, sprite->GetMovieFileName());
    EXPECT_EQ(3, sprite->GetSlotCount()); EXPECT_EQ(0, sprite->GetCurrentSlot());
    EXPECT_FALSE(sprite->SetSlotCount(2)); EXPECT_FALSE(sprite->ReleaseSlot(0)); EXPECT_FALSE(sprite->ReleaseAllSlots());
    EXPECT_EQ(3, sprite->GetSlotCount()); EXPECT_EQ(provider, sprite->GetMovieReader());
}

TEST_F(SpriteBitmapTest, MovieCurrentSlotDecodesIntoProductionStorageAndCachesSameFrame) {
    RCKSprite *sprite = Sprite(); WriteMovie(); ASSERT_TRUE(sprite->LoadMovie(MoviePath));
    SpriteMovieProvider *provider = static_cast<SpriteMovieProvider *>(sprite->GetMovieReader()); ASSERT_NE(nullptr, provider);
    provider->Requests = 0; provider->LastFrame = -1;
    ASSERT_TRUE(sprite->SetCurrentSlot(1)); EXPECT_EQ(1, sprite->GetCurrentSlot());
    EXPECT_EQ(1, provider->Requests); EXPECT_EQ(1, provider->LastFrame);
    EXPECT_EQ(0xFF405060u, sprite->GetPixel(0, 0, 0)); EXPECT_EQ(0xFF405060u, sprite->GetPixel(1, 1, 0));
    ASSERT_TRUE(sprite->SetCurrentSlot(1)); EXPECT_EQ(1, provider->Requests);
    EXPECT_FALSE(sprite->SetCurrentSlot(-1)); EXPECT_FALSE(sprite->SetCurrentSlot(3));
    EXPECT_EQ(1, sprite->GetCurrentSlot()); EXPECT_EQ(1, provider->Requests);
    ASSERT_TRUE(sprite->SetCurrentSlot(2)); EXPECT_EQ(2, sprite->GetCurrentSlot());
    EXPECT_EQ(0xFF708090u, sprite->GetPixel(1, 1, 0));
}
} // namespace
