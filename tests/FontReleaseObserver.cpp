#include "FontReleaseObserver.h"

#include <gtest/gtest.h>

static FONT_HANDLE releasedFonts[512];
static unsigned releaseCount;

void CkreFontTestReset() { releaseCount = 0; }

void CkreFontTestExpectReleased(FONT_HANDLE font) {
    for (unsigned i = 0; i < releaseCount; ++i) {
        if (releasedFonts[i] == font) {
            releasedFonts[i] = nullptr;
            return;
        }
    }
    ADD_FAILURE() << "The owned font was not released";
}

void CkreFontTestDeleteFont(FONT_HANDLE font) {
    VxDeleteFont(font);
    if (font) {
        ASSERT_LT(releaseCount, 512u);
        releasedFonts[releaseCount++] = font;
    }
}
