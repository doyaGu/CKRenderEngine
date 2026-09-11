#include "CKSdlGpuBackend.h"
#include "CKSdlGpuShaders.h"

#include "CKFFRasterizerContext.h"
#include "CKRasterizerCapsBaseline.h"
#include "CKRasterizerDriverCaps.h"

#include <new>

namespace {

class SdlDriver final : public CKRasterizerDriver {
public:
    SdlDriver(CKRasterizer *owner, CKDWORD index)
    {
        m_Owner = owner;
        m_DriverIndex = index;
        m_Desc = "SDL_gpu Driver";
        CKRSTInitializeDriverCaps(this);
        BuildShaderLibrary();
    }

    ~SdlDriver() override
    {
        while (m_Contexts.Size() > 0) {
            const int index = m_Contexts.Size() - 1;
            CKRasterizerContext *context = m_Contexts[index];
            CKSdlGpuBackend *backend = m_Backends[index];
            context->BeginShutdown();
            CKFFDeleteRasterizerContext(context);
            delete backend;
            m_Contexts.PopBack();
            m_Backends.PopBack();
        }
    }

    CKRasterizerContext *CreateContext() override
    {
        if (m_Shaders.Empty())
            return NULL;

        CKSdlGpuBackend *backend = new (std::nothrow) CKSdlGpuBackend();
        if (!backend)
            return NULL;

        CKFFRasterizerContextDesc desc;
        desc.Driver = this;
        desc.Backend = backend;
        desc.Shaders = &m_Shaders;
        desc.BackendReady = OnBackendReady;
        desc.BackendReadyUser = this;

        CKRasterizerContext *context = CKFFCreateRasterizerContext(desc);
        if (!context) {
            delete backend;
            return NULL;
        }

        m_Backends.PushBack(backend);
        m_Contexts.PushBack(context);
        return context;
    }

    CKBOOL DestroyContext(CKRasterizerContext *context) override
    {
        if (!context)
            return FALSE;

        for (int index = 0; index < m_Contexts.Size(); ++index) {
            if (m_Contexts[index] != context)
                continue;

            CKSdlGpuBackend *backend = m_Backends[index];
            if (!context->BeginShutdown() || !backend->IsIdle())
                return FALSE;

            CKFFDeleteRasterizerContext(context);
            delete backend;
            m_Contexts.RemoveAt(index);
            m_Backends.RemoveAt(index);
            return TRUE;
        }
        return FALSE;
    }

private:
    static void OnBackendReady(void *user, CKRasterizerBackend *backend)
    {
        SdlDriver *driver = static_cast<SdlDriver *>(user);
        if (driver && backend)
            driver->RefreshCaps(*static_cast<CKSdlGpuBackend *>(backend));
    }

    void BuildShaderLibrary()
    {
        const SDL_GPUShaderFormat formats[] = {
            SDL_GPU_SHADERFORMAT_DXIL,
            SDL_GPU_SHADERFORMAT_SPIRV,
        };
        for (size_t index = 0; index < sizeof(formats) / sizeof(formats[0]); ++index) {
            CKBackendShaderSet shaderSet;
            if (CKSdlGpuShaderSet(formats[index], shaderSet))
                m_Shaders.Add(shaderSet);
        }
    }

    void RefreshCaps(CKSdlGpuBackend &backend)
    {
        if (backend.GetDeviceStatus() != CK_OK)
            return;

        const CKBackendCaps &caps = backend.GetCaps();
        Vx3DCapsDesc limits = {};
        limits.MaxTextureWidth = caps.MaxTextureSize;
        limits.MaxTextureHeight = caps.MaxTextureSize;
        limits.MaxTextureRatio = caps.MaxTextureSize;
        limits.MaxNumberTextureStage = CKRST_MAX_TEXTURE_STAGES;
        limits.MaxNumberBlendStage = CKRST_MAX_TEXTURE_STAGES;
        CKRSTLowerCapsToLimits(&m_3DCaps, &limits);

        m_TextureFormats.Clear();
        for (int value = _32_ARGB8888; value <= _32_X8L8V8U8; ++value) {
            const VX_PIXELFORMAT format = static_cast<VX_PIXELFORMAT>(value);
            if (!backend.SupportsTexture2D(format))
                continue;

            CKTextureDesc desc;
            VxPixelFormat2ImageDesc(format, desc.Format);
            desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB;
            if (desc.Format.AlphaMask || format == _DXT1 || format == _DXT3 || format == _DXT5)
                desc.Flags |= CKRST_TEXTURE_ALPHA;
            m_TextureFormats.PushBack(desc);
        }
        m_CapsUpToDate = TRUE;
    }

    CKFFShaderLibrary m_Shaders;
    XArray<CKSdlGpuBackend *> m_Backends;
};

class SdlRasterizer final : public CKRasterizer {
public:
    ~SdlRasterizer() override
    {
        Close();
    }

    CKBOOL Start(WIN_HANDLE window) override
    {
        if (m_Drivers.Size() > 0)
            return TRUE;

        m_MainWindow = window;
        SdlDriver *driver = new (std::nothrow) SdlDriver(this, 0);
        if (!driver)
            return FALSE;
        m_Drivers.PushBack(driver);
        return TRUE;
    }

    void Close() override
    {
        for (int index = 0; index < m_Drivers.Size(); ++index)
            delete m_Drivers[index];
        m_Drivers.Clear();
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
