#include "CKSdlGpuInternal.h"

CKSdlGpuBackend::CKSdlGpuBackend() : m(new CKSdlGpuDevice) {}
CKSdlGpuBackend::~CKSdlGpuBackend() { Shutdown(); }

CKERROR CKSdlGpuDevice::Fail(const char *operation)
{
    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "SDL_gpu %s failed: %s", operation, SDL_GetError());
    Error = CKERR_INVALIDOPERATION;
    // Consumers fail immediately; keep GPU-owned transfer storage until its
    // fence completes or shutdown has retired the device.
    for (auto &ticket : Readbacks) ticket->Error = Error;
    return Error;
}

bool CKSdlGpuDevice::EnsureCommands()
{
    if (!Ready()) return false;
    if (!Commands) Commands = SDL_AcquireGPUCommandBuffer(Device);
    if (!Commands) { Fail("AcquireGPUCommandBuffer"); return false; }
    return true;
}

static SDL_Window *FindWindow(WIN_HANDLE handle)
{
    int count = 0;
    SDL_Window **windows = SDL_GetWindows(&count);
    SDL_Window *result = nullptr;
    for (int i = 0; windows && i < count; ++i) {
        if (windows[i] == handle) result = windows[i];
#ifdef _WIN32
        if (SDL_GetPointerProperty(SDL_GetWindowProperties(windows[i]),
                SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr) == handle) result = windows[i];
#endif
    }
    SDL_free(windows);
    return result;
}

