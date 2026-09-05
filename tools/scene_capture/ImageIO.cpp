#include "ImageIO.h"

#include <string.h>

#include "VxMath.h"
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {

int MaskShift(uint32_t mask)
{
    if (!mask)
        return 0;
    int shift = 0;
    while (((mask >> shift) & 1u) == 0)
        ++shift;
    return shift;
}

int MaskBits(uint32_t mask)
{
    int bits = 0;
    while (mask) {
        bits += (int)(mask & 1u);
        mask >>= 1;
    }
    return bits;
}

uint8_t Expand(uint32_t value, int bits)
{
    if (bits <= 0)
        return 0;
    if (bits >= 8)
        return (uint8_t)(value >> (bits - 8));
    // Replicate the top bits to fill 8 bits (e.g. 5 -> 8: v << 3 | v >> 2).
    uint32_t out = value << (8 - bits);
    out |= out >> bits;
    return (uint8_t)out;
}

} // namespace

bool ConvertVxImageToRgba(const VxImageDescEx &desc, RgbaImage &out, std::string &error)
{
    if (!desc.Image) {
        error = "image has no pixel data";
        return false;
    }
    if (desc.Width <= 0 || desc.Height <= 0) {
        error = "image has an empty size";
        return false;
    }
    if (desc.ColorMapEntries != 0) {
        error = "palettized images are not supported";
        return false;
    }
    if (desc.Flags != 0) {
        error = "compressed images are not supported";
        return false;
    }
    const int bpp = desc.BitsPerPixel;
    if (bpp != 8 && bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) {
        error = "unsupported bits per pixel";
        return false;
    }
    const int bytesPerPixel = (bpp + 7) / 8;
    const int pitch = desc.BytesPerLine > 0 ? desc.BytesPerLine : desc.Width * bytesPerPixel;

    const uint32_t rMask = desc.RedMask, gMask = desc.GreenMask, bMask = desc.BlueMask, aMask = desc.AlphaMask;
    const int rShift = MaskShift(rMask), gShift = MaskShift(gMask), bShift = MaskShift(bMask), aShift = MaskShift(aMask);
    const int rBits = MaskBits(rMask), gBits = MaskBits(gMask), bBits = MaskBits(bMask), aBits = MaskBits(aMask);

    out.Allocate(desc.Width, desc.Height);
    for (int y = 0; y < desc.Height; ++y) {
        const uint8_t *src = desc.Image + (size_t)y * pitch;
        uint8_t *dst = out.Row(y);
        for (int x = 0; x < desc.Width; ++x) {
            uint32_t v = 0;
            for (int b = 0; b < bytesPerPixel; ++b)
                v |= (uint32_t)src[x * bytesPerPixel + b] << (8 * b);
            dst[x * 4 + 0] = Expand((v & rMask) >> rShift, rBits);
            dst[x * 4 + 1] = Expand((v & gMask) >> gShift, gBits);
            dst[x * 4 + 2] = Expand((v & bMask) >> bShift, bBits);
            dst[x * 4 + 3] = aMask ? Expand((v & aMask) >> aShift, aBits) : 255;
        }
    }
    return true;
}

bool WritePng(const std::string &path, const RgbaImage &image, std::string &error)
{
    if (!image.Valid()) {
        error = "invalid image";
        return false;
    }
    if (!stbi_write_png(path.c_str(), image.Width, image.Height, 4,
                        image.Pixels.data(), image.Width * 4)) {
        error = "could not write PNG: " + path;
        return false;
    }
    return true;
}

bool ReadPng(const std::string &path, RgbaImage &image, std::string &error)
{
    int w = 0, h = 0, channels = 0;
    unsigned char *pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels) {
        const char *reason = stbi_failure_reason();
        error = std::string("could not read PNG: ") + (reason ? reason : path.c_str());
        return false;
    }
    image.Width = (int)w;
    image.Height = (int)h;
    image.Pixels.assign(pixels, pixels + (size_t)w * h * 4);
    stbi_image_free(pixels);
    return true;
}

CompareResult CompareImages(const RgbaImage &a, const RgbaImage &b, int threshold,
                            const RgbaImage *mask, RgbaImage *diff)
{
    CompareResult result;
    if (a.Width != b.Width || a.Height != b.Height) {
        result.SizeMismatch = true;
        return result;
    }
    if (mask && (mask->Width != a.Width || mask->Height != a.Height))
        mask = NULL;
    if (diff)
        diff->Allocate(a.Width, a.Height);

    for (int y = 0; y < a.Height; ++y) {
        const uint8_t *pa = a.Row(y);
        const uint8_t *pb = b.Row(y);
        const uint8_t *pm = mask ? mask->Row(y) : NULL;
        uint8_t *pd = diff ? diff->Row(y) : NULL;
        for (int x = 0; x < a.Width; ++x) {
            if (pm && pm[x * 4] != 0) {
                ++result.MaskedPixels;
                if (pd) {
                    pd[x * 4 + 0] = 0;
                    pd[x * 4 + 1] = 0;
                    pd[x * 4 + 2] = 64;
                    pd[x * 4 + 3] = 255;
                }
                continue;
            }
            int maxDiff = 0;
            for (int c = 0; c < 3; ++c) {
                const int d = abs((int)pa[x * 4 + c] - (int)pb[x * 4 + c]);
                if (d > maxDiff)
                    maxDiff = d;
            }
            ++result.ComparedPixels;
            if (maxDiff > result.MaxChannelDiff)
                result.MaxChannelDiff = maxDiff;
            const bool pass = maxDiff <= threshold;
            if (pass)
                ++result.PassingPixels;
            if (pd) {
                if (pass) {
                    pd[x * 4 + 0] = pd[x * 4 + 1] = pd[x * 4 + 2] = 0;
                } else {
                    const uint8_t v = (uint8_t)(maxDiff < 128 ? 128 + maxDiff : 255);
                    pd[x * 4 + 0] = 255;
                    pd[x * 4 + 1] = (uint8_t)(255 - v);
                    pd[x * 4 + 2] = (uint8_t)(255 - v);
                }
                pd[x * 4 + 3] = 255;
            }
        }
    }
    return result;
}
