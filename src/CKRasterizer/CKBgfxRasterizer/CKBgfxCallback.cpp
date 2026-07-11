#include "CKBgfxRasterizer.h"
#include "CKBgfxInternal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif
static int fopen_s(FILE **file, const char *path, const char *mode)
{
    *file = fopen(path, mode);
    return *file ? 0 : 1;
}

static int _vsnprintf_s(char *buffer, size_t size, size_t, const char *format, va_list args)
{
    if (!buffer || size == 0)
        return 0;
    int result = vsnprintf(buffer, size, format, args);
    if (result < 0) {
        buffer[0] = '\0';
        return 0;
    }
    if ((size_t)result >= size) {
        buffer[size - 1] = '\0';
        return (int)(size - 1);
    }
    return result;
}

static int _snprintf_s(char *buffer, size_t size, size_t truncate, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int result = _vsnprintf_s(buffer, size, truncate, format, args);
    va_end(args);
    return result;
}
#endif

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
    if (m_Context) {
        m_Context->m_DebugFatalCount.fetch_add(1, std::memory_order_relaxed);
        m_Context->LatchFatalError(CKERR_INVALIDRENDERCONTEXT);
    }
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
                                uint32_t _pitch, bgfx::TextureFormat::Enum _format,
                                const void *_data, uint32_t _size, bool _yflip)
{
    if (!m_Context)
        return;

    CKBgfxScreenShotRequest request = {};
    bool foundRequest = false;
    {
        VxMutexLock lock(m_Context->m_ScreenShotMutex);
        for (int i = 0; i < m_Context->m_PendingScreenShots.Size(); ++i) {
            if (_filePath &&
                m_Context->m_PendingScreenShots[i].Path.Compare(_filePath) == 0) {
                m_Context->m_PendingScreenShots.RemoveAt((unsigned int)i, request);
                foundRequest = true;
                break;
            }
        }
    }
    if (foundRequest && request.Callback) {
        VX_PIXELFORMAT format = UNKNOWN_PF;
        CKBgfxTryPixelFormat(_format, format);
        request.Callback(
            request.UserData, request.FrameBuffer,
            (CKDWORD)_width, (CKDWORD)_height, (CKDWORD)_pitch,
            format, _data, (CKDWORD)_size, _yflip ? TRUE : FALSE);
        return;
    }

    if (_filePath && strstr(_filePath, ".bmp") != NULL) {
        bool ok = WriteBmp32(_filePath, _width, _height, _pitch, _data, _yflip);
        CKBgfxLogf("Capture", "saved path=%s ok=%d size=%ux%u pitch=%u bytes=%u yflip=%d",
                   _filePath, ok ? 1 : 0, (unsigned)_width, (unsigned)_height,
                   (unsigned)_pitch, (unsigned)_size, _yflip ? 1 : 0);
    }
}

void CKBgfxCallback::captureBegin(uint32_t _width, uint32_t _height,
                                  uint32_t _pitch,
                                  bgfx::TextureFormat::Enum _format,
                                  bool _yflip)
{
    if (!m_Context)
        return;
    VxMutexLock lock(m_Context->m_ScreenShotMutex);
    m_Context->m_CaptureWidth = (CKDWORD)_width;
    m_Context->m_CaptureHeight = (CKDWORD)_height;
    m_Context->m_CapturePitch = (CKDWORD)_pitch;
    m_Context->m_CaptureFormat = _format;
    m_Context->m_CaptureYFlip = _yflip ? TRUE : FALSE;
}

void CKBgfxCallback::captureEnd()
{
    if (!m_Context)
        return;
    VxMutexLock lock(m_Context->m_ScreenShotMutex);
    m_Context->m_CaptureWidth = 0;
    m_Context->m_CaptureHeight = 0;
    m_Context->m_CapturePitch = 0;
    m_Context->m_CaptureFormat = bgfx::TextureFormat::Count;
    m_Context->m_CaptureYFlip = FALSE;
}

void CKBgfxCallback::captureFrame(const void *_data, uint32_t _size)
{
    if (!m_Context || !_data || _size == 0)
        return;

    XArray<CKBgfxScreenShotRequest> completed;
    CKDWORD width = 0;
    CKDWORD height = 0;
    CKDWORD pitch = 0;
    bgfx::TextureFormat::Enum nativeFormat = bgfx::TextureFormat::Count;
    CKBOOL yFlip = FALSE;
    {
        VxMutexLock lock(m_Context->m_ScreenShotMutex);
        width = m_Context->m_CaptureWidth;
        height = m_Context->m_CaptureHeight;
        pitch = m_Context->m_CapturePitch;
        nativeFormat = m_Context->m_CaptureFormat;
        yFlip = m_Context->m_CaptureYFlip;
        for (int i = m_Context->m_PendingScreenShots.Size() - 1; i >= 0; --i) {
            if (!m_Context->m_PendingScreenShots[i].UseCaptureFrame)
                continue;
            CKBgfxScreenShotRequest request = {};
            m_Context->m_PendingScreenShots.RemoveAt(
                (unsigned int)i, request);
            completed.PushBack(request);
        }
    }

    VX_PIXELFORMAT format = UNKNOWN_PF;
    CKBgfxTryPixelFormat(nativeFormat, format);
    for (int i = 0; i < completed.Size(); ++i) {
        CKBgfxScreenShotRequest &request = completed[i];
        if (request.Callback) {
            request.Callback(request.UserData, request.FrameBuffer,
                             width, height, pitch, format,
                             _data, (CKDWORD)_size, yFlip);
        }
    }
}
