#include "CKSdlGpuInternal.h"
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
    case _DXT1: return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
    case _DXT3: return SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM;
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

CKERROR CKSdlGpuDevice::UploadBuffer(SDL_GPUBuffer *buffer, const void *data, unsigned size, bool cycle)
{
    if (!EnsureCommands()) return Error;
    SDL_GPUTransferBufferCreateInfo info = {SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size, 0};
    auto transfer = CKSdlGpuOwn(Device, SDL_CreateGPUTransferBuffer(Device, &info), SDL_ReleaseGPUTransferBuffer);
    if (!transfer) return Fail("CreateGPUTransferBuffer.upload");
    void *mapped = SDL_MapGPUTransferBuffer(Device, transfer.get(), false);
    if (!mapped) return Fail("MapGPUTransferBuffer.upload");
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(Device, transfer.get());
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(Commands);
    if (!copy) return Fail("BeginGPUCopyPass.upload");
    SDL_GPUTransferBufferLocation source = {transfer.get(), 0};
    SDL_GPUBufferRegion destination = {buffer, 0, size};
    SDL_UploadToGPUBuffer(copy, &source, &destination, cycle);
    SDL_EndGPUCopyPass(copy);
    ++FrameStats.BufferUploads;
    return CK_OK;
}

CKERROR CKSdlGpuBackend::CreateBuffer(const CKBackendBufferDesc *desc, CKDWORD *out)
{
    if (out) *out = 0;
    if (!m->Ready()) return CKERR_INVALIDOPERATION;
    if (!desc || !out || !desc->Size || unsigned(desc->Kind) > CKRST_BACKEND_BUFFER_INDEX ||
        (desc->Kind == CKRST_BACKEND_BUFFER_VERTEX && (!desc->Stride || (desc->Layout && !m->Layouts.Get(desc->Layout)))))
        return CKERR_INVALIDPARAMETER;
    const CKERROR flush = m->Flush();
    if (flush != CK_OK) return flush;
    auto buffer = std::make_shared<CKSdlGpuBuffer>();
    buffer->Desc = *desc; buffer->Desc.InitialData = nullptr;
    buffer->Shadow.resize(desc->Size);
    if (desc->InitialData) std::memcpy(buffer->Shadow.data(), desc->InitialData, desc->Size);
    SDL_GPUBufferCreateInfo info = {};
    info.usage = desc->Kind == CKRST_BACKEND_BUFFER_VERTEX ? SDL_GPU_BUFFERUSAGE_VERTEX : SDL_GPU_BUFFERUSAGE_INDEX;
    info.size = desc->Size;
    buffer->Buffer = CKSdlGpuOwn(m->Device, SDL_CreateGPUBuffer(m->Device, &info), SDL_ReleaseGPUBuffer);
    if (!buffer->Buffer) return m->Fail("CreateGPUBuffer");
    if (m->UploadBuffer(buffer->Buffer.get(), buffer->Shadow.data(), desc->Size, false) != CK_OK) return m->Error;
    *out = (desc->Kind == CKRST_BACKEND_BUFFER_VERTEX ? m->VertexBuffers : m->IndexBuffers).Add(buffer);
    return *out ? CK_OK : CKERR_OUTOFMEMORY;
}

CKERROR CKSdlGpuBackend::UpdateBuffer(CKBackendBufferKind kind, CKDWORD handle, CKDWORD offset,
                                     CKDWORD size, const void *data)
{
    if (!m->Ready()) return CKERR_INVALIDOPERATION;
    if (unsigned(kind) > CKRST_BACKEND_BUFFER_INDEX) return CKERR_INVALIDPARAMETER;
    auto buffer = (kind == CKRST_BACKEND_BUFFER_VERTEX ? m->VertexBuffers : m->IndexBuffers).Get(handle);
    if (!buffer || !data || !size || offset > buffer->Shadow.size() || size > buffer->Shadow.size() - offset)
        return CKERR_INVALIDPARAMETER;
    const CKERROR flush = m->Flush();
    if (flush != CK_OK) return flush;
    std::memcpy(buffer->Shadow.data() + offset, data, size);
    // Cycling is safe only because this upload defines the entire new version.
    return m->UploadBuffer(buffer->Buffer.get(), buffer->Shadow.data(), unsigned(buffer->Shadow.size()), true);
}

