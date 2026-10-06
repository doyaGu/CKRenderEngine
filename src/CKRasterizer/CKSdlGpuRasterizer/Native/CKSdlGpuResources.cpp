#include "CKRenderProfile.h"
#include "CKSdlGpuRasterizerContext.h"
#include "CKSdlGpuShaderJob.h"
#include "CKSdlGpuTextureData.h"

SDL_GPUSampleCount CKSdlGpuSampleCount(unsigned samples)
{
    switch (samples) {
    case 2: return SDL_GPU_SAMPLECOUNT_2;
    case 4: return SDL_GPU_SAMPLECOUNT_4;
    case 8: return SDL_GPU_SAMPLECOUNT_8;
    default: return SDL_GPU_SAMPLECOUNT_1;
    }
}

SDL_GPUTextureFormat CKSdlGpuTextureFormat(VX_PIXELFORMAT format)
{
    switch (format) {
    case _32_ARGB8888: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    case _32_ABGR8888: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    case _16_RGB565: return SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM;
    case _16_ARGB1555: return SDL_GPU_TEXTUREFORMAT_B5G5R5A1_UNORM;
    case _DXT1: return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
    // DXT2/4 store premultiplied colors; decode to straight-alpha BGRA for SDL shaders and blending.
    case _DXT2: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    case _DXT3: return SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM;
    case _DXT4: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    case _DXT5: return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
    case _16_V8U8: return SDL_GPU_TEXTUREFORMAT_R8G8_SNORM;
    case _32_V16U16: return SDL_GPU_TEXTUREFORMAT_R16G16_SNORM;
    case _16_L6V5U5: case _32_X8L8V8U8: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    default: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    }
}

unsigned CKSdlGpuTextureLayers(const CKSdlGpuTexture &texture, unsigned mip)
{
    return texture.Info.type == SDL_GPU_TEXTURETYPE_3D ? std::max(1u, texture.Info.layer_count_or_depth >> mip)
                                                     : texture.Info.layer_count_or_depth;
}

bool CKSdlGpuBuffer::FindMaxIndex(unsigned start, unsigned count, bool index32, unsigned &maximum)
{
    const size_t elementSize = index32 ? sizeof(CKDWORD) : sizeof(CKWORD);
    const size_t dataSize = (size_t)IndexData.Size();
    if (!count || start > dataSize / elementSize || count > dataSize / elementSize - start)
        return false;
    if (++IndexRangeClock == 0) {
        IndexRangeClock = 1;
        for (auto &range : IndexRanges) range.LastUse = 0;
    }
    for (auto &range : IndexRanges) {
        if (!range.Valid || range.Start != start || range.Count != count || range.Index32 != index32) continue;
        range.LastUse = IndexRangeClock;
        maximum = range.MaxIndex;
        return true;
    }
    unsigned result = 0;
    const CKBYTE *first = IndexData.Begin() + size_t(start) * elementSize;
    if (index32) {
        for (unsigned i = 0; i < count; ++i) {
            CKDWORD index;
            std::memcpy(&index, first + size_t(i) * sizeof(index), sizeof(index));
            result = std::max(result, unsigned(index));
        }
    } else {
        for (unsigned i = 0; i < count; ++i) {
            CKWORD index;
            std::memcpy(&index, first + size_t(i) * sizeof(index), sizeof(index));
            result = std::max(result, unsigned(index));
        }
    }
    auto oldest = &IndexRanges[0];
    for (auto &range : IndexRanges) {
        if (!range.Valid) { oldest = &range; break; }
        if (range.LastUse < oldest->LastUse) oldest = &range;
    }
    *oldest = {start, count, result, IndexRangeClock, index32, true};
    maximum = result;
    return true;
}

CKSdlGpuGeometryUpload CKSdlGpuRasterizerContext::UploadGeometry(
    const XArray<CKBYTE> &vertices, const XArray<CKBYTE> &indices)
{
    CKRE_PROFILE_SCOPE("CKRE.SDL.UploadGeometry");
    CKSdlGpuGeometryUpload upload;
    if (vertices.Size() == 0 && indices.Size() == 0) return upload;
    const uint64_t indexOffset = ((uint64_t)vertices.Size() + 3) & ~uint64_t(3);
    const uint64_t totalSize = indexOffset + indices.Size();
    if (totalSize > UINT32_MAX) {
        SDL_SetError("Transient geometry exceeds the native buffer size limit");
        Fail("UploadGeometry.size"); return upload;
    }
    const unsigned size = unsigned(totalSize);
    SDL_GPUBufferUsageFlags usage = 0;
    if (vertices.Size() != 0) usage |= SDL_GPU_BUFFERUSAGE_VERTEX;
    if (indices.Size() != 0) usage |= SDL_GPU_BUFFERUSAGE_INDEX;
    int best = -1;
    for (int i = 0; i < FreeGeometry.Size(); ++i) {
        if (FreeGeometry[i]->Usage == usage && FreeGeometry[i]->Capacity >= size &&
            (best < 0 || FreeGeometry[i]->Capacity < FreeGeometry[best]->Capacity)) best = i;
    }
    std::shared_ptr<CKSdlGpuGeometryBuffer> page;
    if (best >= 0) {
        page = FreeGeometry[best];
        FreeGeometryBytes -= size_t(page->Capacity) * 2;
        FreeGeometry.RemoveAt(best);
    } else {
        page = std::make_shared<CKSdlGpuGeometryBuffer>();
        // Capacity buckets absorb small frame-to-frame geometry variations.
        uint64_t capacity = 65536;
        while (capacity < size) capacity *= 2;
        page->Capacity = capacity <= UINT32_MAX ? unsigned(capacity) : size;
        page->Usage = usage;
        SDL_GPUBufferCreateInfo bufferInfo = {usage, page->Capacity, 0};
        page->Buffer = CKSdlGpuOwn(Device, SDL_CreateGPUBuffer(Device, &bufferInfo), SDL_ReleaseGPUBuffer);
        if (!page->Buffer) { Fail("CreateGPUBuffer.batch"); return upload; }
        SDL_GPUTransferBufferCreateInfo transferInfo = {SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, page->Capacity, 0};
        page->Transfer = CKSdlGpuOwn(Device, SDL_CreateGPUTransferBuffer(Device, &transferInfo), SDL_ReleaseGPUTransferBuffer);
        if (!page->Transfer) { Fail("CreateGPUTransferBuffer.batch"); return upload; }
    }
    // Pending pages are ineligible for reuse even across multiple Flush calls
    // in the same command buffer. Submit attaches the completion fence.
    PendingGeometry.PushBack(page);
    void *mapped = SDL_MapGPUTransferBuffer(Device, page->Transfer.get(), false);
    if (!mapped) { Fail("MapGPUTransferBuffer.batch"); return upload; }
    if (vertices.Size() != 0) std::memcpy(mapped, vertices.Begin(), (size_t)vertices.Size());
    if (indexOffset > (uint64_t)vertices.Size())
        std::memset(static_cast<CKBYTE *>(mapped) + vertices.Size(), 0,
                    (size_t)(indexOffset - vertices.Size()));
    if (indices.Size() != 0)
        std::memcpy(static_cast<CKBYTE *>(mapped) + indexOffset,
                    indices.Begin(), (size_t)indices.Size());
    SDL_UnmapGPUTransferBuffer(Device, page->Transfer.get());
    auto *copy = SDL_BeginGPUCopyPass(Commands);
    if (!copy) { Fail("BeginGPUCopyPass.batch"); return upload; }
    SDL_GPUTransferBufferLocation source = {page->Transfer.get(), 0};
    SDL_GPUBufferRegion destination = {page->Buffer.get(), 0, size};
    SDL_UploadToGPUBuffer(copy, &source, &destination, false);
    SDL_EndGPUCopyPass(copy);
    ++FrameStats.BufferUploads;
    upload.Buffer = page->Buffer;
    upload.IndexOffset = unsigned(indexOffset);
    return upload;
}

