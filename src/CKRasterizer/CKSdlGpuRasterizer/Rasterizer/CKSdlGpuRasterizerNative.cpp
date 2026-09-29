#include "CKSdlGpuRasterizerContext.h"

CKERROR CKSdlGpuRasterizerContext::Fail(const char *operation)
{
    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "SDL_gpu %s failed: %s", operation, SDL_GetError());
    Error = CKERR_INVALIDOPERATION;
    // Consumers fail immediately; keep GPU-owned transfer storage until its
    // fence completes or shutdown has retired the device.
    for (auto &ticket : Readbacks) ticket->Error = Error;
    return Error;
}

bool CKSdlGpuRasterizerContext::EnsureCommands()
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

CKERROR CKSdlGpuRasterizerContext::Init(const CKRasterizerInitParameters *desc)
{
    if (!desc || !desc->Window || desc->Width <= 0 || desc->Height <= 0) return CKERR_INVALIDPARAMETER;
    if (Device) return CKERR_INVALIDOPERATION;
    if (!SDL_IsMainThread()) return CKERR_INVALIDOPERATION;
    Thread = SDL_GetCurrentThreadID();
    Error = CK_OK;
    // Retain resource-table generations across shutdown, but start the new
    // device with fresh bindings, constants and submission/presentation state.
    for (int i = 0; i < CKFF_TEXTURE_SLOT_COUNT; ++i)
        SamplerBindings[i] = CKSdlGpuBinding();
    Uniforms.Clear(); Bindings.Clear(); DrawResources.Clear();
    Caps = CKRasterizerDeviceCaps();
    Stats = CKSdlGpuSubmissionStats();
    FrameStats = CKSdlGpuSubmissionStats();
    Pass = CKRenderPassDesc();
    Submission = 0; DrawApproximations = 0;
    SwapWidth = SwapHeight = 0;
    PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    Window = FindWindow(desc->Window);
    if (!Window) { SDL_SetError("Rasterizer requires a Player-owned SDL_Window"); return Fail("Init.window"); }
    SDL_GPUShaderFormat allowedFormats = desc->ShaderTargets.Size() == 0 ?
        SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV : 0;
    for (int i = 0; i < desc->ShaderTargets.Size(); ++i) {
        const CKFFShaderTarget &target = desc->ShaderTargets[i];
        if (target.Format == CKRST_SHADER_FORMAT_DXIL && target.Profile == CKRST_SHADER_PROFILE_DX12)
            allowedFormats |= SDL_GPU_SHADERFORMAT_DXIL;
        else if (target.Format == CKRST_SHADER_FORMAT_SPIRV && target.Profile == CKRST_SHADER_PROFILE_SPIRV)
            allowedFormats |= SDL_GPU_SHADERFORMAT_SPIRV;
    }
    if (!allowedFormats) {
        SDL_SetError("Requested shader targets contain neither DXIL/DX12 nor SPIR-V/SPIR-V");
        return Fail("Init.shaderTargets");
    }
    const char *driver = SDL_getenv("CKRE_SDL_GPU_DRIVER");
    if (driver && SDL_strcmp(driver, "auto") == 0) driver = nullptr;
#ifdef _WIN32
    if (!driver) driver = allowedFormats & SDL_GPU_SHADERFORMAT_DXIL ? "direct3d12" : "vulkan";
#endif
    const bool debug = SDL_getenv("CKRE_SDL_GPU_DEBUG") && SDL_strcmp(SDL_getenv("CKRE_SDL_GPU_DEBUG"), "0") != 0;
    Device = SDL_CreateGPUDevice(allowedFormats, debug, driver);
    if (!Device) return Fail("CreateGPUDevice");
    PresentCopySupported = CKSdlGpuSupportsSwapchainCopy(SDL_GetGPUDeviceDriver(Device));
    // Keep the SDL default depth explicit. A third queued frame improves peak
    // throughput on some systems, but increases presentation latency.
    if (!SDL_SetGPUAllowedFramesInFlight(Device, 2)) {
        Fail("SetGPUAllowedFramesInFlight.init"); Shutdown(); return CKERR_INVALIDOPERATION;
    }
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(Device) & allowedFormats;
    if (!formats) {
        SDL_SetError("Device and requested native shader targets have no common format");
        Fail("Init.shaders"); Shutdown(); return CKERR_INVALIDOPERATION;
    }
    ShaderFormat = formats & SDL_GPU_SHADERFORMAT_DXIL ? SDL_GPU_SHADERFORMAT_DXIL : SDL_GPU_SHADERFORMAT_SPIRV;
    if (!SDL_ClaimWindowForGPUDevice(Device, Window)) {
        Fail("ClaimWindowForGPUDevice"); Window = nullptr; Shutdown(); return CKERR_INVALIDOPERATION;
    }
    WindowClaimed = true;
    if (!SDL_SetGPUSwapchainParameters(Device, Window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                       SDL_GPU_PRESENTMODE_VSYNC)) {
        Fail("SetGPUSwapchainParameters"); Shutdown(); return CKERR_INVALIDOPERATION;
    }
    Width = unsigned(desc->Width);
    Height = unsigned(desc->Height);
    DebugFlags = desc->DebugFlags;
    Caps.ShaderFormat = ShaderFormat == SDL_GPU_SHADERFORMAT_DXIL ? CKRST_SHADER_FORMAT_DXIL : CKRST_SHADER_FORMAT_SPIRV;
    Caps.ShaderProfile = ShaderFormat == SDL_GPU_SHADERFORMAT_DXIL ? CKRST_SHADER_PROFILE_DX12 : CKRST_SHADER_PROFILE_SPIRV;
    Caps.RequiresIntermediateTarget = TRUE;
    Caps.MaxTextureSize = 16384;
    Caps.MaxTextureBindings = CKFF_TEXTURE_SLOT_COUNT;
    Caps.MaxPasses = CKRST_MAX_PASSES;
    Caps.MaxMSAASamples = 1;
    for (unsigned samples : {2u, 4u, 8u}) {
        if (!SDL_GPUTextureSupportsSampleCount(Device, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
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
        auto *attachment = SDL_CreateGPUTexture(Device, &probe);
        if (attachment) {
            Caps.MaxMSAASamples = samples;
            SDL_ReleaseGPUTexture(Device, attachment);
        }
    }
    Caps.Features = CKRST_DEVCAPS_VERTEX_SHADER | CKRST_DEVCAPS_PIXEL_SHADER | CKRST_DEVCAPS_PASSES |
        CKRST_DEVCAPS_FRAMEBUFFER | CKRST_DEVCAPS_TRANSIENT_BUFFERS | CKRST_DEVCAPS_SCISSOR |
        CKRST_DEVCAPS_TEXTURE_READBACK | CKRST_DEVCAPS_BUFFER_UPDATE | CKRST_DEVCAPS_TEXTURE_UPDATE |
        CKRST_DEVCAPS_DEPTH_TEXTURE | CKRST_DEVCAPS_BLEND_EQUATION | CKRST_DEVCAPS_BLIT |
        CKRST_DEVCAPS_INDEX32 | CKRST_DEVCAPS_TEXTURE_CUBE | CKRST_DEVCAPS_TEXTURE_3D |
        CKRST_DEVCAPS_VERTEX_ATTRIB_HALF | CKRST_DEVCAPS_STENCIL_WRITE_MASK;

    // Only context-private image operations are created here. Shader-family
    // completeness and FFP variants are the caller's initialization policy.
    for (CKSdlGpuNativeProgramKind kind :
         {CKSDL_NATIVE_CLEAR, CKSDL_NATIVE_VOLUME, CKSDL_NATIVE_DITHER}) {
        CKShaderDesc vertex, fragment;
        CKDWORD vs = 0, fs = 0, program = 0;
        const bool available = kind == CKSDL_NATIVE_VOLUME
            ? CKSdlGpuNativeVolumeShaders(ShaderFormat, vertex, fragment)
            : (kind == CKSDL_NATIVE_DITHER
                ? CKSdlGpuNativeDitherShaders(ShaderFormat, vertex, fragment)
                : CKSdlGpuNativeClearShaders(ShaderFormat, vertex, fragment));
        if (!available || CreateShader(&vertex, &vs) != CK_OK || CreateShader(&fragment, &fs) != CK_OK) {
            Shutdown(); return CKERR_INVALIDOPERATION;
        }
        const auto programDesc = CKSdlGpuNativeProgram(vs, fs, kind);
        if (CreateProgram(&programDesc, &program) != CK_OK) { Shutdown(); return CKERR_INVALIDOPERATION; }
        if (kind == CKSDL_NATIVE_VOLUME) VolumeMipProgram = Programs.Get(program);
        else if (kind == CKSDL_NATIVE_DITHER) DitherProgram = Programs.Get(program);
        else ClearProgram = Programs.Get(program);
        DestroyObject(program, CKRST_OBJ_PROGRAM);
        DestroyObject(vs, CKRST_OBJ_SHADER);
        DestroyObject(fs, CKRST_OBJ_SHADER);
    }
    CKSamplerDesc ditherSampler = {CKRST_FILTER_NEAREST, CKRST_FILTER_NEAREST,
        CKRST_FILTER_NONE, CKRST_ADDRESS_CLAMP, CKRST_ADDRESS_CLAMP,
        CKRST_ADDRESS_CLAMP, 0, CKRST_COMPARE_NONE};
    DitherSampler = Sampler(ditherSampler);
    if (!DitherSampler) { Shutdown(); return CKERR_INVALIDOPERATION; }
    SDL_Log("SDL_gpu ready: driver=%s format=0x%x window=%u thread=%llu size=%ux%u gpu=%s",
        SDL_GetGPUDeviceDriver(Device), unsigned(ShaderFormat), SDL_GetWindowID(Window),
        static_cast<unsigned long long>(Thread), Width, Height,
        SDL_GetStringProperty(SDL_GetGPUDeviceProperties(Device), SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown"));
    return CK_OK;
}

void CKSdlGpuRasterizerContext::PruneProgramCaches()
{
    for (CKSdlGpuDefaultVertexTable::Iterator it = DefaultVertexBuffers.Begin();
         it != DefaultVertexBuffers.End();) {
        if ((*it).expired()) {
            (*it).reset();
            it = DefaultVertexBuffers.Remove(it);
        } else {
            ++it;
        }
    }
    for (CKSdlGpuDefaultTextureTable::Iterator it = DefaultTextures.Begin();
         it != DefaultTextures.End();) {
        if ((*it).expired()) {
            (*it).reset();
            it = DefaultTextures.Remove(it);
        } else {
            ++it;
        }
    }
}

void CKSdlGpuRasterizerContext::Collect()
{
    for (int i = 0; i < Readbacks.Size();) {
        std::shared_ptr<CKSdlGpuReadback> &ticket = Readbacks[i];
        if (!ticket->Fence || !SDL_QueryGPUFence(Device, ticket->Fence.get())) {
            ++i;
            continue;
        }
        const void *bytes = SDL_MapGPUTransferBuffer(Device, ticket->Transfer.get(), false);
        if (!bytes) ticket->Error = CKERR_INVALIDOPERATION;
        else {
            std::memcpy(ticket->Data.Begin(), bytes, ticket->Data.Size());
            SDL_UnmapGPUTransferBuffer(Device, ticket->Transfer.get());
        }
        ticket->Transfer.reset();
        ticket->Fence.reset();
        ticket->Complete = TRUE;
        Readbacks.RemoveAt(i);
    }
    while (Submissions.Size() != 0) {
        CKSdlGpuSubmission &submission = Submissions[0];
        if (submission.Fence &&
            !SDL_QueryGPUFence(Device, submission.Fence.get()))
            break;
        for (auto &page : submission.Geometry) {
            // Bound idle retention after unusually large frames. Active pages
            // remain owned by their submission irrespective of this cache cap.
            const uint64_t bytes = uint64_t(page->Capacity) * 2;
            if (bytes <= 16u * 1024u * 1024u - FreeGeometryBytes) {
                FreeGeometryBytes += size_t(bytes);
                FreeGeometry.PushBack(page);
            }
        }
        for (int i = 0; i < submission.BufferUploads.Size(); ++i) {
            std::shared_ptr<CKSdlGpuBufferUploadPage> &page =
                submission.BufferUploads[i];
            page->Used = 0;
            if (page->Capacity <= 16u * 1024u * 1024u -
                                      FreeBufferUploadBytes) {
                FreeBufferUploadBytes += page->Capacity;
                FreeBufferUploads.PushBack(page);
            }
        }
        CompletedSubmitId = submission.SubmitId;
        Submissions.RemoveAt(0);
    }
}

bool CKSdlGpuRasterizerContext::SubmitJob(CKSdlGpuJob *job)
{
    if (!Worker.Running() && !Worker.Start("CKSdlGpuWorker")) {
        delete job;
        return false;
    }
    return Worker.Submit(job);
}

void CKSdlGpuRasterizerContext::CollectJobs()
{
    XArray<CKSdlGpuJob *> finished;
    Worker.Collect(finished);
    for (int i = 0; i < finished.Size(); ++i) {
        finished[i]->Complete();
        delete finished[i];
    }
}

CKERROR CKSdlGpuRasterizerContext::FlushPendingCommandsForTests()
{
    return Flush(false);
}

CKERROR CKSdlGpuRasterizerContext::AcquireSwapchainForTests()
{
    if (!EnsureCommands())
        return Error;
    const CKERROR result = AcquireSwapchain();
    if (result != CK_OK)
        return result;
    return Swapchain ? CK_OK : CKERR_INVALIDOPERATION;
}

void CKSdlGpuRasterizerContext::CollectForTests()
{
    Collect();
}

CKBOOL CKSdlGpuRasterizerContext::CompleteEmptySubmissionsForTests()
{
    Submissions.PushBack(CKSdlGpuSubmission());
    Submissions.Back().SubmitId = 7;
    Submissions.PushBack(CKSdlGpuSubmission());
    Submissions.Back().SubmitId = 8;
    Collect();
    return Submissions.Size() == 0 && CompletedSubmitId == 8;
}

void CKSdlGpuRasterizerContext::Shutdown()
{
    if (!Device) return;
    // Jobs hold device objects; release them while the device is alive.
    Worker.Stop();
    Draws.Clear();
    DrawResources.Clear();
    Bindings.Clear();
    if (Commands) {
        // SDL forbids cancelling after swapchain acquisition.
        if (Swapchain) SDL_SubmitGPUCommandBuffer(Commands);
        else SDL_CancelGPUCommandBuffer(Commands);
        Commands = nullptr;
    }
    SDL_WaitForGPUIdle(Device);
    Collect();
    CompletedSubmitId = LastSubmitId;
    for (auto &ticket : Readbacks) {
        ticket->Error = CKERR_INVALIDOPERATION;
        ticket->Transfer.reset(); ticket->Fence.reset();
    }
    Readbacks.Clear(); Submissions.Clear();
    PendingGeometry.Clear(); FreeGeometry.Clear(); FreeGeometryBytes = 0;
    PendingBufferUploads.Clear();
    FreeBufferUploads.Clear();
    FreeBufferUploadBytes = 0;
    BatchVertices.Clear(); BatchIndices.Clear();
    for (int i = 0; i < CKFF_TEXTURE_SLOT_COUNT; ++i)
        SamplerBindings[i] = CKSdlGpuBinding();
    for (CKSdlGpuSamplerTable::Iterator it = Samplers.Begin();
         it != Samplers.End(); ++it)
        (*it).reset();
    Samplers.Clear();
    ClearProgram.reset(); VolumeMipProgram.reset(); DitherProgram.reset();
    DitherSampler.reset(); DitherScratch.reset();
    DitherMultisample.reset(); DitherResolved.reset(); DitherSource.reset();
    DitherSourceFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    DitherScratchWidth = DitherScratchHeight = 0;
    DitherScratchSamples = 1;
    DefaultVertexBuffers.Clear(); DefaultTextures.Clear();
    Uniforms.Clear();
    Target.reset(); Targets.Clear(); Programs.Clear(); ShaderObjects.Clear();
    VertexBuffers.Clear(); IndexBuffers.Clear(); Layouts.Clear(); Textures.Clear();
    if (WindowClaimed) SDL_ReleaseWindowFromGPUDevice(Device, Window);
    SDL_DestroyGPUDevice(Device);
    WindowClaimed = false; Device = nullptr; Window = nullptr; Swapchain = nullptr; PassOpen = false;
    PresentCopySupported = false;
    PresentCopyLogged = false;
    TransientVertices.Clear(); TransientIndices.Clear();
    TransientVertexInfo.Clear(); TransientIndexInfo.Clear();
}

CKERROR CKSdlGpuRasterizerContext::Resize(int x, int y, int width, int height)
{
    if (!Ready() || PassOpen || Draws.Size() != 0) return CKERR_INVALIDOPERATION;
    if (width <= 0 || height <= 0 || unsigned(width) > Caps.MaxTextureSize || unsigned(height) > Caps.MaxTextureSize)
        return CKERR_INVALIDPARAMETER;
    Width = unsigned(width); Height = unsigned(height);
    Collect();
    return CK_OK;
}

CKERROR CKSdlGpuRasterizerContext::GetDeviceStatus() const { return Device ? Error : CKERR_INVALIDOPERATION; }
const CKRasterizerDeviceCaps &CKSdlGpuRasterizerContext::GetCaps() const { return Caps; }
CKBOOL CKSdlGpuRasterizerContext::IsNativeIdle() const { return !PassOpen && Draws.Size() == 0; }
void CKSdlGpuRasterizerContext::SetDebugFlags(CKDWORD flags) { DebugFlags = flags; }
uint64_t CKSdlGpuRasterizerContext::GetDrawApproximationMask() const { return DrawApproximations; }
const CKSdlGpuSubmissionStats &CKSdlGpuRasterizerContext::GetSubmissionStats() const { return Stats; }

CKReadbackState CKSdlGpuRasterizerContext::PollReadback(const CKSdlGpuReadbackTicket &ticket, CKBOOL wait)
{
    if (!ticket || !Ready() || ticket->Error != CK_OK) return CKRST_READBACK_FAILED;
    if (ticket->Complete) return CKRST_READBACK_READY;
    if (!ticket->Fence) return CKRST_READBACK_NEEDS_SUBMIT;
    if (wait) {
        SDL_GPUFence *fence = ticket->Fence.get();
        if (!SDL_WaitForGPUFences(Device, true, &fence, 1)) {
            Fail("WaitForGPUFences"); return CKRST_READBACK_FAILED;
        }
    }
    Collect();
    if (ticket->Error != CK_OK) return CKRST_READBACK_FAILED;
    return ticket->Complete ? CKRST_READBACK_READY : CKRST_READBACK_PENDING;
}

const XArray<CKBYTE> &CKSdlGpuRasterizerContext::GetReadbackData(
    const CKSdlGpuReadbackTicket &ticket) const
{
    static const XArray<CKBYTE> empty;
    return ticket ? ticket->Data : empty;
}

CKBOOL CKSdlGpuRasterizerContext::SupportsTexture2D(VX_PIXELFORMAT format) const
{
    if (!Ready() || format < _32_ARGB8888 || format > _32_X8L8V8U8) return FALSE;
    const SDL_GPUTextureFormat nativeFormat = CKSdlGpuTextureFormat(format);
    return nativeFormat != SDL_GPU_TEXTUREFORMAT_INVALID &&
        SDL_GPUTextureSupportsFormat(Device, nativeFormat, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER);
}
