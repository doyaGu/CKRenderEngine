#include "CKSdlGpuRasterizerContext.h"
#include "CKSdlGpuShaders.h"

#include "CKRasterizerCapsBaseline.h"
#include "CKRasterizerDriverCaps.h"

#include <new>

namespace {

class SdlDriver final : public CKRasterizerDriver {
public:
    SdlDriver(CKRasterizer *owner, CKDWORD index)
        : CKRasterizerDriver(owner, index, "SDL_gpu Driver", TRUE)
    {
        CKRSTInitializeDriverCaps(
            m_DisplayModes, m_TextureFormats, m_NativeCaps);
    }

    ~SdlDriver() override
    {
        DestroyContexts();
    }

    CKRasterizerContext *CreateContext() override
    {
        CKSdlGpuRasterizerContext *context = new (std::nothrow)
            CKSdlGpuRasterizerContext(this, OnContextReady, this);
        if (!context)
            return NULL;

        AddContext(context);
        return context;
    }

private:
    static void OnContextReady(void *user, CKSdlGpuRasterizerContext &context)
    {
        SdlDriver *driver = static_cast<SdlDriver *>(user);
        if (driver)
            driver->RefreshCaps(context);
    }

    void RefreshCaps(CKSdlGpuRasterizerContext &context)
    {
        if (context.GetDeviceStatus() != CK_OK)
            return;

        const CKRasterizerDeviceCaps &caps = context.GetCaps();
        CKFFUpdateDriverCaps(caps, m_NativeCaps);

        m_TextureFormats.Clear();
        for (int value = _32_ARGB8888; value <= _32_X8L8V8U8; ++value) {
            const VX_PIXELFORMAT format = static_cast<VX_PIXELFORMAT>(value);
            if (!context.SupportsTexture2D(format))
                continue;

            CKTextureDesc desc;
            VxPixelFormat2ImageDesc(format, desc.Format);
            desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB;
            if (desc.Format.AlphaMask || (format >= _DXT1 && format <= _DXT5))
                desc.Flags |= CKRST_TEXTURE_ALPHA;
            m_TextureFormats.PushBack(desc);
            if (format >= _DXT1 && format <= _DXT5)
                m_NativeCaps.Features |= CKRST_CAPS_TEXTURE_DXT;
        }
        m_CapsFinal = TRUE;
    }
};

class SdlRasterizer final : public CKRasterizer {
public:
    CKBOOL Start(WIN_HANDLE window) override
    {
        if (GetDriverCount() != 0)
            return TRUE;

        CKRasterizer::Start(window);
        SdlDriver *driver = new (std::nothrow) SdlDriver(this, 0);
        if (!driver)
            return FALSE;
        AddDriver(driver);
        return TRUE;
    }
};

CKRasterizer *StartRasterizer(WIN_HANDLE window)
{
    SdlRasterizer *rasterizer = new (std::nothrow) SdlRasterizer();
    if (!rasterizer)
        return NULL;
    if (!rasterizer->Start(window)) {
        delete rasterizer;
        return NULL;
    }
    return rasterizer;
}
void CloseRasterizer(CKRasterizer *rasterizer)
{
    delete rasterizer;
}

} // namespace

extern "C" int CKSdlGpuQueryFFJitSnapshotV1(const CKRasterizerContext *context, uint32_t size,
                               CKSdlGpuFFJitSnapshotV1 *snapshot)
{
    if (!context || !snapshot || size != sizeof(*snapshot)) return 0;
    const auto *sdl = dynamic_cast<const CKSdlGpuRasterizerContext *>(context);
    return sdl && sdl->CopyFFJitSnapshot(*snapshot) ? 1 : 0;
}

#ifdef CK_LIB
void CKSdlGpuRasterizerGetInfo(CKRasterizerInfo *info)
#else
#ifdef _WIN32
extern "C" __declspec(dllexport) void CKRasterizerGetInfo(CKRasterizerInfo *info)
#else
extern "C" __attribute__((visibility("default"))) void CKRasterizerGetInfo(CKRasterizerInfo *info)
#endif
#endif
{
    if (!info)
        return;

    info->Desc = "SDL_gpu Rasterizer";
    info->StartFct = StartRasterizer;
    info->CloseFct = CloseRasterizer;
    info->InterfaceRevision = CKRST_INTERFACE_REVISION;
}