CKERROR CKSdlGpuBackend::CreateTexture(const CKTextureDesc *desc, const VxImageDescEx *data, CKDWORD *out)
{
    if (out) *out = 0;
    if (!m->Ready()) return CKERR_INVALIDOPERATION;
    if (!desc || !out || desc->Format.Width <= 0 || desc->Format.Height <= 0 ||
        unsigned(desc->Format.Width) > m->Caps.MaxTextureSize || unsigned(desc->Format.Height) > m->Caps.MaxTextureSize)
        return CKERR_INVALIDPARAMETER;
    auto texture = std::make_shared<CKSdlGpuTexture>();
    texture->PixelFormat = VxImageDesc2PixelFormat(desc->Format);
    if (texture->PixelFormat == UNKNOWN_PF) return CKERR_NOTIMPLEMENTED;
    texture->Flags = desc->Flags;
    auto &info = texture->Info;
    info.type = desc->Flags & CKRST_TEXTURE_CUBEMAP ? SDL_GPU_TEXTURETYPE_CUBE :
        (desc->Flags & CKRST_TEXTURE_VOLUMEMAP ? SDL_GPU_TEXTURETYPE_3D : SDL_GPU_TEXTURETYPE_2D);
    info.format = CKSdlGpuTextureFormat(texture->PixelFormat);
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
    if (texture->AutoMips && (texture->PixelFormat == _DXT1 || texture->PixelFormat == _DXT3 ||
                              texture->PixelFormat == _DXT5)) info.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    info.num_levels = texture->AutoMips ? fullLevels : std::max(1u, unsigned(desc->MipMapCount));
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    // SDL 3.4's D3D12 3D RTV allocation uses base depth for every mip.
    // Keep volumes sample/copy-only and render their slices through 2D targets.
    if (info.type == SDL_GPU_TEXTURETYPE_3D) {
        if ((desc->Flags & CKRST_TEXTURE_RENDERTARGET) && !SDL_GPUTextureSupportsFormat(m->Device,
            info.format, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)) return CKERR_NOTIMPLEMENTED;
    } else if (desc->Flags & CKRST_TEXTURE_RENDERTARGET) info.usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    if (texture->AutoMips && info.type != SDL_GPU_TEXTURETYPE_3D && SDL_GPUTextureSupportsFormat(m->Device,
        info.format, info.type, info.usage | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)) info.usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    if (!SDL_GPUTextureSupportsFormat(m->Device, info.format, info.type, info.usage)) return CKERR_NOTIMPLEMENTED;
    texture->Samples = std::max(1u, unsigned(CKRSTTextureMSAASamples(desc->Flags)));
    if (texture->Samples > m->Caps.MaxMSAASamples ||
        (texture->Samples > 1 && (info.type != SDL_GPU_TEXTURETYPE_2D || info.num_levels > 1 ||
                                !(desc->Flags & CKRST_TEXTURE_RENDERTARGET)))) return CKERR_NOTIMPLEMENTED;
    texture->Image = CKSdlGpuOwn(m->Device, SDL_CreateGPUTexture(m->Device, &info), SDL_ReleaseGPUTexture);
    if (!texture->Image) return m->Fail("CreateGPUTexture");
    if (texture->Samples > 1) {
        auto attachment = info;
        attachment.sample_count = CKSdlGpuSampleCount(texture->Samples);
        attachment.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        texture->Multisample = CKSdlGpuOwn(m->Device, SDL_CreateGPUTexture(m->Device, &attachment), SDL_ReleaseGPUTexture);
        if (!texture->Multisample) return m->Fail("CreateGPUTexture.multisample");
    }
    texture->Defined.resize(size_t(info.num_levels) * info.layer_count_or_depth);
    texture->GpuMipBase.resize(info.layer_count_or_depth);
    *out = m->Textures.Add(texture);
    if (!*out) return CKERR_OUTOFMEMORY;
    if (data) {
        const CKERROR error = UpdateTexture(*out, 0, 0, nullptr, data);
        if (error != CK_OK) { m->Textures.Remove(*out); *out = 0; return error; }
    }
    return CK_OK;
}

