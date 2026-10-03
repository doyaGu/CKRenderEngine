#ifndef CKSDLGPU_INTERNAL_H
#define CKSDLGPU_INTERNAL_H

#include "CKRasterizer.h"
#include "CKRasterizerContextData.h"
#include "CKSdlGpuNativeShaders.h"
#include "CKFFDrawTypes.h"
#include "CKFFProgramLayout.h"
#include "CKSdlGpuUniformBatch.h"
#include "XArray.h"
#include "XBitArray.h"
#include "XClassArray.h"
#include "XSHashTable.h"

#include <SDL3/SDL.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <algorithm>
#include <cstring>
#include <memory>

class CKSdlGpuJob;

template<int Count>
struct CKSdlGpuFixedKey {
    CKDWORD Values[Count] = {};

    bool operator==(const CKSdlGpuFixedKey &other) const {
        return std::memcmp(Values, other.Values, sizeof(Values)) == 0;
    }
};

template<int Count>
struct CKSdlGpuFixedKeyHash {
    int operator()(const CKSdlGpuFixedKey<Count> &key) const {
        return (int)CKFFHashBytes(key.Values, sizeof(key.Values), 2166136261u);
    }
};

struct CKSdlGpuQwordHash {
    int operator()(const uint64_t &key) const {
        return (int)((uint32_t)key ^ (uint32_t)(key >> 32));
    }
};

typedef CKSdlGpuFixedKey<11> CKSdlGpuPipelineKey;
typedef CKSdlGpuFixedKey<10> CKSdlGpuSamplerKey;
typedef CKSdlGpuFixedKey<64> CKSdlGpuDefaultVertexKey;
// A program's pipeline for a key. Null while the worker creates it, or after
// it failed to.
struct CKSdlGpuPipelineEntry {
    std::shared_ptr<SDL_GPUGraphicsPipeline> Pipeline;
    // Accessed only on the context thread. Pending jobs pin their cache entry.
    const CKSdlGpuJob *Job = nullptr;
    uint64_t LastUse = 0;
    CKQWORD RetainedSubmitId = 0;
    bool Failed = false;
    // A draw used it. The manifest records the precompiled programs' ones.
    bool Drawn = false;
};
typedef XSHashTable<CKSdlGpuPipelineEntry,
                    CKSdlGpuPipelineKey,
                    CKSdlGpuFixedKeyHash<11>> CKSdlGpuPipelineTable;
typedef XSHashTable<std::weak_ptr<SDL_GPUBuffer>,
                    CKSdlGpuDefaultVertexKey,
                    CKSdlGpuFixedKeyHash<64>> CKSdlGpuDefaultVertexTable;
typedef XSHashTable<std::shared_ptr<SDL_GPUSampler>,
                    CKSdlGpuSamplerKey,
                    CKSdlGpuFixedKeyHash<10>> CKSdlGpuSamplerTable;

