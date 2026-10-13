#include <gtest/gtest.h>
#include <cstring>
#include <cstdint>
#include "CKAll.h"
#include "RCKSpriteText.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class TextLifecycleProbe : public RCKSpriteText {
public:
    static FONT_HANDLE Font(RCKSpriteText &o) { return o.*(&TextLifecycleProbe::m_Font); }
    static void LayoutAndDefaults(RCKSpriteText &o) {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(&o);
        std::size_t previous = sizeof(RCKSprite);
#define FIELD(field) { auto member = &TextLifecycleProbe::field; const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&(o.*member)); \
        const std::size_t offset = address - base; EXPECT_GE(offset, previous); EXPECT_LE(offset + sizeof(o.*member), sizeof(RCKSpriteText)); \
        EXPECT_EQ(0u, address % alignof(decltype(o.*member))); ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(offset)); \
        ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(o.*member))); previous = offset + sizeof(o.*member); }
        FIELD(m_Text); FIELD(m_FontName); FIELD(m_FontSize); FIELD(m_FontWeight); FIELD(m_FontItalic); FIELD(m_FontUnderline);
        FIELD(m_FontColor); FIELD(m_BkColor); FIELD(m_Font); FIELD(m_Flags);
#undef FIELD
        ::testing::Test::RecordProperty("SDLSize", static_cast<int>(sizeof(RCKSpriteText)));
        ::testing::Test::RecordProperty("SDLAlignment", static_cast<int>(alignof(RCKSpriteText)));
        ::testing::Test::RecordProperty("SDLBaseSize", static_cast<int>(sizeof(RCKSprite)));
        ::testing::Test::RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
        ::testing::Test::RecordProperty("m_ClassID_value", static_cast<int>(m_ClassID));
        EXPECT_EQ(nullptr, o.*(&TextLifecycleProbe::m_Text)); EXPECT_EQ(nullptr, o.*(&TextLifecycleProbe::m_FontName));
        EXPECT_EQ(nullptr, Font(o)); EXPECT_EQ(12, o.*(&TextLifecycleProbe::m_FontSize));
        EXPECT_EQ(400, o.*(&TextLifecycleProbe::m_FontWeight)); EXPECT_FALSE(o.*(&TextLifecycleProbe::m_FontItalic));
        EXPECT_FALSE(o.*(&TextLifecycleProbe::m_FontUnderline)); EXPECT_EQ(1u, o.*(&TextLifecycleProbe::m_Flags));
    }
};
class SpriteTextLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKSprite, CKCID_2DENTITY); CKCLASSREGISTERCID(RCKSpriteText, CKCID_SPRITE);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
};
}
TEST_F(SpriteTextLifecycleTest, RegisteredIdentityDefaultsAndActualPlatformLayout) {
    RCKSpriteText *s = static_cast<RCKSpriteText *>(Context->CreateObject(CKCID_SPRITETEXT)); ASSERT_NE(nullptr, s);
    EXPECT_EQ(29, s->GetClassID()); EXPECT_EQ(s, Context->GetObject(s->GetID())); EXPECT_TRUE(CKIsChildClassOf(s, CKCID_SPRITE));
    EXPECT_EQ(nullptr, s->GetText()); EXPECT_EQ(0xffffffffu, s->GetTextColor()); EXPECT_EQ(0u, s->GetBackgroundTextColor());
    EXPECT_EQ(CKSPRITETEXT_CENTER, s->GetAlign()); EXPECT_TRUE(s->IsTransparent());
    EXPECT_FALSE((s->GetFlags() & 0x100u) != 0); TextLifecycleProbe::LayoutAndDefaults(*s);
}
TEST_F(SpriteTextLifecycleTest, ClassNameMatchesRegisteredNativeDisplayName) {
    EXPECT_STREQ("Sprite Text", RCKSpriteText::GetClassName());
}
TEST_F(SpriteTextLifecycleTest, RegistrationPublishesAssociatedParameterAndNoOwnDependencies) {
    CKClassDesc *d = CKGetClassDesc(CKCID_SPRITETEXT); ASSERT_NE(nullptr, d); d->Parameter = CKGUID(); RCKSpriteText::Register();
    EXPECT_EQ(CKPGUID_SPRITETEXT, d->Parameter); EXPECT_EQ(0x5c2e69e3u, d->Parameter.d1); EXPECT_EQ(0xfe156f09u, d->Parameter.d2);
    for (int mode = -1; mode < 6; ++mode) { EXPECT_EQ(0, RCKSpriteText::GetDependenciesCount(mode)); EXPECT_EQ(nullptr, RCKSpriteText::GetDependencies(0, mode)); }
}
TEST_F(SpriteTextLifecycleTest, FactoryVirtualDestructorReleasesActualFontAndObjectRegistration) {
    CKSpriteText *api = RCKSpriteText::CreateInstance(Context); ASSERT_NE(nullptr, api); RCKSpriteText *s = reinterpret_cast<RCKSpriteText *>(api);
    char face[] = "Arial", text[] = "Owned factory text"; s->SetFont(face, -17, 700, TRUE, TRUE); s->SetText(text);
    FONT_HANDLE font = TextLifecycleProbe::Font(*s); ASSERT_NE(nullptr, font); VXFONTINFO info; ASSERT_TRUE(VxGetFontInfo(font, info));
    EXPECT_EQ(17, info.Height); EXPECT_EQ(700, info.Weight); EXPECT_TRUE(info.Italic); EXPECT_TRUE(info.Underline);
    const CK_ID id = s->GetID(); delete static_cast<CKObject *>(s);
    EXPECT_EQ(nullptr, Context->GetObject(id)); EXPECT_FALSE(VxGetFontInfo(font, info));
}
TEST_F(SpriteTextLifecycleTest, MemoryOccupationIncludesPlatformTailAndOwnedTextAndFontName) {
    RCKSpriteText s(Context); const int base = s.RCKSprite::GetMemoryOccupation();
    const int fixed = base + static_cast<int>(sizeof(RCKSpriteText) - sizeof(RCKSprite)); EXPECT_EQ(fixed, s.GetMemoryOccupation());
    char text[] = "memory text", face[] = "Arial"; s.SetText(text); EXPECT_EQ(fixed + static_cast<int>(sizeof(text)), s.GetMemoryOccupation());
    s.SetFont(face, -17, 400, FALSE, FALSE); EXPECT_EQ(fixed + static_cast<int>(sizeof(text) + sizeof(face)), s.GetMemoryOccupation());
    s.SetText(nullptr); EXPECT_EQ(fixed + static_cast<int>(sizeof(face)), s.GetMemoryOccupation());
}
TEST_F(SpriteTextLifecycleTest, ConstructorAndDestructorKeepDistinctFontsAndOwnedTextIndependent) {
    RCKSpriteText survivor(Context); char face[] = "Arial", first[] = "survivor", second[] = "temporary";
    survivor.SetFont(face, -17, 400, FALSE, FALSE); survivor.SetText(first); FONT_HANDLE keep = TextLifecycleProbe::Font(survivor);
    ASSERT_NE(nullptr, keep);
    FONT_HANDLE released = nullptr;
    { RCKSpriteText temporary(Context); temporary.SetFont(face, -19, 700, TRUE, FALSE); temporary.SetText(second);
      released = TextLifecycleProbe::Font(temporary); ASSERT_NE(nullptr, released); EXPECT_NE(keep, released);
      first[0] = 'X'; second[0] = 'Y'; EXPECT_STREQ("survivor", survivor.GetText()); EXPECT_STREQ("temporary", temporary.GetText()); }
    VXFONTINFO info; EXPECT_FALSE(VxGetFontInfo(released, info)); EXPECT_TRUE(VxGetFontInfo(keep, info)); EXPECT_STREQ("survivor", survivor.GetText());
}
