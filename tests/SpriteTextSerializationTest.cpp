#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKSpriteText.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"
#include "RCKMaterial.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class SerializedTextSprite : public RCKSpriteText {
public:
    explicit SerializedTextSprite(CKContext *context) : RCKSpriteText(context) {}
    const char *FontName() const { return m_FontName; }
    int FontSize() const { return m_FontSize; }
    int FontWeight() const { return m_FontWeight; }
    CKBOOL Italic() const { return m_FontItalic; }
    CKBOOL Underline() const { return m_FontUnderline; }
    FONT_HANDLE Font() const { return m_Font; }
    void DropFont() { if (m_Font) VxDeleteFont(m_Font); m_Font = nullptr; }
};
class SpriteTextSerializationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKSprite, CKCID_2DENTITY);
        CKCLASSREGISTERCID(RCKSpriteText, CKCID_SPRITE);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void FontWire(CKStateChunk &chunk, const char *name, int height, int weight, CKBOOL italic, CKBOOL underline) {
        chunk.WriteIdentifier(0x02000000); chunk.WriteString(const_cast<char *>(name));
        chunk.WriteInt(height); chunk.WriteInt(weight); chunk.WriteInt(italic); chunk.WriteInt(underline);
    }
    static void TextWire(CKStateChunk &chunk, const char *text) {
        chunk.WriteIdentifier(0x01000000); chunk.WriteString(const_cast<char *>(text));
    }
    static void ColorWire(CKStateChunk &chunk, CKDWORD foreground, CKDWORD background) {
        chunk.WriteIdentifier(0x04000000); chunk.WriteDword(foreground); chunk.WriteDword(background);
    }
    static void BaseWire(CKStateChunk &chunk) {
        chunk.WriteIdentifier(CK_STATESAVE_2DENTITYONLY); chunk.WriteDword(CK_2DENTITY_RESERVED3 | CK_2DENTITY_RESERVED0);
        chunk.WriteFloat(7); chunk.WriteFloat(11); chunk.WriteFloat(47); chunk.WriteFloat(71);
        chunk.WriteFloat(.5f); chunk.WriteFloat(.75f); chunk.WriteFloat(1.5f); chunk.WriteFloat(1.75f);
    }
    static CKERROR Load(SerializedTextSprite &sprite, CKStateChunk &chunk, CKFile *file = nullptr) {
        chunk.CloseChunk(); return sprite.Load(&chunk, file);
    }
    static void FontState(const SerializedTextSprite &sprite, const char *name, int height, int weight, CKBOOL italic, CKBOOL underline) {
        if (name) { ASSERT_NE(nullptr, sprite.FontName()); EXPECT_STREQ(name, sprite.FontName()); }
        else EXPECT_EQ(nullptr, sprite.FontName());
        EXPECT_EQ(height, sprite.FontSize()); EXPECT_EQ(weight, sprite.FontWeight());
        EXPECT_EQ(italic, sprite.Italic()); EXPECT_EQ(underline, sprite.Underline());
    }
    static void ReadString(CKStateChunk &chunk, const char *expected) {
        char *text = nullptr; chunk.ReadString(&text);
        if (expected) { ASSERT_NE(nullptr, text); EXPECT_STREQ(expected, text); }
        else EXPECT_TRUE(!text || !*text);
        CKDeletePointer(text);
    }
    static void FontPayload(CKStateChunk &chunk, const char *name, int height, int weight, CKBOOL italic, CKBOOL underline) {
        ASSERT_TRUE(chunk.SeekIdentifier(0x02000000)); ReadString(chunk, name);
        EXPECT_EQ(height, chunk.ReadInt()); EXPECT_EQ(weight, chunk.ReadInt());
        EXPECT_EQ(italic, chunk.ReadInt()); EXPECT_EQ(underline, chunk.ReadInt());
    }
    static void NoSpritePayload(CKStateChunk &chunk) {
        const CKDWORD identifiers[] = {0x00800000, 0x08000000, CK_STATESAVE_SPRITETRANSPARENT,
            CK_STATESAVE_SPRITECURRENTIMAGE, CK_STATESAVE_SPRITESHARED, CK_STATESAVE_SPRITEFORMAT, CK_STATESAVE_SPRITEREADER};
        for (CKDWORD identifier : identifiers) EXPECT_FALSE(chunk.SeekIdentifier(identifier)) << std::hex << identifier;
    }
};
}

