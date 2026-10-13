#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "RCKSpriteText.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class SpriteTextStateProbe : public RCKSpriteText {
public:
    explicit SpriteTextStateProbe(CKContext *c) : RCKSpriteText(c) {}
    using RCKSpriteText::ClearFont;
    using RCKSpriteText::IsUpToDate;
    using RCKSpriteText::Redraw;
    FONT_HANDLE Font() const { return m_Font; }
    char *FontName() const { return m_FontName; }
    int FontSize() const { return m_FontSize; }
    int FontWeight() const { return m_FontWeight; }
    CKBOOL Italic() const { return m_FontItalic; }
    CKBOOL Underline() const { return m_FontUnderline; }
    CKDWORD Flags() const { return m_Flags; }
    void SetTestFlags(CKDWORD flags) { m_Flags = flags; }
};
class SpriteTextStateTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKSprite, CKCID_2DENTITY);
        CKCLASSREGISTERCID(RCKSpriteText, CKCID_SPRITE);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Surface(SpriteTextStateProbe &s) {
        ASSERT_TRUE(s.Create(3, 2, 32)); s.SetTransparentColor(0x00123456u); s.Redraw();
        ASSERT_TRUE(s.IsUpToDate());
    }
    static void Blank(const SpriteTextStateProbe &sprite) {
        SpriteTextStateProbe &s = const_cast<SpriteTextStateProbe &>(sprite);
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 3; ++x)
            EXPECT_EQ(0x00123456u, s.GetPixel(x, y));
        EXPECT_TRUE(s.IsUpToDate());
    }
    static void Disturb(SpriteTextStateProbe &s) {
        ASSERT_TRUE(s.SetPixel(1, 0, 0xffabcdefu));
        s.SetTestFlags(s.Flags() & ~0x10000u);
    }
    static void RequestedFont(SpriteTextStateProbe &s, const char *face, int size, int weight,
                              CKBOOL italic, CKBOOL underline) {
        EXPECT_STREQ(face, s.FontName()); EXPECT_EQ(size, s.FontSize()); EXPECT_EQ(weight, s.FontWeight());
        EXPECT_EQ(italic, s.Italic()); EXPECT_EQ(underline, s.Underline());
#ifdef _WIN32
        ASSERT_NE(nullptr, s.Font()); VXFONTINFO info; ASSERT_TRUE(VxGetFontInfo(s.Font(), info));
        EXPECT_STREQ(face, info.FaceName.CStr()); EXPECT_EQ(size < 0 ? -size : info.Height, info.Height);
        EXPECT_EQ(weight, info.Weight); EXPECT_EQ(italic != FALSE, info.Italic != FALSE);
        EXPECT_EQ(underline != FALSE, info.Underline != FALSE);
#else
        EXPECT_EQ(nullptr, s.Font());
#endif
    }
};
}

