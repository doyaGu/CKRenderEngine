#include "CKSdlGpuBackend.h"
#include "CKSdlGpuShaders.h"
#include "CKRasterizerPlugin.h"
#include "CKTranslatedRasterizer.h"
#include "CKRasterizerCapsBaseline.h"
#include <algorithm>

namespace {
class SdlDriver final : public CKRasterizerBackendDriver {
    std::vector<CKRasterizerBackend *> Backends;
public:
    SdlDriver() { m_Desc = "SDL_gpu Driver"; InitializeDisplayCaps(); }
    ~SdlDriver() override { for (auto *backend : Backends) delete backend; }
    void GetShaderTargets(std::vector<CKBackendShaderTarget> &out) const override {
        out.clear();
        const CKBackendShaderTarget targets[] = {
            {CKRST_SHADER_FORMAT_DXIL, CKRST_SHADER_PROFILE_DX12},
            {CKRST_SHADER_FORMAT_SPIRV, CKRST_SHADER_PROFILE_SPIRV},
        };
        for (const auto &target : targets) {
            CKBackendCaps caps;
            caps.ShaderFormat = target.Format;
            caps.ShaderProfile = target.Profile;
            CKBackendShaderSet shaders;
            if (GetShaderSet(caps, shaders))
                out.push_back(target);
        }
    }
    CKBOOL GetShaderSet(const CKBackendCaps &caps, CKBackendShaderSet &out) const override {
        out = CKBackendShaderSet();
        const SDL_GPUShaderFormat format = caps.ShaderFormat == CKRST_SHADER_FORMAT_DXIL ?
            SDL_GPU_SHADERFORMAT_DXIL : caps.ShaderFormat == CKRST_SHADER_FORMAT_SPIRV ?
            SDL_GPU_SHADERFORMAT_SPIRV : 0;
        return format && CKSdlGpuShaderSet(format, out) &&
            out.Matches(caps.ShaderFormat, caps.ShaderProfile) ? TRUE : FALSE;
    }
    CKRasterizerBackend *CreateBackend() override {
        auto *backend = new CKSdlGpuBackend;
        Backends.push_back(backend);
        return backend;
    }
    void RefreshCaps() override {
        for (auto *entry : Backends) {
            auto *backend = static_cast<CKSdlGpuBackend *>(entry);
            if (backend->GetDeviceStatus() != CK_OK) continue;
            const auto &caps = backend->GetCaps();
            Vx3DCapsDesc limits = {};
            limits.MaxTextureWidth = limits.MaxTextureHeight = limits.MaxTextureRatio = caps.MaxTextureSize;
            limits.MaxNumberTextureStage = limits.MaxNumberBlendStage = CKRST_MAX_TEXTURE_STAGES;
            CKRSTLowerCapsToLimits(&m_3DCaps, &limits);
            m_TextureFormats.Clear();
            for (int value = _32_ARGB8888; value <= _32_X8L8V8U8; ++value) {
                const auto format = static_cast<VX_PIXELFORMAT>(value);
                if (!backend->SupportsTexture2D(format)) continue;
                CKTextureDesc desc;
                VxPixelFormat2ImageDesc(format, desc.Format);
                desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB;
                if (desc.Format.AlphaMask || format == _DXT1 || format == _DXT3 || format == _DXT5)
                    desc.Flags |= CKRST_TEXTURE_ALPHA;
                m_TextureFormats.PushBack(desc);
            }
            m_CapsUpToDate = TRUE;
            break;
        }
    }
    CKBOOL DestroyBackend(CKRasterizerBackend *backend) override {
        const auto found = std::find(Backends.begin(), Backends.end(), backend);
        if (found == Backends.end() || !backend->IsIdle()) return FALSE;
        delete backend; Backends.erase(found);
        return TRUE;
    }
};
class SdlLibrary final : public CKRasterizerBackendLibrary {
    std::unique_ptr<SdlDriver> Driver;
    WIN_HANDLE Window = nullptr;
public:
    CKBOOL Start(WIN_HANDLE window) override { Window = window; Driver.reset(new SdlDriver); return TRUE; }
    void Close() override { Driver.reset(); }
    int GetDriverCount() const override { return Driver ? 1 : 0; }
    CKRasterizerBackendDriver *GetDriver(CKDWORD index) const override { return index == 0 ? Driver.get() : nullptr; }
    WIN_HANDLE GetMainWindow() const override { return Window; }
};
void CloseLibrary(CKRasterizerBackendLibrary *library) { delete library; }
CKRasterizer *StartRasterizer(WIN_HANDLE window)
{
    auto *library = new SdlLibrary;
    if (!library->Start(window)) { delete library; return nullptr; }
    return CKTranslatedRasterizerStart(library, CloseLibrary);
}
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
    if (!info) return;
    info->Desc = "SDL_gpu Rasterizer";
    info->StartFct = StartRasterizer;
    info->CloseFct = CKTranslatedRasterizerClose;
    info->InterfaceRevision = CKRST_INTERFACE_REVISION;
}