CKERROR CKSdlGpuRasterizerContext::UploadBuffer(SDL_GPUBuffer *buffer, unsigned offset,
                                     const void *data, unsigned size, bool cycle)
{
    if (!buffer || !data || !size)
        return CKERR_INVALIDPARAMETER;
    if (!EnsureCommands())
        return Error;

    const unsigned alignment = 16;
    std::shared_ptr<CKSdlGpuBufferUploadPage> page;
    unsigned sourceOffset = 0;
    if (PendingBufferUploads.Size() != 0) {
        page = PendingBufferUploads.Back();
        const CKQWORD aligned = ((CKQWORD)page->Used + alignment - 1) &
                                ~(CKQWORD)(alignment - 1);
        if (aligned + size <= page->Capacity)
            sourceOffset = (unsigned)aligned;
        else
            page.reset();
    }
    if (!page) {
        int best = -1;
        for (int i = 0; i < FreeBufferUploads.Size(); ++i) {
            if (FreeBufferUploads[i]->Capacity >= size &&
                (best < 0 || FreeBufferUploads[i]->Capacity <
                                 FreeBufferUploads[best]->Capacity))
                best = i;
        }
        if (best >= 0) {
            page = FreeBufferUploads[best];
            FreeBufferUploadBytes -= page->Capacity;
            FreeBufferUploads.RemoveAt(best);
        } else {
            page = std::make_shared<CKSdlGpuBufferUploadPage>();
            CKQWORD capacity = 64u * 1024u;
            while (capacity < size && capacity <= 0x7fffffffu)
                capacity *= 2;
            page->Capacity = capacity <= UINT32_MAX
                ? (unsigned)capacity : size;
            SDL_GPUTransferBufferCreateInfo info = {
                SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, page->Capacity, 0};
            page->Transfer = CKSdlGpuOwn(
                Device, SDL_CreateGPUTransferBuffer(Device, &info),
                SDL_ReleaseGPUTransferBuffer);
            if (!page->Transfer)
                return Fail("CreateGPUTransferBuffer.upload");
        }
        page->Used = 0;
        sourceOffset = 0;
        PendingBufferUploads.PushBack(page);
    }

    void *mapped = SDL_MapGPUTransferBuffer(Device, page->Transfer.get(), false);
    if (!mapped)
        return Fail("MapGPUTransferBuffer.upload");
    std::memcpy(static_cast<CKBYTE *>(mapped) + sourceOffset, data, size);
    SDL_UnmapGPUTransferBuffer(Device, page->Transfer.get());
    page->Used = sourceOffset + size;
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(Commands);
    if (!copy)
        return Fail("BeginGPUCopyPass.upload");
    SDL_GPUTransferBufferLocation source = {page->Transfer.get(), sourceOffset};
    SDL_GPUBufferRegion destination = {buffer, offset, size};
    SDL_UploadToGPUBuffer(copy, &source, &destination, cycle);
    SDL_EndGPUCopyPass(copy);
    ++FrameStats.BufferUploads;
    return CK_OK;
}

CKERROR CKSdlGpuRasterizerContext::CreateBuffer(const CKBufferDesc *desc, CKDWORD *out)
{
    if (out) *out = 0;
    if (!Ready()) return CKERR_INVALIDOPERATION;
    if (!desc || !out || !desc->Size || unsigned(desc->Kind) > CKRST_BUFFER_INDEX ||
        (desc->Kind == CKRST_BUFFER_VERTEX && (!desc->Stride || (desc->Layout && !Layouts.Get(desc->Layout)))))
        return CKERR_INVALIDPARAMETER;
    const CKERROR flush = Flush();
    if (flush != CK_OK) return flush;
    auto buffer = std::make_shared<CKSdlGpuBuffer>();
    buffer->Desc = *desc; buffer->Desc.InitialData = nullptr;
    if (desc->Kind == CKRST_BUFFER_INDEX) {
        if (desc->Size > 0x7fffffffu)
            return CKERR_OUTOFMEMORY;
        buffer->IndexData.Resize((int)desc->Size);
        if (desc->InitialData)
            std::memcpy(buffer->IndexData.Begin(), desc->InitialData, desc->Size);
        else
            std::memset(buffer->IndexData.Begin(), 0, desc->Size);
    }
    SDL_GPUBufferCreateInfo info = {};
    info.usage = desc->Kind == CKRST_BUFFER_VERTEX ? SDL_GPU_BUFFERUSAGE_VERTEX : SDL_GPU_BUFFERUSAGE_INDEX;
    // SDL rejects buffers under 4 bytes; a single 16-bit index is valid here.
    info.size = SDL_max(desc->Size, 4u);
    buffer->Buffer = CKSdlGpuOwn(Device, SDL_CreateGPUBuffer(Device, &info), SDL_ReleaseGPUBuffer);
    if (!buffer->Buffer) return Fail("CreateGPUBuffer");
    const void *initialData = desc->InitialData;
    if (!initialData && desc->Kind == CKRST_BUFFER_INDEX)
        initialData = buffer->IndexData.Begin();
    if (initialData &&
        UploadBuffer(buffer->Buffer.get(), 0, initialData,
                        desc->Size, false) != CK_OK)
        return Error;
    *out = (desc->Kind == CKRST_BUFFER_VERTEX ? VertexBuffers : IndexBuffers).Add(buffer);
    return *out ? CK_OK : CKERR_OUTOFMEMORY;
}