CKERROR CKSdlGpuBackend::Init(const CKBackendInitDesc *desc)
{
    if (!desc || !desc->Window || desc->Width <= 0 || desc->Height <= 0) return CKERR_INVALIDPARAMETER;
    if (m->Device) return CKERR_INVALIDOPERATION;
    if (!SDL_IsMainThread()) return CKERR_INVALIDOPERATION;
    m->Thread = SDL_GetCurrentThreadID();
    m->Error = CK_OK;
    // Retain resource-table generations across shutdown, but start the new
    // device with fresh bindings, constants and submission/presentation state.
    m->SamplerBindings = {};
    m->Uniforms.Clear(); m->Bindings.Clear();
    m->Caps = CKBackendCaps();
    m->Pass = CKBackendPassDesc();
    m->Stats = {}; m->FrameStats = {};
    m->Submission = 0; m->DrawApproximations = 0;
    m->SwapWidth = m->SwapHeight = 0;
    m->PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    m->Window = FindWindow(desc->Window);
    if (!m->Window) { SDL_SetError("Rasterizer requires a Player-owned SDL_Window"); return m->Fail("Init.window"); }
    SDL_GPUShaderFormat allowedFormats = desc->ShaderTargets.empty() ?
        SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV : 0;
    for (const auto &target : desc->ShaderTargets) {
        if (target.Format == CKRST_SHADER_FORMAT_DXIL && target.Profile == CKRST_SHADER_PROFILE_DX12)
            allowedFormats |= SDL_GPU_SHADERFORMAT_DXIL;
        else if (target.Format == CKRST_SHADER_FORMAT_SPIRV && target.Profile == CKRST_SHADER_PROFILE_SPIRV)
            allowedFormats |= SDL_GPU_SHADERFORMAT_SPIRV;
    }
    if (!allowedFormats) {
        SDL_SetError("Requested shader targets contain neither DXIL/DX12 nor SPIR-V/SPIR-V");
        return m->Fail("Init.shaderTargets");
    }
    const char *driver = SDL_getenv("CKRE_SDL_GPU_DRIVER");
    if (driver && SDL_strcmp(driver, "auto") == 0) driver = nullptr;
#ifdef _WIN32
    if (!driver) driver = allowedFormats & SDL_GPU_SHADERFORMAT_DXIL ? "direct3d12" : "vulkan";
#endif
    const bool debug = SDL_getenv("CKRE_SDL_GPU_DEBUG") && SDL_strcmp(SDL_getenv("CKRE_SDL_GPU_DEBUG"), "0") != 0;
    m->Device = SDL_CreateGPUDevice(allowedFormats, debug, driver);
    if (!m->Device) return m->Fail("CreateGPUDevice");
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(m->Device) & allowedFormats;
    if (!formats) {
        SDL_SetError("Device and requested native shader targets have no common format");
        m->Fail("Init.shaders"); Shutdown(); return CKERR_INVALIDOPERATION;
    }
    m->ShaderFormat = formats & SDL_GPU_SHADERFORMAT_DXIL ? SDL_GPU_SHADERFORMAT_DXIL : SDL_GPU_SHADERFORMAT_SPIRV;
    if (!SDL_ClaimWindowForGPUDevice(m->Device, m->Window)) {
        m->Fail("ClaimWindowForGPUDevice"); m->Window = nullptr; Shutdown(); return CKERR_INVALIDOPERATION;
    }
    m->WindowClaimed = true;
    if (!SDL_SetGPUSwapchainParameters(m->Device, m->Window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                       SDL_GPU_PRESENTMODE_VSYNC)) {
        m->Fail("SetGPUSwapchainParameters"); Shutdown(); return CKERR_INVALIDOPERATION;
    }
    m->Width = unsigned(desc->Width);
    m->Height = unsigned(desc->Height);
    m->DebugFlags = desc->DebugFlags;
    m->Caps.ShaderFormat = m->ShaderFormat == SDL_GPU_SHADERFORMAT_DXIL ? CKRST_SHADER_FORMAT_DXIL : CKRST_SHADER_FORMAT_SPIRV;
    m->Caps.ShaderProfile = m->ShaderFormat == SDL_GPU_SHADERFORMAT_DXIL ? CKRST_SHADER_PROFILE_DX12 : CKRST_SHADER_PROFILE_SPIRV;
    m->Caps.RequiresIntermediateTarget = TRUE;
    m->Caps.MaxTextureSize = 16384;
    m->Caps.MaxTextureBindings = CKBACKEND_MAX_TEXTURE_SLOTS;
    m->Caps.MaxPasses = CKRST_MAX_PASSES;
    m->Caps.MaxMSAASamples = 1;
    for (unsigned samples : {2u, 4u, 8u}) {
        if (!SDL_GPUTextureSupportsSampleCount(m->Device, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
                                               CKSdlGpuSampleCount(samples))) continue;
        // SDL's D3D12 sample query uses the shader-view format for depth/stencil.
        // Verify the actual attachment allocation as well: its native DSV format
        // can support MSAA even when that query reports false.
        SDL_GPUTextureCreateInfo probe = {};
        probe.type = SDL_GPU_TEXTURETYPE_2D;
        probe.format = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
        probe.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        probe.width = probe.height = 8;
        probe.layer_count_or_depth = probe.num_levels = 1;
        probe.sample_count = CKSdlGpuSampleCount(samples);
        auto *attachment = SDL_CreateGPUTexture(m->Device, &probe);
        if (attachment) {
            m->Caps.MaxMSAASamples = samples;
            SDL_ReleaseGPUTexture(m->Device, attachment);
        }
    }
    m->Caps.Features = CKRST_DEVCAPS_VERTEX_SHADER | CKRST_DEVCAPS_PIXEL_SHADER | CKRST_DEVCAPS_PASSES |
        CKRST_DEVCAPS_FRAMEBUFFER | CKRST_DEVCAPS_TRANSIENT_BUFFERS | CKRST_DEVCAPS_SCISSOR |
        CKRST_DEVCAPS_TEXTURE_READBACK | CKRST_DEVCAPS_BUFFER_UPDATE | CKRST_DEVCAPS_TEXTURE_UPDATE |
        CKRST_DEVCAPS_DEPTH_TEXTURE | CKRST_DEVCAPS_BLEND_EQUATION | CKRST_DEVCAPS_BLIT |
        CKRST_DEVCAPS_INDEX32 | CKRST_DEVCAPS_TEXTURE_CUBE | CKRST_DEVCAPS_TEXTURE_3D |
        CKRST_DEVCAPS_VERTEX_ATTRIB_HALF | CKRST_DEVCAPS_STENCIL_WRITE_MASK;

    // Only backend-private image operations are created here. Shader-family
    // completeness and FFP variants are the caller's initialization policy.
    for (bool volume : {false, true}) {
        CKShaderDesc vertex, fragment;
        CKDWORD vs = 0, fs = 0, program = 0;
        const bool available = volume ? CKSdlGpuNativeVolumeShaders(m->ShaderFormat, vertex, fragment) :
                                        CKSdlGpuNativeClearShaders(m->ShaderFormat, vertex, fragment);
        if (!available || CreateShader(&vertex, &vs) != CK_OK || CreateShader(&fragment, &fs) != CK_OK) {
            Shutdown(); return CKERR_INVALIDOPERATION;
        }
        const auto programDesc = CKSdlGpuNativeProgram(vs, fs, volume);
        if (CreateProgram(&programDesc, &program) != CK_OK) { Shutdown(); return CKERR_INVALIDOPERATION; }
        if (volume) m->VolumeMipProgram = m->Programs.Get(program);
        else m->ClearProgram = m->Programs.Get(program);
        DestroyObject(program, CKRST_OBJ_PROGRAM);
        DestroyObject(vs, CKRST_OBJ_SHADER);
        DestroyObject(fs, CKRST_OBJ_SHADER);
    }
    SDL_Log("SDL_gpu ready: driver=%s format=0x%x window=%u thread=%llu size=%ux%u gpu=%s",
        SDL_GetGPUDeviceDriver(m->Device), unsigned(m->ShaderFormat), SDL_GetWindowID(m->Window),
        static_cast<unsigned long long>(m->Thread), m->Width, m->Height,
        SDL_GetStringProperty(SDL_GetGPUDeviceProperties(m->Device), SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown"));
    return CK_OK;
}

void CKSdlGpuDevice::Collect()
{
    for (auto it = Readbacks.begin(); it != Readbacks.end();) {
        auto &ticket = *it;
        if (!ticket->Fence || !SDL_QueryGPUFence(Device, ticket->Fence.get())) { ++it; continue; }
        const void *bytes = SDL_MapGPUTransferBuffer(Device, ticket->Transfer.get(), false);
        if (!bytes) ticket->Error = CKERR_INVALIDOPERATION;
        else {
            std::memcpy(ticket->Data.data(), bytes, ticket->Data.size());
            SDL_UnmapGPUTransferBuffer(Device, ticket->Transfer.get());
        }
        ticket->Transfer.reset();
        ticket->Fence.reset();
        ticket->Complete = TRUE;
        it = Readbacks.erase(it);
    }
    while (!Submissions.empty() && SDL_QueryGPUFence(Device, Submissions.front().Fence.get())) {
        for (auto &page : Submissions.front().Geometry) {
            // Bound idle retention after unusually large frames. Active pages
            // remain owned by their submission irrespective of this cache cap.
            const uint64_t bytes = uint64_t(page->Capacity) * 2;
            if (bytes <= 16u * 1024u * 1024u - FreeGeometryBytes) {
                FreeGeometryBytes += size_t(bytes);
                FreeGeometry.push_back(std::move(page));
            }
        }
        Submissions.pop_front();
    }
}

void CKSdlGpuBackend::Shutdown()
{
    if (!m->Device) return;
    m->Draws.clear();
    m->Bindings.Clear();
    if (m->Commands) {
        // SDL forbids cancelling after swapchain acquisition.
        if (m->Swapchain) SDL_SubmitGPUCommandBuffer(m->Commands);
        else SDL_CancelGPUCommandBuffer(m->Commands);
        m->Commands = nullptr;
    }
    SDL_WaitForGPUIdle(m->Device);
    for (auto &ticket : m->Readbacks) {
        ticket->Error = CKERR_INVALIDOPERATION;
        ticket->Transfer.reset(); ticket->Fence.reset();
    }
    m->Readbacks.clear(); m->Submissions.clear();
    m->PendingGeometry.clear(); m->FreeGeometry.clear(); m->FreeGeometryBytes = 0;
    m->BatchVertices.clear(); m->BatchIndices.clear();
    m->SamplerBindings = {};
    m->Samplers.clear();
    m->ClearProgram.reset(); m->VolumeMipProgram.reset();
    m->DefaultVertexBuffers.clear(); m->DefaultTextures.clear();
    m->Uniforms.Clear();
    m->Target.reset(); m->Targets.Clear(); m->Programs.Clear(); m->ShaderObjects.Clear();
    m->VertexBuffers.Clear(); m->IndexBuffers.Clear(); m->Layouts.Clear(); m->Textures.Clear();
    if (m->WindowClaimed) SDL_ReleaseWindowFromGPUDevice(m->Device, m->Window);
    SDL_DestroyGPUDevice(m->Device);
    m->WindowClaimed = false; m->Device = nullptr; m->Window = nullptr; m->Swapchain = nullptr; m->PassOpen = false;
    m->TransientVertices.clear(); m->TransientIndices.clear();
    m->TransientVertexInfo.clear(); m->TransientIndexInfo.clear();
}

CKERROR CKSdlGpuBackend::Resize(int x, int y, int width, int height)
{
    if (!m->Ready() || m->PassOpen || !m->Draws.empty()) return CKERR_INVALIDOPERATION;
    if (width <= 0 || height <= 0 || unsigned(width) > m->Caps.MaxTextureSize || unsigned(height) > m->Caps.MaxTextureSize)
        return CKERR_INVALIDPARAMETER;
    m->Width = unsigned(width); m->Height = unsigned(height);
    m->Collect();
    return CK_OK;
}

CKERROR CKSdlGpuBackend::GetDeviceStatus() const { return m->Device ? m->Error : CKERR_INVALIDOPERATION; }
const CKBackendCaps &CKSdlGpuBackend::GetCaps() const { return m->Caps; }
CKBOOL CKSdlGpuBackend::IsIdle() const { return !m->PassOpen && m->Draws.empty(); }
void CKSdlGpuBackend::SetDebugFlags(CKDWORD flags) { m->DebugFlags = flags; }
const CKBackendStats &CKSdlGpuBackend::GetStats() const { return m->Stats; }
uint64_t CKSdlGpuBackend::GetDrawApproximationMask() const { return m->DrawApproximations; }

CKBackendReadbackState CKSdlGpuBackend::PollReadback(const CKBackendReadbackTicket &base, CKBOOL wait)
{
    auto ticket = std::dynamic_pointer_cast<CKSdlGpuReadback>(base);
    if (!ticket || !m->Ready() || ticket->Error != CK_OK) return CKRST_READBACK_FAILED;
    if (ticket->Complete) return CKRST_READBACK_READY;
    if (!ticket->Fence) return CKRST_READBACK_NEEDS_SUBMIT;
    if (wait) {
        SDL_GPUFence *fence = ticket->Fence.get();
        if (!SDL_WaitForGPUFences(m->Device, true, &fence, 1)) {
            m->Fail("WaitForGPUFences"); return CKRST_READBACK_FAILED;
        }
    }
    m->Collect();
    if (ticket->Error != CK_OK) return CKRST_READBACK_FAILED;
    return ticket->Complete ? CKRST_READBACK_READY : CKRST_READBACK_PENDING;
}

CKBOOL CKSdlGpuBackend::SupportsTexture2D(VX_PIXELFORMAT format) const
{
    return m->Ready() && format >= _32_ARGB8888 && format <= _32_X8L8V8U8 &&
        SDL_GPUTextureSupportsFormat(m->Device, CKSdlGpuTextureFormat(format), SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER);
}
