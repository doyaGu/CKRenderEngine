#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"
#include "RCKRenderContext.h"
#include "RCKSpriteText.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class TextRenderProbe : public RCKSpriteText {
public:
    explicit TextRenderProbe(CKContext *context) : RCKSpriteText(context) {}
    void Repaint() { Redraw(); }
    CKBOOL Ready() { return IsUpToDate(); }
    void Invalidate() { m_Flags &= ~0x10000u; }
    void RemoveFont() { ClearFont(); }
};

class SpriteTextRenderTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *View = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKSprite, CKCID_2DENTITY);
        CKCLASSREGISTERCID(RCKSpriteText, CKCID_SPRITE);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        View = new RCKRenderContext(Context);
        View->m_Settings.m_Rect.right = 400;
        View->m_Settings.m_Rect.bottom = 300;
    }
    void TearDown() override {
        delete View;
        CKCloseContext(Context);
        CKShutdown();
    }
    static int Ink(TextRenderProbe &sprite, int *left = nullptr, int *right = nullptr) {
        int count = 0, first = sprite.GetWidth(), last = -1;
        for (int y = 0; y < sprite.GetHeight(); ++y) {
            for (int x = 0; x < sprite.GetWidth(); ++x) {
                if ((sprite.GetPixel(x, y) & 0x00ffffffu) != 0) {
                    ++count;
                    if (x < first) first = x;
                    if (x > last) last = x;
                }
            }
        }
        if (left) *left = first;
        if (right) *right = last;
        return count;
    }
};
}

TEST_F(SpriteTextRenderTest, MissingImageRemainsPendingWithoutInventingPixels) {
    TextRenderProbe sprite(Context);
    char text[] = "pending";
    sprite.SetText(text);
    sprite.Repaint();
    EXPECT_FALSE(sprite.Ready());
    EXPECT_EQ(nullptr, sprite.LockSurfacePtr());
}

TEST_F(SpriteTextRenderTest, NoTextFillsEveryPixelOfTheCurrentSlotWithTransparentColor) {
    TextRenderProbe sprite(Context);
    ASSERT_TRUE(sprite.Create(3, 2, 32));
    sprite.SetTransparentColor(0x00a1b2c3u);
    sprite.Repaint();
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x)
            EXPECT_EQ(0x00a1b2c3u, sprite.GetPixel(x, y));
    EXPECT_TRUE(sprite.Ready());
    EXPECT_TRUE(sprite.ToRestore());
}

TEST_F(SpriteTextRenderTest, RepaintOnlyChangesTheSelectedBitmapSlot) {
    TextRenderProbe sprite(Context);
    ASSERT_TRUE(sprite.Create(3, 2, 32, 0));
    ASSERT_TRUE(sprite.Create(3, 2, 32, 1));
    ASSERT_TRUE(sprite.SetPixel(1, 1, 0xff123456u, 0));
    ASSERT_TRUE(sprite.SetCurrentSlot(1));
    sprite.SetTransparentColor(0x00112233u);
    sprite.Repaint();
    EXPECT_EQ(0xff123456u, sprite.GetPixel(1, 1, 0));
    EXPECT_EQ(0x00112233u, sprite.GetPixel(1, 1, 1));
    EXPECT_EQ(1, sprite.GetCurrentSlot());
}

TEST_F(SpriteTextRenderTest, ReleasedImageDoesNotKeepAnEarlierSuccessfulRepaintStatus) {
    TextRenderProbe sprite(Context);
    ASSERT_TRUE(sprite.Create(3, 2, 32));
    sprite.Repaint();
    ASSERT_TRUE(sprite.Ready());
    ASSERT_TRUE(sprite.ReleaseSlot(0));
    sprite.Repaint();
    EXPECT_FALSE(sprite.Ready());
    EXPECT_EQ(nullptr, sprite.LockSurfacePtr());
}

TEST_F(SpriteTextRenderTest, VisibleRenderUpdatesTheCPUImageBeforeBaseVisibilitySkipsSubmission) {
    TextRenderProbe sprite(Context);
    ASSERT_TRUE(sprite.Create(3, 2, 32));
    ASSERT_TRUE(sprite.SetPixel(1, 1, 0xffabcdefu));
    sprite.SetTransparentColor(0x00112233u);
    sprite.Invalidate();
    ASSERT_TRUE(sprite.IsVisible());
    EXPECT_EQ(CK_OK, sprite.Render(View));
    EXPECT_EQ(0x00112233u, sprite.GetPixel(1, 1));
    EXPECT_TRUE(sprite.Ready());
}

TEST_F(SpriteTextRenderTest, HiddenRenderKeepsDirtyImageAndPixelsUntilVisibleAgain) {
    TextRenderProbe sprite(Context);
    ASSERT_TRUE(sprite.Create(3, 2, 32));
    ASSERT_TRUE(sprite.SetPixel(1, 1, 0xffabcdefu));
    sprite.Invalidate();
    sprite.Show(CKHIDE);
    EXPECT_EQ(CK_OK, sprite.Render(View));
    EXPECT_EQ(0xffabcdefu, sprite.GetPixel(1, 1));
    EXPECT_FALSE(sprite.Ready());
    sprite.Show(CKSHOW);
    EXPECT_EQ(CK_OK, sprite.Render(View));
    EXPECT_EQ(0u, sprite.GetPixel(1, 1));
    EXPECT_TRUE(sprite.Ready());
}