// Public handles identify logical resources, never SDL pointers. Generations
// do not wrap: exhausted slots are retired for the life of the device.
template<class T> class CKSdlGpuTable {
    struct Slot { std::shared_ptr<T> Value; uint16_t Generation = 1; };
    XClassArray<Slot> Slots;
public:
    CKDWORD Add(std::shared_ptr<T> value) {
        for (int i = 0; i < Slots.Size(); ++i)
            if (!Slots[i].Value && Slots[i].Generation) {
                Slots[i].Value = std::move(value);
                return (CKDWORD(Slots[i].Generation) << 16) | CKDWORD(i + 1);
            }
        if (Slots.Size() == 65535) return 0;
        Slots.PushBack(Slot{std::move(value), 1});
        return 0x10000u | CKDWORD(Slots.Size());
    }
    // Borrow only until the next mutation of this table. Queued work must own
    // a copy (through Get or a batch resource group) before returning to callers.
    const std::shared_ptr<T> &Borrow(CKDWORD handle) const {
        static const std::shared_ptr<T> empty;
        const unsigned index = (handle & 65535u) - 1;
        if (index >= (unsigned)Slots.Size() ||
            Slots[index].Generation != (handle >> 16)) return empty;
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
    XBitArray Defined;
    XClassArray<XArray<CKBYTE>> BasePixels;
    XBitArray GpuMipBase;
};
struct CKSdlGpuDepthPad {
    std::weak_ptr<CKSdlGpuTexture> Source;
    std::shared_ptr<CKSdlGpuTexture> Texture;
    CKDWORD BorderDepth = 0;
    bool PadU = false;
    bool PadV = false;
};
typedef XSHashTable<std::weak_ptr<CKSdlGpuTexture>, uint64_t,
                    CKSdlGpuQwordHash> CKSdlGpuDefaultTextureTable;
struct CKSdlGpuBuffer {
    struct IndexRange {
        unsigned Start = 0, Count = 0, MaxIndex = 0;
        uint64_t LastUse = 0;
        bool Index32 = false;
        bool Valid = false;
    };
    std::shared_ptr<SDL_GPUBuffer> Buffer;
    CKBufferDesc Desc;
    XArray<CKBYTE> IndexData;
    IndexRange IndexRanges[8] = {};
    uint64_t IndexRangeClock = 0;

    bool FindMaxIndex(unsigned start, unsigned count, bool index32, unsigned &maximum);
    void InvalidateIndexRanges() {
        for (auto &range : IndexRanges) range.Valid = false;
        IndexRangeClock = 0;
    }
};
struct CKSdlGpuLayout {
    XArray<CKVertexElementDesc> Elements;
    unsigned Stride = 0;
};
class CKSdlGpuJob;
struct CKSdlGpuShader {
    std::shared_ptr<SDL_GPUShader> Shader;
    CKShaderDesc Desc;
    // The sampler slots Shader binds: those of Desc, but a compiled FF
    // fragment program binds only the slots up to the highest it reads. Set
    // before Shader is created.
    CKDWORD SamplerCount = 0;
    // The worker job creating Shader, which the pipelines of the shader wait
    // for, until the job is deleted. Used on the owner's thread only.
    const CKSdlGpuJob *Job = nullptr;
};
struct CKSdlGpuProgram : std::enable_shared_from_this<CKSdlGpuProgram> {
    enum VertexJitKind { PRECOMPILED_VERTEX, POSITIONT_VERTEX, UNLIT_VERTEX, LIT_VERTEX };
    VertexJitKind VertexJit = PRECOMPILED_VERTEX;
    bool UserClip = false;
    std::shared_ptr<CKSdlGpuShader> Vertex, Fragment;
    CKFFProgramDesc Interface;
    CKFFProgramLayout UniformLayout;
    CKSdlGpuUniformCursor UniformCursor;
    CKDWORD Identity = 0;
    CKDWORD CompareSamplerCount = 0;
    int AttributeLocations[CKRST_ATTRIB_COUNT];
    std::shared_ptr<SDL_GPUBuffer> DefaultVertices;
    XClassArray<std::shared_ptr<CKSdlGpuTexture>> DefaultTextures;
    XArray<CKDWORD> SamplerMetadataOffsets;
    CKSamplerDesc SamplerMetadata[CKFF_TEXTURE_SLOT_COUNT] = {};
    CKDWORD SamplerMetadataValidMask = 0;
    // The slots whose metadata a draw leaving them default has written.
    CKDWORD SamplerMetadataDefaultMask = 0;
    // The native sampler of each slot a draw leaves default, made by its
    // first such draw, once CompareSamplerCount is set.
    std::shared_ptr<SDL_GPUSampler> DefaultSamplers[CKFF_TEXTURE_SLOT_COUNT];
    // A queued draw retains its program; private image helpers do too after
    // their public handle is removed. The context bounds these pipeline maps
    // together; encoded commands retain selected PSOs separately until fenced.
    CKSdlGpuPipelineTable Pipelines;
    // A specialized program shares Fallback's interface. It draws with
    // Fallback's pipelines until the worker has created its own.
    std::shared_ptr<CKSdlGpuProgram> Fallback;
    // The jobs of pending pipelines queued at idle priority. A draw that
    // comes to wait for one promotes it, or claims it if the program has no
    // fallback. They are only compared.
    XSHashTable<CKSdlGpuJob *, CKSdlGpuPipelineKey, CKSdlGpuFixedKeyHash<11>> IdlePipelines;
};
struct CKSdlGpuTarget {
    CKRenderTargetDesc Desc;
    std::shared_ptr<CKSdlGpuTexture> Color, Depth;
    std::shared_ptr<SDL_GPUTexture> VolumeSlice;
};
// How Draw adjusts a slot's sampler before creating the native object.
enum CKSdlGpuSamplerMode : CKDWORD {
    CKSDLGPU_SAMPLER_NATIVE_COMPARE = 1u, // Keep the comparison function.
    CKSDLGPU_SAMPLER_VOLUME = 2u,         // The shader takes the anisotropic taps.
};
// The native sampler of one texture slot, keyed by the slot's source sampler
// and adjustment mode so that unchanged draws skip the adjustment.
struct CKSdlGpuBinding {
    CKSamplerDesc Sampler = {CKRST_FILTER_LINEAR, CKRST_FILTER_LINEAR, CKRST_FILTER_NONE,
        CKRST_ADDRESS_WRAP, CKRST_ADDRESS_WRAP, CKRST_ADDRESS_WRAP, 0, CKRST_COMPARE_NONE};
    CKDWORD Mode = 0;
    std::shared_ptr<SDL_GPUSampler> NativeSampler;
};

// Sampler descriptors are plain dwords. Draw compares several per binding, so
// fold the words inline instead of calling memcmp for each one.
inline bool CKSdlGpuSameSampler(const CKSamplerDesc &a, const CKSamplerDesc &b) {
    static_assert(sizeof(CKSamplerDesc) == 12 * sizeof(CKDWORD),
                  "CKSamplerDesc must stay a packed dword record");
    CKDWORD wa[12], wb[12];
    std::memcpy(wa, &a, sizeof(wa));
    std::memcpy(wb, &b, sizeof(wb));
    CKDWORD diff = 0;
    for (int i = 0; i < 12; ++i) diff |= wa[i] ^ wb[i];
    return diff == 0;
}

// Lends per-slot owners to one draw and releases the borrowed ones on every
// return path. Ordinary draws borrow none, so they construct no owners.
class CKSdlGpuOwnerScratch {
public:
    explicit CKSdlGpuOwnerScratch(std::shared_ptr<CKSdlGpuTexture> *owners)
        : m_Owners(owners) {}
    CKSdlGpuOwnerScratch(const CKSdlGpuOwnerScratch &) = delete;
    CKSdlGpuOwnerScratch &operator=(const CKSdlGpuOwnerScratch &) = delete;
    ~CKSdlGpuOwnerScratch() {
        for (unsigned slot = 0; m_Used != 0; ++slot, m_Used >>= 1)
            if (m_Used & 1u) m_Owners[slot].reset();
    }
    std::shared_ptr<CKSdlGpuTexture> &Borrow(unsigned slot) {
        m_Used |= 1u << slot;
        return m_Owners[slot];
    }
private:
    std::shared_ptr<CKSdlGpuTexture> *m_Owners;
    CKDWORD m_Used = 0;
};

// Drops every retained reference but keeps the list storage for reuse.
template<class T> inline void CKSdlGpuReleaseAll(XClassArray<std::shared_ptr<T>> &resources) {
    for (auto &resource : resources) resource.reset();
    resources.Resize(0);
}

// Resources are retained once per batch. Draw packets and binding groups use raw
// pointers only while this owner is alive, avoiding shared_ptr atomics per draw.
class CKSdlGpuDrawResourceBatch {
public:
    template<class T> static T *RetainUnique(XClassArray<std::shared_ptr<T>> &resources,
                                             const std::shared_ptr<T> &resource) {
        if (!resource) return nullptr;
        for (const auto &entry : resources) {
            if (entry.get() == resource.get())
                return resource.get();
        }
        resources.PushBack(resource);
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
    SDL_GPUBuffer *Retain(const std::shared_ptr<SDL_GPUBuffer> &resource) {
        return RetainUnique(NativeBuffers, resource);
    }
    void Clear() {
        m_LastProgram = nullptr;
        for (int i = 0; i < 2; ++i) m_RecentLayouts[i] = nullptr;
        for (int i = 0; i < 3; ++i) m_RecentBuffers[i] = nullptr;
        Programs.Clear(); Layouts.Clear(); Buffers.Clear(); NativeBuffers.Clear();
    }
    // Releases the batch references like Clear, keeping storage for the next batch.
    void Reset() {
        m_LastProgram = nullptr;
        for (int i = 0; i < 2; ++i) m_RecentLayouts[i] = nullptr;
        for (int i = 0; i < 3; ++i) m_RecentBuffers[i] = nullptr;
        CKSdlGpuReleaseAll(Programs); CKSdlGpuReleaseAll(Layouts);
        CKSdlGpuReleaseAll(Buffers); CKSdlGpuReleaseAll(NativeBuffers);
    }
private:
    XClassArray<std::shared_ptr<CKSdlGpuProgram>> Programs;
    XClassArray<std::shared_ptr<CKSdlGpuLayout>> Layouts;
    XClassArray<std::shared_ptr<CKSdlGpuBuffer>> Buffers;
    XClassArray<std::shared_ptr<SDL_GPUBuffer>> NativeBuffers;
    CKSdlGpuProgram *m_LastProgram = nullptr;
    CKSdlGpuLayout *m_RecentLayouts[2] = {};
    CKSdlGpuBuffer *m_RecentBuffers[3] = {};
};

// Native binding arrays expire at every render/copy boundary, before image
// versions can change. The fixed bucket table avoids allocation per lookup.
class CKSdlGpuBindingBatch {
public:
    struct Inputs {
        // Draw fills exactly Interface.Samplers.Size() entries. Intern never
        // reads the unused tail, so avoid clearing 1 KiB on every draw.
        CKSdlGpuTexture *Textures[32];
        SDL_GPUSampler *Samplers[32];
        const std::shared_ptr<CKSdlGpuTexture> *TextureOwners[32];
        const std::shared_ptr<SDL_GPUSampler> *SamplerOwners[32];
        size_t Hash = 0;
    };
    struct Group {
        CKDWORD Program = 0;
        int Next = -1;
        CKSdlGpuTexture *Textures[32] = {};
        SDL_GPUSampler *Samplers[32] = {};
        SDL_GPUTextureSamplerBinding Vertex[16] = {}, Fragment[16] = {};
    };
    CKSdlGpuBindingBatch() { ResetBuckets(); }
    unsigned Intern(const CKSdlGpuProgram &program, const Inputs &inputs) {
        const size_t count = program.Interface.Samplers.Size();
        const size_t bucket = inputs.Hash % 512;
        for (int index = Buckets[bucket]; index >= 0; index = Groups[index].Next) {
            const auto &group = Groups[index];
            if (group.Program != program.Identity) continue;
            if (std::memcmp(group.Textures, inputs.Textures,
                            count * sizeof(inputs.Textures[0])) == 0 &&
                std::memcmp(group.Samplers, inputs.Samplers,
                            count * sizeof(inputs.Samplers[0])) == 0)
                return unsigned(index);
        }
        const unsigned index = unsigned(Groups.Size());
        Groups.PushBack(Group());
        Group &group = Groups.Back();
        group.Program = program.Identity; group.Next = Buckets[bucket]; Buckets[bucket] = int(index);
        for (size_t i = 0; i < count; ++i) {
            const auto &decl = program.Interface.Samplers[i];
            group.Textures[i] = CKSdlGpuDrawResourceBatch::RetainUnique(RetainedTextures, *inputs.TextureOwners[i]);
            group.Samplers[i] = CKSdlGpuDrawResourceBatch::RetainUnique(RetainedSamplers, *inputs.SamplerOwners[i]);
            SDL_GPUTextureSamplerBinding *bindings =
                decl.Stage == CKRST_SHADER_VERTEX ? group.Vertex : group.Fragment;
            bindings[decl.NativeSlot] = {group.Textures[i]->Image.get(), group.Samplers[i]};
        }
        return index;
    }
    const Group &operator[](unsigned index) const { return Groups[index]; }
    size_t Size() const { return (size_t)Groups.Size(); }
    void MarkReferenced() {
        for (auto &texture : RetainedTextures) texture->Referenced = true;
    }
    void Clear() {
        Groups.Clear();
        RetainedTextures.Clear();
        RetainedSamplers.Clear();
        ResetBuckets();
    }
    // Releases the batch references like Clear, keeping storage for the next batch.
    void Reset() {
        Groups.Resize(0);
        CKSdlGpuReleaseAll(RetainedTextures);
        CKSdlGpuReleaseAll(RetainedSamplers);
        ResetBuckets();
    }
private:
    void ResetBuckets() { for (int i = 0; i < 512; ++i) Buckets[i] = -1; }
    int Buckets[512];
    XClassArray<Group> Groups;
    XClassArray<std::shared_ptr<CKSdlGpuTexture>> RetainedTextures;
    XClassArray<std::shared_ptr<SDL_GPUSampler>> RetainedSamplers;
};

struct CKSdlGpuDraw {
    CKFFPipelineState State;
    CKSdlGpuProgram *Program = nullptr;
    CKSdlGpuLayout *Layout = nullptr, *Layout1 = nullptr;
    CKSdlGpuBuffer *VB = nullptr, *VB1 = nullptr, *IB = nullptr;
    SDL_GPUBuffer *NativeVB = nullptr, *NativeVB1 = nullptr, *NativeIB = nullptr;
    CKDWORD LayoutHandle = 0, Layout1Handle = 0;
    CKDWORD StartVertex = 0, Stream1StartVertex = 0, VertexCount = 0;
    CKDWORD StartIndex = 0, IndexCount = 0;
    unsigned Bindings = 0;
    unsigned UniformOffsets[2 * CKFF_UNIFORM_BUFFER_COUNT] = {};
    unsigned VertexOffset = 0, VertexOffset1 = 0, IndexOffset = 0;
    bool Index32 = false;
    bool DitherEnable = false;
    bool Tween = false, MatrixBlend = false, DepthPad = false;
    CKDWORD ColorTargetFormat = 0;
    // Ordinary Release draws keep this empty; diagnostics and profiling copy
    // the caller's label into the queued packet.
    XString Marker;
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
struct CKSdlGpuBufferUploadPage {
    std::shared_ptr<SDL_GPUTransferBuffer> Transfer;
    unsigned Capacity = 0;
    unsigned Used = 0;
};
struct CKSdlGpuSubmission {
    std::shared_ptr<SDL_GPUFence> Fence;
    XClassArray<std::shared_ptr<SDL_GPUGraphicsPipeline>> Pipelines;
    XClassArray<std::shared_ptr<CKSdlGpuGeometryBuffer>> Geometry;
    XClassArray<std::shared_ptr<CKSdlGpuBufferUploadPage>> BufferUploads;
    CKQWORD SubmitId = 0;
};

struct CKSdlGpuReadback {
    XArray<CKBYTE> Data;
    CKBOOL Complete = FALSE;
    CKERROR Error = CK_OK;
    std::shared_ptr<SDL_GPUTransferBuffer> Transfer;
    std::shared_ptr<SDL_GPUFence> Fence;
};

struct CKSdlGpuSubmissionStats {
    CKDWORD Frames = 0;
    CKDWORD Passes = 0;
    CKDWORD Draws = 0;
    CKDWORD Blits = 0;
    CKDWORD TextureUploads = 0;
    CKDWORD BufferUploads = 0;
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

constexpr bool CKSdlGpuValidPresentSync(CKPresentSync sync)
{
    return sync == CKRST_PRESENT_UNCHANGED || sync == CKRST_PRESENT_VSYNC ||
           sync == CKRST_PRESENT_IMMEDIATE;
}

struct CKSdlGpuTransientVertexInfo {
    CKDWORD Token = 0, Layout = 0, Stride = 0;
};
struct CKSdlGpuTransientIndexInfo {
    CKDWORD Token = 0;
    CKBOOL Index32 = FALSE;
};

// Returned transient pointers must remain valid while callers allocate more
// streams for the same draw. Keep each byte array at a stable address even
// when the outer pointer table grows.
class CKSdlGpuTransientStorage {
public:
    CKSdlGpuTransientStorage() {}
    ~CKSdlGpuTransientStorage() { Clear(); }

    XArray<CKBYTE> *Acquire(int Index)
    {
        if (Index < 0 || Index > m_Blocks.Size())
            return NULL;
        if (Index == m_Blocks.Size())
            m_Blocks.PushBack(new XArray<CKBYTE>());
        return m_Blocks[Index];
    }

    const XArray<CKBYTE> *Get(int Index) const
    {
        return Index >= 0 && Index < m_Blocks.Size()
            ? m_Blocks[Index] : NULL;
    }

    void Trim(uint64_t &Retained)
    {
        static const uint64_t kRetentionLimit = 16u * 1024u * 1024u;
        int keep = 0;
        while (keep < m_Blocks.Size() &&
               Retained + m_Blocks[keep]->Allocated() <= kRetentionLimit) {
            Retained += m_Blocks[keep]->Allocated();
            ++keep;
        }
        for (int index = keep; index < m_Blocks.Size(); ++index)
            delete m_Blocks[index];
        m_Blocks.Resize(keep);
    }

    void Clear()
    {
        for (int index = 0; index < m_Blocks.Size(); ++index)
            delete m_Blocks[index];
        m_Blocks.Clear();
    }

private:
    CKSdlGpuTransientStorage(
        const CKSdlGpuTransientStorage &) = delete;
    CKSdlGpuTransientStorage &operator=(
        const CKSdlGpuTransientStorage &) = delete;

    XArray<XArray<CKBYTE> *> m_Blocks;
};

SDL_GPUSampleCount CKSdlGpuSampleCount(unsigned samples);
SDL_GPUVertexElementFormat CKSdlGpuVertexFormat(const CKVertexElementDesc &element);
SDL_GPUTextureFormat CKSdlGpuTextureFormat(VX_PIXELFORMAT format);
unsigned CKSdlGpuTextureLayers(const CKSdlGpuTexture &texture, unsigned mip);
// The create info of a validly described shader in its SDL format.
SDL_GPUShaderCreateInfo CKSdlGpuShaderInfo(const CKSdlGpuShader &shader, SDL_GPUShaderFormat format);

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