CKERROR CKSdlGpuBackend::CreateDepthTexture(const CKBackendDepthDesc *desc, CKDWORD *out)
{
    if (out) *out = 0;
    if (!m->Ready()) return CKERR_INVALIDOPERATION;
    if (!desc || !out || !desc->Width || !desc->Height || desc->Width > m->Caps.MaxTextureSize ||
        desc->Height > m->Caps.MaxTextureSize || desc->Samples > m->Caps.MaxMSAASamples ||
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
    texture->Image = CKSdlGpuOwn(m->Device, SDL_CreateGPUTexture(m->Device, &info), SDL_ReleaseGPUTexture);
    if (!texture->Image) return m->Fail("CreateGPUTexture.depth");
    *out = m->Textures.Add(texture);
    return *out ? CK_OK : CKERR_OUTOFMEMORY;
}

CKERROR CKSdlGpuBackend::CreateRenderTarget(const CKBackendRenderTargetDesc *desc, CKDWORD *out)
{
    if (out) *out = 0;
    if (!m->Ready()) return CKERR_INVALIDOPERATION;
    if (!desc || !out) return CKERR_INVALIDPARAMETER;
    auto target = std::make_shared<CKSdlGpuTarget>();
    target->Desc = *desc;
    target->Color = m->Textures.Get(desc->ColorTexture);
    target->Depth = m->Textures.Get(desc->DepthTexture);
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
        target->VolumeSlice = CKSdlGpuOwn(m->Device, SDL_CreateGPUTexture(m->Device, &slice), SDL_ReleaseGPUTexture);
        if (!target->VolumeSlice) return m->Fail("CreateGPUTexture.volumeTarget");
    }
    *out = m->Targets.Add(target);
    return *out ? CK_OK : CKERR_OUTOFMEMORY;
}

CKERROR CKSdlGpuDevice::PreserveTexture(CKSdlGpuTexture &texture)
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
            if (!texture.Defined[mip * texture.Info.layer_count_or_depth + layer]) continue;
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