TEST_F(SpriteTextRenderTest, UpToDateRenderDoesNotErasePixelsAgain) {
    TextRenderProbe sprite(Context);
    ASSERT_TRUE(sprite.Create(3, 2, 32));
    sprite.Repaint();
    ASSERT_TRUE(sprite.Ready());
    ASSERT_TRUE(sprite.SetPixel(1, 1, 0xff123456u));
    EXPECT_EQ(CK_OK, sprite.Render(View));
    EXPECT_EQ(0xff123456u, sprite.GetPixel(1, 1));
}

TEST_F(SpriteTextRenderTest, RenderForwardsTheInheritedDrawFailureAfterCPURepaint) {
    TextRenderProbe sprite(Context);
    ASSERT_TRUE(sprite.Create(3, 2, 32));
    sprite.ModifyObjectFlags(CK_OBJECT_INTERFACEOBJ, 0);
    sprite.EnableRatioOffset(FALSE);
    sprite.SetRect(VxRect(0, 0, 30, 20));
    sprite.Invalidate();
    EXPECT_EQ(CKERR_INVALIDRENDERCONTEXT, sprite.Render(View));
    EXPECT_TRUE(sprite.Ready());
}

TEST_F(SpriteTextRenderTest, ActualFontDrawingProducesReadableCPUImage) {
    TextRenderProbe sprite(Context);
    ASSERT_TRUE(sprite.Create(160, 40, 32));
    char font[] = "Arial", text[] = "Ballanced";
    sprite.SetFont(font, 18, 700, FALSE, FALSE);
    sprite.SetText(text);
    EXPECT_TRUE(sprite.Ready());
    EXPECT_GT(Ink(sprite), 0);
}

TEST_F(SpriteTextRenderTest, ActualGlyphBoundsMoveToTheRequestedRightAlignment) {
    TextRenderProbe sprite(Context);
    ASSERT_TRUE(sprite.Create(160, 40, 32));
    char font[] = "Arial", text[] = "I";
    sprite.SetFont(font, 18, 400, FALSE, FALSE);
    sprite.SetText(text);
    sprite.SetAlign(CKSPRITETEXT_LEFT);
    int leftMin, leftMax, rightMin, rightMax;
    ASSERT_GT(Ink(sprite, &leftMin, &leftMax), 0);
    sprite.SetAlign(CKSPRITETEXT_RIGHT);
    ASSERT_GT(Ink(sprite, &rightMin, &rightMax), 0);
    EXPECT_GT(rightMin, leftMax + 80);
    EXPECT_LT(rightMax, 160);
}

TEST_F(SpriteTextRenderTest, UnavailableFontDoesNotReportNonemptyTextAsRendered) {
    TextRenderProbe sprite(Context);
    ASSERT_TRUE(sprite.Create(160, 40, 32));
    char font[] = "Arial", text[] = "pending";
    sprite.SetText(text);
    sprite.SetFont(font, 0, 400, FALSE, FALSE);
    EXPECT_FALSE(sprite.Ready());
    EXPECT_EQ(0, Ink(sprite));
    sprite.SetFont(font, 18, 400, FALSE, FALSE);
    EXPECT_TRUE(sprite.Ready());
    EXPECT_GT(Ink(sprite), 0);
}

TEST_F(SpriteTextRenderTest, RemovedFontRepaintRemainsPendingThenRealFontCanCompleteIt) {
    TextRenderProbe sprite(Context);
    ASSERT_TRUE(sprite.Create(160, 40, 32));
    char font[] = "Arial", text[] = "retry";
    sprite.SetFont(font, 18, 400, FALSE, FALSE);
    sprite.SetText(text);
    ASSERT_GT(Ink(sprite), 0);
    sprite.RemoveFont();
    sprite.Repaint();
    EXPECT_FALSE(sprite.Ready());
    EXPECT_EQ(0, Ink(sprite));
    sprite.SetFont(font, 18, 400, FALSE, FALSE);
    EXPECT_TRUE(sprite.Ready());
    EXPECT_GT(Ink(sprite), 0);
}

TEST_F(SpriteTextRenderTest, EmptyTextIsACompleteTransparentImageWithoutAFont) {
    TextRenderProbe sprite(Context);
    ASSERT_TRUE(sprite.Create(3, 2, 32));
    char previous[] = "previous", empty[] = "";
    sprite.SetText(previous);
    sprite.SetText(empty);
    EXPECT_STREQ("", sprite.GetText());
    EXPECT_TRUE(sprite.Ready());
    EXPECT_EQ(0u, sprite.GetPixel(1, 1));
}
