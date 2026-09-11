#ifndef CKSDLGPU_INTERNAL_H
#define CKSDLGPU_INTERNAL_H

#include "CKSdlGpuBackend.h"
#include "CKSdlGpuNativeShaders.h"
#include "CKBackendProgramLayout.h"
#include "CKSdlGpuUniformBatch.h"
#include <SDL3/SDL.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <string>

// Public handles identify logical resources, never SDL pointers. Generations
// do not wrap: exhausted slots are retired for the life of the device.
template<class T> class CKSdlGpuTable {
    struct Slot { std::shared_ptr<T> Value; uint16_t Generation = 1; };
    std::vector<Slot> Slots;
public:
    CKDWORD Add(std::shared_ptr<T> value) {
        for (size_t i = 0; i < Slots.size(); ++i)
            if (!Slots[i].Value && Slots[i].Generation) {
                Slots[i].Value = std::move(value);
                return (CKDWORD(Slots[i].Generation) << 16) | CKDWORD(i + 1);
            }
        if (Slots.size() == 65535) return 0;
        Slots.push_back({std::move(value), 1});
        return 0x10000u | CKDWORD(Slots.size());
    }
    // Borrow only until the next mutation of this table. Queued work must own
    // a copy (through Get or a batch resource group) before returning to callers.
    const std::shared_ptr<T> &Borrow(CKDWORD handle) const {
        static const std::shared_ptr<T> empty;
        const unsigned index = (handle & 65535u) - 1;
        if (index >= Slots.size() || Slots[index].Generation != (handle >> 16)) return empty;
        return Slots[index].Value;
    }
    std::shared_ptr<T> Get(CKDWORD handle) const { return Borrow(handle); }
    bool Remove(CKDWORD handle) {
        if (!Get(handle)) return false;
        auto &slot = Slots[(handle & 65535u) - 1];
        slot.Value.reset();
        ++slot.Generation;
        return true;
    }
    void Clear() {
        for (auto &slot : Slots) {
            if (slot.Value) { slot.Value.reset(); ++slot.Generation; }
        }
    }
};

struct CKSdlGpuTexture {
    SDL_GPUTextureCreateInfo Info = {};
    std::shared_ptr<SDL_GPUTexture> Image;
    std::shared_ptr<SDL_GPUTexture> Multisample;
    VX_PIXELFORMAT PixelFormat = UNKNOWN_PF;
    CKDWORD Flags = 0;
    unsigned Samples = 1;
    bool Depth = false;
    bool AutoMips = false;
    bool Referenced = false;
    bool AttachmentInitialized = false;
    std::vector<bool> Defined;
    std::vector<std::vector<CKBYTE>> BasePixels;
    std::vector<bool> GpuMipBase;
};
struct CKSdlGpuBuffer {
    struct IndexRange {
        unsigned Start = 0, Count = 0, MaxIndex = 0;
        uint64_t LastUse = 0;
        bool Index32 = false;
        bool Valid = false;
    };
    std::shared_ptr<SDL_GPUBuffer> Buffer;
    CKBackendBufferDesc Desc;
    std::vector<CKBYTE> Shadow;
    std::array<IndexRange, 8> IndexRanges = {};
    uint64_t IndexRangeClock = 0;