CKERROR CKSdlGpuRasterizerContext::UpdateBuffer(const CKBufferUpdateDesc *desc)
{
    if (!Ready()) return CKERR_INVALIDOPERATION;
    if (!desc || unsigned(desc->Kind) > CKRST_BUFFER_INDEX ||
        unsigned(desc->Mode) > CKRST_BUFFER_UPDATE_NOOVERWRITE ||
        (desc->Mode == CKRST_BUFFER_UPDATE_NOOVERWRITE &&
         desc->Rename))
        return CKERR_INVALIDPARAMETER;
    auto buffer = (desc->Kind == CKRST_BUFFER_VERTEX ?
        VertexBuffers : IndexBuffers).Get(desc->Buffer);
    if (!buffer || !desc->Data || !desc->Size ||
        desc->Offset > buffer->Desc.Size ||
        desc->Size > buffer->Desc.Size - desc->Offset)
        return CKERR_INVALIDPARAMETER;

    std::shared_ptr<SDL_GPUBuffer> native = buffer->Buffer;
    if (desc->Rename) {
        SDL_GPUBufferCreateInfo info = {};
        info.usage = desc->Kind == CKRST_BUFFER_VERTEX
            ? SDL_GPU_BUFFERUSAGE_VERTEX : SDL_GPU_BUFFERUSAGE_INDEX;
        info.size = SDL_max(buffer->Desc.Size, 4u);
        native = CKSdlGpuOwn(Device,
            SDL_CreateGPUBuffer(Device, &info), SDL_ReleaseGPUBuffer);
        if (!native)
            return Fail("CreateGPUBuffer.rename");

        if (desc->Mode == CKRST_BUFFER_UPDATE_PRESERVE) {
            if (!EnsureCommands())
                return Error;
            SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(Commands);
            if (!copy)
                return Fail("BeginGPUCopyPass.rename");
            const SDL_GPUBufferLocation source = {buffer->Buffer.get(), 0};
            const SDL_GPUBufferLocation destination = {native.get(), 0};
            SDL_CopyGPUBufferToBuffer(copy, &source, &destination,
                                      buffer->Desc.Size, false);
            SDL_EndGPUCopyPass(copy);
        }
    }

    const bool cycle = !desc->Rename &&
                       desc->Mode == CKRST_BUFFER_UPDATE_DISCARD;
    const CKERROR error = UploadBuffer(native.get(), desc->Offset,
                                          desc->Data, desc->Size, cycle);
    if (error == CK_OK) {
        if (desc->Kind == CKRST_BUFFER_INDEX) {
            std::memcpy(buffer->IndexData.Begin() + desc->Offset,
                        desc->Data, desc->Size);
            buffer->InvalidateIndexRanges();
        }
        buffer->Buffer.swap(native);
    }
    return error;
}

CKERROR CKSdlGpuRasterizerContext::CreateTexture(const CKTextureDesc *desc, const VxImageDescEx *data, CKDWORD *out)
{
    if (out) *out = 0;
    if (!Ready()) return CKERR_INVALIDOPERATION;
    if (!desc || !out || desc->Format.Width <= 0 || desc->Format.Height <= 0 ||
        unsigned(desc->Format.Width) > Caps.MaxTextureSize || unsigned(desc->Format.Height) > Caps.MaxTextureSize)
        return CKERR_INVALIDPARAMETER;
    auto texture = std::make_shared<CKSdlGpuTexture>();
    texture->PixelFormat = VxImageDesc2PixelFormat(desc->Format);
    if (texture->PixelFormat == UNKNOWN_PF) return CKERR_NOTIMPLEMENTED;
    texture->Flags = desc->Flags;
    auto &info = texture->Info;
    info.type = desc->Flags & CKRST_TEXTURE_CUBEMAP ? SDL_GPU_TEXTURETYPE_CUBE :
        (desc->Flags & CKRST_TEXTURE_VOLUMEMAP ? SDL_GPU_TEXTURETYPE_3D : SDL_GPU_TEXTURETYPE_2D);
    info.format = CKSdlGpuTextureFormat(texture->PixelFormat);
    if (info.format == SDL_GPU_TEXTUREFORMAT_INVALID) return CKERR_NOTIMPLEMENTED;
    info.width = desc->Format.Width; info.height = desc->Format.Height;
    info.layer_count_or_depth = info.type == SDL_GPU_TEXTURETYPE_CUBE ? 6 :
        (info.type == SDL_GPU_TEXTURETYPE_3D ? std::max(1u, unsigned(desc->Depth)) : 1);
    if (info.layer_count_or_depth > 2048 || (info.type == SDL_GPU_TEXTURETYPE_CUBE && info.width != info.height))
        return CKERR_INVALIDPARAMETER;
    unsigned fullLevels = 1;
    for (unsigned extent = std::max({info.width, info.height, info.type == SDL_GPU_TEXTURETYPE_3D ? info.layer_count_or_depth : 1u});
         extent > 1; extent >>= 1) ++fullLevels;
    texture->AutoMips = desc->MipMapCount == CKRST_MIPMAP_GENERATE;
    if (!texture->AutoMips && desc->MipMapCount > fullLevels) return CKERR_INVALIDPARAMETER;
    if (texture->AutoMips && (texture->PixelFormat == _DXT1 || texture->PixelFormat == _DXT2 ||
                              texture->PixelFormat == _DXT3 || texture->PixelFormat == _DXT4 ||
                              texture->PixelFormat == _DXT5)) info.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    info.num_levels = texture->AutoMips ? fullLevels : std::max(1u, unsigned(desc->MipMapCount));
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    // SDL 3.4's D3D12 3D RTV allocation uses base depth for every mip.
    // Keep volumes sample/copy-only and render their slices through 2D targets.
    if (info.type == SDL_GPU_TEXTURETYPE_3D) {
        if ((desc->Flags & CKRST_TEXTURE_RENDERTARGET) && !SDL_GPUTextureSupportsFormat(Device,
            info.format, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)) return CKERR_NOTIMPLEMENTED;
    } else if (desc->Flags & CKRST_TEXTURE_RENDERTARGET) info.usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    if (texture->AutoMips && info.type != SDL_GPU_TEXTURETYPE_3D && SDL_GPUTextureSupportsFormat(Device,
        info.format, info.type, info.usage | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)) info.usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    if (!SDL_GPUTextureSupportsFormat(Device, info.format, info.type, info.usage)) return CKERR_NOTIMPLEMENTED;
    texture->Samples = std::max(1u, unsigned(CKRSTTextureMSAASamples(desc->Flags)));
    if (texture->Samples > Caps.MaxMSAASamples ||
        (texture->Samples > 1 && (info.type != SDL_GPU_TEXTURETYPE_2D || info.num_levels > 1 ||
                                !(desc->Flags & CKRST_TEXTURE_RENDERTARGET)))) return CKERR_NOTIMPLEMENTED;
    texture->Image = CKSdlGpuOwn(Device, SDL_CreateGPUTexture(Device, &info), SDL_ReleaseGPUTexture);
    if (!texture->Image) return Fail("CreateGPUTexture");
    if (texture->Samples > 1) {
        auto attachment = info;
        attachment.sample_count = CKSdlGpuSampleCount(texture->Samples);
        attachment.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        texture->Multisample = CKSdlGpuOwn(Device, SDL_CreateGPUTexture(Device, &attachment), SDL_ReleaseGPUTexture);
        if (!texture->Multisample) return Fail("CreateGPUTexture.multisample");
    }
    texture->Defined.CheckSize((int)(info.num_levels * info.layer_count_or_depth - 1));
    texture->GpuMipBase.CheckSize((int)(info.layer_count_or_depth - 1));
    *out = Textures.Add(texture);
    if (!*out) return CKERR_OUTOFMEMORY;
    if (data) {
        const CKERROR error = UpdateTexture(*out, 0, 0, nullptr, data);
        if (error != CK_OK) { Textures.Remove(*out); *out = 0; return error; }
    }
    return CK_OK;
}