TEST_F(SpriteTextStateTest, EmptyTextAndDefaultPublicState) {
    SpriteTextStateProbe s(Context); EXPECT_EQ(nullptr, s.GetText());
    EXPECT_EQ(0xffffffffu, s.GetTextColor()); EXPECT_EQ(0u, s.GetBackgroundTextColor());
    EXPECT_EQ(CKSPRITETEXT_CENTER, s.GetAlign()); EXPECT_FALSE(s.IsUpToDate());
}
TEST_F(SpriteTextStateTest, TextCopiesCallerBufferAndReplacementRemainsOwned) {
    SpriteTextStateProbe s(Context); char first[] = "owned caller buffer"; s.SetText(first);
    EXPECT_NE(first, s.GetText()); first[0] = 'X'; EXPECT_STREQ("owned caller buffer", s.GetText());
    char second[] = "replacement"; s.SetText(second); second[0] = 'Z'; EXPECT_STREQ("replacement", s.GetText());
}
TEST_F(SpriteTextStateTest, TextAcceptsOwnSubstringAsNewOwnedValue) {
    SpriteTextStateProbe s(Context); char first[] = "prefix:owned substring survives replacement"; s.SetText(first);
    s.SetText(s.GetText() + 7); EXPECT_STREQ("owned substring survives replacement", s.GetText());
}
TEST_F(SpriteTextStateTest, TextAcceptsOwnExactPointerAndRedrawsNewlyCreatedSurface) {
    SpriteTextStateProbe s(Context); char text[] = "x"; s.SetText(text); ASSERT_FALSE(s.IsUpToDate());
    ASSERT_TRUE(s.Create(3, 2, 32)); s.SetTransparentColor(0x00123456u); s.SetText(s.GetText());
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 3; ++x)
        EXPECT_EQ(0x00123456u, s.GetPixel(x, y));
    EXPECT_STREQ("x", s.GetText()); EXPECT_FALSE(s.IsUpToDate());
}
TEST_F(SpriteTextStateTest, NullTextClearsOwnedValueAndRaster) {
    SpriteTextStateProbe s(Context); Surface(s); char text[] = "text"; s.SetText(text); Disturb(s);
    s.SetText(nullptr); EXPECT_EQ(nullptr, s.GetText()); Blank(s);
}
TEST_F(SpriteTextStateTest, NullTextRedrawsEvenWhenTextAlreadyNull) {
    SpriteTextStateProbe s(Context); Surface(s); Disturb(s); s.SetText(nullptr); Blank(s);
}
TEST_F(SpriteTextStateTest, EmptyTextRetainsNonNullOwnedEmptyStringAndClearsRaster) {
    SpriteTextStateProbe s(Context); Surface(s); char text[] = "before"; s.SetText(text); Disturb(s);
    char empty[] = ""; s.SetText(empty); ASSERT_NE(nullptr, s.GetText()); EXPECT_STREQ("", s.GetText()); Blank(s);
}
TEST_F(SpriteTextStateTest, EmptyInputCreatesOwnedEmptyStringFromInitialNull) {
    SpriteTextStateProbe s(Context); char empty[] = ""; s.SetText(empty);
    ASSERT_NE(nullptr, s.GetText()); EXPECT_NE(empty, s.GetText()); EXPECT_STREQ("", s.GetText());
}
TEST_F(SpriteTextStateTest, TextColorRoundTripsArgbAndRedrawsBitmap) {
    SpriteTextStateProbe s(Context); Surface(s); Disturb(s); s.SetTextColor(0x7f123456u);
    EXPECT_EQ(0x7f123456u, s.GetTextColor()); Blank(s); EXPECT_EQ(0u, s.GetBackgroundTextColor());
}
TEST_F(SpriteTextStateTest, SameTextColorRedrawsAfterSurfaceChanges) {
    SpriteTextStateProbe s(Context); Surface(s); Disturb(s); s.SetTextColor(s.GetTextColor()); Blank(s);
}
TEST_F(SpriteTextStateTest, BackgroundColorRoundTripsArgbAndRedrawsBitmap) {
    SpriteTextStateProbe s(Context); Surface(s); Disturb(s); s.SetBackgroundColor(0x91827364u);
    EXPECT_EQ(0x91827364u, s.GetBackgroundTextColor()); Blank(s); EXPECT_EQ(0xffffffffu, s.GetTextColor());
}
TEST_F(SpriteTextStateTest, SameBackgroundColorRedrawsAfterSurfaceChanges) {
    SpriteTextStateProbe s(Context); Surface(s); Disturb(s); s.SetBackgroundColor(s.GetBackgroundTextColor()); Blank(s);
}
TEST_F(SpriteTextStateTest, AlignUsesLowWordAndPreservesOtherFlags) {
    SpriteTextStateProbe s(Context); s.SetTestFlags(0xa5020001u);
    s.SetAlign(static_cast<CKSPRITETEXT_ALIGNMENT>(0x77ff0064u));
    EXPECT_EQ(0xa5020064u, s.Flags()); EXPECT_EQ(0x64u, static_cast<CKDWORD>(s.GetAlign()));
    EXPECT_FALSE(s.IsUpToDate());
}
TEST_F(SpriteTextStateTest, SameAlignRedrawsAfterSurfaceChanges) {
    SpriteTextStateProbe s(Context); Surface(s); Disturb(s); s.SetAlign(s.GetAlign()); Blank(s);
}
TEST_F(SpriteTextStateTest, IsUpToDateReflectsOnlyDedicatedBitWithoutMutatingState) {
    SpriteTextStateProbe s(Context); s.SetTestFlags(0xfffeffffu); EXPECT_FALSE(s.IsUpToDate());
    EXPECT_EQ(0xfffeffffu, s.Flags()); s.SetTestFlags(0x10000u); EXPECT_TRUE(s.IsUpToDate());
    EXPECT_EQ(0x10000u, s.Flags());
}
TEST_F(SpriteTextStateTest, FontOwnsCallerFaceAndCreatesQueryableRealPlatformHandle) {
    SpriteTextStateProbe s(Context); char face[] = "Arial"; s.SetFont(face, -19, 700, TRUE, TRUE);
    EXPECT_NE(face, s.FontName()); face[0] = 'X'; RequestedFont(s, "Arial", -19, 700, TRUE, TRUE);
}
TEST_F(SpriteTextStateTest, FontReplacementUpdatesFaceAndAttributes) {
    SpriteTextStateProbe s(Context); char first[] = "Arial", second[] = "Courier New";
    s.SetFont(first, -13, 400, FALSE, TRUE); s.SetFont(second, -23, 600, TRUE, FALSE);
    RequestedFont(s, "Courier New", -23, 600, TRUE, FALSE);
}
TEST_F(SpriteTextStateTest, FontAcceptsOwnNamePointerWhenChangingAttributes) {
    SpriteTextStateProbe s(Context); char face[] = "Arial"; s.SetFont(face, -17, 400, FALSE, FALSE);
    s.SetFont(s.FontName(), -29, 700, TRUE, FALSE); RequestedFont(s, "Arial", -29, 700, TRUE, FALSE);
}
TEST_F(SpriteTextStateTest, SameFontRequestRedrawsAfterSurfaceChanges) {
    SpriteTextStateProbe s(Context); Surface(s); char face[] = "Arial"; s.SetFont(face, -19, 400, FALSE, FALSE);
    Disturb(s); s.SetFont(face, -19, 400, FALSE, FALSE); Blank(s);
}
TEST_F(SpriteTextStateTest, NullFontRetainsRequestedAttributesForUnavailableHandle) {
    SpriteTextStateProbe s(Context); s.SetFont(nullptr, -21, 500, TRUE, FALSE);
    EXPECT_EQ(nullptr, s.Font()); EXPECT_EQ(nullptr, s.FontName()); EXPECT_EQ(-21, s.FontSize());
    EXPECT_EQ(500, s.FontWeight()); EXPECT_TRUE(s.Italic()); EXPECT_FALSE(s.Underline());
}
TEST_F(SpriteTextStateTest, NullFontRequestReplacesAnEmptyFaceFontWithUnavailableHandle) {
    SpriteTextStateProbe s(Context);
    char empty[] = "";
    s.SetFont(empty, -21, 500, TRUE, FALSE);
    ASSERT_NE(nullptr, s.Font());
    ASSERT_NE(nullptr, s.FontName());
    EXPECT_STREQ("", s.FontName());
    s.SetFont(nullptr, -21, 500, TRUE, FALSE);
    EXPECT_EQ(nullptr, s.Font());
    EXPECT_EQ(nullptr, s.FontName());
    EXPECT_EQ(-21, s.FontSize());
    EXPECT_EQ(500, s.FontWeight());
    EXPECT_TRUE(s.Italic());
    EXPECT_FALSE(s.Underline());
}
TEST_F(SpriteTextStateTest, ClearFontReleasesHandleAndOwnedNameWithoutChangingTextColorsOrFlags) {
    SpriteTextStateProbe s(Context); char face[] = "Arial", text[] = "keep text";
    s.SetText(text); s.SetFont(face, -19, 600, TRUE, TRUE); s.SetTextColor(0x12345678u);
    s.SetBackgroundColor(0xabcdef01u); s.SetTestFlags(0xa5010022u); s.ClearFont();
    EXPECT_EQ(nullptr, s.Font()); EXPECT_EQ(nullptr, s.FontName()); EXPECT_EQ(12, s.FontSize());
    EXPECT_EQ(400, s.FontWeight()); EXPECT_FALSE(s.Italic()); EXPECT_FALSE(s.Underline());
    EXPECT_STREQ("keep text", s.GetText()); EXPECT_EQ(0x12345678u, s.GetTextColor());
    EXPECT_EQ(0xabcdef01u, s.GetBackgroundTextColor()); EXPECT_EQ(0xa5010022u, s.Flags());
    s.ClearFont(); EXPECT_EQ(nullptr, s.Font()); EXPECT_EQ(0xa5010022u, s.Flags());
}