CKERROR CKSdlGpuDevice::UploadTexture(CKSdlGpuTexture &texture, unsigned mip, unsigned layer,
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
    if (!compressed && (sourceFormat == _DXT1 || sourceFormat == _DXT3 || sourceFormat == _DXT5)) {
        std::vector<unsigned char> decoded;
        if (!CKSdlGpuDecodeDXT(data, decoded)) return CKERR_INVALIDPARAMETER;
        VxImageDescEx image;
        VxPixelFormat2ImageDesc(_32_ARGB8888, image);
        image.Width = w; image.Height = h; image.BytesPerLine = w * 4; image.Image = decoded.data();
        return UploadTexture(texture, mip, layer, rect, image);
    }
    if (!compressed && !ImagePitch(data)) return CKERR_INVALIDPARAMETER;
    if (compressed && (sourceFormat != texture.PixelFormat || x % 4 || y % 4 ||
                       (w % 4 && x + w != width) || (h % 4 && y + h != height))) return CKERR_INVALIDPARAMETER;
    const unsigned block = compressed ? SDL_GPUTextureFormatTexelBlockSize(texture.Info.format) : 0;
    const unsigned bytes = compressed ? 0 : SDL_GPUTextureFormatTexelBlockSize(texture.Info.format);
    const unsigned rowBytes = compressed ? ((w + 3) / 4) * block : w * bytes;
    const unsigned rows = compressed ? (h + 3) / 4 : h;
    if (uint64_t(rowBytes) * rows > UINT32_MAX) return CKERR_INVALIDPARAMETER;
    std::vector<CKBYTE> pixels(size_t(rowBytes) * rows);
    if (compressed) {
        if (data.TotalImageSize > 0 && unsigned(data.TotalImageSize) < pixels.size()) return CKERR_INVALIDPARAMETER;
        std::memcpy(pixels.data(), data.Image, pixels.size());
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
            auto *dst = pixels.data() + row * rowBytes + col * 4;
            dst[0] = CKBYTE(std::clamp(int((float(u) / scale * 0.5f + 0.5f) * 255 + 0.5f), 0, 255));
            dst[1] = CKBYTE(std::clamp(int((float(v) / scale * 0.5f + 0.5f) * 255 + 0.5f), 0, 255));
            dst[2] = CKBYTE(l); dst[3] = 255;
        }
    } else if (sourceFormat == texture.PixelFormat &&
               (sourceFormat == _32_ARGB8888 || sourceFormat == _32_ABGR8888 || sourceFormat == _16_V8U8 ||
                sourceFormat == _32_V16U16)) {
        for (unsigned row = 0; row < h; ++row)
            std::memcpy(pixels.data() + row * rowBytes, data.Image + row * ImagePitch(data), rowBytes);
    } else {
        VxImageDescEx source = data, dest;
        source.BytesPerLine = ImagePitch(data);
        VxPixelFormat2ImageDesc(texture.Info.format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ? _32_ABGR8888 : _32_ARGB8888, dest);
        dest.Width = w; dest.Height = h; dest.BytesPerLine = rowBytes; dest.Image = pixels.data();
        VxDoBlit(source, dest);
    }
    if (texture.AutoMips && mip == 0) {
        if (texture.GpuMipBase[layer]) {
            const CKERROR error = UploadTexturePixels(texture, mip, layer, x, y, w, h, pixels);
            return error == CK_OK ? GenerateGpuMips(texture, layer) : error;
        }
        // Keep each base face/slice in native storage format. A patch must not
        // erase the other faces, slices, or texels used to regenerate the chain.
        if (texture.BasePixels.empty()) texture.BasePixels.resize(texture.Info.layer_count_or_depth);
        auto &base = texture.BasePixels[layer];
        const bool initialize = base.empty();
        if (initialize) base.resize(size_t(width) * height * bytes);
        for (unsigned row = 0; row < h; ++row)
            std::memcpy(base.data() + (size_t(y + row) * width + x) * bytes,
                        pixels.data() + row * rowBytes, rowBytes);
        const CKERROR error = initialize ? UploadTexturePixels(texture, 0, layer, 0, 0, width, height, base)
            : UploadTexturePixels(texture, mip, layer, x, y, w, h, pixels);
        return error == CK_OK ? GenerateUploadMips(texture, layer) : error;
    }
    return UploadTexturePixels(texture, mip, layer, x, y, w, h, pixels);
}

CKERROR CKSdlGpuDevice::UploadTexturePixels(CKSdlGpuTexture &texture, unsigned mip, unsigned layer,
                                           unsigned x, unsigned y, unsigned w, unsigned h,
                                           const std::vector<CKBYTE> &pixels)
{
    if (!EnsureCommands()) return Error;
    SDL_GPUTransferBufferCreateInfo transferInfo = {SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, unsigned(pixels.size()), 0};
    auto transfer = CKSdlGpuOwn(Device, SDL_CreateGPUTransferBuffer(Device, &transferInfo), SDL_ReleaseGPUTransferBuffer);
    if (!transfer) return Fail("CreateGPUTransferBuffer.texture");
    void *mapped = SDL_MapGPUTransferBuffer(Device, transfer.get(), false);
    if (!mapped) return Fail("MapGPUTransferBuffer.texture");
    std::memcpy(mapped, pixels.data(), pixels.size());
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
    texture.Defined[mip * texture.Info.layer_count_or_depth + layer] = true;
    ++FrameStats.TextureUploads;
    return CK_OK;
}