CKERROR CKSdlGpuRasterizerContext::CreateDepthTexture(const CKDepthTextureDesc *desc, CKDWORD *out)
{
    if (out) *out = 0;
    if (!Ready()) return CKERR_INVALIDOPERATION;
    if (!desc || !out || !desc->Width || !desc->Height || desc->Width > Caps.MaxTextureSize ||
        desc->Height > Caps.MaxTextureSize || desc->Samples > Caps.MaxMSAASamples ||
        (desc->Samples > 1 && desc->Samples != 2 && desc->Samples != 4 && desc->Samples != 8)) return CKERR_INVALIDPARAMETER;
    auto texture = std::make_shared<CKSdlGpuTexture>();
    texture->Depth = true; texture->Samples = std::max(1u, unsigned(desc->Samples));
    auto &info = texture->Info;
    info.type = SDL_GPU_TEXTURETYPE_2D;
    switch (desc->Format) {
    case CKRST_DEPTHFMT_D16: info.format = SDL_GPU_TEXTUREFORMAT_D16_UNORM; break;
    case CKRST_DEPTHFMT_D24: info.format = SDL_GPU_TEXTUREFORMAT_D24_UNORM; break;
    case CKRST_DEPTHFMT_D24S8: info.format = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT; break;
    case CKRST_DEPTHFMT_D32F: info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT; break;
    default: return CKERR_NOTIMPLEMENTED;
    }
    info.width = desc->Width; info.height = desc->Height; info.num_levels = info.layer_count_or_depth = 1;
    info.sample_count = CKSdlGpuSampleCount(texture->Samples);
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    // Single-sample depth textures can be used by a later comparison draw.
    // Keep attachment-only formats available for the internal backbuffer.
    if (texture->Samples == 1 && SDL_GPUTextureSupportsFormat(Device, info.format,
        info.type, info.usage | SDL_GPU_TEXTUREUSAGE_SAMPLER))
        info.usage |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture->Image = CKSdlGpuOwn(Device, SDL_CreateGPUTexture(Device, &info), SDL_ReleaseGPUTexture);
    if (!texture->Image) return Fail("CreateGPUTexture.depth");
    *out = Textures.Add(texture);
    return *out ? CK_OK : CKERR_OUTOFMEMORY;
}

CKERROR CKSdlGpuRasterizerContext::CreateRenderTarget(const CKRenderTargetDesc *desc, CKDWORD *out)
{
    if (out) *out = 0;
    if (!Ready()) return CKERR_INVALIDOPERATION;
    if (!desc || !out) return CKERR_INVALIDPARAMETER;
    auto target = std::make_shared<CKSdlGpuTarget>();
    target->Desc = *desc;
    target->Color = Textures.Get(desc->ColorTexture);
    target->Depth = Textures.Get(desc->DepthTexture);
    if (!target->Color || (!(target->Color->Info.usage & SDL_GPU_TEXTUREUSAGE_COLOR_TARGET) &&
        !(target->Color->Info.type == SDL_GPU_TEXTURETYPE_3D && (target->Color->Flags & CKRST_TEXTURE_RENDERTARGET))) ||
        desc->ColorMip >= target->Color->Info.num_levels || desc->ColorLayer >= CKSdlGpuTextureLayers(*target->Color, desc->ColorMip) ||
        (desc->DepthTexture && (!target->Depth || !target->Depth->Depth))) return CKERR_INVALIDPARAMETER;
    if (target->Depth && (target->Depth->Samples != target->Color->Samples ||
        target->Depth->Info.width != std::max(1u, target->Color->Info.width >> desc->ColorMip) ||
        target->Depth->Info.height != std::max(1u, target->Color->Info.height >> desc->ColorMip))) return CKERR_INVALIDPARAMETER;
    if (target->Color->Info.type == SDL_GPU_TEXTURETYPE_3D) {
        auto slice = target->Color->Info;
        slice.type = SDL_GPU_TEXTURETYPE_2D; slice.num_levels = slice.layer_count_or_depth = 1;
        slice.width = std::max(1u, slice.width >> desc->ColorMip);
        slice.height = std::max(1u, slice.height >> desc->ColorMip);
        slice.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        target->VolumeSlice = CKSdlGpuOwn(Device, SDL_CreateGPUTexture(Device, &slice), SDL_ReleaseGPUTexture);
        if (!target->VolumeSlice) return Fail("CreateGPUTexture.volumeTarget");
    }
    *out = Targets.Add(target);
    return *out ? CK_OK : CKERR_OUTOFMEMORY;
}

CKERROR CKSdlGpuRasterizerContext::PreserveTexture(CKSdlGpuTexture &texture)
{
    if (!texture.Referenced) return CK_OK;
    if (texture.Multisample || texture.Depth) return CKERR_NOTIMPLEMENTED;
    auto replacement = CKSdlGpuOwn(Device, SDL_CreateGPUTexture(Device, &texture.Info), SDL_ReleaseGPUTexture);
    if (!replacement) return Fail("CreateGPUTexture.version");
    if (!EnsureCommands()) return Error;
    auto *copy = SDL_BeginGPUCopyPass(Commands);
    if (!copy) return Fail("BeginGPUCopyPass.preserve");
    for (unsigned mip = 0; mip < texture.Info.num_levels; ++mip) {
        for (unsigned layer = 0; layer < CKSdlGpuTextureLayers(texture, mip); ++layer) {
            if (!texture.Defined.IsSet((int)(mip * texture.Info.layer_count_or_depth + layer))) continue;
            SDL_GPUTextureLocation source = {}, dest = {};
            source.texture = texture.Image.get(); dest.texture = replacement.get();
            source.mip_level = dest.mip_level = mip;
            if (texture.Info.type == SDL_GPU_TEXTURETYPE_3D) source.z = dest.z = layer;
            else source.layer = dest.layer = layer;
            SDL_CopyGPUTextureToTexture(copy, &source, &dest, std::max(1u, texture.Info.width >> mip),
                                        std::max(1u, texture.Info.height >> mip), 1, false);
        }
    }
    SDL_EndGPUCopyPass(copy);
    texture.Image = std::move(replacement);
    texture.Referenced = false;
    return CK_OK;
}

static unsigned ImagePitch(const VxImageDescEx &data)
{
    const unsigned row = unsigned(data.Width) * unsigned(data.BitsPerPixel) / 8;
    const unsigned pitch = data.BytesPerLine > 0 ? unsigned(data.BytesPerLine) : row;
    // TotalImageSize occupies this field only for compressed formats. A large
    // uncompressed pitch is still a pitch (for example a narrow image in a
    // larger CPU surface), and must never be divided by the image height.
    return pitch >= row ? pitch : 0;
}

