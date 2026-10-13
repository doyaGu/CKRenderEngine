#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKBitmapReader.h"
#include "RCKTexture.h"
#include "RCKRenderManager.h"
#include "TextureTestObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class SerializedTexture : public RCKTexture {
public:
    explicit SerializedTexture(CKContext *context) : RCKTexture(context) {}
    void MipCount(CKDWORD value) { m_MipMapLevel = value; }
    CKDWORD BitmapFlags() const { return m_BitmapFlags; }
};
class DerivedTexture : public SerializedTexture {
public:
    explicit DerivedTexture(CKContext *context) : SerializedTexture(context) {}
    CK_CLASSID GetClassID() override { return CKCID_SPRITE; }
};
class TextureSerializationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); Manager = new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Begin(CKStateChunk &chunk, int version = 10) { chunk.StartWrite(); chunk.SetDataVersion(version); }
    static CKERROR Load(RCKTexture &texture, CKStateChunk &chunk) { chunk.CloseChunk(); return texture.Load(&chunk, nullptr); }
    static void Image(RCKTexture &texture, int slots = 1, int width = 2, int height = 2) {
        ASSERT_TRUE(texture.SetSlotCount(slots));
        for (int i = 0; i < slots; ++i) ASSERT_TRUE(texture.CreateImage(width, height, 32, i));
    }
    static void Modern(CKStateChunk &chunk, CKDWORD flags, const CKDWORD *tail, int count) {
        // Independent native 2.1 wire: state word plus size-selected color/slot/format tail.
        chunk.WriteIdentifier(0x2ff000); chunk.WriteDword(flags);
        for (int i = 0; i < count; ++i) chunk.WriteDword(tail[i]);
    }
    static CKStateChunk *Save(SerializedTexture &texture, CKFile *file = nullptr, CKDWORD flags = 0x2ff000) {
        texture.SetSaveOptions(CKTEXTURE_EXTERNAL); return texture.Save(file, flags);
    }
    static void Raw(CKStateChunk &chunk, int width, int height, const CKDWORD *pixels) {
        // Literal raw bitmap planes in bottom-up wire order, independent of WriteRawBitmap.
        chunk.WriteInt(32); chunk.WriteInt(width); chunk.WriteInt(height);
        chunk.WriteDword(0xff000000); chunk.WriteDword(0x00ff0000); chunk.WriteDword(0x0000ff00); chunk.WriteDword(0x000000ff); chunk.WriteDword(0);
        CKBYTE b[16], g[16], r[16], a[16]; int n = 0;
        for (int y = height - 1; y >= 0; --y) for (int x = 0; x < width; ++x) {
            CKDWORD p = pixels[y * width + x]; b[n] = static_cast<CKBYTE>(p); g[n] = static_cast<CKBYTE>(p >> 8);
            r[n] = static_cast<CKBYTE>(p >> 16); a[n] = static_cast<CKBYTE>(p >> 24); ++n;
        }
        chunk.WriteBuffer_LEndian(n, b); chunk.WriteBuffer_LEndian(n, g); chunk.WriteBuffer_LEndian(n, r); chunk.WriteBuffer_LEndian(n, a);
    }
};
class TextureLoadVideoTest : public TextureTestWorld {
protected:
    static void LiteralMipPixel(CKStateChunk &chunk, CKDWORD pixel) {
        chunk.WriteInt(32); chunk.WriteInt(1); chunk.WriteInt(1);
        chunk.WriteDword(0xff000000); chunk.WriteDword(0xff0000); chunk.WriteDword(0xff00); chunk.WriteDword(0xff); chunk.WriteDword(0);
        for (int channel = 0; channel < 4; ++channel) { CKBYTE byte = static_cast<CKBYTE>(pixel >> (channel * 8)); chunk.WriteBuffer_LEndian(1, &byte); }
    }
};
}

