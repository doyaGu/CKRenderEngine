#ifndef TESTFFIMAGEDIFF_H
#define TESTFFIMAGEDIFF_H

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstdio>

namespace FFImageDiff {
struct Result {
    unsigned Pixels = 0, Coverage = 0, MaxError = 0;
    int FirstX = -1, FirstY = -1;
    bool Matches() const { return Pixels == 0 && Coverage == 0; }
};

// Packed top-first BGRA8. Coverage is optional and compares exact clear-pixel
// membership, so replay cases must choose a clear colour outside their output.
inline Result Compare(const uint8_t *expected, const uint8_t *actual, int width, int height,
                      unsigned tolerance, const uint8_t *clear = nullptr) {
    Result result;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int offset = (y * width + x) * 4;
            bool different = false, expectedClear = true, actualClear = true;
            for (int c = 0; c < 4; ++c) {
                const unsigned error = unsigned(std::abs(int(expected[offset + c]) - int(actual[offset + c])));
                result.MaxError = (std::max)(result.MaxError, error);
                different |= error > tolerance;
                expectedClear &= clear && expected[offset + c] == clear[c];
                actualClear &= clear && actual[offset + c] == clear[c];
            }
            result.Pixels += different;
            result.Coverage += expectedClear != actualClear;
            if ((different || expectedClear != actualClear) && result.FirstX < 0) {
                result.FirstX = x;
                result.FirstY = y;
            }
        }
    }
    return result;
}

// Portable images for CI artifacts; the amplified difference includes alpha
// by taking its error into every RGB channel.
inline bool SavePPM(const char *path, const uint8_t *pixels, int width, int height,
                    const uint8_t *reference = nullptr) {
    FILE *file = std::fopen(path, "wb");
    if (!file) return false;
    bool ok = std::fprintf(file, "P6\n%d %d\n255\n", width, height) > 0;
    for (int i = 0; i < width * height; ++i) {
        uint8_t rgb[3];
        for (int c = 0; c < 3; ++c) {
            const int offset = i * 4;
            const int error = reference ? (std::max)(std::abs(int(pixels[offset + 2 - c]) - int(reference[offset + 2 - c])),
                                                  std::abs(int(pixels[offset + 3]) - int(reference[offset + 3]))) : 0;
            rgb[c] = reference ? uint8_t((std::min)(255, error * 16)) : pixels[offset + 2 - c];
        }
        ok &= std::fwrite(rgb, 1, 3, file) == 3;
    }
    return std::fclose(file) == 0 && ok;
}
}
#endif
