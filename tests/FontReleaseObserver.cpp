#include "FontReleaseObserver.h"

namespace {
FONT_HANDLE releasedFonts[512];
unsigned releaseCount = 0;
}

void CkreFontTest::Reset() { releaseCount = 0; }

bool CkreFontTest::ConsumeRelease(FONT_HANDLE font) {
    for (unsigned i = 0; i < releaseCount; ++i) {
        if (releasedFonts[i] == font) {
            releasedFonts[i] = nullptr;
            return true;
        }
    }
    return false;
}

extern "C" void __real__Z12VxDeleteFontPv(FONT_HANDLE font);
extern "C" void __wrap__Z12VxDeleteFontPv(FONT_HANDLE font) {
    __real__Z12VxDeleteFontPv(font);
    if (font) {
        ASSERT_LT(releaseCount, 512u);
        releasedFonts[releaseCount++] = font;
    }
}