TEST_F(SpriteTextSerializationTest, NullLoadRejectsWithoutChangingExistingTextAndFont) {
    SerializedTextSprite sprite(Context); sprite.SetText(const_cast<char *>("existing"));
    sprite.SetFont(const_cast<char *>("Arial"), 19, 600, FALSE, TRUE); sprite.SetTextColor(0xff123456);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, sprite.Load(nullptr, nullptr)); EXPECT_STREQ("existing", sprite.GetText());
    FontState(sprite, "Arial", 19, 600, FALSE, TRUE); EXPECT_EQ(0xff123456u, sprite.GetTextColor());
}
TEST_F(SpriteTextSerializationTest, LiteralFilelessPayloadLoadsFontInWireOrderAndIndependentColors) {
    SerializedTextSprite sprite(Context); CKStateChunk chunk(CKCID_SPRITETEXT, nullptr); chunk.StartWrite(); chunk.SetDataVersion(7);
    BaseWire(chunk); TextWire(chunk, "literal text"); FontWire(chunk, "Arial", 19, 600, FALSE, TRUE); ColorWire(chunk, 0xff123456, 0xffabcdef);
    ASSERT_EQ(CK_OK, Load(sprite, chunk)); EXPECT_STREQ("literal text", sprite.GetText()); FontState(sprite, "Arial", 19, 600, FALSE, TRUE);
    EXPECT_EQ(0xff123456u, sprite.GetTextColor()); EXPECT_EQ(0xffabcdefu, sprite.GetBackgroundTextColor());
    VxRect rect; sprite.GetRect(rect); EXPECT_FLOAT_EQ(7, rect.left); EXPECT_FLOAT_EQ(11, rect.top); EXPECT_FLOAT_EQ(47, rect.right); EXPECT_FLOAT_EQ(71, rect.bottom);
    sprite.GetSourceRect(rect); EXPECT_FLOAT_EQ(.5f, rect.left); EXPECT_FLOAT_EQ(.75f, rect.top); EXPECT_FLOAT_EQ(1.5f, rect.right); EXPECT_FLOAT_EQ(1.75f, rect.bottom);
    ASSERT_NE(nullptr, sprite.Font()); VXFONTINFO info; ASSERT_TRUE(VxGetFontInfo(sprite.Font(), info));
    EXPECT_EQ(600, info.Weight); EXPECT_FALSE(info.Italic); EXPECT_TRUE(info.Underline);
}
TEST_F(SpriteTextSerializationTest, LiteralFilePayloadLoadsTextAndColorsAlongsideAncestorRawBitmap) {
    SerializedTextSprite sprite(Context); CKFile file(Context); CKStateChunk chunk(CKCID_SPRITETEXT, nullptr); chunk.StartWrite(); chunk.SetDataVersion(7);
    BaseWire(chunk); chunk.WriteIdentifier(0x00800000); chunk.WriteInt(1); chunk.WriteInt(32); chunk.WriteInt(2); chunk.WriteInt(2);
    chunk.WriteDword(A_MASK); chunk.WriteDword(R_MASK); chunk.WriteDword(G_MASK); chunk.WriteDword(B_MASK); chunk.WriteInt(0);
    CKBYTE blue[] = {3, 4, 1, 2}, green[] = {7, 8, 5, 6}, red[] = {11, 12, 9, 10}, alpha[] = {255, 255, 255, 255};
    chunk.WriteBuffer(4, blue); chunk.WriteBuffer(4, green); chunk.WriteBuffer(4, red); chunk.WriteBuffer(4, alpha);
    TextWire(chunk, nullptr); FontWire(chunk, "Arial", 17, 500, TRUE, FALSE); ColorWire(chunk, 0xff102030, 0xff405060);
    ASSERT_EQ(CK_OK, Load(sprite, chunk, &file)); EXPECT_EQ(2, sprite.GetWidth()); EXPECT_EQ(2, sprite.GetHeight());
    // Final Redraw fills the actual loaded bitmap with the configured transparent color when text is absent.
    EXPECT_EQ(sprite.GetTransparentColor(), sprite.GetPixel(0, 0)); EXPECT_EQ(sprite.GetTransparentColor(), sprite.GetPixel(1, 1));
    EXPECT_EQ(nullptr, sprite.GetText()); FontState(sprite, "Arial", 17, 500, TRUE, FALSE);
    EXPECT_EQ(0xff102030u, sprite.GetTextColor()); EXPECT_EQ(0xff405060u, sprite.GetBackgroundTextColor());
}
TEST_F(SpriteTextSerializationTest, FontOnlyLiteralPayloadPreservesExistingTextAndColors) {
    SerializedTextSprite sprite(Context); sprite.SetText(const_cast<char *>("keep text")); sprite.SetTextColor(0xff010203); sprite.SetBackgroundColor(0xff040506);
    CKStateChunk chunk(CKCID_SPRITETEXT, nullptr); chunk.StartWrite(); FontWire(chunk, "Arial", 23, 700, TRUE, FALSE);
    ASSERT_EQ(CK_OK, Load(sprite, chunk)); EXPECT_STREQ("keep text", sprite.GetText()); FontState(sprite, "Arial", 23, 700, TRUE, FALSE);
    EXPECT_EQ(0xff010203u, sprite.GetTextColor()); EXPECT_EQ(0xff040506u, sprite.GetBackgroundTextColor());
}
TEST_F(SpriteTextSerializationTest, TextOnlyLiteralPayloadPreservesFontAndColors) {
    SerializedTextSprite sprite(Context); sprite.SetFont(const_cast<char *>("Arial"), 21, 500, TRUE, TRUE); sprite.SetTextColor(0xff112233);
    CKStateChunk chunk(CKCID_SPRITETEXT, nullptr); chunk.StartWrite(); TextWire(chunk, "replacement");
    ASSERT_EQ(CK_OK, Load(sprite, chunk)); EXPECT_STREQ("replacement", sprite.GetText()); FontState(sprite, "Arial", 21, 500, TRUE, TRUE); EXPECT_EQ(0xff112233u, sprite.GetTextColor());
}
TEST_F(SpriteTextSerializationTest, ColorOnlyLiteralPayloadPreservesTextAndFont) {
    SerializedTextSprite sprite(Context); sprite.SetText(const_cast<char *>("keep")); sprite.SetFont(const_cast<char *>("Arial"), 18, 400, FALSE, FALSE);
    CKStateChunk chunk(CKCID_SPRITETEXT, nullptr); chunk.StartWrite(); ColorWire(chunk, 0xff223344, 0xff556677);
    ASSERT_EQ(CK_OK, Load(sprite, chunk)); EXPECT_STREQ("keep", sprite.GetText()); FontState(sprite, "Arial", 18, 400, FALSE, FALSE);
    EXPECT_EQ(0xff223344u, sprite.GetTextColor()); EXPECT_EQ(0xff556677u, sprite.GetBackgroundTextColor());
}
TEST_F(SpriteTextSerializationTest, MissingTextIdentifiersPreserveRequestedState) {
    SerializedTextSprite sprite(Context); sprite.SetText(const_cast<char *>("keep")); sprite.SetFont(const_cast<char *>("Arial"), 15, 550, TRUE, FALSE);
    sprite.SetTextColor(0xff123456); CKStateChunk chunk(CKCID_SPRITETEXT, nullptr); chunk.StartWrite(); chunk.WriteIdentifier(0x76543210); chunk.WriteInt(73);
    ASSERT_EQ(CK_OK, Load(sprite, chunk)); EXPECT_STREQ("keep", sprite.GetText()); FontState(sprite, "Arial", 15, 550, TRUE, FALSE); EXPECT_EQ(0xff123456u, sprite.GetTextColor());
}
TEST_F(SpriteTextSerializationTest, ExplicitNullTextClearsExistingOwnedString) {
    SerializedTextSprite sprite(Context); sprite.SetText(const_cast<char *>("clear me")); CKStateChunk chunk(CKCID_SPRITETEXT, nullptr); chunk.StartWrite(); TextWire(chunk, nullptr);
    ASSERT_EQ(CK_OK, Load(sprite, chunk)); EXPECT_EQ(nullptr, sprite.GetText());
}
TEST_F(SpriteTextSerializationTest, FontWireOrderIsIndependentOfChunkVersion) {
    const int versions[] = {5, 7, 8};
    for (int version : versions) {
        SerializedTextSprite sprite(Context); CKStateChunk chunk(CKCID_SPRITETEXT, nullptr); chunk.StartWrite(); chunk.SetDataVersion(version);
        FontWire(chunk, "Arial", -20, 650, TRUE, FALSE); ASSERT_EQ(CK_OK, Load(sprite, chunk)); FontState(sprite, "Arial", -20, 650, TRUE, FALSE);
    }
}
TEST_F(SpriteTextSerializationTest, FilelessSaveWithZeroMaskEmitsAllTextSectionsAndOnly2dAncestor) {
    SerializedTextSprite sprite(Context); sprite.SetText(const_cast<char *>("saved text")); sprite.SetRect(VxRect(3, 5, 33, 45));
    sprite.SetTextColor(0xff123456); sprite.SetBackgroundColor(0xffabcdef); CKStateChunk *chunk = sprite.Save(nullptr, 0); ASSERT_NE(nullptr, chunk);
    EXPECT_EQ(CKCID_SPRITETEXT, chunk->GetChunkClassID()); EXPECT_EQ(-1, chunk->GetCurrentPos()); chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(CK_STATESAVE_2DENTITYONLY)); chunk->ReadDword();
    EXPECT_FLOAT_EQ(3, chunk->ReadFloat()); EXPECT_FLOAT_EQ(5, chunk->ReadFloat()); EXPECT_FLOAT_EQ(33, chunk->ReadFloat()); EXPECT_FLOAT_EQ(45, chunk->ReadFloat());
    ASSERT_TRUE(chunk->SeekIdentifier(0x01000000)); ReadString(*chunk, "saved text"); FontPayload(*chunk, nullptr, 12, 400, FALSE, FALSE);
    ASSERT_TRUE(chunk->SeekIdentifier(0x04000000)); EXPECT_EQ(0xff123456u, chunk->ReadDword()); EXPECT_EQ(0xffabcdefu, chunk->ReadDword()); NoSpritePayload(*chunk); DeleteCKStateChunk(chunk);
}
TEST_F(SpriteTextSerializationTest, FileSaveOmitsActualBitmapAndSpriteMetadataEvenWithAllFlags) {
    SerializedTextSprite sprite(Context); ASSERT_TRUE(sprite.Create(2, 2, 32)); sprite.SetText(nullptr); ASSERT_TRUE(sprite.SetPixel(1, 1, 0xff123456));
    sprite.SetSaveOptions(CKTEXTURE_RAWDATA); sprite.SetTransparentColor(0x654321); sprite.SetRect(VxRect(2, 4, 12, 24));
    CKFile file(Context); CKStateChunk *chunk = sprite.Save(&file, CK_STATESAVE_ALL); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(CK_STATESAVE_2DENTITYONLY)); ASSERT_TRUE(chunk->SeekIdentifier(0x01000000)); ReadString(*chunk, nullptr);
    FontPayload(*chunk, nullptr, 12, 400, FALSE, FALSE); ASSERT_TRUE(chunk->SeekIdentifier(0x04000000));
    EXPECT_EQ(0xffffffffu, chunk->ReadDword()); EXPECT_EQ(0u, chunk->ReadDword()); NoSpritePayload(*chunk); DeleteCKStateChunk(chunk);
}
TEST_F(SpriteTextSerializationTest, SaveWithValidFontPreservesRequestedSizeAndStyles) {
    SerializedTextSprite sprite(Context); sprite.SetFont(const_cast<char *>("Arial"), 18, 600, TRUE, TRUE); ASSERT_NE(nullptr, sprite.Font());
    VXFONTINFO actual; ASSERT_TRUE(VxGetFontInfo(sprite.Font(), actual));
    CKStateChunk *chunk = sprite.Save(nullptr, 0); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    EXPECT_EQ(600, actual.Weight); EXPECT_TRUE(actual.Italic); EXPECT_TRUE(actual.Underline);
    FontPayload(*chunk, "Arial", 18, 600, TRUE, TRUE); DeleteCKStateChunk(chunk);
}
TEST_F(SpriteTextSerializationTest, IndependentPositiveSizeWireStaysInRequestedUnitsWhenSaved) {
    SerializedTextSprite sprite(Context); CKStateChunk literal(CKCID_SPRITETEXT, nullptr); literal.StartWrite();
    FontWire(literal, "Arial", 17, 600, FALSE, TRUE); ASSERT_EQ(CK_OK, Load(sprite, literal)); FontState(sprite, "Arial", 17, 600, FALSE, TRUE);
    ASSERT_NE(nullptr, sprite.Font()); VXFONTINFO actual; ASSERT_TRUE(VxGetFontInfo(sprite.Font(), actual)); EXPECT_GT(actual.Height, 0);
    CKStateChunk *saved = sprite.Save(nullptr, 0); ASSERT_NE(nullptr, saved); saved->StartRead(); FontPayload(*saved, "Arial", 17, 600, FALSE, TRUE); DeleteCKStateChunk(saved);
}
TEST_F(SpriteTextSerializationTest, IndependentNegativeNativeLogicalHeightWirePreservesSignWhenSaved) {
    SerializedTextSprite sprite(Context); CKStateChunk literal(CKCID_SPRITETEXT, nullptr); literal.StartWrite();
    FontWire(literal, "Arial", -20, 650, TRUE, FALSE); ASSERT_EQ(CK_OK, Load(sprite, literal)); FontState(sprite, "Arial", -20, 650, TRUE, FALSE);
    ASSERT_NE(nullptr, sprite.Font()); VXFONTINFO actual; ASSERT_TRUE(VxGetFontInfo(sprite.Font(), actual)); EXPECT_EQ(20, actual.Height);
    CKStateChunk *saved = sprite.Save(nullptr, 0); ASSERT_NE(nullptr, saved); saved->StartRead(); FontPayload(*saved, "Arial", -20, 650, TRUE, FALSE); DeleteCKStateChunk(saved);
}
TEST_F(SpriteTextSerializationTest, RepeatedSaveLoadDoesNotEnlargeActualFontHandle) {
    SerializedTextSprite source(Context), destination(Context); source.SetFont(const_cast<char *>("Arial"), 17, 600, FALSE, TRUE);
    ASSERT_NE(nullptr, source.Font()); VXFONTINFO initial; ASSERT_TRUE(VxGetFontInfo(source.Font(), initial));
    for (int iteration = 0; iteration < 4; ++iteration) {
        CKStateChunk *saved = source.Save(nullptr, 0); ASSERT_NE(nullptr, saved); ASSERT_EQ(CK_OK, destination.Load(saved, nullptr)); DeleteCKStateChunk(saved);
        FontState(destination, "Arial", 17, 600, FALSE, TRUE); ASSERT_NE(nullptr, destination.Font());
        VXFONTINFO actual; ASSERT_TRUE(VxGetFontInfo(destination.Font(), actual)); EXPECT_EQ(initial.Height, actual.Height);
        EXPECT_EQ(initial.Weight, actual.Weight); EXPECT_EQ(initial.Italic, actual.Italic); EXPECT_EQ(initial.Underline, actual.Underline);
        saved = destination.Save(nullptr, 0); ASSERT_NE(nullptr, saved); ASSERT_EQ(CK_OK, source.Load(saved, nullptr)); DeleteCKStateChunk(saved);
    }
}
TEST_F(SpriteTextSerializationTest, SaveWithoutFontHandlePreservesRequestedFontState) {
    SerializedTextSprite sprite(Context); sprite.SetFont(const_cast<char *>("Arial"), 23, 700, TRUE, FALSE); sprite.DropFont();
    CKStateChunk *chunk = sprite.Save(nullptr, 0); ASSERT_NE(nullptr, chunk); chunk->StartRead(); FontPayload(*chunk, "Arial", 23, 700, TRUE, FALSE); DeleteCKStateChunk(chunk);
}
TEST_F(SpriteTextSerializationTest, SaveOfNewSpriteUsesDefinedDefaultFontState) {
    SerializedTextSprite sprite(Context); ASSERT_EQ(nullptr, sprite.Font()); CKStateChunk *chunk = sprite.Save(nullptr, 0); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    FontPayload(*chunk, nullptr, 12, 400, FALSE, FALSE); ASSERT_TRUE(chunk->SeekIdentifier(0x01000000)); ReadString(*chunk, nullptr); DeleteCKStateChunk(chunk);
}
TEST_F(SpriteTextSerializationTest, ClosedTextSaveCanBeEmbeddedBeforeDerivedPayload) {
    SerializedTextSprite sprite(Context); sprite.SetText(const_cast<char *>("embedded")); CKStateChunk *parent = sprite.Save(nullptr, 0); ASSERT_NE(nullptr, parent);
    EXPECT_EQ(-1, parent->GetCurrentPos()); CKStateChunk derived(CKCID_SPRITETEXT, nullptr); derived.StartWrite();
    // A closed parent has no append cursor. Start a real, nonempty outer
    // payload so AddChunkAndDelete merges its data rather than stealing an
    // empty container's parser state from the closed child.
    derived.WriteIdentifier(0x76543211); derived.WriteInt(43); derived.AddChunkAndDelete(parent);
    derived.WriteIdentifier(0x76543212); derived.WriteInt(79); derived.CloseChunk(); derived.StartRead();
    ASSERT_TRUE(derived.SeekIdentifier(0x76543211)); EXPECT_EQ(43, derived.ReadInt());
    ASSERT_TRUE(derived.SeekIdentifier(0x01000000)); ReadString(derived, "embedded"); ASSERT_TRUE(derived.SeekIdentifier(0x76543212)); EXPECT_EQ(79, derived.ReadInt());
}
