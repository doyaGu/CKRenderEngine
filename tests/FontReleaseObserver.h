#pragma once

#include <gtest/gtest.h>
#include "VxMath.h"

namespace CkreFontTest {
#if defined(CKRE_WRAP_FONT_DELETE)
void Reset();
bool ConsumeRelease(FONT_HANDLE font);
#else
inline void Reset() {}
#endif

inline void ExpectReleased(FONT_HANDLE font) {
#if defined(CKRE_WRAP_FONT_DELETE)
    EXPECT_TRUE(ConsumeRelease(font));
#elif defined(_WIN32)
    VXFONTINFO information;
    EXPECT_FALSE(VxGetFontInfo(font, information));
#else
    GTEST_SKIP() << "Released SDL pointer handles cannot be queried without a deletion observer";
#endif
}
} // namespace CkreFontTest
