#include "CKSdlGpuTextureData.h"
#include <cstdint>
#include <limits>

bool CKSdlGpuDecodeDXT(const VxImageDescEx &image, std::vector<unsigned char> &pixels)
{
    const auto format = VxImageDesc2PixelFormat(image);
    if (!image.Image || image.Width <= 0 || image.Height <= 0 ||
        (format != _DXT1 && format != _DXT3 && format != _DXT5)) return false;
    const unsigned width = image.Width, height = image.Height, blockBytes = format == _DXT1 ? 8 : 16;
    const uint64_t blocksX = (uint64_t(width) + 3) / 4, blocksY = (uint64_t(height) + 3) / 4;
    const uint64_t sourceSize = blocksX * blocksY * blockBytes, size = uint64_t(width) * height * 4;
    if (size > (std::numeric_limits<size_t>::max)() || sourceSize > UINT32_MAX ||
        (image.TotalImageSize > 0 && uint64_t(image.TotalImageSize) < sourceSize)) return false;
    pixels.resize(size_t(size));
    const auto word = [](const unsigned char *p) { return unsigned(p[0]) | (unsigned(p[1]) << 8); };
    for (unsigned by = 0; by < blocksY; ++by) for (unsigned bx = 0; bx < blocksX; ++bx) {
        const auto *block = image.Image + (size_t(by) * size_t(blocksX) + bx) * blockBytes;
        const auto *colors = block + (format == _DXT1 ? 0 : 8);
        const unsigned c0 = word(colors), c1 = word(colors + 2);
        unsigned char palette[4][4] = {};
        for (unsigned i = 0; i < 2; ++i) {
            const unsigned value = i ? c1 : c0;
            const unsigned b = value & 31, g = (value >> 5) & 63, r = value >> 11;
            palette[i][0] = (b << 3) | (b >> 2);
            palette[i][1] = (g << 2) | (g >> 4);
            palette[i][2] = (r << 3) | (r >> 2);
            palette[i][3] = 255;
        }
        const bool transparent = format == _DXT1 && c0 <= c1;
        for (unsigned c = 0; c < 3; ++c) {
            palette[2][c] = transparent ? (palette[0][c] + palette[1][c]) / 2
                                        : (2 * palette[0][c] + palette[1][c]) / 3;
            palette[3][c] = transparent ? 0 : (palette[0][c] + 2 * palette[1][c]) / 3;
        }
        palette[2][3] = 255; palette[3][3] = transparent ? 0 : 255;
        unsigned alpha[8] = {block[0], block[1]};
        uint64_t alphaIndices = 0;
        if (format == _DXT5) {
            const unsigned count = alpha[0] > alpha[1] ? 7 : 5;
            for (unsigned i = 1; i < count; ++i)
                alpha[i + 1] = ((count - i) * alpha[0] + i * alpha[1]) / count;
            if (count == 5) { alpha[6] = 0; alpha[7] = 255; }
            for (unsigned i = 0; i < 6; ++i) alphaIndices |= uint64_t(block[2 + i]) << (i * 8);
        }
        for (unsigned y = 0; y < 4 && by * 4 + y < height; ++y)
            for (unsigned x = 0; x < 4 && bx * 4 + x < width; ++x) {
                const unsigned index = y * 4 + x;
                const auto *color = palette[(colors[4 + y] >> (x * 2)) & 3];
                auto *dest = pixels.data() + (size_t(by * 4 + y) * width + bx * 4 + x) * 4;
                for (unsigned c = 0; c < 4; ++c) dest[c] = color[c];
                if (format == _DXT3) dest[3] = ((block[index / 2] >> ((index & 1) * 4)) & 15) * 17;
                if (format == _DXT5) dest[3] = (unsigned char)alpha[(alphaIndices >> (index * 3)) & 7];
            }
    }
    return true;
}