CKERROR CKSdlGpuDevice::GenerateUploadMips(CKSdlGpuTexture &texture, unsigned layer)
{
    const bool volume = texture.Info.type == SDL_GPU_TEXTURETYPE_3D;
    if (volume) {
        // A volume mip averages adjacent depth slices. Wait until all source
        // slices exist instead of generating from uninitialized GPU storage.
        for (const auto &slice : texture.BasePixels) if (slice.empty()) return CK_OK;
    }
    const unsigned bytes = SDL_GPUTextureFormatTexelBlockSize(texture.Info.format);
    const bool signed8 = texture.Info.format == SDL_GPU_TEXTUREFORMAT_R8G8_SNORM;
    const bool signed16 = texture.Info.format == SDL_GPU_TEXTUREFORMAT_R16G16_SNORM;
    const unsigned componentBytes = signed16 ? 2 : 1;
    std::vector<std::vector<CKBYTE>> previous = volume ? texture.BasePixels :
        std::vector<std::vector<CKBYTE>>{texture.BasePixels[layer]};
    unsigned pw = texture.Info.width, ph = texture.Info.height, pd = unsigned(previous.size());
    for (unsigned mip = 1; mip < texture.Info.num_levels; ++mip) {
        const unsigned nw = std::max(1u, pw >> 1), nh = std::max(1u, ph >> 1);
        const unsigned nd = volume ? std::max(1u, pd >> 1) : 1;
        std::vector<std::vector<CKBYTE>> next(nd, std::vector<CKBYTE>(size_t(nw) * nh * bytes));
        for (unsigned z = 0; z < nd; ++z) for (unsigned y = 0; y < nh; ++y) for (unsigned x = 0; x < nw; ++x) {
            const unsigned x0 = x * pw / nw, x1 = (x + 1) * pw / nw;
            const unsigned y0 = y * ph / nh, y1 = (y + 1) * ph / nh;
            const unsigned z0 = z * pd / nd, z1 = (z + 1) * pd / nd;
            const int count = int((x1 - x0) * (y1 - y0) * (z1 - z0));
            for (unsigned c = 0; c < bytes; c += componentBytes) {
                int sum = 0;
                for (unsigned sz = z0; sz < z1; ++sz) for (unsigned sy = y0; sy < y1; ++sy)
                    for (unsigned sx = x0; sx < x1; ++sx) {
                        const auto *source = previous[sz].data() + (size_t(sy) * pw + sx) * bytes + c;
                        if (signed16) { int16_t value; std::memcpy(&value, source, 2); sum += std::max(-32767, int(value)); }
                        else if (signed8) sum += std::max(-127, int(static_cast<int8_t>(*source)));
                        else sum += *source;
                    }
                const int average = (sum + (sum < 0 ? -count / 2 : count / 2)) / count;
                auto *destination = next[z].data() + (size_t(y) * nw + x) * bytes + c;
                if (signed16) { const int16_t value = int16_t(average); std::memcpy(destination, &value, 2); }
                else *destination = CKBYTE(average);
            }
        }
        for (unsigned z = 0; z < nd; ++z) {
            const CKERROR error = UploadTexturePixels(texture, mip, volume ? z : layer, 0, 0, nw, nh, next[z]);
            if (error != CK_OK) return error;
        }
        previous = std::move(next); pw = nw; ph = nh; pd = nd;
    }
    return CK_OK;
}

CKERROR CKSdlGpuDevice::GenerateGpuMips(CKSdlGpuTexture &texture, unsigned layer)
{
    if (!texture.AutoMips) return CK_OK;
    if (texture.Info.type == SDL_GPU_TEXTURETYPE_3D) {
        // Any GPU write invalidates the CPU reduction of the whole volume.
        std::fill(texture.GpuMipBase.begin(), texture.GpuMipBase.end(), true);
        texture.BasePixels.clear();
        for (unsigned z = 0; z < texture.Info.layer_count_or_depth; ++z)
            if (!texture.Defined[z]) return CK_OK;
        return GenerateVolumeMips(texture);
    }
    if (!(texture.Info.usage & SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)) return CKERR_NOTIMPLEMENTED;
    texture.GpuMipBase[layer] = true;
    if (!texture.BasePixels.empty()) texture.BasePixels[layer].clear();
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
        texture.Defined[mip * texture.Info.layer_count_or_depth + layer] = true;
    }
    return CK_OK;
}

