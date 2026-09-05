#include "ImageIO.h"
#include <cstdio>
#include <iostream>

int main()
{
    const char *path = "scene_capture_image_roundtrip.png";
    RgbaImage original, decoded;
    original.Allocate(7, 5);
    for (size_t i = 0; i < original.Pixels.size(); ++i)
        original.Pixels[i] = static_cast<uint8_t>((i * 37) & 255);
    std::string error;
    const bool roundtrip = WritePng(path, original, error) && ReadPng(path, decoded, error) &&
        original.Width == decoded.Width && original.Height == decoded.Height && original.Pixels == decoded.Pixels;
    std::remove(path);
    RgbaImage invalid;
    if (!roundtrip || WritePng(path, invalid, error) || ReadPng(path, decoded, error)) {
        std::cerr << "PNG RGBA/alpha roundtrip or invalid-input check failed: " << error << '\n';
        return 1;
    }
    return 0;
}