    bool FindMaxIndex(unsigned start, unsigned count, bool index32, unsigned &maximum);
    void InvalidateIndexRanges() {
        for (auto &range : IndexRanges) range.Valid = false;
        IndexRangeClock = 0;
    }
};
struct CKSdlGpuLayout {
    std::vector<CKVertexElementDesc> Elements;
    unsigned Stride = 0;
};
struct CKSdlGpuShader {
    std::shared_ptr<SDL_GPUShader> Shader;
    CKShaderDesc Desc;
};
struct CKSdlGpuProgram {
    std::shared_ptr<CKSdlGpuShader> Vertex, Fragment;
    CKBackendProgramDesc Interface;
    CKBackendProgramLayout UniformLayout;
    CKSdlGpuUniformCursor UniformCursor;
    CKDWORD Identity = 0;
    std::array<int, CKRST_ATTRIB_COUNT> AttributeLocations;
    std::shared_ptr<SDL_GPUBuffer> DefaultVertices;
    std::vector<std::shared_ptr<CKSdlGpuTexture>> DefaultTextures;
    std::vector<CKDWORD> SamplerMetadataOffsets;
    std::array<CKSamplerDesc, CKBACKEND_MAX_TEXTURE_SLOTS> SamplerMetadata = {};
    CKDWORD SamplerMetadataValidMask = 0;
    // A queued draw retains its program; private image helpers do too after
    // their public handle is removed. Pipelines follow those exact lifetimes.
    std::map<std::array<unsigned, 10>, std::shared_ptr<SDL_GPUGraphicsPipeline>> Pipelines;
};
struct CKSdlGpuTarget {
    CKBackendRenderTargetDesc Desc;
    std::shared_ptr<CKSdlGpuTexture> Color, Depth;
    std::shared_ptr<SDL_GPUTexture> VolumeSlice;
};
struct CKSdlGpuBinding {
    CKSamplerDesc Sampler = {CKRST_FILTER_LINEAR, CKRST_FILTER_LINEAR, CKRST_FILTER_NONE,
        CKRST_ADDRESS_WRAP, CKRST_ADDRESS_WRAP, CKRST_ADDRESS_WRAP, 0, CKRST_COMPARE_NONE};
    std::shared_ptr<SDL_GPUSampler> NativeSampler;
};

// Resources are retained once per batch. Draw packets and binding groups use raw
// pointers only while this owner is alive, avoiding shared_ptr atomics per draw.
class CKSdlGpuDrawResourceBatch {
public:
    template<class T> static T *RetainUnique(std::vector<std::shared_ptr<T>> &resources,
                                             const std::shared_ptr<T> &resource) {
        if (!resource) return nullptr;
        for (const auto &entry : resources) {
            if (entry.get() == resource.get())
                return resource.get();
        }
        resources.push_back(resource);
        return resource.get();
    }
    CKSdlGpuProgram *Retain(const std::shared_ptr<CKSdlGpuProgram> &resource) {
        if (!resource) return nullptr;
        if (resource.get() == m_LastProgram) return m_LastProgram;
        m_LastProgram = RetainUnique(Programs, resource);
        return m_LastProgram;
    }
    CKSdlGpuLayout *Retain(const std::shared_ptr<CKSdlGpuLayout> &resource) {
        if (!resource) return nullptr;
        for (CKSdlGpuLayout *recent : m_RecentLayouts) {
            if (resource.get() == recent)
                return recent;
        }
        CKSdlGpuLayout *retained = RetainUnique(Layouts, resource);
        m_RecentLayouts[1] = m_RecentLayouts[0];
        m_RecentLayouts[0] = retained;
        return retained;
    }
    CKSdlGpuBuffer *Retain(const std::shared_ptr<CKSdlGpuBuffer> &resource) {
        if (!resource) return nullptr;
        for (CKSdlGpuBuffer *recent : m_RecentBuffers) {
            if (resource.get() == recent)
                return recent;
        }
        CKSdlGpuBuffer *retained = RetainUnique(Buffers, resource);
        m_RecentBuffers[2] = m_RecentBuffers[1];
        m_RecentBuffers[1] = m_RecentBuffers[0];
        m_RecentBuffers[0] = retained;
        return retained;
    }
    void Clear() {
        m_LastProgram = nullptr;
        m_RecentLayouts.fill(nullptr);
        m_RecentBuffers.fill(nullptr);
        Programs.clear(); Layouts.clear(); Buffers.clear();
    }
private:
    std::vector<std::shared_ptr<CKSdlGpuProgram>> Programs;
    std::vector<std::shared_ptr<CKSdlGpuLayout>> Layouts;
    std::vector<std::shared_ptr<CKSdlGpuBuffer>> Buffers;
    CKSdlGpuProgram *m_LastProgram = nullptr;
    std::array<CKSdlGpuLayout *, 2> m_RecentLayouts = {};
    std::array<CKSdlGpuBuffer *, 3> m_RecentBuffers = {};
};