CKERROR CKSdlGpuBackend::UpdateTexture(CKDWORD handle, CKDWORD mip, CKDWORD layer,
                                     const CKRECT *rect, const VxImageDescEx *data)
{
    if (!m->Ready()) return CKERR_INVALIDOPERATION;
    auto texture = m->Textures.Get(handle);
    if (!texture || texture->Depth || !data || !data->Image || mip >= texture->Info.num_levels ||
        layer >= CKSdlGpuTextureLayers(*texture, mip)) return CKERR_INVALIDPARAMETER;
    const CKERROR flush = m->Flush();
    if (flush != CK_OK) return flush;
    const CKERROR error = m->PreserveTexture(*texture);
    return error == CK_OK ? m->UploadTexture(*texture, mip, layer, rect, *data) : error;
}

CKERROR CKSdlGpuBackend::CreateVertexLayout(const CKVertexLayoutDesc *desc, CKDWORD *out)
{
    if (out) *out = 0;
    if (!m->Ready()) return CKERR_INVALIDOPERATION;
    if (!desc || !out || !desc->Elements || !desc->ElementCount || desc->ElementCount > 16 || !desc->Stride)
        return CKERR_INVALIDPARAMETER;
    auto layout = std::make_shared<CKSdlGpuLayout>();
    std::array<bool, CKRST_ATTRIB_COUNT> attributes = {};
    layout->Elements.reserve(desc->ElementCount);
    for (unsigned i = 0; i < desc->ElementCount; ++i) {
        const auto &e = desc->Elements[i];
        const unsigned attribute = static_cast<unsigned>(e.Attrib);
        const unsigned elementBytes = e.Count * (e.Type == CKRST_ATTRIBTYPE_FLOAT ? 4u :
            (e.Type == CKRST_ATTRIBTYPE_INT8 || e.Type == CKRST_ATTRIBTYPE_UINT8 ? 1u : 2u));
        if (attribute >= CKRST_ATTRIB_COUNT || attributes[attribute] ||
            CKSdlGpuVertexFormat(e) == SDL_GPU_VERTEXELEMENTFORMAT_INVALID ||
            e.Offset >= desc->Stride || elementBytes > unsigned(desc->Stride - e.Offset)) return CKERR_INVALIDPARAMETER;
        attributes[attribute] = true;
        layout->Elements.push_back(e);
    }
    layout->Stride = desc->Stride;
    *out = m->Layouts.Add(layout);
    return *out ? CK_OK : CKERR_OUTOFMEMORY;
}

CKERROR CKSdlGpuBackend::CreateShader(const CKShaderDesc *desc, CKDWORD *out)
{
    if (out) *out = 0;
    if (!m->Ready()) return CKERR_INVALIDOPERATION;
    if (!desc || !out || !desc->Code || !desc->CodeSize || !desc->EntryPoint || !*desc->EntryPoint ||
        desc->Format != m->Caps.ShaderFormat || desc->Profile != m->Caps.ShaderProfile ||
        desc->SamplerCount > 16 || desc->UniformBufferCount > 4 || desc->StorageBufferCount || desc->StorageTextureCount ||
        (desc->Stage != CKRST_SHADER_VERTEX && desc->Stage != CKRST_SHADER_PIXEL)) return CKERR_INVALIDPARAMETER;
    SDL_GPUShaderCreateInfo info = {};
    info.code = desc->Code; info.code_size = desc->CodeSize; info.entrypoint = desc->EntryPoint;
    info.format = m->ShaderFormat;
    info.stage = desc->Stage == CKRST_SHADER_VERTEX ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = desc->SamplerCount; info.num_uniform_buffers = desc->UniformBufferCount;
    auto shader = std::make_shared<CKSdlGpuShader>();
    shader->Desc = *desc;
    shader->Shader = CKSdlGpuOwn(m->Device, SDL_CreateGPUShader(m->Device, &info), SDL_ReleaseGPUShader);
    if (!shader->Shader) return m->Fail("CreateGPUShader");
    *out = m->ShaderObjects.Add(shader);
    return *out ? CK_OK : CKERR_OUTOFMEMORY;
}