CKERROR CKSdlGpuRasterizerContext::UploadTexture(CKSdlGpuTexture &texture, unsigned mip, unsigned layer,
                                    const CKRECT *rect, const VxImageDescEx &data)
{
    const unsigned width = std::max(1u, texture.Info.width >> mip), height = std::max(1u, texture.Info.height >> mip);
    const unsigned x = rect ? rect->left : 0, y = rect ? rect->top : 0;
    const unsigned w = rect ? rect->right - rect->left : data.Width, h = rect ? rect->bottom - rect->top : data.Height;
    if (x >= width || y >= height || !w || !h || w > width - x || h > height - y ||
        unsigned(data.Width) != w || unsigned(data.Height) != h) return CKERR_INVALIDPARAMETER;
    const VX_PIXELFORMAT sourceFormat = VxImageDesc2PixelFormat(data);
    const bool compressed = texture.Info.format == SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM ||
        texture.Info.format == SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM || texture.Info.format == SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
    if ((texture.PixelFormat == _16_V8U8 || texture.PixelFormat == _32_V16U16) &&
        sourceFormat != texture.PixelFormat) return CKERR_NOTIMPLEMENTED;
    if (!compressed && (sourceFormat == _DXT1 || sourceFormat == _DXT2 || sourceFormat == _DXT3 ||
                        sourceFormat == _DXT4 || sourceFormat == _DXT5)) {
        XArray<unsigned char> decoded;
        if (!CKSdlGpuDecodeDXT(data, decoded)) return CKERR_INVALIDPARAMETER;
        VxImageDescEx image;
        VxPixelFormat2ImageDesc(_32_ARGB8888, image);
        image.Width = w; image.Height = h; image.BytesPerLine = w * 4; image.Image = decoded.Begin();
        return UploadTexture(texture, mip, layer, rect, image);
    }
    if (!compressed && !ImagePitch(data)) return CKERR_INVALIDPARAMETER;
    if (compressed && (sourceFormat != texture.PixelFormat || x % 4 || y % 4 ||
                       (w % 4 && x + w != width) || (h % 4 && y + h != height))) return CKERR_INVALIDPARAMETER;
    const unsigned block = compressed ? SDL_GPUTextureFormatTexelBlockSize(texture.Info.format) : 0;
    const unsigned bytes = compressed ? 0 : SDL_GPUTextureFormatTexelBlockSize(texture.Info.format);
    const unsigned rowBytes = compressed ? ((w + 3) / 4) * block : w * bytes;
    const unsigned rows = compressed ? (h + 3) / 4 : h;
    const uint64_t pixelBytes = uint64_t(rowBytes) * rows;
    if (pixelBytes > 0x7fffffffu) return CKERR_INVALIDPARAMETER;
    XArray<CKBYTE> pixels;
    pixels.Resize((int)pixelBytes);
    if (compressed) {
        if (data.TotalImageSize > 0 && data.TotalImageSize < pixels.Size()) return CKERR_INVALIDPARAMETER;
        std::memcpy(pixels.Begin(), data.Image, (size_t)pixels.Size());
    } else if (sourceFormat == _16_L6V5U5 || sourceFormat == _32_X8L8V8U8) {
        if (sourceFormat != texture.PixelFormat) return CKERR_INVALIDPARAMETER;
        const unsigned pitch = ImagePitch(data), sourceBytes = sourceFormat == _16_L6V5U5 ? 2 : 4;
        for (unsigned row = 0; row < h; ++row) for (unsigned col = 0; col < w; ++col) {
            CKDWORD packed = 0;
            std::memcpy(&packed, data.Image + row * pitch + col * sourceBytes, sourceBytes);
            int u, v, l, scale;
            if (sourceBytes == 2) {
                u = int(packed & 31); v = int((packed >> 5) & 31);
                if (u & 16) u -= 32;
                if (v & 16) v -= 32;
                l = int((packed >> 10) & 63) * 255 / 63; scale = 15;
            } else {
                u = int(static_cast<signed char>(packed)); v = int(static_cast<signed char>(packed >> 8));
                l = (packed >> 16) & 255; scale = 127;
            }
            CKBYTE *dst = pixels.Begin() + row * rowBytes + col * 4;
            dst[0] = CKBYTE(std::clamp(int((float(u) / scale * 0.5f + 0.5f) * 255 + 0.5f), 0, 255));
            dst[1] = CKBYTE(std::clamp(int((float(v) / scale * 0.5f + 0.5f) * 255 + 0.5f), 0, 255));
            dst[2] = CKBYTE(l); dst[3] = 255;
        }
    } else if (sourceFormat == texture.PixelFormat &&
               (sourceFormat == _32_ARGB8888 || sourceFormat == _32_ABGR8888 || sourceFormat == _16_V8U8 ||
                sourceFormat == _32_V16U16)) {
        for (unsigned row = 0; row < h; ++row)
            std::memcpy(pixels.Begin() + row * rowBytes,
                        data.Image + row * ImagePitch(data), rowBytes);
    } else {
        VxImageDescEx source = data, dest;
        source.BytesPerLine = ImagePitch(data);
        VxPixelFormat2ImageDesc(texture.Info.format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ? _32_ABGR8888 : _32_ARGB8888, dest);
        dest.Width = w; dest.Height = h; dest.BytesPerLine = rowBytes; dest.Image = pixels.Begin();
        VxDoBlit(source, dest);
    }
    if (texture.AutoMips && mip == 0) {
        if (texture.GpuMipBase.IsSet((int)layer)) {
            const CKERROR error = UploadTexturePixels(texture, mip, layer, x, y, w, h, pixels);
            return error == CK_OK ? GenerateGpuMips(texture, layer) : error;
        }
        // Keep each base face/slice in native storage format. A patch must not
        // erase the other faces, slices, or texels used to regenerate the chain.
        if (texture.BasePixels.Size() == 0)
            texture.BasePixels.Resize((int)texture.Info.layer_count_or_depth);
        XArray<CKBYTE> &base = texture.BasePixels[(int)layer];
        const bool initialize = base.Size() == 0;
        if (initialize) base.Resize((int)(size_t(width) * height * bytes));
        for (unsigned row = 0; row < h; ++row)
            std::memcpy(base.Begin() + (size_t(y + row) * width + x) * bytes,
                        pixels.Begin() + row * rowBytes, rowBytes);
        const CKERROR error = initialize ? UploadTexturePixels(texture, 0, layer, 0, 0, width, height, base)
            : UploadTexturePixels(texture, mip, layer, x, y, w, h, pixels);
        return error == CK_OK ? GenerateUploadMips(texture, layer) : error;
    }
    return UploadTexturePixels(texture, mip, layer, x, y, w, h, pixels);
}

CKERROR CKSdlGpuRasterizerContext::UploadTexturePixels(CKSdlGpuTexture &texture, unsigned mip, unsigned layer,
                                           unsigned x, unsigned y, unsigned w, unsigned h,
                                           const XArray<CKBYTE> &pixels)
{
    if (!EnsureCommands()) return Error;
    SDL_GPUTransferBufferCreateInfo transferInfo = {
        SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, (unsigned)pixels.Size(), 0};
    auto transfer = CKSdlGpuOwn(Device, SDL_CreateGPUTransferBuffer(Device, &transferInfo), SDL_ReleaseGPUTransferBuffer);
    if (!transfer) return Fail("CreateGPUTransferBuffer.texture");
    void *mapped = SDL_MapGPUTransferBuffer(Device, transfer.get(), false);
    if (!mapped) return Fail("MapGPUTransferBuffer.texture");
    std::memcpy(mapped, pixels.Begin(), (size_t)pixels.Size());
    SDL_UnmapGPUTransferBuffer(Device, transfer.get());
    auto *copy = SDL_BeginGPUCopyPass(Commands);
    if (!copy) return Fail("BeginGPUCopyPass.texture");
    SDL_GPUTextureTransferInfo src = {transfer.get(), 0, 0, 0};
    SDL_GPUTextureRegion dst = {};
    dst.texture = texture.Image.get(); dst.mip_level = mip;
    if (texture.Info.type == SDL_GPU_TEXTURETYPE_3D) dst.z = layer; else dst.layer = layer;
    dst.x = x; dst.y = y; dst.w = w; dst.h = h; dst.d = 1;
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    texture.Defined.Set((int)(mip * texture.Info.layer_count_or_depth + layer));
    ++FrameStats.TextureUploads;
    return CK_OK;
}

