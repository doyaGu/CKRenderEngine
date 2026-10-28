#include "CKSdlGpuRasterizerContext.h"
#include <stdio.h>
#include <string.h>

static bool TextureUploadResult(CKERROR error, const char *operation)
{
    if (error == CK_OK) return true;
    fprintf(stderr, "texture upload regression %s failed: %d\n", operation, int(error));
    return false;
}

struct TextureUploadReadback {
    CKSdlGpuRasterizerContext &Backend;
    CKDWORD Packed = 0, Output = 0;

    explicit TextureUploadReadback(CKSdlGpuRasterizerContext &backend) : Backend(backend) {}
    ~TextureUploadReadback()
    {
        if (Packed) Backend.DestroyObject(Packed, CKRST_OBJ_TEXTURE);
        if (Output) Backend.DestroyObject(Output, CKRST_OBJ_TEXTURE);
    }

    bool Capture(CKDWORD texture, VX_PIXELFORMAT format, unsigned mip,
                 unsigned width, unsigned height, XArray<CKBYTE> &pixels)
    {
        CKTextureDesc desc;
        VxPixelFormat2ImageDesc(format, desc.Format);
        desc.Format.Width = width; desc.Format.Height = height;
        desc.MipMapCount = 1;
        desc.Flags = CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
        if (!TextureUploadResult(Backend.CreateTexture(&desc, NULL, &Packed), "create snapshot") ||
            !TextureUploadResult(Backend.Blit(Packed, 0, 0, 0, 0, texture, mip, 0, NULL), "copy mip")) return false;
        VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
        desc.Flags |= CKRST_TEXTURE_RENDERTARGET;
        if (!TextureUploadResult(Backend.CreateTexture(&desc, NULL, &Output), "create output") ||
            !TextureUploadResult(Backend.Blit(Output, 0, 0, 0, 0, Packed, 0, 0, NULL), "expand pixels")) return false;
        CKReadbackDesc read;
        CKSdlGpuReadbackTicket ticket;
        if (!TextureUploadResult(Backend.ReadTexture(Output, 0, &read, &ticket), "readback") ||
            !TextureUploadResult(Backend.Submit(CKRST_PRESENT_UNCHANGED, FALSE, NULL), "submit") ||
            Backend.PollReadback(ticket, TRUE) != CKRST_READBACK_READY) return false;
        pixels = Backend.GetReadbackData(ticket);
        return pixels.Size() == int(width * height * 4);
    }
};

bool CheckSdlGpuNegativePitchUploads(CKSdlGpuRasterizerContext &backend)
{
    const VX_PIXELFORMAT formats[] = {_32_ARGB8888, _32_ABGR8888, _24_RGB888, _16_RGB565, _16_L6V5U5};
    const int directions[] = {-1, 1, 0};
    const CKDWORD colors[] = {0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff};
    const CKWORD rgb565[] = {0xf800, 0x07e0, 0x001f, 0xffff};
    const unsigned width = 17, height = 3;
    for (VX_PIXELFORMAT format : formats) {
        const bool bump = format == _16_L6V5U5;
        const VX_PIXELFORMAT target = format == _24_RGB888 || format == _16_RGB565 ? _32_ARGB8888 : format;
        for (int direction : directions) {
            CKTextureDesc desc;
            VxPixelFormat2ImageDesc(target, desc.Format);
            desc.Format.Width = width; desc.Format.Height = height;
            desc.MipMapCount = 1; desc.Flags = CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
            VxImageDescEx source;
            VxPixelFormat2ImageDesc(format, source);
            source.Width = width; source.Height = height;
            const unsigned bytes = source.BitsPerPixel / 8;
            const unsigned pitch = width * bytes + (direction ? 8 : 0);
            XArray<CKBYTE> storage;
            // Extra rows make wrong forward reads visible as canaries without invoking undefined reads.
            storage.Resize(pitch * (height * 2 - 1));
            source.BytesPerLine = direction * int(pitch);
            source.Image = storage.Begin() + (direction < 0 ? (height - 1) * pitch : 0);
            CKDWORD texture = 0;
            for (unsigned phase = 0; phase < 2; ++phase) {
                memset(storage.Begin(), 0x4d, storage.Size());
                for (unsigned y = 0; y < height; ++y) {
                    CKBYTE *row = storage.Begin() + (direction < 0 ? height - 1 - y : y) * pitch;
                    for (unsigned x = 0; x < width; ++x) {
                        const unsigned index = (x + y + phase) % 4;
                        CKDWORD pixel = colors[index];
                        if (format == _16_RGB565) pixel = rgb565[index];
                        else if (format == _16_L6V5U5) pixel = (index * 21) << 10;
                        else if (format == _32_ABGR8888)
                            pixel = (pixel & 0xff00ff00) | ((pixel & 0xff) << 16) | ((pixel >> 16) & 0xff);
                        memcpy(row + x * bytes, &pixel, bytes);
                    }
                }
                if (phase == 0) {
                    if (!TextureUploadResult(backend.CreateTexture(&desc, &source, &texture), "create signed rows")) return false;
                } else if (!TextureUploadResult(backend.UpdateTexture(texture, 0, 0, NULL, &source), "update signed rows")) return false;
                XArray<CKBYTE> pixels;
                TextureUploadReadback capture(backend);
                if (!capture.Capture(texture, target, 0, width, height, pixels)) return false;
                for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
                    const unsigned index = (x + y + phase) % 4;
                    const CKDWORD expected = bump ? 0xff808000 | (index * 85) : colors[index];
                    CKDWORD actual;
                    memcpy(&actual, pixels.Begin() + (y * width + x) * 4, sizeof(actual));
                    if (actual != expected) {
                        fprintf(stderr, "signed rows format=%d direction=%d phase=%u pixel=%u,%u: %08x expected %08x\n",
                                int(format), direction, phase, x, y, unsigned(actual), unsigned(expected));
                        return false;
                    }
                }
            }
            backend.DestroyObject(texture, CKRST_OBJ_TEXTURE);
        }
    }
    puts("SDL_gpu signed/zero/padded pitches preserve every row through copy, conversion and bump uploads");
    return true;
}