TEST_F(TextureSerializationTest, NullLoadPreservesOwnStateAndBitmapPixels) {
    SerializedTexture texture(Context); Image(texture); texture.SetPixel(1, 0, 0x7f123456); texture.MipCount(3);
    texture.SetPickThreshold(29); texture.SetTransparentColor(0x11223344); texture.SetTransparent(TRUE);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, texture.Load(nullptr, nullptr)); EXPECT_EQ(0x7f123456u, texture.GetPixel(1, 0));
    EXPECT_EQ(3, texture.GetMipmapCount()); EXPECT_EQ(29, texture.GetPickThreshold()); EXPECT_EQ(0x11223344u, texture.GetTransparentColor()); EXPECT_TRUE(texture.IsTransparent());
}
TEST_F(TextureSerializationTest, LiteralResizedBitmapRebuildsUserMipDimensionsAndLoadsIndependentPixels) {
    SerializedTexture texture(Context); Image(texture, 1, 4, 4);
    ASSERT_TRUE(texture.SetUserMipMapMode(TRUE)); VxImageDescEx previous;
    ASSERT_TRUE(texture.GetUserMipMapLevel(1, previous));
    const CKDWORD pixels[] = {0xffff0000u, 0xff00ff00u, 0xff0000ffu, 0xffffffffu};
    const CKDWORD mip[] = {0x7f123456u};
    CKStateChunk chunk(CKCID_TEXTURE, nullptr); Begin(chunk);
    chunk.WriteIdentifier(0x20000); chunk.WriteInt(1); Raw(chunk, 2, 2, pixels);
    chunk.WriteIdentifier(0x400000); chunk.WriteInt(1); Raw(chunk, 1, 1, mip);
    ASSERT_EQ(CK_OK, Load(texture, chunk)); EXPECT_EQ(2, texture.GetWidth()); EXPECT_EQ(2, texture.GetHeight());
    for (int i = 0; i < 4; ++i) EXPECT_EQ(pixels[i], texture.GetPixel(i % 2, i / 2, 0));
    VxImageDescEx current; ASSERT_TRUE(texture.GetUserMipMapLevel(0, current));
    EXPECT_EQ(1, current.Width); EXPECT_EQ(1, current.Height); ASSERT_NE(nullptr, current.Image);
    EXPECT_EQ(mip[0], reinterpret_cast<CKDWORD *>(current.Image)[0]);
    EXPECT_FALSE(texture.GetUserMipMapLevel(1, current));
}
TEST_F(TextureSerializationTest, FilelessUnrelatedMasksReturnAncestorWithoutTextureSections) {
    SerializedTexture texture(Context); Image(texture);
    CKDWORD masks[] = {0, 1, 0x400000, 0x800000};
    for (CKDWORD mask : masks) { CKStateChunk *chunk = Save(texture, nullptr, mask); ASSERT_NE(nullptr, chunk); chunk->StartRead();
        EXPECT_FALSE(chunk->SeekIdentifier(0x2ff000)); EXPECT_FALSE(chunk->SeekIdentifier(0x400000)); DeleteCKStateChunk(chunk); }
}
TEST_F(TextureSerializationTest, EveryOldTextureMaskBitTriggersTheCompleteOwnPayload) {
    SerializedTexture texture(Context); Image(texture); texture.MipCount(3); texture.SetDesiredVideoFormat(_32_ARGB8888); texture.SetTransparentColor(0x88776655);
    for (int bit = 12; bit <= 21; ++bit) { CKDWORD mask = 1u << bit; if (!(mask & 0x2ff000)) continue;
        CKStateChunk *chunk = Save(texture, nullptr, mask); ASSERT_NE(nullptr, chunk); chunk->StartRead(); ASSERT_TRUE(chunk->SeekIdentifier(0x2ff000));
        EXPECT_EQ(0x00010203u, chunk->ReadDword()); EXPECT_EQ(0x88776655u, chunk->ReadDword()); EXPECT_EQ(static_cast<CKDWORD>(_32_ARGB8888), chunk->ReadDword()); DeleteCKStateChunk(chunk); }
}
TEST_F(TextureSerializationTest, FileZeroMaskStillWritesOwnPayload) {
    SerializedTexture texture(Context); Image(texture); CKFile file(Context); CKStateChunk *chunk = Save(texture, &file, 0); ASSERT_NE(nullptr, chunk);
    chunk->StartRead(); EXPECT_TRUE(chunk->SeekIdentifier(0x2ff000)); DeleteCKStateChunk(chunk);
}
TEST_F(TextureSerializationTest, SaveSingleSlotWithoutFormatHasEightByteStateSection) {
    SerializedTexture texture(Context); Image(texture); texture.SetDesiredVideoFormat(UNKNOWN_PF); texture.MipCount(7); texture.SetTransparentColor(0xa1b2c3d4);
    CKStateChunk *chunk = Save(texture); ASSERT_NE(nullptr, chunk); chunk->StartRead(); EXPECT_EQ(8, chunk->SeekIdentifierAndReturnSize(0x2ff000));
    EXPECT_EQ(0x00010007u, chunk->ReadDword()); EXPECT_EQ(0xa1b2c3d4u, chunk->ReadDword()); DeleteCKStateChunk(chunk);
}
TEST_F(TextureSerializationTest, SaveMultipleSlotsWithFormatHasOrderedSixteenByteStateSection) {
    SerializedTexture texture(Context); Image(texture, 3); ASSERT_TRUE(texture.SetCurrentSlot(2)); texture.SetTransparent(TRUE); texture.MipCount(static_cast<CKDWORD>(-1));
    texture.SetDesiredVideoFormat(_32_ARGB8888); texture.SetTransparentColor(0x1234);
    CKStateChunk *chunk = Save(texture); ASSERT_NE(nullptr, chunk); chunk->StartRead(); EXPECT_EQ(16, chunk->SeekIdentifierAndReturnSize(0x2ff000));
    EXPECT_EQ(0x000103ffu, chunk->ReadDword()); EXPECT_EQ(0x1234u, chunk->ReadDword()); EXPECT_EQ(2, chunk->ReadInt());
    EXPECT_EQ(static_cast<CKDWORD>(_32_ARGB8888), chunk->ReadDword()); DeleteCKStateChunk(chunk);
}
TEST_F(TextureSerializationTest, SavePickThresholdOmitsZeroAndWritesNonzeroSignedValue) {
    SerializedTexture texture(Context); Image(texture); CKStateChunk *chunk = Save(texture); ASSERT_NE(nullptr, chunk); chunk->StartRead(); EXPECT_FALSE(chunk->SeekIdentifier(0x200000)); DeleteCKStateChunk(chunk);
    texture.SetPickThreshold(-7); chunk = Save(texture); ASSERT_NE(nullptr, chunk); chunk->StartRead(); ASSERT_TRUE(chunk->SeekIdentifier(0x200000)); EXPECT_EQ(-7, chunk->ReadInt()); DeleteCKStateChunk(chunk);
}
TEST_F(TextureSerializationTest, SaveCubeFlagDoesNotConfuseTransparencyFormatOrSlotFields) {
    SerializedTexture texture(Context); Image(texture, 6); texture.SetCubeMap(TRUE); texture.SetDesiredVideoFormat(UNKNOWN_PF); texture.SetCurrentSlot(4); texture.SetTransparentColor(0x12345678);
    CKStateChunk *chunk = Save(texture); ASSERT_NE(nullptr, chunk); chunk->StartRead(); EXPECT_EQ(12, chunk->SeekIdentifierAndReturnSize(0x2ff000));
    EXPECT_EQ(0x00010400u, chunk->ReadDword()); EXPECT_EQ(0x12345678u, chunk->ReadDword()); EXPECT_EQ(4, chunk->ReadInt()); DeleteCKStateChunk(chunk);
}
TEST_F(TextureSerializationTest, SaveUserMipPlanesContainIndependentDimensionsAndPixels) {
    SerializedTexture texture(Context); Image(texture, 1, 4, 2); ASSERT_TRUE(texture.SetUserMipMapMode(TRUE)); VxImageDescEx desc;
    ASSERT_TRUE(texture.GetUserMipMapLevel(0, desc)); reinterpret_cast<CKDWORD *>(desc.Image)[0] = 0xff102030; reinterpret_cast<CKDWORD *>(desc.Image)[1] = 0x80405060;
    ASSERT_TRUE(texture.GetUserMipMapLevel(1, desc)); reinterpret_cast<CKDWORD *>(desc.Image)[0] = 0x7f112233;
    CKStateChunk *chunk = Save(texture); ASSERT_NE(nullptr, chunk); chunk->StartRead(); ASSERT_TRUE(chunk->SeekIdentifier(0x400000)); EXPECT_EQ(2, chunk->ReadInt());
    CKBYTE expected[2][4] = {{0x30, 0x60, 0, 0}, {0x33, 0, 0, 0}};
    for (int level = 0; level < 2; ++level) {
        EXPECT_EQ(32, chunk->ReadInt()); EXPECT_EQ(level ? 1 : 2, chunk->ReadInt()); EXPECT_EQ(1, chunk->ReadInt());
        EXPECT_EQ(0xff000000u, chunk->ReadDword()); EXPECT_EQ(0xff0000u, chunk->ReadDword()); EXPECT_EQ(0xff00u, chunk->ReadDword()); EXPECT_EQ(0xffu, chunk->ReadDword()); EXPECT_EQ(0u, chunk->ReadDword());
        void *plane = nullptr; EXPECT_EQ(level ? 1 : 2, chunk->ReadBuffer(&plane)); ASSERT_NE(nullptr, plane);
        EXPECT_EQ(expected[level][0], static_cast<CKBYTE *>(plane)[0]); if (!level) EXPECT_EQ(expected[level][1], static_cast<CKBYTE *>(plane)[1]); CKDeletePointer(plane);
        for (int p = 0; p < 3; ++p) { plane = nullptr; chunk->ReadBuffer(&plane); CKDeletePointer(plane); }
    }
    DeleteCKStateChunk(chunk);
}
TEST_F(TextureSerializationTest, LiteralModernSixteenBytePayloadDecodesAllOwnFields) {
    SerializedTexture texture(Context); Image(texture, 3); CKStateChunk chunk(CKCID_TEXTURE, nullptr); Begin(chunk);
    CKDWORD tail[] = {0x7f345678, 2, _32_ARGB8888}; Modern(chunk, 0x000103ff, tail, 3); chunk.WriteIdentifier(0x200000); chunk.WriteInt(31);
    ASSERT_EQ(CK_OK, Load(texture, chunk)); EXPECT_EQ(-1, texture.GetMipmapCount()); EXPECT_EQ(CKTEXTURE_EXTERNAL, texture.GetSaveOptions());
    EXPECT_TRUE(texture.IsTransparent()); EXPECT_FALSE(texture.IsCubeMap()); EXPECT_EQ(0x7f345678u, texture.GetTransparentColor()); EXPECT_EQ(2, texture.GetCurrentSlot());
    EXPECT_EQ(_32_ARGB8888, texture.GetDesiredVideoFormat()); EXPECT_EQ(31, texture.GetPickThreshold());
}
TEST_F(TextureSerializationTest, VersionsFiveSixTenAndTwelveDecodeTheSamePackedFields) {
    for (int version : {5, 6, 10, 12}) { SerializedTexture texture(Context); Image(texture); CKStateChunk chunk(CKCID_TEXTURE, nullptr); Begin(chunk, version);
        CKDWORD tail[] = {0x12345678, _32_ARGB8888}; Modern(chunk, 0x00020203, tail, 2); ASSERT_EQ(CK_OK, Load(texture, chunk));
        EXPECT_EQ(3, texture.GetMipmapCount()); EXPECT_EQ(CKTEXTURE_IMAGEFORMAT, texture.GetSaveOptions()); EXPECT_EQ(0x12345678u, texture.GetTransparentColor()); EXPECT_EQ(_32_ARGB8888, texture.GetDesiredVideoFormat()); }
}
TEST_F(TextureSerializationTest, EightByteTailWithoutFormatUsesColorThenCurrentSlot) {
    SerializedTexture texture(Context); Image(texture, 3); CKStateChunk chunk(CKCID_TEXTURE, nullptr); Begin(chunk); CKDWORD tail[] = {0x44556677, 2};
    Modern(chunk, 0x00010002, tail, 2); ASSERT_EQ(CK_OK, Load(texture, chunk)); EXPECT_EQ(0x44556677u, texture.GetTransparentColor()); EXPECT_EQ(2, texture.GetCurrentSlot());
}
TEST_F(TextureSerializationTest, EightByteTailWithMultipleSlotsAndFormatUsesSlotThenFormat) {
    SerializedTexture texture(Context); Image(texture, 3); texture.SetTransparentColor(0x11223344); CKStateChunk chunk(CKCID_TEXTURE, nullptr); Begin(chunk); CKDWORD tail[] = {2, _32_ARGB8888};
    Modern(chunk, 0x00010202, tail, 2); ASSERT_EQ(CK_OK, Load(texture, chunk)); EXPECT_EQ(0x11223344u, texture.GetTransparentColor()); EXPECT_EQ(2, texture.GetCurrentSlot()); EXPECT_EQ(_32_ARGB8888, texture.GetDesiredVideoFormat());
}
TEST_F(TextureSerializationTest, FourByteTailSelectsColorForSingleSlotOrSlotForMultipleSlots) {
    for (int slots : {1, 3}) { SerializedTexture texture(Context); Image(texture, slots); CKStateChunk chunk(CKCID_TEXTURE, nullptr); Begin(chunk); CKDWORD tail[] = {slots == 1 ? 0x12345678u : 2u};
        Modern(chunk, 0x00010001, tail, 1); ASSERT_EQ(CK_OK, Load(texture, chunk)); if (slots == 1) EXPECT_EQ(0x12345678u, texture.GetTransparentColor()); else EXPECT_EQ(2, texture.GetCurrentSlot()); }
}
TEST_F(TextureSerializationTest, FourByteTailWithFormatLeavesColorAndUsesSlotZero) {
    SerializedTexture texture(Context); Image(texture, 3); texture.SetCurrentSlot(2); texture.SetTransparentColor(0x12345678); CKStateChunk chunk(CKCID_TEXTURE, nullptr); Begin(chunk); CKDWORD tail[] = {_32_ARGB8888};
    Modern(chunk, 0x00010201, tail, 1); ASSERT_EQ(CK_OK, Load(texture, chunk)); EXPECT_EQ(0x12345678u, texture.GetTransparentColor()); EXPECT_EQ(0, texture.GetCurrentSlot()); EXPECT_EQ(_32_ARGB8888, texture.GetDesiredVideoFormat());
}
TEST_F(TextureSerializationTest, HeaderOnlyStateChangesFlagsAndDefaultsTheCurrentSlot) {
    SerializedTexture texture(Context); Image(texture, 3); texture.SetCurrentSlot(2); texture.SetTransparentColor(0x1234); CKStateChunk chunk(CKCID_TEXTURE, nullptr); Begin(chunk);
    Modern(chunk, 0x00010104, nullptr, 0); ASSERT_EQ(CK_OK, Load(texture, chunk)); EXPECT_EQ(4, texture.GetMipmapCount()); EXPECT_TRUE(texture.IsTransparent()); EXPECT_EQ(0, texture.GetCurrentSlot()); EXPECT_EQ(0x1234u, texture.GetTransparentColor());
}
TEST_F(TextureSerializationTest, LiteralCubeFlagIsIndependentOfTransparencyAndCurrentFace) {
    SerializedTexture texture(Context); Image(texture, 6); texture.SetTransparent(TRUE); CKStateChunk chunk(CKCID_TEXTURE, nullptr); Begin(chunk); CKDWORD tail[] = {0x12345678, 4};
    Modern(chunk, 0x00010402, tail, 2); ASSERT_EQ(CK_OK, Load(texture, chunk)); EXPECT_TRUE(texture.IsCubeMap()); EXPECT_FALSE(texture.IsTransparent());
    EXPECT_EQ(4, texture.GetCurrentSlot()); EXPECT_EQ(2, texture.GetMipmapCount()); EXPECT_EQ(0x12345678u, texture.GetTransparentColor()); EXPECT_EQ(6, texture.GetSlotCount());
}
TEST_F(TextureSerializationTest, MissingOwnSectionsRetainCurrentSlotPickThresholdAndMipmapCount) {
    SerializedTexture texture(Context); Image(texture, 3); texture.SetCurrentSlot(2); texture.SetPickThreshold(23); texture.MipCount(5);
    CKStateChunk chunk(CKCID_TEXTURE, nullptr); Begin(chunk); chunk.WriteIdentifier(0x70000000); chunk.WriteInt(1); ASSERT_EQ(CK_OK, Load(texture, chunk));
    EXPECT_EQ(2, texture.GetCurrentSlot()); EXPECT_EQ(23, texture.GetPickThreshold()); EXPECT_EQ(5, texture.GetMipmapCount());
}
TEST_F(TextureSerializationTest, LegacySeparateColorTransparencySlotAndMipmapSectionsDecodeInOrder) {
    SerializedTexture texture(Context); Image(texture, 3); CKStateChunk chunk(CKCID_TEXTURE, nullptr); Begin(chunk, 4);
    chunk.WriteIdentifier(0x8000); chunk.WriteDword(0x7f102030); chunk.WriteDword(1); chunk.WriteIdentifier(0x2000); chunk.WriteInt(2);
    chunk.WriteIdentifier(0x40000); chunk.WriteInt(-1); ASSERT_EQ(CK_OK, Load(texture, chunk));
    EXPECT_TRUE(texture.IsTransparent()); EXPECT_EQ(0x7f102030u, texture.GetTransparentColor()); EXPECT_EQ(2, texture.GetCurrentSlot()); EXPECT_EQ(-1, texture.GetMipmapCount());
}
TEST_F(TextureSerializationTest, LegacySaveFormatSectionStartsWithSaveOptionsAndIgnoresObsoleteProperties) {
    SerializedTexture texture(Context); Image(texture); CKStateChunk chunk(CKCID_TEXTURE, nullptr); Begin(chunk, 4); CKBitmapProperties properties;
    chunk.WriteIdentifier(0x80000); chunk.WriteDword(CKTEXTURE_EXTERNAL); chunk.WriteBuffer(sizeof(properties), &properties); ASSERT_EQ(CK_OK, Load(texture, chunk));
    EXPECT_EQ(CKTEXTURE_EXTERNAL, texture.GetSaveOptions()); EXPECT_EQ(nullptr, texture.GetSaveFormat());
}
TEST_F(TextureSerializationTest, UserMipLiteralRawPlanesRetainTopDownPixelOrder) {
    SerializedTexture texture(Context); Image(texture, 1, 4, 4); CKStateChunk chunk(CKCID_TEXTURE, nullptr); Begin(chunk);
    chunk.WriteIdentifier(0x400000); chunk.WriteInt(2); const CKDWORD first[] = {0xff102030, 0x80405060, 0x7f112233, 0xffabcdef}; const CKDWORD second[] = {0x45123456};
    Raw(chunk, 2, 2, first); Raw(chunk, 1, 1, second); ASSERT_EQ(CK_OK, Load(texture, chunk)); VxImageDescEx desc;
    ASSERT_TRUE(texture.GetUserMipMapLevel(0, desc)); EXPECT_EQ(2, desc.Width); EXPECT_EQ(2, desc.Height); EXPECT_EQ(8, desc.BytesPerLine);
    for (int i = 0; i < 4; ++i) EXPECT_EQ(first[i], reinterpret_cast<CKDWORD *>(desc.Image)[i]);
    ASSERT_TRUE(texture.GetUserMipMapLevel(1, desc)); EXPECT_EQ(second[0], reinterpret_cast<CKDWORD *>(desc.Image)[0]);
}
TEST_F(TextureSerializationTest, UserMipCountMismatchKeepsAllocatedLevelsAndDoesNotConsumeOtherSections) {
    SerializedTexture texture(Context); Image(texture, 1, 4, 4); CKStateChunk chunk(CKCID_TEXTURE, nullptr); Begin(chunk); chunk.WriteIdentifier(0x400000); chunk.WriteInt(7);
    chunk.WriteIdentifier(0x200000); chunk.WriteInt(41); ASSERT_EQ(CK_OK, Load(texture, chunk)); EXPECT_EQ(41, texture.GetPickThreshold()); VxImageDescEx desc;
    ASSERT_TRUE(texture.GetUserMipMapLevel(0, desc)); EXPECT_EQ(0u, reinterpret_cast<CKDWORD *>(desc.Image)[0]);
}
TEST_F(TextureSerializationTest, RepeatedUserMipLoadsReplacePixelsWithoutSharingTheWireStorage) {
    SerializedTexture texture(Context); Image(texture, 1, 2, 2);
    for (CKDWORD color : {0xff112233u, 0x7fabcdefu}) { CKStateChunk chunk(CKCID_TEXTURE, nullptr); Begin(chunk); chunk.WriteIdentifier(0x400000); chunk.WriteInt(1); Raw(chunk, 1, 1, &color);
        ASSERT_EQ(CK_OK, Load(texture, chunk)); VxImageDescEx desc; ASSERT_TRUE(texture.GetUserMipMapLevel(0, desc)); EXPECT_EQ(color, reinterpret_cast<CKDWORD *>(desc.Image)[0]); }
}
TEST_F(TextureSerializationTest, SavePropertiesBufferIncludesTheOwnedReaderPayload) {
    struct Properties : CKBitmapProperties { CKDWORD Marker; } properties; properties.m_Size = sizeof(properties); properties.Marker = 0x1234abcd;
    properties.m_ReaderGuid = CKGUID(0x10203040, 0x50607080); SerializedTexture texture(Context); Image(texture); texture.SetSaveFormat(&properties);
    CKStateChunk *chunk = Save(texture); ASSERT_NE(nullptr, chunk); chunk->StartRead(); ASSERT_TRUE(chunk->SeekIdentifier(0x80000)); void *data = nullptr;
    EXPECT_EQ(sizeof(properties), static_cast<size_t>(chunk->ReadBuffer(&data))); ASSERT_NE(nullptr, data); EXPECT_EQ(0x1234abcdu, static_cast<Properties *>(data)->Marker);
    EXPECT_EQ(properties.m_ReaderGuid, static_cast<Properties *>(data)->m_ReaderGuid); CKDeletePointer(data); DeleteCKStateChunk(chunk);
}
TEST_F(TextureSerializationTest, ModernPropertiesAreInstalledOnlyAfterVersionSix) {
    struct Properties : CKBitmapProperties { CKDWORD Marker; } properties; properties.m_Size = sizeof(properties); properties.Marker = 0x1234abcd;
    for (int version : {5, 6, 7, 10, 12}) { SerializedTexture texture(Context); Image(texture); CKStateChunk chunk(CKCID_TEXTURE, nullptr); Begin(chunk, version);
        chunk.WriteIdentifier(0x80000); chunk.WriteBuffer(sizeof(properties), &properties); ASSERT_EQ(CK_OK, Load(texture, chunk));
        if (version <= 6) EXPECT_EQ(nullptr, texture.GetSaveFormat()); else { ASSERT_NE(nullptr, texture.GetSaveFormat()); EXPECT_EQ(0x1234abcdu, static_cast<Properties *>(texture.GetSaveFormat())->Marker); } }
}
TEST_F(TextureSerializationTest, UnsupportedVideoFormatUsesManagerFallbackInLegacyAndModernLoads) {
    Manager->m_TextureVideoFormat.Value = _16_RGB565;
    for (int version : {4, 5, 10, 12}) { SerializedTexture texture(Context); Image(texture); texture.SetDesiredVideoFormat(static_cast<VX_PIXELFORMAT>(999));
        CKStateChunk chunk(CKCID_TEXTURE, nullptr); Begin(chunk, version); chunk.WriteIdentifier(0x70000000); chunk.WriteInt(1);
        ASSERT_EQ(CK_OK, Load(texture, chunk)); EXPECT_EQ(_16_RGB565, texture.GetDesiredVideoFormat()); }
}
TEST_F(TextureSerializationTest, LiteralBaseBitmapPlanesDecodeBeforeOwnSlotAndTransparencySettings) {
    SerializedTexture texture(Context); CKStateChunk chunk(CKCID_TEXTURE, nullptr); Begin(chunk); chunk.WriteIdentifier(0x20000); chunk.WriteInt(2);
    const CKDWORD first[] = {0xff102030, 0x80405060, 0x7f112233, 0xffabcdef}; const CKDWORD second[] = {0xff000001, 0xff000002, 0xff000003, 0xff000004};
    Raw(chunk, 2, 2, first); Raw(chunk, 2, 2, second); CKDWORD tail[] = {0xabcdef, 1}; Modern(chunk, 0x00010103, tail, 2);
    ASSERT_EQ(CK_OK, Load(texture, chunk)); EXPECT_EQ(2, texture.GetWidth()); EXPECT_EQ(2, texture.GetHeight()); EXPECT_EQ(2, texture.GetSlotCount()); EXPECT_EQ(1, texture.GetCurrentSlot());
    EXPECT_TRUE(texture.IsTransparent()); EXPECT_EQ(0xabcdefu, texture.GetTransparentColor()); EXPECT_EQ(0xffabcdefu, texture.GetPixel(1, 1, 0)); EXPECT_EQ(0xff000001u, texture.GetPixel(0, 0, 1));
}
TEST_F(TextureSerializationTest, BaseTextureSaveClosesTheChunkAndDerivedSaveCanAppendItsOwnSection) {
    SerializedTexture texture(Context); Image(texture); CKStateChunk *chunk = Save(texture); ASSERT_NE(nullptr, chunk); EXPECT_EQ(CKCID_TEXTURE, chunk->GetChunkClassID()); EXPECT_EQ(-1, chunk->GetCurrentPos()); DeleteCKStateChunk(chunk);
    DerivedTexture derived(Context); Image(derived); chunk = Save(derived); ASSERT_NE(nullptr, chunk); EXPECT_EQ(CKCID_TEXTURE, chunk->GetChunkClassID()); EXPECT_GE(chunk->GetCurrentPos(), 0);
    chunk->WriteIdentifier(0x76543210); chunk->WriteInt(17); chunk->CloseChunk(); chunk->StartRead(); ASSERT_TRUE(chunk->SeekIdentifier(0x76543210)); EXPECT_EQ(17, chunk->ReadInt()); DeleteCKStateChunk(chunk);
}
TEST_F(TextureLoadVideoTest, SameSizeFormatLoadDeletesTheOldResourceAndUploadsTheRequestedPixelEncoding) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff}; Pixels(texture, pixels, 2, 2);
    texture.SetDesiredVideoFormat(_32_ARGB8888); ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE)); ASSERT_TRUE(texture.IsInVideoMemory());
    const CKDWORD oldHandle = texture.GetRstTextureIndex(); ASSERT_TRUE(State.Live[oldHandle]); ASSERT_EQ(1, State.Creates); ASSERT_EQ(1, State.Loads);
    CKStateChunk chunk(CKCID_TEXTURE, nullptr); chunk.StartWrite(); chunk.SetDataVersion(10);
    // Independent native packed single-slot payload; no bitmap replacement or size change.
    chunk.WriteIdentifier(0x2ff000); chunk.WriteDword(0x200); chunk.WriteDword(0); chunk.WriteDword(_16_RGB565); chunk.CloseChunk();
    ASSERT_EQ(CK_OK, texture.Load(&chunk, nullptr)); EXPECT_EQ(_16_RGB565, texture.GetDesiredVideoFormat()); EXPECT_FALSE(State.Live[oldHandle]); EXPECT_EQ(1, State.Deletes);
    EXPECT_FALSE(texture.IsInVideoMemory()); State.Loads = 0; ASSERT_TRUE(texture.SetAsCurrent(Device, FALSE, 2));
    EXPECT_EQ(2, State.Creates); EXPECT_EQ(16, State.Created[1].Format.BitsPerPixel); EXPECT_EQ(0xf800u, State.Created[1].Format.RedMask);
    EXPECT_EQ(0x7e0u, State.Created[1].Format.GreenMask); EXPECT_EQ(0x1fu, State.Created[1].Format.BlueMask); ASSERT_EQ(1, State.Loads);
    EXPECT_EQ(16, State.Uploads[0].Image.BitsPerPixel); EXPECT_EQ(2, State.Uploads[0].Image.Width); EXPECT_EQ(2, State.Uploads[0].Image.Height);
    const CKWORD expected[] = {0xf800, 0x07e0, 0x001f, 0xffff};
    for (int i = 0; i < 4; ++i) { CKWORD actual = 0; memcpy(&actual, State.Uploads[0].Bytes + i * 2, 2); EXPECT_EQ(expected[i], actual); }
    EXPECT_NE(oldHandle, texture.GetRstTextureIndex()); EXPECT_EQ(texture.GetRstTextureIndex(), State.Textures[2]); EXPECT_TRUE(State.Live[texture.GetRstTextureIndex()]);
    for (int i = 0; i < 4; ++i) EXPECT_EQ(pixels[i], texture.GetPixel(i % 2, i / 2));
}
TEST_F(TextureLoadVideoTest, ReplacingResidentUserMipPixelsMarksAllLevelsDirtyAndUploadsTheNewLiteralPixel) {
    TextureTestProbe texture(Context); const CKDWORD pixels[] = {0xff102030, 0xff405060, 0xff708090, 0xffabcdef}; Pixels(texture, pixels, 2, 2);
    ASSERT_TRUE(texture.SetUserMipMapMode(TRUE)); VxImageDescEx mip; ASSERT_TRUE(texture.GetUserMipMapLevel(0, mip)); reinterpret_cast<CKDWORD *>(mip.Image)[0] = 0xff00ff00;
    texture.SetDesiredVideoFormat(_32_ARGB8888); ASSERT_TRUE(texture.SystemToVideoMemory(Device, FALSE)); ASSERT_TRUE(texture.IsInVideoMemory());
    const CKDWORD handle = texture.GetRstTextureIndex(); ASSERT_TRUE(State.Live[handle]); ASSERT_EQ(1, State.Creates); ASSERT_EQ(2, State.Loads);
    EXPECT_EQ(0xff00ff00u, Pixel(State.Uploads[1], 0)); EXPECT_EQ(0u, texture.BitmapFlags() & CKBITMAPDATA_FORCERESTORE); State.Loads = 0;
    CKStateChunk chunk(CKCID_TEXTURE, nullptr); chunk.StartWrite(); chunk.SetDataVersion(10); chunk.WriteIdentifier(0x400000); chunk.WriteInt(1); LiteralMipPixel(chunk, 0xffff0000); chunk.CloseChunk();
    ASSERT_EQ(CK_OK, texture.Load(&chunk, nullptr)); ASSERT_TRUE(texture.GetUserMipMapLevel(0, mip)); EXPECT_EQ(0xffff0000u, reinterpret_cast<CKDWORD *>(mip.Image)[0]);
    EXPECT_NE(0u, texture.BitmapFlags() & CKBITMAPDATA_FORCERESTORE); EXPECT_EQ(handle, texture.GetRstTextureIndex()); EXPECT_TRUE(State.Live[handle]);
    ASSERT_TRUE(texture.SetAsCurrent(Device, FALSE, 1)); EXPECT_EQ(1, State.Creates); EXPECT_EQ(0, State.Deletes); ASSERT_EQ(2, State.Loads);
    EXPECT_EQ(0, State.Uploads[0].Level); EXPECT_EQ(1, State.Uploads[1].Level); EXPECT_EQ(handle, State.Uploads[0].Handle); EXPECT_EQ(handle, State.Uploads[1].Handle);
    EXPECT_EQ(1, State.Uploads[1].Image.Width); EXPECT_EQ(1, State.Uploads[1].Image.Height); EXPECT_EQ(0xffff0000u, Pixel(State.Uploads[1], 0));
    EXPECT_EQ(handle, State.Textures[1]); EXPECT_EQ(0u, texture.BitmapFlags() & CKBITMAPDATA_FORCERESTORE);
}