// Native binding arrays expire at every render/copy boundary, before image
// versions can change. The fixed bucket table avoids allocation per lookup.
class CKSdlGpuBindingBatch {
public:
    struct Inputs {
        // Draw fills exactly Interface.Samplers.size() entries. Intern never
        // reads the unused tail, so avoid clearing 1 KiB on every draw.
        std::array<CKSdlGpuTexture *, 32> Textures;
        std::array<SDL_GPUSampler *, 32> Samplers;
        std::array<const std::shared_ptr<CKSdlGpuTexture> *, 32> TextureOwners;
        std::array<const std::shared_ptr<SDL_GPUSampler> *, 32> SamplerOwners;
        size_t Hash = 0;
    };
    struct Group {
        CKDWORD Program = 0;
        int Next = -1;
        std::array<CKSdlGpuTexture *, 32> Textures = {};
        std::array<SDL_GPUSampler *, 32> Samplers = {};
        std::array<SDL_GPUTextureSamplerBinding, 16> Vertex = {}, Fragment = {};
    };
    CKSdlGpuBindingBatch() { Buckets.fill(-1); }
    unsigned Intern(const CKSdlGpuProgram &program, const Inputs &inputs) {
        const size_t count = program.Interface.Samplers.size();
        const size_t bucket = inputs.Hash % Buckets.size();
        for (int index = Buckets[bucket]; index >= 0; index = Groups[index].Next) {
            const auto &group = Groups[index];
            if (group.Program != program.Identity) continue;
            if (std::memcmp(group.Textures.data(), inputs.Textures.data(),
                            count * sizeof(inputs.Textures[0])) == 0 &&
                std::memcmp(group.Samplers.data(), inputs.Samplers.data(),
                            count * sizeof(inputs.Samplers[0])) == 0)
                return unsigned(index);
        }
        const unsigned index = unsigned(Groups.size());
        Groups.emplace_back();
        auto &group = Groups.back();
        group.Program = program.Identity; group.Next = Buckets[bucket]; Buckets[bucket] = int(index);
        for (size_t i = 0; i < count; ++i) {
            const auto &decl = program.Interface.Samplers[i];
            group.Textures[i] = CKSdlGpuDrawResourceBatch::RetainUnique(RetainedTextures, *inputs.TextureOwners[i]);
            group.Samplers[i] = CKSdlGpuDrawResourceBatch::RetainUnique(RetainedSamplers, *inputs.SamplerOwners[i]);
            auto &bindings = decl.Stage == CKRST_SHADER_VERTEX ? group.Vertex : group.Fragment;
            bindings[decl.NativeSlot] = {group.Textures[i]->Image.get(), group.Samplers[i]};
        }
        return index;
    }
    const Group &operator[](unsigned index) const { return Groups[index]; }
    size_t Size() const { return Groups.size(); }
    void MarkReferenced() {
        for (auto &texture : RetainedTextures) texture->Referenced = true;
    }
    void Clear() {
        Groups.clear();
        RetainedTextures.clear();
        RetainedSamplers.clear();
        Buckets.fill(-1);
    }
private:
    std::array<int, 512> Buckets;
    std::vector<Group> Groups;
    std::vector<std::shared_ptr<CKSdlGpuTexture>> RetainedTextures;
    std::vector<std::shared_ptr<SDL_GPUSampler>> RetainedSamplers;
};

struct CKSdlGpuDraw {
    CKBackendPipelineState State;
    CKSdlGpuProgram *Program = nullptr;
    CKSdlGpuLayout *Layout = nullptr, *Layout1 = nullptr;
    CKSdlGpuBuffer *VB = nullptr, *VB1 = nullptr, *IB = nullptr;
    CKDWORD LayoutHandle = 0, Layout1Handle = 0;
    CKDWORD StartVertex = 0, Stream1StartVertex = 0, VertexCount = 0;
    CKDWORD StartIndex = 0, IndexCount = 0;
    unsigned Bindings = 0;
    std::array<unsigned, 2 * CKBACKEND_MAX_UNIFORM_BUFFERS> UniformOffsets = {};
    unsigned VertexOffset = 0, VertexOffset1 = 0, IndexOffset = 0;
    bool Index32 = false;
    // Labels are owned only when diagnostics, profiling or runtime GPU debug
    // is enabled. Ordinary Release draws carry one null pointer.
    std::unique_ptr<std::string> Marker;
};
// A batch owns both sides of its upload until the submission fence completes.
// Reuse never cycles or overwrites storage referenced by an earlier batch.
struct CKSdlGpuGeometryBuffer {
    std::shared_ptr<SDL_GPUBuffer> Buffer;
    std::shared_ptr<SDL_GPUTransferBuffer> Transfer;
    unsigned Capacity = 0;
    SDL_GPUBufferUsageFlags Usage = 0;
};
struct CKSdlGpuGeometryUpload {
    std::shared_ptr<SDL_GPUBuffer> Buffer;
    unsigned IndexOffset = 0;
};
struct CKSdlGpuSubmission {
    std::shared_ptr<SDL_GPUFence> Fence;
    std::vector<std::shared_ptr<CKSdlGpuGeometryBuffer>> Geometry;
};

