#ifndef CKSDLGPU_INTERNAL_H
#define CKSDLGPU_INTERNAL_H

#include "CKSdlGpuBackend.h"
#include "CKSdlGpuNativeShaders.h"
#include "CKBackendProgramLayout.h"
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
    std::shared_ptr<T> Get(CKDWORD handle) const {
        const unsigned index = (handle & 65535u) - 1;
        if (index >= Slots.size() || Slots[index].Generation != (handle >> 16)) return {};
        return Slots[index].Value;
    }
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
    std::shared_ptr<SDL_GPUBuffer> Buffer;
    CKBackendBufferDesc Desc;
    std::vector<CKBYTE> Shadow;
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
    CKDWORD Identity = 0;
    std::array<int, CKRST_ATTRIB_COUNT> AttributeLocations;
    std::shared_ptr<SDL_GPUBuffer> DefaultVertices;
    std::vector<std::shared_ptr<CKSdlGpuTexture>> DefaultTextures;
    std::vector<CKDWORD> SamplerMetadataOffsets;
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
    CKDWORD Texture = 0;
    CKSamplerDesc Sampler = {CKRST_FILTER_LINEAR, CKRST_FILTER_LINEAR, CKRST_FILTER_NONE,
        CKRST_ADDRESS_WRAP, CKRST_ADDRESS_WRAP, CKRST_ADDRESS_WRAP, 0, CKRST_COMPARE_NONE};
    std::shared_ptr<SDL_GPUSampler> NativeSampler;
};
struct CKSdlGpuDraw {
    CKBackendDraw Desc;
    CKBackendPipelineState State;
    std::shared_ptr<CKSdlGpuProgram> Program;
    std::shared_ptr<CKSdlGpuLayout> Layout, Layout1;
    std::shared_ptr<CKSdlGpuBuffer> VB, VB1, IB;
    std::array<std::shared_ptr<CKSdlGpuTexture>, 32> Textures;
    std::array<std::shared_ptr<SDL_GPUSampler>, 32> Samplers;
    unsigned UniformOffset = 0;
    std::vector<CKBYTE> Vertices, Vertices1, Indices;
    unsigned VertexOffset = 0, VertexOffset1 = 0, IndexOffset = 0;
    bool Index32 = false;
    std::string Marker;
};
// A batch owns both sides of its upload until the submission fence completes.
// Reuse never cycles or overwrites storage referenced by an earlier batch.
struct CKSdlGpuGeometryBuffer {
    std::shared_ptr<SDL_GPUBuffer> Buffer;
    std::shared_ptr<SDL_GPUTransferBuffer> Transfer;
    unsigned Capacity = 0;
    SDL_GPUBufferUsageFlags Usage = 0;
};
struct CKSdlGpuSubmission {
    std::shared_ptr<SDL_GPUFence> Fence;
    std::vector<std::shared_ptr<CKSdlGpuGeometryBuffer>> Geometry;
};

struct CKSdlGpuReadback : CKBackendReadback {
    std::shared_ptr<SDL_GPUTransferBuffer> Transfer;
    std::shared_ptr<SDL_GPUFence> Fence;
};

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
    bool PassOpen = false;
    CKBackendPassDesc Pass;
    std::shared_ptr<CKSdlGpuTarget> Target;
    std::vector<CKSdlGpuDraw> Draws;
    CKBackendPipelineState State;
    std::array<CKSdlGpuBinding, CKBACKEND_MAX_TEXTURE_SLOTS> Bindings;
    CKBackendConstantValues Constants;
    std::vector<CKBYTE> UniformArena;
    std::string Marker;
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
    CKERROR Flush(bool presentWindow = true);
    std::shared_ptr<SDL_GPUBuffer> UploadGeometry(const std::vector<CKBYTE> &data, SDL_GPUBufferUsageFlags usage);
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
    std::shared_ptr<SDL_GPUGraphicsPipeline> Pipeline(const CKSdlGpuDraw &draw,
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

template<class T> std::shared_ptr<T> CKSdlGpuOwn(SDL_GPUDevice *device, T *object,
                                               void (SDLCALL *release)(SDL_GPUDevice *, T *)) {
    return std::shared_ptr<T>(object, [device, release](T *value) { if (value) release(device, value); });
}

#endif