CKERROR CKSdlGpuBackend::CreateProgram(const CKBackendProgramDesc *desc, CKDWORD *out)
{
    if (out) *out = 0;
    if (!m->Ready()) return CKERR_INVALIDOPERATION;
    if (!desc || !out) return CKERR_INVALIDPARAMETER;
    auto vertex = m->ShaderObjects.Get(desc->VertexShader);
    auto fragment = m->ShaderObjects.Get(desc->PixelShader);
    if (!vertex || !fragment) return CKERR_INVALIDPARAMETER;
    const CKERROR valid = CKValidateBackendProgram(*desc, vertex->Desc, fragment->Desc);
    if (valid != CK_OK) return valid;
    m->PruneProgramCaches();

    auto program = std::make_shared<CKSdlGpuProgram>();
    program->Vertex = std::move(vertex);
    program->Fragment = std::move(fragment);
    program->Interface = *desc;
    program->UniformLayout.Init(program->Interface);
    program->AttributeLocations.fill(-1);

    // Attribute identities describe geometry; shader locations and missing
    // stream values come entirely from this program's resource interface.
    if (!desc->VertexInputs.empty()) {
        std::vector<CKDWORD> defaults(16 * 4, 0);
        for (const auto &input : desc->VertexInputs) {
            program->AttributeLocations[input.Attribute] = int(input.Location);
            std::copy(input.DefaultValue.begin(), input.DefaultValue.end(), defaults.begin() + input.Location * 4);
        }
        const auto found = m->DefaultVertexBuffers.find(defaults);
        if (found != m->DefaultVertexBuffers.end()) program->DefaultVertices = found->second.lock();
        if (!program->DefaultVertices) {
            SDL_GPUBufferCreateInfo info = {};
            info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            info.size = unsigned(defaults.size() * sizeof(CKDWORD));
            auto buffer = CKSdlGpuOwn(m->Device, SDL_CreateGPUBuffer(m->Device, &info), SDL_ReleaseGPUBuffer);
            if (!buffer) return m->Fail("CreateGPUBuffer.programDefaults");
            const CKERROR uploaded = m->UploadBuffer(buffer.get(), defaults.data(), info.size, false);
            if (uploaded != CK_OK) return uploaded;
            program->DefaultVertices = buffer;
            m->DefaultVertexBuffers[std::move(defaults)] = buffer;
        }
    }

    program->DefaultTextures.reserve(desc->Samplers.size());
    program->SamplerMetadataOffsets.reserve(desc->Samplers.size());
    for (const auto &binding : desc->Samplers) {
        program->SamplerMetadataOffsets.push_back(binding.MetadataBufferSlot == ~0u ? ~0u :
            program->UniformLayout.BufferOffset(binding.Stage, binding.MetadataBufferSlot));
        const auto key = std::make_pair(unsigned(binding.Dimension), binding.DefaultColor);
        const auto found = m->DefaultTextures.find(key);
        auto texture = found == m->DefaultTextures.end() ? nullptr : found->second.lock();
        if (!texture) {
            CKTextureDesc textureDesc;
            VxPixelFormat2ImageDesc(_32_ARGB8888, textureDesc.Format);
            textureDesc.Format.Width = textureDesc.Format.Height = 1;
            textureDesc.Depth = 1;
            textureDesc.MipMapCount = 1;
            textureDesc.Flags = CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
            if (binding.Dimension == CKBACKEND_TEXTURE_CUBE) textureDesc.Flags |= CKRST_TEXTURE_CUBEMAP;
            if (binding.Dimension == CKBACKEND_TEXTURE_3D) textureDesc.Flags |= CKRST_TEXTURE_VOLUMEMAP;
            CKDWORD handle = 0;
            CKERROR error = CreateTexture(&textureDesc, nullptr, &handle);
            if (error != CK_OK) return error;
            CKDWORD color = binding.DefaultColor;
            VxImageDescEx pixels = textureDesc.Format;
            pixels.BytesPerLine = sizeof(color);
            pixels.Image = reinterpret_cast<CKBYTE *>(&color);
            const unsigned layers = binding.Dimension == CKBACKEND_TEXTURE_CUBE ? 6u : 1u;
            for (unsigned layer = 0; layer < layers && error == CK_OK; ++layer)
                error = UpdateTexture(handle, 0, layer, nullptr, &pixels);
            texture = error == CK_OK ? m->Textures.Get(handle) : nullptr;
            // The program keeps the native image, never a public texture slot.
            // SDL retains uploads already referenced by the command buffer.
            DestroyObject(handle, CKRST_OBJ_TEXTURE);
            if (error != CK_OK) return error;
            m->DefaultTextures[key] = texture;
        }
        program->DefaultTextures.push_back(std::move(texture));
    }

    // Reserve the declared constant capacity during program creation. Later
    // draws reuse these slots and the compiled packing layout without allocation.
    for (const auto &uniform : desc->Uniforms) {
        auto &bytes = m->Constants[uniform.Slot].Bytes;
        if (bytes.size() < uniform.Size()) bytes.resize(uniform.Size());
    }
    *out = m->Programs.Add(program);
    program->Identity = *out;
    return *out ? CK_OK : CKERR_OUTOFMEMORY;
}