CKERROR CKSdlGpuRasterizerContext::GenerateUploadMips(CKSdlGpuTexture &texture, unsigned layer)
{
    const bool volume = texture.Info.type == SDL_GPU_TEXTURETYPE_3D;
    if (volume) {
        // A volume mip averages adjacent depth slices. Wait until all source
        // slices exist instead of generating from uninitialized GPU storage.
        for (const XArray<CKBYTE> &slice : texture.BasePixels)
            if (slice.Size() == 0) return CK_OK;
    }
    const unsigned bytes = SDL_GPUTextureFormatTexelBlockSize(texture.Info.format);
    const bool premultiplied = texture.PixelFormat == _DXT2 || texture.PixelFormat == _DXT4;
    const bool signed8 = texture.Info.format == SDL_GPU_TEXTUREFORMAT_R8G8_SNORM;
    const bool signed16 = texture.Info.format == SDL_GPU_TEXTUREFORMAT_R16G16_SNORM;
    const unsigned componentBytes = signed16 ? 2 : 1;
    XClassArray<XArray<CKBYTE>> previous;
    if (volume) previous = texture.BasePixels;
    else previous.PushBack(texture.BasePixels[(int)layer]);
    unsigned pw = texture.Info.width, ph = texture.Info.height;
    unsigned pd = (unsigned)previous.Size();
    for (unsigned mip = 1; mip < texture.Info.num_levels; ++mip) {
        const unsigned nw = std::max(1u, pw >> 1), nh = std::max(1u, ph >> 1);
        const unsigned nd = volume ? std::max(1u, pd >> 1) : 1;
        const uint64_t nextSliceBytes = uint64_t(nw) * nh * bytes;
        if (nextSliceBytes > 0x7fffffffu) return CKERR_OUTOFMEMORY;
        XClassArray<XArray<CKBYTE>> next;
        next.Resize((int)nd);
        for (unsigned z = 0; z < nd; ++z)
            next[(int)z].Resize((int)nextSliceBytes);
        for (unsigned z = 0; z < nd; ++z) for (unsigned y = 0; y < nh; ++y) for (unsigned x = 0; x < nw; ++x) {
            const unsigned x0 = x * pw / nw, x1 = (x + 1) * pw / nw;
            const unsigned y0 = y * ph / nh, y1 = (y + 1) * ph / nh;
            const unsigned z0 = z * pd / nd, z1 = (z + 1) * pd / nd;
            const int count = int((x1 - x0) * (y1 - y0) * (z1 - z0));
            if (premultiplied) {
                // Filter in premultiplied space, then store straight-alpha BGRA.
                unsigned alphaSum = 0, colorSums[3] = {};
                for (unsigned sz = z0; sz < z1; ++sz) for (unsigned sy = y0; sy < y1; ++sy)
                    for (unsigned sx = x0; sx < x1; ++sx) {
                        const CKBYTE *source = previous[(int)sz].Begin() +
                            (size_t(sy) * pw + sx) * bytes;
                        alphaSum += source[3];
                        for (unsigned c = 0; c < 3; ++c) colorSums[c] += unsigned(source[c]) * source[3];
                    }
                CKBYTE *destination = next[(int)z].Begin() + (size_t(y) * nw + x) * bytes;
                for (unsigned c = 0; c < 3; ++c)
                    destination[c] = alphaSum ? CKBYTE((colorSums[c] + alphaSum / 2) / alphaSum) : 0;
                destination[3] = CKBYTE((alphaSum + unsigned(count) / 2) / unsigned(count));
                continue;
            }
            for (unsigned c = 0; c < bytes; c += componentBytes) {
                int sum = 0;
                for (unsigned sz = z0; sz < z1; ++sz) for (unsigned sy = y0; sy < y1; ++sy)
                    for (unsigned sx = x0; sx < x1; ++sx) {
                        const CKBYTE *source = previous[(int)sz].Begin() +
                            (size_t(sy) * pw + sx) * bytes + c;
                        if (signed16) { int16_t value; std::memcpy(&value, source, 2); sum += std::max(-32767, int(value)); }
                        else if (signed8) sum += std::max(-127, int(static_cast<int8_t>(*source)));
                        else sum += *source;
                    }
                const int average = (sum + (sum < 0 ? -count / 2 : count / 2)) / count;
                CKBYTE *destination = next[(int)z].Begin() +
                    (size_t(y) * nw + x) * bytes + c;
                if (signed16) { const int16_t value = int16_t(average); std::memcpy(destination, &value, 2); }
                else *destination = CKBYTE(average);
            }
        }
        for (unsigned z = 0; z < nd; ++z) {
            const CKERROR error = UploadTexturePixels(
                texture, mip, volume ? z : layer, 0, 0, nw, nh, next[(int)z]);
            if (error != CK_OK) return error;
        }
        previous.Swap(next); pw = nw; ph = nh; pd = nd;
    }
    return CK_OK;
}

CKERROR CKSdlGpuRasterizerContext::GenerateGpuMips(CKSdlGpuTexture &texture, unsigned layer)
{
    if (!texture.AutoMips) return CK_OK;
    if (texture.Info.type == SDL_GPU_TEXTURETYPE_3D) {
        // Any GPU write invalidates the CPU reduction of the whole volume.
        for (unsigned z = 0; z < texture.Info.layer_count_or_depth; ++z)
            texture.GpuMipBase.Set((int)z);
        texture.BasePixels.Clear();
        for (unsigned z = 0; z < texture.Info.layer_count_or_depth; ++z)
            if (!texture.Defined.IsSet((int)z)) return CK_OK;
        return GenerateVolumeMips(texture);
    }
    if (!(texture.Info.usage & SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)) return CKERR_NOTIMPLEMENTED;
    texture.GpuMipBase.Set((int)layer);
    if (texture.BasePixels.Size() != 0)
        texture.BasePixels[(int)layer].Clear();
    // Encode only the written face, outside any pass. Other faces may not yet
    // have been uploaded. Mips remain part of this ordered command buffer.
    for (unsigned mip = 1; mip < texture.Info.num_levels; ++mip) {
        SDL_GPUBlitInfo blit = {};
        blit.source.texture = blit.destination.texture = texture.Image.get();
        blit.source.mip_level = mip - 1; blit.destination.mip_level = mip;
        blit.source.layer_or_depth_plane = blit.destination.layer_or_depth_plane = layer;
        blit.source.w = std::max(1u, texture.Info.width >> (mip - 1));
        blit.source.h = std::max(1u, texture.Info.height >> (mip - 1));
        blit.destination.w = std::max(1u, texture.Info.width >> mip);
        blit.destination.h = std::max(1u, texture.Info.height >> mip);
        blit.load_op = SDL_GPU_LOADOP_DONT_CARE; blit.filter = SDL_GPU_FILTER_LINEAR;
        SDL_BlitGPUTexture(Commands, &blit);
        texture.Defined.Set((int)(mip * texture.Info.layer_count_or_depth + layer));
    }
    return CK_OK;
}

CKERROR CKSdlGpuRasterizerContext::UpdateTexture(CKDWORD handle, CKDWORD mip, CKDWORD layer,
                                     const CKRECT *rect, const VxImageDescEx *data)
{
    if (!Ready()) return CKERR_INVALIDOPERATION;
    auto texture = Textures.Get(handle);
    if (!texture || texture->Depth || !data || !data->Image || mip >= texture->Info.num_levels ||
        layer >= CKSdlGpuTextureLayers(*texture, mip)) return CKERR_INVALIDPARAMETER;
    const CKERROR flush = Flush();
    if (flush != CK_OK) return flush;
    const CKERROR error = PreserveTexture(*texture);
    return error == CK_OK ? UploadTexture(*texture, mip, layer, rect, *data) : error;
}