struct CKSdlGpuReadback : CKBackendReadback {
    std::shared_ptr<SDL_GPUTransferBuffer> Transfer;
    std::shared_ptr<SDL_GPUFence> Fence;
};

constexpr bool CKSdlGpuCanCopyPresent(SDL_GPUTextureFormat sourceFormat,
                                      SDL_GPUTextureFormat swapchainFormat,
                                      unsigned width, unsigned height,
                                      unsigned swapchainWidth, unsigned swapchainHeight)
{
    return sourceFormat == swapchainFormat && width == swapchainWidth &&
           height == swapchainHeight;
}

inline bool CKSdlGpuSupportsSwapchainCopy(const char *driver)
{
    // SDL 3.4.8's Vulkan swapchain acquisition waits at the color-attachment
    // stage, which does not safely cover a transfer copy to the acquired image.
    return driver && SDL_strcmp(driver, "direct3d12") == 0;
}

constexpr bool CKSdlGpuValidPresentSync(CKBackendPresentSync sync)
{
    return sync == CKRST_BACKEND_SYNC_UNCHANGED || sync == CKRST_BACKEND_SYNC_VSYNC ||
           sync == CKRST_BACKEND_SYNC_IMMEDIATE;
}

struct CKSdlGpuTransientVertexInfo {
    CKDWORD Token = 0, Layout = 0, Stride = 0;
};
struct CKSdlGpuTransientIndexInfo {
    CKDWORD Token = 0;
    CKBOOL Index32 = FALSE;
};

struct CKSdlGpuDevice {
    SDL_GPUDevice *Device = nullptr;
    SDL_Window *Window = nullptr; // borrowed from Player
    bool WindowClaimed = false;
    SDL_ThreadID Thread = 0;
    SDL_GPUShaderFormat ShaderFormat = 0;
    CKBackendCaps Caps;
    CKBackendStats Stats = {}, FrameStats = {};
    CKERROR Error = CK_OK;
    CKDWORD DebugFlags = 0, Submission = 0;
    uint64_t DrawApproximations = 0;
    unsigned Width = 0, Height = 0;
    SDL_GPUPresentMode PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    SDL_GPUCommandBuffer *Commands = nullptr;
    SDL_GPUTexture *Swapchain = nullptr;
    unsigned SwapWidth = 0, SwapHeight = 0;
    bool PresentCopySupported = false;
    bool PresentCopyLogged = false;
    bool PassOpen = false;
    CKBackendPassDesc Pass;
    std::shared_ptr<CKSdlGpuTarget> Target;
    std::vector<CKSdlGpuDraw> Draws;
    CKSdlGpuDrawResourceBatch DrawResources;
    std::array<CKSdlGpuBinding, CKBACKEND_MAX_TEXTURE_SLOTS> SamplerBindings;
    CKSdlGpuUniformBatch Uniforms;
    CKSdlGpuBindingBatch Bindings;
    // Info arrays describe this submission's live allocations; storage is reused
    // by allocation ordinal only after Submit invalidates every token.
    std::vector<std::vector<CKBYTE>> TransientVertices, TransientIndices;
    CKDWORD NextTransientToken = 1; // never reset or reused across submissions/device reinitialization
    std::vector<CKSdlGpuTransientVertexInfo> TransientVertexInfo;
    std::vector<CKSdlGpuTransientIndexInfo> TransientIndexInfo;
    // Sharing does not extend the lifetime of a program's defaults. Expired
    // keys are pruned on program creation/destruction, never by a draw lookup.
    std::map<std::vector<CKDWORD>, std::weak_ptr<SDL_GPUBuffer>> DefaultVertexBuffers;
    std::shared_ptr<CKSdlGpuProgram> ClearProgram;
    std::shared_ptr<CKSdlGpuProgram> VolumeMipProgram;
    std::map<std::pair<unsigned, CKDWORD>, std::weak_ptr<CKSdlGpuTexture>> DefaultTextures;
    std::vector<std::shared_ptr<CKSdlGpuReadback>> Readbacks;
    std::deque<CKSdlGpuSubmission> Submissions;
    std::vector<std::shared_ptr<CKSdlGpuGeometryBuffer>> PendingGeometry, FreeGeometry;
    size_t FreeGeometryBytes = 0;
    std::vector<CKBYTE> BatchVertices, BatchIndices;
    std::map<std::array<unsigned, 7>, std::shared_ptr<SDL_GPUSampler>> Samplers;
    CKSdlGpuTable<CKSdlGpuTexture> Textures;
    CKSdlGpuTable<CKSdlGpuBuffer> VertexBuffers, IndexBuffers;
    CKSdlGpuTable<CKSdlGpuLayout> Layouts;
    CKSdlGpuTable<CKSdlGpuShader> ShaderObjects;
    CKSdlGpuTable<CKSdlGpuProgram> Programs;
    CKSdlGpuTable<CKSdlGpuTarget> Targets;