CKBOOL CKSdlGpuBackend::IsObjectAlive(CKDWORD object, CKDWORD type) const
{
    switch (type) {
    case CKRST_OBJ_TEXTURE: return bool(m->Textures.Get(object));
    case CKRST_OBJ_VERTEXBUFFER: return bool(m->VertexBuffers.Get(object));
    case CKRST_OBJ_INDEXBUFFER: return bool(m->IndexBuffers.Get(object));
    case CKRST_OBJ_VERTEXLAYOUT: return bool(m->Layouts.Get(object));
    case CKRST_OBJ_SHADER: return bool(m->ShaderObjects.Get(object));
    case CKRST_OBJ_PROGRAM: return bool(m->Programs.Get(object));
    case CKRST_OBJ_RENDERTARGET: return bool(m->Targets.Get(object));
    default: return FALSE;
    }
}

CKERROR CKSdlGpuBackend::DestroyObject(CKDWORD object, CKDWORD type)
{
    if (!m->Device || SDL_GetCurrentThreadID() != m->Thread) return CKERR_INVALIDOPERATION;
    bool removed = false;
    switch (type) {
    case CKRST_OBJ_TEXTURE: removed = m->Textures.Remove(object); break;
    case CKRST_OBJ_VERTEXBUFFER: removed = m->VertexBuffers.Remove(object); break;
    case CKRST_OBJ_INDEXBUFFER: removed = m->IndexBuffers.Remove(object); break;
    case CKRST_OBJ_VERTEXLAYOUT: removed = m->Layouts.Remove(object); break;
    case CKRST_OBJ_SHADER: removed = m->ShaderObjects.Remove(object); break;
    case CKRST_OBJ_PROGRAM:
        removed = m->Programs.Remove(object);
        if (removed) m->PruneProgramCaches();
        break;
    case CKRST_OBJ_RENDERTARGET: removed = m->Targets.Remove(object); break;
    }
    return removed ? CK_OK : CKERR_INVALIDPARAMETER;
}

void CKSdlGpuBackend::SetObjectName(CKDWORD object, CKDWORD type, const char *name)
{
    if (!m->Ready() || !name) return;
    if (type == CKRST_OBJ_TEXTURE) {
        if (auto texture = m->Textures.Get(object)) SDL_SetGPUTextureName(m->Device, texture->Image.get(), name);
    } else if (type == CKRST_OBJ_VERTEXBUFFER || type == CKRST_OBJ_INDEXBUFFER) {
        if (auto buffer = (type == CKRST_OBJ_VERTEXBUFFER ? m->VertexBuffers : m->IndexBuffers).Get(object))
            SDL_SetGPUBufferName(m->Device, buffer->Buffer.get(), name);
    }
}
