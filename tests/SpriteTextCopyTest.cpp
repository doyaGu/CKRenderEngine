#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKSpriteText.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"
#include "FontReleaseObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class TextCopyProbe : public RCKSpriteText {
public:
    static FONT_HANDLE Font(RCKSpriteText &o) { return o.*(&TextCopyProbe::m_Font); }
    static const char *Face(RCKSpriteText &o) { return o.*(&TextCopyProbe::m_FontName); }
    static CKDWORD Flags(RCKSpriteText &o) { return o.*(&TextCopyProbe::m_Flags); }
};
class SpriteTextCopyTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        CkreFontTest::Reset();
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKSprite, CKCID_2DENTITY); CKCLASSREGISTERCID(RCKSpriteText, CKCID_SPRITE);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Options(CKDependencies &o) { o.Resize(80); for (int i = 0; i < 80; ++i) o[i] = 0; o.m_Flags = CK_DEPENDENCIES_CUSTOM; o[CKCID_2DENTITY] = 3; }
    static void Configure(RCKSpriteText &s) { char face[] = "Arial", text[] = "Copy me";
        s.SetFont(face, -17, 700, TRUE, TRUE); s.SetText(text); s.SetTextColor(0xff112233u); s.SetBackgroundColor(0xff445566u);
        s.SetAlign(static_cast<CKSPRITETEXT_ALIGNMENT>(CKSPRITETEXT_RIGHT | CKSPRITETEXT_BOTTOM)); }
    static void CompareFont(FONT_HANDLE a, FONT_HANDLE b) {
        VXFONTINFO first, second; ASSERT_TRUE(VxGetFontInfo(a, first)); ASSERT_TRUE(VxGetFontInfo(b, second));
        EXPECT_STREQ(first.FaceName.CStr(), second.FaceName.CStr()); EXPECT_EQ(first.Height, second.Height); EXPECT_EQ(first.Weight, second.Weight);
        EXPECT_EQ(first.Italic, second.Italic); EXPECT_EQ(first.Underline, second.Underline);
    }
};
}
TEST_F(SpriteTextCopyTest, CopyOwnsIndependentTextFontAndPixelBuffers) {
    RCKSpriteText source(Context), destination(Context); ASSERT_TRUE(source.Create(96, 32, 32)); Configure(source);
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_STREQ("Copy me", destination.GetText()); EXPECT_NE(source.GetText(), destination.GetText());
    EXPECT_NE(TextCopyProbe::Font(source), TextCopyProbe::Font(destination)); CompareFont(TextCopyProbe::Font(source), TextCopyProbe::Font(destination));
    EXPECT_NE(source.LockSurfacePtr(), destination.LockSurfacePtr());
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 96; ++x) EXPECT_EQ(source.GetPixel(x, y), destination.GetPixel(x, y));
    dependencies.StopDependencies();
}
TEST_F(SpriteTextCopyTest, CopyPreservesColorsAlignmentAndInheritedHierarchy) {
    RCK2dEntity parent(Context); RCKSpriteText source(Context), destination(Context); Configure(source); source.SetRect(VxRect(7, 9, 17, 29)); source.SetZOrder(-27);
    source.SetParent(reinterpret_cast<CK2dEntity *>(&parent)); CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_EQ(0xff112233u, destination.GetTextColor()); EXPECT_EQ(0xff445566u, destination.GetBackgroundTextColor());
    EXPECT_EQ(source.GetAlign(), destination.GetAlign()); EXPECT_EQ(TextCopyProbe::Flags(source), TextCopyProbe::Flags(destination));
    EXPECT_EQ(-27, destination.GetZOrder()); EXPECT_EQ(reinterpret_cast<CK2dEntity *>(&parent), destination.GetParent()); EXPECT_EQ(2, parent.GetChildrenCount());
    VxRect r; destination.GetRect(r); EXPECT_FLOAT_EQ(7, r.left); EXPECT_FLOAT_EQ(9, r.top); EXPECT_FLOAT_EQ(17, r.right); EXPECT_FLOAT_EQ(29, r.bottom);
    destination.SetParent(nullptr); source.SetParent(nullptr); dependencies.StopDependencies();
}
TEST_F(SpriteTextCopyTest, DestinationRemainsUsableAfterSourceAndItsFontAreDestroyed) {
    RCKSpriteText destination(Context); CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    FONT_HANDLE sourceFont = nullptr;
    { RCKSpriteText source(Context); Configure(source); sourceFont = TextCopyProbe::Font(source); ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); }
    VXFONTINFO info; CkreFontTest::ExpectReleased(sourceFont); EXPECT_TRUE(VxGetFontInfo(TextCopyProbe::Font(destination), info));
    EXPECT_STREQ("Copy me", destination.GetText()); char changed[] = "Destination only"; destination.SetText(changed); EXPECT_STREQ(changed, destination.GetText()); dependencies.StopDependencies();
}
TEST_F(SpriteTextCopyTest, EmptySourceClearsDestinationTextAndReleasesPreviousFont) {
    RCKSpriteText source(Context), destination(Context); Configure(destination); FONT_HANDLE oldFont = TextCopyProbe::Font(destination);
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options); ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
    EXPECT_EQ(nullptr, destination.GetText()); EXPECT_EQ(nullptr, TextCopyProbe::Font(destination)); EXPECT_EQ(nullptr, TextCopyProbe::Face(destination));
    EXPECT_EQ(0xffffffffu, destination.GetTextColor()); EXPECT_EQ(0u, destination.GetBackgroundTextColor()); EXPECT_EQ(CKSPRITETEXT_CENTER, destination.GetAlign());
    CkreFontTest::ExpectReleased(oldFont); dependencies.StopDependencies();
}
TEST_F(SpriteTextCopyTest, SelfCopyPreservesOwnedTextFontPixelsAndSingleParentEntry) {
    RCK2dEntity parent(Context); RCKSpriteText s(Context); ASSERT_TRUE(s.Create(96, 32, 32)); Configure(s); s.SetParent(reinterpret_cast<CK2dEntity *>(&parent));
    CKSTRING text = s.GetText(); FONT_HANDLE font = TextCopyProbe::Font(s); CKBYTE *pixels = s.LockSurfacePtr(); const CKDWORD pixel = s.GetPixel(95, 31);
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options); ASSERT_EQ(CK_OK, s.Copy(s, dependencies));
    EXPECT_STREQ("Copy me", s.GetText()); EXPECT_EQ(text, s.GetText()); EXPECT_EQ(font, TextCopyProbe::Font(s)); EXPECT_EQ(pixels, s.LockSurfacePtr());
    EXPECT_EQ(pixel, s.GetPixel(95, 31)); EXPECT_EQ(1, parent.GetChildrenCount()); s.SetParent(nullptr); dependencies.StopDependencies();
}
TEST_F(SpriteTextCopyTest, CopyStoresIndependentRequestedFaceAndKeepsSourceWhenDestinationChangesFont) {
    RCKSpriteText source(Context), destination(Context); Configure(source); CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_STREQ("Arial", TextCopyProbe::Face(destination)); EXPECT_NE(TextCopyProbe::Face(source), TextCopyProbe::Face(destination));
    FONT_HANDLE sourceFont = TextCopyProbe::Font(source); char other[] = "Courier New"; destination.SetFont(other, -21, 400, FALSE, FALSE);
    EXPECT_STREQ("Arial", TextCopyProbe::Face(source)); VXFONTINFO info; ASSERT_TRUE(VxGetFontInfo(sourceFont, info)); EXPECT_EQ(17, info.Height); EXPECT_EQ(700, info.Weight);
    EXPECT_STREQ("Copy me", source.GetText()); dependencies.StopDependencies();
}