CKERROR CKSdlGpuRasterizerContext::CreateVertexLayout(const CKVertexLayoutDesc *desc, CKDWORD *out)
{
    if (out) *out = 0;
    if (!Ready()) return CKERR_INVALIDOPERATION;
    if (!desc || !out || !desc->Elements || !desc->ElementCount || desc->ElementCount > 16 || !desc->Stride)
        return CKERR_INVALIDPARAMETER;
    auto layout = std::make_shared<CKSdlGpuLayout>();
    bool attributes[CKRST_ATTRIB_COUNT] = {};
    layout->Elements.Reserve((int)desc->ElementCount);
    for (unsigned i = 0; i < desc->ElementCount; ++i) {
        const auto &e = desc->Elements[i];
        const unsigned attribute = static_cast<unsigned>(e.Attrib);
        const unsigned elementBytes = e.Count * (e.Type == CKRST_ATTRIBTYPE_FLOAT ? 4u :
            (e.Type == CKRST_ATTRIBTYPE_INT8 || e.Type == CKRST_ATTRIBTYPE_UINT8 ? 1u : 2u));
        if (attribute >= CKRST_ATTRIB_COUNT || attributes[attribute] ||
            CKSdlGpuVertexFormat(e) == SDL_GPU_VERTEXELEMENTFORMAT_INVALID ||
            e.Offset >= desc->Stride || elementBytes > unsigned(desc->Stride - e.Offset)) return CKERR_INVALIDPARAMETER;
        attributes[attribute] = true;
        layout->Elements.PushBack(e);
    }
    layout->Stride = desc->Stride;
    *out = Layouts.Add(layout);
    return *out ? CK_OK : CKERR_OUTOFMEMORY;
}

SDL_GPUShaderFormat CKSdlGpuRasterizerContext::NativeShaderFormat(
    CK_SHADER_FORMAT format, CK_SHADER_PROFILE profile) const
{
    if (format == Caps.ShaderFormat && profile == Caps.ShaderProfile)
        return ShaderFormat;
    // D3D12 devices also run shader model 5.1 DXBC. A pipeline cannot mix it
    // with DXIL, so a DXBC program supplies both of its stages.
    if (format == CKRST_SHADER_FORMAT_DXBC && profile == CKRST_SHADER_PROFILE_DX12 &&
        ShaderFormat == SDL_GPU_SHADERFORMAT_DXIL &&
        (SDL_GetGPUShaderFormats(Device) & SDL_GPU_SHADERFORMAT_DXBC))
        return SDL_GPU_SHADERFORMAT_DXBC;
    return SDL_GPU_SHADERFORMAT_INVALID;
}

SDL_GPUShaderCreateInfo CKSdlGpuShaderInfo(const CKSdlGpuShader &shader, SDL_GPUShaderFormat format)
{
    const CKShaderDesc &desc = shader.Desc;
    SDL_GPUShaderCreateInfo info = {};
    info.code = desc.Code; info.code_size = desc.CodeSize; info.entrypoint = desc.EntryPoint;
    info.format = format;
    info.stage = desc.Stage == CKRST_SHADER_VERTEX ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = shader.SamplerCount; info.num_uniform_buffers = desc.UniformBufferCount;
    return info;
}

CKSdlGpuRasterizerContext::ShaderJob::~ShaderJob()
{
    for (int i = 0; i < Shaders.Size(); ++i)
        Shaders[i].Shader->Job = nullptr;
    if (Context.m_FFShaderJob == this)
        Context.m_FFShaderJob = nullptr;
}

void CKSdlGpuRasterizerContext::ShaderJob::Add(const std::shared_ptr<CKSdlGpuShader> &shader,
                                               SDL_GPUShaderFormat format)
{
    shader->Job = this;
    Pending pending = {shader, format};
    Shaders.PushBack(pending);
}

void CKSdlGpuRasterizerContext::ShaderJob::Run()
{
    for (int i = 0; i < Shaders.Size(); ++i) {
        CKSdlGpuShader &shader = *Shaders[i].Shader;
        const SDL_GPUShaderCreateInfo info = CKSdlGpuShaderInfo(shader, Shaders[i].Format);
        shader.Shader = CKSdlGpuOwn(Device, SDL_CreateGPUShader(Device, &info), SDL_ReleaseGPUShader);
        if (shader.Shader) {
            ++Created;
        } else if (Created == i) {
            Error = SDL_GetError();
        }
    }
}

void CKSdlGpuRasterizerContext::ShaderJob::Complete()
{
    Context.m_FFWorkerShaders += CKDWORD(Created);
    if (Created == Shaders.Size())
        return;
    SDL_SetError("%s", Error.CStr());
    Context.Fail("CreateGPUShader");
}

CKERROR CKSdlGpuRasterizerContext::CreateShader(const CKShaderDesc *desc, CKDWORD *out)
{
    return CreateShader(desc, out, nullptr);
}

CKERROR CKSdlGpuRasterizerContext::CreateShader(const CKShaderDesc *desc, CKDWORD *out,
                                                ShaderJob *job)
{
    if (out) *out = 0;
    if (!Ready()) return CKERR_INVALIDOPERATION;
    const SDL_GPUShaderFormat format = desc
        ? NativeShaderFormat(desc->Format, desc->Profile) : SDL_GPU_SHADERFORMAT_INVALID;
    if (!desc || !out || !desc->Code || !desc->CodeSize || !desc->EntryPoint || !*desc->EntryPoint ||
        format == SDL_GPU_SHADERFORMAT_INVALID ||
        desc->SamplerCount > 16 || desc->UniformBufferCount > 4 || desc->StorageBufferCount || desc->StorageTextureCount ||
        (desc->Stage != CKRST_SHADER_VERTEX && desc->Stage != CKRST_SHADER_PIXEL)) return CKERR_INVALIDPARAMETER;
    auto shader = std::make_shared<CKSdlGpuShader>();
    shader->Desc = *desc;
    shader->SamplerCount = desc->SamplerCount;
    if (job) {
        job->Add(shader, format);
    } else {
        const SDL_GPUShaderCreateInfo info = CKSdlGpuShaderInfo(*shader, format);
        shader->Shader = CKSdlGpuOwn(Device, SDL_CreateGPUShader(Device, &info), SDL_ReleaseGPUShader);
        if (!shader->Shader) return Fail("CreateGPUShader");
    }
    *out = ShaderObjects.Add(shader);
    return *out ? CK_OK : CKERR_OUTOFMEMORY;
}

