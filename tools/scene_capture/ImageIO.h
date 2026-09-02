#ifndef CKRE_SCENE_CAPTURE_IMAGEIO_H
#define CKRE_SCENE_CAPTURE_IMAGEIO_H

#include <stdint.h>
#include <string>
#include <vector>

struct VxImageDescEx;

// Straight RGBA8 image, top-left origin, tightly packed.
struct RgbaImage {
    int Width = 0;
    int Height = 0;
    std::vector<uint8_t> Pixels;

    bool Valid() const { return Width > 0 && Height > 0 && Pixels.size() == (size_t)Width * Height * 4; }
    void Allocate(int w, int h)
    {
        Width = w;
        Height = h;
        Pixels.assign((size_t)w * h * 4, 0);
    }
    uint8_t *Row(int y) { return &Pixels[(size_t)y * Width * 4]; }
    const uint8_t *Row(int y) const { return &Pixels[(size_t)y * Width * 4]; }
};

// Converts any masked RGB(A) VxImageDescEx (8..32 bpp) into RGBA8. Alpha is
// 255 when the source has no alpha mask. Returns false for palettized or
// compressed sources.
bool ConvertVxImageToRgba(const VxImageDescEx &desc, RgbaImage &out, std::string &error);

bool WritePng(const std::string &path, const RgbaImage &image, std::string &error);
bool ReadPng(const std::string &path, RgbaImage &image, std::string &error);

struct CompareResult {
    bool SizeMismatch = false;
    long long ComparedPixels = 0;
    long long PassingPixels = 0;
    long long MaskedPixels = 0;
    int MaxChannelDiff = 0;
    double PassRatio() const { return ComparedPixels ? (double)PassingPixels / (double)ComparedPixels : 0.0; }
};

// Per-channel RGB comparison. A pixel passes when every channel differs by at
// most Threshold. Mask (optional, same size) excludes pixels whose red
// channel is non-zero. Diff (optional) receives a visualization: passing
// pixels black, masked pixels dark blue, failing pixels scaled red/white.
CompareResult CompareImages(const RgbaImage &a, const RgbaImage &b, int threshold,
                            const RgbaImage *mask, RgbaImage *diff);

#endif // CKRE_SCENE_CAPTURE_IMAGEIO_H