    bool Ready() const { return Device && SDL_GetCurrentThreadID() == Thread && Error == CK_OK; }
    CKERROR Fail(const char *operation);
    bool EnsureCommands();
    CKERROR AcquireSwapchain();
    CKERROR Flush(bool presentWindow = true);
    CKSdlGpuGeometryUpload UploadGeometry(const std::vector<CKBYTE> &vertices,
                                          const std::vector<CKBYTE> &indices);
    CKERROR UploadBuffer(SDL_GPUBuffer *buffer, const void *data, unsigned size, bool cycle);
    CKERROR UploadTexture(CKSdlGpuTexture &texture, unsigned mip, unsigned layer,
                          const CKRECT *region, const VxImageDescEx &data);
    CKERROR UploadTexturePixels(CKSdlGpuTexture &texture, unsigned mip, unsigned layer,
                                unsigned x, unsigned y, unsigned width, unsigned height,
                                const std::vector<CKBYTE> &pixels);
    CKERROR GenerateUploadMips(CKSdlGpuTexture &texture, unsigned layer);
    CKERROR GenerateGpuMips(CKSdlGpuTexture &texture, unsigned layer);
    CKERROR GenerateVolumeMips(CKSdlGpuTexture &texture);
    CKERROR CopyVolumeSlice(CKSdlGpuTexture &texture, unsigned mip, unsigned layer,
                            SDL_GPUTexture *slice, bool toVolume);
    CKERROR PreserveTexture(CKSdlGpuTexture &texture);
    CKERROR ClearRect(SDL_GPURenderPass *pass, const CKBackendPassDesc &desc,
                      SDL_GPUTextureFormat colorFormat, SDL_GPUTextureFormat depthFormat,
                      SDL_GPUSampleCount samples);
    SDL_GPUGraphicsPipeline *Pipeline(const CKSdlGpuDraw &draw,
        SDL_GPUTextureFormat color, SDL_GPUTextureFormat depth, SDL_GPUSampleCount samples);
    std::shared_ptr<SDL_GPUSampler> Sampler(const CKSamplerDesc &desc);
    void PruneProgramCaches() {
        for (auto it = DefaultVertexBuffers.begin(); it != DefaultVertexBuffers.end();) {
            if (it->second.expired()) it = DefaultVertexBuffers.erase(it);
            else ++it;
        }
        for (auto it = DefaultTextures.begin(); it != DefaultTextures.end();) {
            if (it->second.expired()) it = DefaultTextures.erase(it);
            else ++it;
        }
    }
    void Collect();
};

SDL_GPUSampleCount CKSdlGpuSampleCount(unsigned samples);
SDL_GPUVertexElementFormat CKSdlGpuVertexFormat(const CKVertexElementDesc &element);
SDL_GPUTextureFormat CKSdlGpuTextureFormat(VX_PIXELFORMAT format);
unsigned CKSdlGpuTextureLayers(const CKSdlGpuTexture &texture, unsigned mip);

template<class T>
struct CKSdlGpuResourceDeleter {
    SDL_GPUDevice *Device;
    void (SDLCALL *Release)(SDL_GPUDevice *, T *);

    void operator()(T *value) const {
        if (value)
            Release(Device, value);
    }
};

template<class T> std::shared_ptr<T> CKSdlGpuOwn(SDL_GPUDevice *device, T *object,
                                               void (SDLCALL *release)(SDL_GPUDevice *, T *)) {
    return std::shared_ptr<T>(object, CKSdlGpuResourceDeleter<T>{device, release});
}

#endif