CKERROR CKSdlGpuRasterizerContext::CreateProgram(const CKFFProgramDesc *desc, CKDWORD *out)
{
    if (out) *out = 0;
    if (!Ready()) return CKERR_INVALIDOPERATION;
    if (!desc || !out) return CKERR_INVALIDPARAMETER;
    auto vertex = ShaderObjects.Get(desc->VertexShader);
    auto fragment = ShaderObjects.Get(desc->PixelShader);
    if (!vertex || !fragment) return CKERR_INVALIDPARAMETER;
    const CKERROR valid = CKFFValidateProgram(*desc, vertex->Desc, fragment->Desc);
    if (valid != CK_OK) return valid;
    PruneProgramCaches();

    auto program = std::make_shared<CKSdlGpuProgram>();
    program->Vertex = std::move(vertex);
    program->Fragment = std::move(fragment);
    program->Interface = *desc;
    program->UniformLayout.Init(program->Interface);
    for (int i = 0; i < CKRST_ATTRIB_COUNT; ++i)
        program->AttributeLocations[i] = -1;

    // Attribute identities describe geometry; shader locations and missing
    // stream values come entirely from this program's resource interface.
    if (desc->VertexInputs.Size() != 0) {
        CKSdlGpuDefaultVertexKey defaults;
        for (int i = 0; i < desc->VertexInputs.Size(); ++i) {
            const CKFFVertexInput &input = desc->VertexInputs[i];
            program->AttributeLocations[input.Attribute] = int(input.Location);
            std::copy(input.DefaultValue, input.DefaultValue + 4,
                      defaults.Values + input.Location * 4);
        }
        std::weak_ptr<SDL_GPUBuffer> *found =
            DefaultVertexBuffers.FindPtr(defaults);
        if (found) program->DefaultVertices = found->lock();
        if (!program->DefaultVertices) {
            SDL_GPUBufferCreateInfo info = {};
            info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            info.size = sizeof(defaults.Values);
            auto buffer = CKSdlGpuOwn(Device, SDL_CreateGPUBuffer(Device, &info), SDL_ReleaseGPUBuffer);
            if (!buffer) return Fail("CreateGPUBuffer.programDefaults");
            const CKERROR uploaded = UploadBuffer(buffer.get(), 0,
                                                      defaults.Values,
                                                      info.size, false);
            if (uploaded != CK_OK) return uploaded;
            program->DefaultVertices = buffer;
            DefaultVertexBuffers.Insert(defaults, buffer, TRUE);
        }
    }

    program->DefaultTextures.Reserve(desc->Samplers.Size());
    program->SamplerMetadataOffsets.Reserve(desc->Samplers.Size());
    for (int i = 0; i < desc->Samplers.Size(); ++i) {
        const CKFFSamplerBinding &binding = desc->Samplers[i];
        program->SamplerMetadataOffsets.PushBack(
            binding.MetadataBufferSlot == UINT32_MAX ? UINT32_MAX :
            program->UniformLayout.BufferOffset(
                binding.Stage, binding.MetadataBufferSlot));
        const uint64_t key = (uint64_t(unsigned(binding.Dimension)) << 32) |
                             binding.DefaultColor;
        std::weak_ptr<CKSdlGpuTexture> *found = DefaultTextures.FindPtr(key);
        auto texture = found ? found->lock() : nullptr;
        if (!texture) {
            CKTextureDesc textureDesc;
            VxPixelFormat2ImageDesc(_32_ARGB8888, textureDesc.Format);
            textureDesc.Format.Width = textureDesc.Format.Height = 1;
            textureDesc.Depth = 1;
            textureDesc.MipMapCount = 1;
            textureDesc.Flags = CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
            if (binding.Dimension == CKFF_TEXTURE_CUBE) textureDesc.Flags |= CKRST_TEXTURE_CUBEMAP;
            if (binding.Dimension == CKFF_TEXTURE_3D) textureDesc.Flags |= CKRST_TEXTURE_VOLUMEMAP;
            CKDWORD handle = 0;
            CKERROR error = CreateTexture(&textureDesc, nullptr, &handle);
            if (error != CK_OK) return error;
            CKDWORD color = binding.DefaultColor;
            VxImageDescEx pixels = textureDesc.Format;
            pixels.BytesPerLine = sizeof(color);
            pixels.Image = reinterpret_cast<CKBYTE *>(&color);
            const unsigned layers = binding.Dimension == CKFF_TEXTURE_CUBE ? 6u : 1u;
            for (unsigned layer = 0; layer < layers && error == CK_OK; ++layer)
                error = UpdateTexture(handle, 0, layer, nullptr, &pixels);
            texture = error == CK_OK ? Textures.Get(handle) : nullptr;
            // The program keeps the native image, never a public texture slot.
            // SDL retains uploads already referenced by the command buffer.
            DestroyObject(handle, CKRST_OBJ_TEXTURE);
            if (error != CK_OK) return error;
            DefaultTextures.Insert(key, texture, TRUE);
        }
        program->DefaultTextures.PushBack(std::move(texture));
    }

    // Reserve the declared constant capacity during program creation. Later
    // draws reuse these slots and the compiled packing layout without allocation.
    *out = Programs.Add(program);
    program->Identity = *out;
    return *out ? CK_OK : CKERR_OUTOFMEMORY;
}

CKBOOL CKSdlGpuRasterizerContext::IsNativeObjectAlive(CKDWORD object, CKDWORD type) const
{
    switch (type) {
    case CKRST_OBJ_TEXTURE: return bool(Textures.Get(object));
    case CKRST_OBJ_VERTEXBUFFER: return bool(VertexBuffers.Get(object));
    case CKRST_OBJ_INDEXBUFFER: return bool(IndexBuffers.Get(object));
    case CKRST_OBJ_VERTEXLAYOUT: return bool(Layouts.Get(object));
    case CKRST_OBJ_SHADER: return bool(ShaderObjects.Get(object));
    case CKRST_OBJ_PROGRAM: return bool(Programs.Get(object));
    case CKRST_OBJ_RENDERTARGET: return bool(Targets.Get(object));
    default: return FALSE;
    }
}

CKERROR CKSdlGpuRasterizerContext::DestroyObject(CKDWORD object, CKDWORD type)
{
    if (!Device || SDL_GetCurrentThreadID() != Thread) return CKERR_INVALIDOPERATION;
    bool removed = false;
    switch (type) {
    case CKRST_OBJ_TEXTURE: removed = Textures.Remove(object); break;
    case CKRST_OBJ_VERTEXBUFFER: removed = VertexBuffers.Remove(object); break;
    case CKRST_OBJ_INDEXBUFFER: removed = IndexBuffers.Remove(object); break;
    case CKRST_OBJ_VERTEXLAYOUT: removed = Layouts.Remove(object); break;
    case CKRST_OBJ_SHADER: removed = ShaderObjects.Remove(object); break;
    case CKRST_OBJ_PROGRAM:
        removed = Programs.Remove(object);
        if (removed) PruneProgramCaches();
        break;
    case CKRST_OBJ_RENDERTARGET: removed = Targets.Remove(object); break;
    }
    return removed ? CK_OK : CKERR_INVALIDPARAMETER;
}

CKERROR CKSdlGpuRasterizerContext::SetObjectName(CKDWORD object, CKDWORD type, const char *name)
{
    if (!Ready() || !name) return CKERR_INVALIDOPERATION;
    if (type == CKRST_OBJ_TEXTURE) {
        if (auto texture = Textures.Get(object)) {
            SDL_SetGPUTextureName(Device, texture->Image.get(), name);
            return CK_OK;
        }
    } else if (type == CKRST_OBJ_VERTEXBUFFER || type == CKRST_OBJ_INDEXBUFFER) {
        if (auto buffer = (type == CKRST_OBJ_VERTEXBUFFER ? VertexBuffers : IndexBuffers).Get(object)) {
            SDL_SetGPUBufferName(Device, buffer->Buffer.get(), name);
            return CK_OK;
        }
    }
    return CKERR_INVALIDPARAMETER;
}
