#include "CKBgfxRasterizer.h"
#include "CKBgfxInternal.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <mutex>

static bool WriteBmp32(const char *path, uint32_t width, uint32_t height,
                       uint32_t pitch, const void *data, bool yflip)
{
    if (!path || !data || width == 0 || height == 0 || pitch < width * 4)
        return false;

    FILE *f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || !f)
        return false;

    const uint32_t outPitch = width * 4;
    const uint32_t imageSize = outPitch * height;
    const uint32_t fileSize = 14 + 40 + imageSize;

    unsigned char fileHeader[14] = {
        'B', 'M',
        (unsigned char)(fileSize), (unsigned char)(fileSize >> 8),
        (unsigned char)(fileSize >> 16), (unsigned char)(fileSize >> 24),
        0, 0, 0, 0,
        54, 0, 0, 0
    };
    unsigned char infoHeader[40] = {
        40, 0, 0, 0,
        (unsigned char)(width), (unsigned char)(width >> 8),
        (unsigned char)(width >> 16), (unsigned char)(width >> 24),
        (unsigned char)(height), (unsigned char)(height >> 8),
        (unsigned char)(height >> 16), (unsigned char)(height >> 24),
        1, 0,
        32, 0,
        0, 0, 0, 0,
        (unsigned char)(imageSize), (unsigned char)(imageSize >> 8),
        (unsigned char)(imageSize >> 16), (unsigned char)(imageSize >> 24),
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0
    };

    fwrite(fileHeader, 1, sizeof(fileHeader), f);
    fwrite(infoHeader, 1, sizeof(infoHeader), f);

    const unsigned char *src = (const unsigned char *)data;
    for (uint32_t outY = 0; outY < height; ++outY) {
        uint32_t srcY = yflip ? outY : (height - 1 - outY);
        fwrite(src + srcY * pitch, 1, outPitch, f);
    }

    fclose(f);
    return true;
}

void CKBgfxCallback::fatal(const char *filePath, uint16_t line, bgfx::Fatal::Enum code, const char *str)
{
    CKBgfxLogf("Fatal", "code=%d at %s:%u: %s",
               (int)code, filePath ? filePath : "?", (unsigned)line, str ? str : "");
}

void CKBgfxCallback::traceVargs(const char *filePath, uint16_t line, const char *format, va_list argList)
{
    if (!CKBgfxLogEnabled("Trace", false))
        return;

    char buf[2048];
    int n = _snprintf_s(buf, sizeof(buf) - 1, _TRUNCATE, "%s(%u): ",
                        filePath ? filePath : "?", (unsigned)line);
    if (n < 0)
        n = 0;
    _vsnprintf_s(buf + n, sizeof(buf) - n, _TRUNCATE, format, argList);
    CKBgfxLogf("Trace", "%s", buf);
}

void CKBgfxCallback::screenShot(const char *_filePath, uint32_t _width, uint32_t _height,
                                uint32_t _pitch, bgfx::TextureFormat::Enum,
                                const void *_data, uint32_t _size, bool _yflip)
{
    if (!m_Context)
        return;

    if (_filePath && strcmp(_filePath, "__backbuffer_read__") == 0) {
        VxImageDescEx *target = m_Context->m_BackbufferReadTarget;
        if (target && target->Image) {
            uint32_t dstSize = (uint32_t)(target->BytesPerLine * target->Height);
            if (dstSize == 0)
                dstSize = (uint32_t)(target->Width * (target->BitsPerPixel / 8) * target->Height);
            uint32_t copySize = (dstSize > 0 && _size > dstSize) ? dstSize : _size;
            memcpy(target->Image, _data, copySize);
            target->Width = (int)_width;
            target->Height = (int)_height;
            target->BytesPerLine = (int)_pitch;
            target->BitsPerPixel = (int)(_pitch / _width * 8);
        }
        m_Context->m_BackbufferReadReady.store(true, std::memory_order_release);
        return;
    }

    std::lock_guard<std::mutex> lock(m_Context->m_ScreenShotMutex);
    if (m_Context->m_PendingScreenShotCallback) {
        m_Context->m_PendingScreenShotCallback(
            m_Context->m_PendingScreenShotFB,
            (CKDWORD)_width, (CKDWORD)_height, (CKDWORD)_pitch,
            _data, (CKDWORD)_size, _yflip ? TRUE : FALSE);
        m_Context->m_PendingScreenShotCallback = NULL;
        return;
    }

    if (_filePath && strstr(_filePath, ".bmp") != NULL) {
        bool ok = WriteBmp32(_filePath, _width, _height, _pitch, _data, _yflip);
        CKBgfxLogf("Capture", "saved path=%s ok=%d size=%ux%u pitch=%u bytes=%u yflip=%d",
                   _filePath, ok ? 1 : 0, (unsigned)_width, (unsigned)_height,
                   (unsigned)_pitch, (unsigned)_size, _yflip ? 1 : 0);
    }
}
