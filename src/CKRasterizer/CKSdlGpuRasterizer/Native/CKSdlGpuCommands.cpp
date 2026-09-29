#include "CKRenderProfile.h"
#include "CKSdlGpuRasterizerContext.h"

namespace {

struct CKSdlGpuByteRange {
    const CKBYTE *Data = nullptr;
    size_t Size = 0;
};

bool CKSdlGpuVertexTokenBefore(const CKSdlGpuTransientVertexInfo &info, CKDWORD token)
{
    return info.Token < token;
}

bool CKSdlGpuIndexTokenBefore(const CKSdlGpuTransientIndexInfo &info, CKDWORD token)
{
    return info.Token < token;
}

bool CKSdlGpuValidateVertexGeometry(
    const CKSdlGpuLayout *layout,
    const CKSdlGpuBuffer *buffer,
    CKDWORD layoutHandle,
    const CKTransientVertexData *transient,
    CKDWORD start,
    CKDWORD count,
    const XArray<CKSdlGpuTransientVertexInfo> &allocations,
    const CKSdlGpuTransientStorage &storage,
    CKSdlGpuByteRange &snapshot)
{
    if (buffer)
        return uint64_t(start) + count <= buffer->Desc.Size / layout->Stride;
    if (!transient || !transient->Token)
        return false;
    const auto allocation = std::lower_bound(
        allocations.Begin(), allocations.End(), transient->Token,
        CKSdlGpuVertexTokenBefore);
    if (allocation == allocations.End() || allocation->Token != transient->Token)
        return false;
    const int index = (int)(allocation - allocations.Begin());
    const XArray<CKBYTE> *bytes = storage.Get(index);
    if (!bytes)
        return false;
    if (allocation->Layout != layoutHandle || transient->Layout != allocation->Layout ||
        transient->Stride != allocation->Stride || transient->Stride != layout->Stride ||
        transient->Data != bytes->Begin() ||
        uint64_t(transient->Count) * transient->Stride !=
            (uint64_t)bytes->Size() ||
        start > transient->Count || count > transient->Count - start)
        return false;
    snapshot.Data = bytes->Begin() + size_t(start) * layout->Stride;
    snapshot.Size = size_t(count) * layout->Stride;
    return true;
}

template<class Index>
bool CKSdlGpuValidateTransientIndices(const CKBYTE *bytes, CKDWORD start,
                                      CKDWORD count, CKDWORD vertexCount)
{
    const CKBYTE *first = bytes + size_t(start) * sizeof(Index);
    for (CKDWORD i = 0; i < count; ++i) {
        Index index = 0;
        std::memcpy(&index, first + size_t(i) * sizeof(Index), sizeof(Index));
        if (index >= vertexCount)
            return false;
    }
    return true;
}

uint64_t CKSdlGpuAlignedEnd(uint64_t offset, size_t size, unsigned alignment)
{
    if (!size)
        return offset;
    const uint64_t remainder = offset % alignment;
    if (remainder)
        offset += alignment - remainder;
    return offset + size;
}

void CKSdlGpuAppendBytes(XArray<CKBYTE> &destination,
                         const CKSdlGpuByteRange &source,
                         unsigned alignment,
                         unsigned &offset)
{
    if (!source.Size)
        return;
    const int oldSize = destination.Size();
    const int remainder = oldSize % (int)alignment;
    const int alignedSize = remainder
        ? oldSize + (int)alignment - remainder : oldSize;
    destination.Resize(alignedSize + (int)source.Size);
    if (alignedSize > oldSize)
        std::memset(destination.Begin() + oldSize, 0,
                    (size_t)(alignedSize - oldSize));
    offset = (unsigned)alignedSize;
    std::memcpy(destination.Begin() + alignedSize,
                source.Data, source.Size);
}

struct CKSdlGpuVertexBindingState {
    SDL_GPUBuffer *Buffer = nullptr;
    Uint32 Offset = 0;
    bool Valid = false;
};

void CKSdlGpuBindVertexBuffer(SDL_GPURenderPass *pass,
                              CKSdlGpuVertexBindingState *states,
                              Uint32 slot,
                              const SDL_GPUBufferBinding &binding)
{
    CKSdlGpuVertexBindingState &bound = states[slot];
    if (bound.Valid && bound.Buffer == binding.buffer && bound.Offset == binding.offset)
        return;
    SDL_BindGPUVertexBuffers(pass, slot, &binding, 1);
    bound.Buffer = binding.buffer;
    bound.Offset = binding.offset;
    bound.Valid = true;
}

bool CKSdlGpuNativePackedColor(SDL_GPUTextureFormat nativeFormat,
                               CKDWORD logicalFormat)
{
    return (logicalFormat == CKFF_COLOR_TARGET_RGB565 &&
            nativeFormat == SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM) ||
           (logicalFormat == CKFF_COLOR_TARGET_RGB5A1 &&
            nativeFormat == SDL_GPU_TEXTUREFORMAT_B5G5R5A1_UNORM);
}

bool CKSdlGpuNeedsOutputResolve(const std::shared_ptr<CKSdlGpuTarget> &target,
                                CKDWORD logicalFormat, bool dither)
{
    if (!target || logicalFormat == CKFF_COLOR_TARGET_RGBA8)
        return false;
    const bool nativePacked = CKSdlGpuNativePackedColor(
        target->Color->Info.format, logicalFormat);
    // A converted MSAA pass must keep using the converted attachment. Returning
    // to the texture's native MSAA image would load samples that predate the
    // last output resolve.
    return dither || !nativePacked || target->Color->Samples > 1;
}

CKDWORD CKSdlGpuQuantizeClearColor(CKDWORD color, CKDWORD format)
{
    if (format == CKFF_COLOR_TARGET_RGBA8)
        return color;
    const unsigned rbLevels = format == CKFF_COLOR_TARGET_RGBA4 ? 15u : 31u;
    const unsigned greenLevels = format == CKFF_COLOR_TARGET_RGB565 ? 63u : rbLevels;
    const unsigned alphaLevels = format == CKFF_COLOR_TARGET_RGB565 ? 0u :
        (format == CKFF_COLOR_TARGET_RGB5A1 ? 1u : 15u);
    const auto quantize = [](unsigned value, unsigned levels) {
        if (!levels)
            return 255u;
        const unsigned level = (value * levels + 127u) / 255u;
        return (level * 255u + levels / 2u) / levels;
    };
    return (quantize((color >> 24) & 255u, alphaLevels) << 24) |
           (quantize((color >> 16) & 255u, rbLevels) << 16) |
           (quantize((color >> 8) & 255u, greenLevels) << 8) |
           quantize(color & 255u, rbLevels);
}

const CKFFUniformBinding *CKSdlGpuFindUniform(
    const CKSdlGpuProgram &program, CK_SHADER_STAGE stage,
    CKFFConstantBlock block)
{
    for (int i = 0; i < program.Interface.Uniforms.Size(); ++i) {
        const CKFFUniformBinding &binding = program.Interface.Uniforms[i];
        if (binding.Stage == stage && binding.Slot == (CKDWORD)block)
            return &binding;
    }
    return nullptr;
}

bool CKSdlGpuPatchDepthPad(const CKSdlGpuProgram &program,
                           CKFFProgramLayout &layout, CKDWORD stage,
                           const float transform[4])
{
    if (stage >= CKFF_MAX_TEXTURE_STAGES)
        return false;
    const CKFFUniformBinding *matrixBinding = CKSdlGpuFindUniform(
        program, CKRST_SHADER_VERTEX, CKRST_BLOCK_TEX_MATRICES);
    const CKFFUniformBinding *stageBinding = CKSdlGpuFindUniform(
        program, CKRST_SHADER_VERTEX, CKRST_BLOCK_STAGE_PARAMS);
    if (!matrixBinding || !stageBinding)
        return false;
    const CKDWORD matrixBuffer = layout.BufferOffset(
        matrixBinding->Stage, matrixBinding->BufferSlot);
    const CKDWORD stageBuffer = layout.BufferOffset(
        stageBinding->Stage, stageBinding->BufferSlot);
    if (matrixBuffer == UINT32_MAX || stageBuffer == UINT32_MAX)
        return false;
    const CKDWORD matrixOffset = matrixBuffer + matrixBinding->Offset + stage * 64u;
    const CKDWORD stageOffset = stageBuffer + stageBinding->Offset + stage * 32u;
    if (matrixOffset + sizeof(VxMatrix) > (CKDWORD)layout.Data.Size() ||
        stageOffset + 8u > (CKDWORD)layout.Data.Size())
        return false;

    float packedFlags = 0.0f;
    std::memcpy(&packedFlags, layout.Data.Begin() + stageOffset + 4u,
                sizeof(packedFlags));
    CKDWORD flags = (CKDWORD)packedFlags;
    const CKDWORD componentCount = flags & 0xffu;
    VxMatrix original;
    const bool positionT = program.Vertex &&
        program.Vertex->Desc.UniformBufferCount == 1;
    if (!positionT && componentCount >= 1u && componentCount <= 4u)
        std::memcpy(&original, layout.Data.Begin() + matrixOffset,
                    sizeof(original));
    else
        original.SetIdentity();
    VxMatrix pad;
    pad.SetIdentity();
    pad[0][0] = transform[0];
    pad[3][0] = transform[1];
    pad[1][1] = transform[2];
    pad[3][1] = transform[3];
    VxMatrix composed;
    Vx3DMultiplyMatrix4(composed, pad, original);
    std::memcpy(layout.Data.Begin() + matrixOffset, &composed,
                sizeof(composed));
    flags |= CKFF_TTF_DEPTH_PAD;
    if (componentCount < 1u || componentCount > 4u)
        flags = (flags & ~0xffu) | 4u;
    packedFlags = (float)flags;
    std::memcpy(layout.Data.Begin() + stageOffset + 4u, &packedFlags,
                sizeof(packedFlags));
    return layout.MarkDataChanged(matrixOffset, sizeof(composed)) &&
           layout.MarkDataChanged(stageOffset + 4u, sizeof(packedFlags));
}

} // namespace

CKERROR CKSdlGpuRasterizerContext::BeginPass(const CKRenderPassDesc *desc)
{
    if (!Ready()) return CKERR_INVALIDOPERATION;
    if (!desc || desc->Rect.left < 0 || desc->Rect.top < 0 || desc->Rect.right <= desc->Rect.left ||
        desc->Rect.bottom <= desc->Rect.top) return CKERR_INVALIDPARAMETER;
    auto target = Targets.Get(desc->RenderTarget);
    if (desc->RenderTarget && !target) return CKERR_INVALIDPARAMETER;
    const CKERROR error = Flush();
    if (error != CK_OK) return error;
    Pass = *desc; Pass.Name = nullptr; Target = std::move(target); PassOpen = true;
    ++FrameStats.Passes;
    return CK_OK;
}

CKBOOL CKSdlGpuRasterizerContext::AllocTransientVertices(CKDWORD count, CKDWORD handle, CKTransientVertexData *out)
{
    if (out) *out = CKTransientVertexData();
    if (!Ready() || !out || !count || !NextTransientToken) return FALSE;
    auto layout = Layouts.Get(handle);
    if (!layout || uint64_t(count) * layout->Stride > 64u * 1024u * 1024u) return FALSE;
    const int index = TransientVertexInfo.Size();
    XArray<CKBYTE> *storage = TransientVertices.Acquire(index);
    if (!storage) return FALSE;
    storage->Resize((int)(count * layout->Stride));
    std::memset(storage->Begin(), 0, (size_t)storage->Size());
    TransientVertexInfo.PushBack(
        {NextTransientToken++, handle, layout->Stride});
    out->Data = storage->Begin(); out->Count = count; out->Stride = layout->Stride;
    out->Layout = handle; out->Token = TransientVertexInfo.Back().Token;
    return TRUE;
}

CKERROR CKSdlGpuRasterizerContext::PrepareDepthPad(
    const std::shared_ptr<CKSdlGpuTexture> &source,
    const CKSamplerDesc &sampler,
    std::shared_ptr<CKSdlGpuTexture> &padded,
    float transform[4])
{
    const bool padU = sampler.AddressU == CKRST_ADDRESS_BORDER;
    const bool padV = sampler.AddressV == CKRST_ADDRESS_BORDER;
    if (!source || !source->Depth || source->Samples != 1 ||
        source->Info.num_levels != 1 || (!padU && !padV))
        return CKERR_INVALIDPARAMETER;
    const CKDWORD borderDepth = (sampler.BorderColor >> 16) & 0xffu;
    for (int i = DepthPads.Size() - 1; i >= 0; --i) {
        std::shared_ptr<CKSdlGpuTexture> owner = DepthPads[i].Source.lock();
        if (!owner) {
            DepthPads.RemoveAt(i);
            continue;
        }
        if (owner == source && DepthPads[i].BorderDepth == borderDepth &&
            DepthPads[i].PadU == padU && DepthPads[i].PadV == padV) {
            padded = DepthPads[i].Texture;
            break;
        }
    }
    const unsigned width = source->Info.width + (padU ? 2u : 0u);
    const unsigned height = source->Info.height + (padV ? 2u : 0u);
    if (width > Caps.MaxTextureSize || height > Caps.MaxTextureSize)
        return CKERR_NOTIMPLEMENTED;
    if (!padded) {
        padded = std::make_shared<CKSdlGpuTexture>();
        padded->Info = source->Info;
        padded->Info.width = width;
        padded->Info.height = height;
        padded->Info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
                             SDL_GPU_TEXTUREUSAGE_SAMPLER;
        padded->Image = CKSdlGpuOwn(
            Device, SDL_CreateGPUTexture(Device, &padded->Info),
            SDL_ReleaseGPUTexture);
        if (!padded->Image)
            return Fail("CreateGPUTexture.depthPad");
        padded->Depth = true;
        padded->Samples = 1;
        CKSdlGpuDepthPad entry;
        entry.Source = source;
        entry.Texture = padded;
        entry.BorderDepth = borderDepth;
        entry.PadU = padU;
        entry.PadV = padV;
        DepthPads.PushBack(entry);
    }
    if (!EnsureCommands())
        return Error;
    SDL_GPUDepthStencilTargetInfo clear = {};
    clear.texture = padded->Image.get();
    clear.clear_depth = float(borderDepth) / 255.0f;
    clear.load_op = SDL_GPU_LOADOP_CLEAR;
    clear.store_op = SDL_GPU_STOREOP_STORE;
    clear.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
    clear.stencil_store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *clearPass = SDL_BeginGPURenderPass(
        Commands, nullptr, 0, &clear);
    if (!clearPass)
        return Fail("BeginGPURenderPass.depthPad");
    SDL_EndGPURenderPass(clearPass);

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(Commands);
    if (!copy)
        return Fail("BeginGPUCopyPass.depthPad");
    SDL_GPUTextureLocation from = {}, to = {};
    from.texture = source->Image.get();
    to.texture = padded->Image.get();
    to.x = padU ? 1u : 0u;
    to.y = padV ? 1u : 0u;
    SDL_CopyGPUTextureToTexture(copy, &from, &to,
                                source->Info.width, source->Info.height,
                                1, false);
    SDL_EndGPUCopyPass(copy);
    source->Referenced = true;
    transform[0] = float(source->Info.width) / float(width);
    transform[1] = padU ? 1.0f / float(width) : 0.0f;
    transform[2] = float(source->Info.height) / float(height);
    transform[3] = padV ? 1.0f / float(height) : 0.0f;
    return CK_OK;
}

CKBOOL CKSdlGpuRasterizerContext::AllocTransientIndices(CKDWORD count, CKBOOL index32, CKTransientIndexData *out)
{
    if (out) *out = CKTransientIndexData();
    if (!Ready() || !out || !count || !NextTransientToken || count > 16u * 1024u * 1024u) return FALSE;
    const int index = TransientIndexInfo.Size();
    XArray<CKBYTE> *storage = TransientIndices.Acquire(index);
    if (!storage) return FALSE;
    storage->Resize((int)(count * (index32 ? 4 : 2)));
    std::memset(storage->Begin(), 0, (size_t)storage->Size());
    TransientIndexInfo.PushBack(
        {NextTransientToken++, index32 ? TRUE : FALSE});
    out->Data = storage->Begin(); out->Count = count;
    out->Index32 = TransientIndexInfo.Back().Index32;
    out->Token = TransientIndexInfo.Back().Token;
    return TRUE;
}

CKERROR CKSdlGpuRasterizerContext::Draw(const CKDrawCommand *desc)
{
    CKRE_PROFILE_SCOPE("CKRE.SDL.RecordDraw");
    if (!Ready() || !PassOpen) return CKERR_INVALIDOPERATION;
    if (!desc || !desc->VertexCount) return CKERR_INVALIDPARAMETER;
    const bool isolateBlendedOutput = CKSdlGpuNeedsOutputResolve(
        Target, desc->ColorTargetFormat, desc->DitherEnable != FALSE) &&
        ((desc->Pipeline.State.Lo >> 16) & 15u) != 0;
    if (Draws.Size() != 0 &&
        (Draws.Back().DitherEnable != (desc->DitherEnable != FALSE) ||
         Draws.Back().ColorTargetFormat != desc->ColorTargetFormat ||
         isolateBlendedOutput)) {
        const CKERROR flushed = Flush(false);
        if (flushed != CK_OK) return flushed;
    }
    CKSdlGpuDraw draw;
    draw.State = desc->Pipeline;
    draw.LayoutHandle = desc->Layout; draw.Layout1Handle = desc->Stream1Layout;
    draw.StartVertex = desc->StartVertex; draw.Stream1StartVertex = desc->Stream1StartVertex;
    draw.VertexCount = desc->VertexCount; draw.StartIndex = desc->StartIndex;
    draw.IndexCount = desc->IndexCount;
    draw.DitherEnable = desc->DitherEnable != FALSE;
    draw.ColorTargetFormat = desc->ColorTargetFormat;
#if (defined(CKRE_ENABLE_PROFILING) && CKRE_ENABLE_PROFILING) || \
    (defined(CKRE_ENABLE_FFP_DIAGNOSTICS) && CKRE_ENABLE_FFP_DIAGNOSTICS)
    draw.Marker = desc->Marker ? desc->Marker : "";
#else
    if (DebugFlags)
        draw.Marker = desc->Marker ? desc->Marker : "";
#endif
    const auto &programOwner = Programs.Borrow(desc->Program);
    const auto &layoutOwner = Layouts.Borrow(desc->Layout);
    const auto &layout1Owner = Layouts.Borrow(desc->Stream1Layout);
    const auto &vbOwner = VertexBuffers.Borrow(desc->VertexBuffer);
    const auto &vb1Owner = VertexBuffers.Borrow(desc->Stream1VertexBuffer);
    const auto &ibOwner = IndexBuffers.Borrow(desc->IndexBuffer);
    draw.Program = programOwner.get(); draw.Layout = layoutOwner.get(); draw.Layout1 = layout1Owner.get();
    draw.VB = vbOwner.get(); draw.VB1 = vb1Owner.get(); draw.IB = ibOwner.get();
    if (!draw.Program || (desc->Stream1Layout && !draw.Layout1)) return CKERR_INVALIDPARAMETER;
    const bool procedural = draw.Program->Interface.VertexInputs.Size() == 0;
    if (procedural ? (desc->IndexCount || desc->StartVertex) : !draw.Layout) return CKERR_INVALIDPARAMETER;
    CKSdlGpuByteRange vertices, vertices1, indices;
    if (!procedural && !CKSdlGpuValidateVertexGeometry(
            draw.Layout, draw.VB, desc->Layout, desc->TransientVertices,
            desc->StartVertex, desc->VertexCount, TransientVertexInfo,
            TransientVertices, vertices))
        return CKERR_INVALIDPARAMETER;
    if (!procedural && draw.Layout1 && !CKSdlGpuValidateVertexGeometry(
            draw.Layout1, draw.VB1, desc->Stream1Layout, desc->Stream1Transient,
            desc->Stream1StartVertex, desc->VertexCount, TransientVertexInfo,
            TransientVertices, vertices1))
        return CKERR_INVALIDPARAMETER;
    if (desc->IndexCount) {
        const CKBYTE *bytes = nullptr;
        unsigned available = 0;
        if (draw.IB) {
            draw.Index32 = draw.IB->Desc.Index32 != FALSE;
            bytes = draw.IB->IndexData.Begin();
            available = unsigned(draw.IB->IndexData.Size()) / (draw.Index32 ? 4 : 2);
        } else if (desc->TransientIndices && desc->TransientIndices->Token) {
            const auto &transient = *desc->TransientIndices;
            const auto allocation = std::lower_bound(
                TransientIndexInfo.Begin(), TransientIndexInfo.End(),
                transient.Token, CKSdlGpuIndexTokenBefore);
            if (allocation == TransientIndexInfo.End() || allocation->Token != transient.Token) return CKERR_INVALIDPARAMETER;
            const int index = (int)(allocation - TransientIndexInfo.Begin());
            const XArray<CKBYTE> *storage = TransientIndices.Get(index);
            if (!storage) return CKERR_INVALIDPARAMETER;
            if (transient.Index32 != allocation->Index32 || transient.Data != storage->Begin() ||
                uint64_t(transient.Count) * (transient.Index32 ? 4 : 2) !=
                    (uint64_t)storage->Size()) return CKERR_INVALIDPARAMETER;
            draw.Index32 = transient.Index32 != FALSE;
            bytes = storage->Begin(); available = transient.Count;
        }
        if (!bytes || desc->StartIndex > available || desc->IndexCount > available - desc->StartIndex) return CKERR_INVALIDPARAMETER;
        const unsigned size = draw.Index32 ? 4 : 2;
        if (draw.IB) {
            unsigned maximum = 0;
            if (!draw.IB->FindMaxIndex(desc->StartIndex, desc->IndexCount, draw.Index32, maximum) ||
                maximum >= desc->VertexCount) return CKERR_INVALIDPARAMETER;
        } else {
            // Transient bytes are caller-writable until Draw returns and cannot
            // share a persistent range cache.
            const bool valid = draw.Index32
                ? CKSdlGpuValidateTransientIndices<CKDWORD>(
                    bytes, desc->StartIndex, desc->IndexCount, desc->VertexCount)
                : CKSdlGpuValidateTransientIndices<CKWORD>(
                    bytes, desc->StartIndex, desc->IndexCount, desc->VertexCount);
            if (!valid)
                return CKERR_INVALIDPARAMETER;
            indices.Data = bytes + size_t(desc->StartIndex) * size;
            indices.Size = size_t(desc->IndexCount) * size;
        }
    }
    auto &uniforms = draw.Program->UniformLayout;
    static const CKFFConstantSet emptyConstants;
    const CKFFConstantSet &constantValues =
        desc->Constants ? *desc->Constants : emptyConstants;
    uniforms.Update(constantValues);
    CKSdlGpuBindingBatch::Inputs bindingInputs;
    bindingInputs.Hash = draw.Program->Identity;
    std::shared_ptr<CKSdlGpuTexture> paddedOwners[CKFF_TEXTURE_SLOT_COUNT];
    bool depthPadStages[CKFF_MAX_TEXTURE_STAGES] = {};
    float depthPadTransforms[CKFF_MAX_TEXTURE_STAGES][4];
    bool hasDepthPad = false;
    for (unsigned slot = 0; slot < (unsigned)draw.Program->Interface.Samplers.Size(); ++slot) {
        const auto &decl = draw.Program->Interface.Samplers[slot];
        const bool nativeComparisonSampler =
            slot < draw.Program->CompareSamplerCount;
        static const CKFFTextureSlot emptyBinding;
        const auto &binding = desc->Textures ? (*desc->Textures)[decl.Slot] : emptyBinding;
        auto &cached = SamplerBindings[decl.Slot];
        if (decl.MetadataBufferSlot == UINT32_MAX && (binding.Sampler.AddressU == CKRST_ADDRESS_BORDER ||
            binding.Sampler.AddressV == CKRST_ADDRESS_BORDER || binding.Sampler.AddressW == CKRST_ADDRESS_BORDER))
            return CKERR_NOTIMPLEMENTED;
        const auto &sourceTexture = binding.Texture ?
            Textures.Borrow(binding.Texture) : draw.Program->DefaultTextures[slot];
        const std::shared_ptr<CKSdlGpuTexture> *textureOwner = &sourceTexture;
        CKSdlGpuTexture *texture = sourceTexture.get();
        if (!texture || (texture->Depth &&
            (texture->Info.usage & SDL_GPU_TEXTUREUSAGE_SAMPLER) == 0) ||
            texture->Info.type != draw.Program->DefaultTextures[slot]->Info.type ||
            (Target && (sourceTexture == Target->Color || sourceTexture == Target->Depth)))
            return CKERR_INVALIDPARAMETER;
        if (nativeComparisonSampler && texture->Depth &&
            binding.Sampler.CompareFunc != CKRST_COMPARE_NONE &&
            (binding.ShaderState &
             CKFF_SAMPLER_SHADER_REQUIRES_EXPLICIT_GRADIENT) == 0 &&
            (binding.Sampler.AddressU == CKRST_ADDRESS_BORDER ||
             binding.Sampler.AddressV == CKRST_ADDRESS_BORDER)) {
            float transform[4];
            const CKERROR padError = PrepareDepthPad(
                sourceTexture, binding.Sampler, paddedOwners[slot], transform);
            if (padError != CK_OK)
                return padError;
            textureOwner = &paddedOwners[slot];
            texture = paddedOwners[slot].get();
            if (binding.FixedStage >= CKFF_MAX_TEXTURE_STAGES)
                return CKERR_INVALIDPARAMETER;
            depthPadStages[binding.FixedStage] = true;
            std::memcpy(depthPadTransforms[binding.FixedStage], transform,
                        sizeof(transform));
            hasDepthPad = true;
        }
        CKSamplerDesc hardwareSampler = binding.Sampler;
        if (!nativeComparisonSampler)
            hardwareSampler.CompareFunc = CKRST_COMPARE_NONE;
        if (texture->Info.type == SDL_GPU_TEXTURETYPE_3D &&
            hardwareSampler.ShaderAnisotropy) {
            // The fixed-function 3D shader controls the anisotropic taps.
            // Keep the native sampler linear for each explicit-LOD lookup.
            if (hardwareSampler.MinFilter == CKRST_FILTER_ANISOTROPIC)
                hardwareSampler.MinFilter = CKRST_FILTER_LINEAR;
            if (hardwareSampler.MagFilter == CKRST_FILTER_ANISOTROPIC)
                hardwareSampler.MagFilter = CKRST_FILTER_LINEAR;
            if (hardwareSampler.MipFilter == CKRST_FILTER_ANISOTROPIC)
                hardwareSampler.MipFilter = CKRST_FILTER_LINEAR;
            hardwareSampler.MaxAnisotropy = 1;
            hardwareSampler.ShaderAnisotropy = 0;
        }
        if (std::memcmp(&cached.Sampler, &hardwareSampler, sizeof(hardwareSampler)) != 0) {
            cached.Sampler = hardwareSampler;
            cached.NativeSampler.reset();
        }
        if (!cached.NativeSampler) cached.NativeSampler = Sampler(hardwareSampler);
        if (!cached.NativeSampler) return Error;
        bindingInputs.Textures[slot] = texture;
        bindingInputs.Samplers[slot] = cached.NativeSampler.get();
        bindingInputs.TextureOwners[slot] = textureOwner;
        bindingInputs.SamplerOwners[slot] = &cached.NativeSampler;
        bindingInputs.Hash = (bindingInputs.Hash * 16777619u) ^
            (reinterpret_cast<uintptr_t>(texture) >> 4);
        bindingInputs.Hash = (bindingInputs.Hash * 16777619u) ^
            (reinterpret_cast<uintptr_t>(cached.NativeSampler.get()) >> 4);
        const CKDWORD metadata = draw.Program->SamplerMetadataOffsets[slot];
        if (metadata == UINT32_MAX) continue;
        const CKDWORD metadataBit = 1u << slot;
        if ((draw.Program->SamplerMetadataValidMask & metadataBit) != 0 &&
            std::memcmp(&draw.Program->SamplerMetadata[slot], &binding.Sampler,
                        sizeof(binding.Sampler)) == 0)
            continue;
        draw.Program->SamplerMetadata[slot] = binding.Sampler;
        draw.Program->SamplerMetadataValidMask |= metadataBit;
        float rgba[4];
        rgba[0] = float((binding.Sampler.BorderColor >> 16) & 255) / 255;
        rgba[1] = float((binding.Sampler.BorderColor >> 8) & 255) / 255;
        rgba[2] = float(binding.Sampler.BorderColor & 255) / 255;
        rgba[3] = float(binding.Sampler.BorderColor >> 24) / 255;
        float samplerInfo[4];
        samplerInfo[0] = float(unsigned(binding.Sampler.AddressU) |
                               (unsigned(binding.Sampler.AddressV) << 4) |
                               (unsigned(binding.Sampler.AddressW) << 8));
        samplerInfo[1] = float(binding.Sampler.MinFilter); samplerInfo[2] = float(binding.Sampler.MagFilter);
        CKDWORD packedMipFilter = unsigned(binding.Sampler.MipFilter) & 0xfu;
        packedMipFilter |= (binding.Sampler.MaxAnisotropy & 0xffu) << 4;
        samplerInfo[3] = float(packedMipFilter);
        std::memcpy(uniforms.Data.Begin() + metadata + decl.BorderColorOffset, rgba, sizeof(rgba));
        std::memcpy(uniforms.Data.Begin() + metadata + decl.SamplerStateOffset, samplerInfo, sizeof(samplerInfo));
        const CKDWORD first = (std::min)(decl.BorderColorOffset, decl.SamplerStateOffset);
        const CKDWORD last = (std::max)(decl.BorderColorOffset, decl.SamplerStateOffset) + sizeof(rgba);
        if (!uniforms.MarkDataChanged(metadata + first, last - first)) return CKERR_INVALIDPARAMETER;
    }
    std::unique_ptr<CKFFProgramLayout> drawUniforms;
    if (hasDepthPad) {
        // Depth-border padding changes texture coordinates for this draw only.
        // Keep the program layout immutable so ordinary draws can continue to
        // reuse its revision-tracked uniform snapshots.
        drawUniforms = std::make_unique<CKFFProgramLayout>(uniforms);
        for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
            if (depthPadStages[stage] &&
                !CKSdlGpuPatchDepthPad(*draw.Program, *drawUniforms, stage,
                                       depthPadTransforms[stage]))
                return CKERR_INVALIDPARAMETER;
        }
    }
    const CKFFProgramLayout &snapshotUniforms = drawUniforms
        ? *drawUniforms : uniforms;
    // Snapshot directly into the batch only after validating the entire draw.
    // Offsets survive arena growth; caller-owned transient data may change as
    // soon as Draw returns. XArray and native GPU offsets are both bounded here
    // before CKSdlGpuAppendBytes narrows sizes to int and unsigned.
    static const uint64_t kArraySizeLimit = 0x7fffffffu;
    if (CKSdlGpuAlignedEnd(
            CKSdlGpuAlignedEnd((uint64_t)BatchVertices.Size(), vertices.Size, 4u),
            vertices1.Size, 4u) > kArraySizeLimit ||
        CKSdlGpuAlignedEnd((uint64_t)BatchIndices.Size(), indices.Size, 4u) > kArraySizeLimit ||
        uint64_t(Uniforms.Data.Size()) + snapshotUniforms.Data.Size() > UINT32_MAX) return CKERR_OUTOFMEMORY;
    CKSdlGpuAppendBytes(BatchVertices, vertices, 4u, draw.VertexOffset);
    CKSdlGpuAppendBytes(BatchVertices, vertices1, 4u, draw.VertexOffset1);
    CKSdlGpuAppendBytes(BatchIndices, indices, 4u, draw.IndexOffset);
    if (drawUniforms) {
        CKSdlGpuUniformCursor drawCursor;
        Uniforms.Snapshot(snapshotUniforms, drawCursor, draw.UniformOffsets);
    } else {
        Uniforms.Snapshot(uniforms, draw.Program->UniformCursor,
                          draw.UniformOffsets);
    }
    draw.Bindings = Bindings.Intern(*draw.Program, bindingInputs);
    DrawResources.Retain(programOwner);
    DrawResources.Retain(layoutOwner); DrawResources.Retain(layout1Owner);
    DrawResources.Retain(vbOwner); DrawResources.Retain(vb1Owner); DrawResources.Retain(ibOwner);
    if (draw.VB) draw.NativeVB = DrawResources.Retain(draw.VB->Buffer);
    if (draw.VB1) draw.NativeVB1 = DrawResources.Retain(draw.VB1->Buffer);
    if (draw.IB) draw.NativeIB = DrawResources.Retain(draw.IB->Buffer);
    DrawApproximations = 0;
    Draws.PushBack(draw);
    ++FrameStats.Draws;
    // Bound packet retention independently of the number of engine draws.
    return isolateBlendedOutput || Draws.Size() >= 256 ? Flush(false) : CK_OK;
}

CKERROR CKSdlGpuRasterizerContext::DrawColorTexture(
    SDL_GPURenderPass *pass, SDL_GPUTexture *source,
    SDL_GPUTextureFormat colorFormat, SDL_GPUTextureFormat depthFormat,
    SDL_GPUSampleCount samples, unsigned width, unsigned height,
    const SDL_Rect &scissor, CKDWORD colorTargetFormat, CKBOOL dither,
    CKBOOL explicitQuantize)
{
    if (!pass || !source || !width || !height || !DitherProgram ||
        !DitherSampler)
        return CKERR_INVALIDPARAMETER;
    CKSdlGpuDraw draw;
    draw.Program = DitherProgram.get();
    draw.State.State.Lo = CKRST_STATE_WRITE_RGBA;
    draw.State.State.Mid = CKRST_STATE_PT(VX_TRIANGLELIST);
    SDL_GPUGraphicsPipeline *pipeline = Pipeline(
        draw, colorFormat, depthFormat, samples);
    if (!pipeline)
        return Error;
    SDL_GPUViewport viewport = {0, 0, float(width), float(height), 0, 1};
    SDL_SetGPUViewport(pass, &viewport);
    SDL_SetGPUScissor(pass, &scissor);
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_GPUTextureSamplerBinding binding = {source, DitherSampler.get()};
    SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    const float vertexParams[4] = {};
    const float fragmentParams[4] = {
        1.0f / float(width), 1.0f / float(height),
        float(colorTargetFormat + (explicitQuantize ? 4u : 0u)),
        dither ? 1.0f : 0.0f};
    SDL_PushGPUVertexUniformData(Commands, 0, vertexParams,
                                 unsigned(sizeof(vertexParams)));
    SDL_PushGPUFragmentUniformData(Commands, 0, fragmentParams,
                                   unsigned(sizeof(fragmentParams)));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    return CK_OK;
}

CKERROR CKSdlGpuRasterizerContext::ConvertColorTexture(
    SDL_GPUTexture *source, SDL_GPUTexture *destination,
    SDL_GPUTextureFormat destinationFormat, unsigned width, unsigned height)
{
    if (!source || !destination || !width || !height)
        return CKERR_INVALIDPARAMETER;
    SDL_GPUColorTargetInfo target = {};
    target.texture = destination;
    target.load_op = SDL_GPU_LOADOP_DONT_CARE;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(
        Commands, &target, 1, nullptr);
    if (!pass) return Fail("BeginGPURenderPass.colorConvert");
    const SDL_Rect scissor = {0, 0, int(width), int(height)};
    const CKERROR error = DrawColorTexture(
        pass, source, destinationFormat, SDL_GPU_TEXTUREFORMAT_INVALID,
        SDL_GPU_SAMPLECOUNT_1, width, height, scissor,
        CKFF_COLOR_TARGET_RGBA8, FALSE, FALSE);
    SDL_EndGPURenderPass(pass);
    return error;
}

CKERROR CKSdlGpuRasterizerContext::EnsureDitherTargets(
    unsigned width, unsigned height, unsigned samples,
    SDL_GPUTextureFormat sourceFormat)
{
    if (!width || !height || !samples ||
        sourceFormat == SDL_GPU_TEXTUREFORMAT_INVALID)
        return CKERR_INVALIDPARAMETER;
    const bool sizeChanged = DitherScratchWidth != width ||
        DitherScratchHeight != height;
    if (sizeChanged) {
        DitherScratch.reset();
        DitherMultisample.reset();
        DitherResolved.reset();
        DitherSource.reset();
        DitherScratchWidth = width;
        DitherScratchHeight = height;
        DitherScratchSamples = 1;
        DitherSourceFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    }
    if (DitherSourceFormat != sourceFormat) {
        DitherSource.reset();
        DitherSourceFormat = sourceFormat;
    }
    auto createTexture = [&](SDL_GPUTextureFormat format,
                             SDL_GPUTextureUsageFlags usage,
                             unsigned sampleCount,
                             const char *operation,
                             std::shared_ptr<SDL_GPUTexture> &texture) {
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = format;
        info.usage = usage;
        info.width = width;
        info.height = height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = CKSdlGpuSampleCount(sampleCount);
        texture = CKSdlGpuOwn(Device, SDL_CreateGPUTexture(Device, &info),
                              SDL_ReleaseGPUTexture);
        return texture ? CK_OK : Fail(operation);
    };
    const SDL_GPUTextureUsageFlags scratchUsage =
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (!DitherScratch && createTexture(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
            scratchUsage, 1, "CreateGPUTexture.ditherScratch",
            DitherScratch) != CK_OK)
        return Error;
    if (samples > 1 &&
        (!DitherMultisample || !DitherResolved ||
         DitherScratchSamples != samples)) {
        DitherMultisample.reset();
        DitherResolved.reset();
        if (createTexture(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
                SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, samples,
                "CreateGPUTexture.ditherMultisample",
                DitherMultisample) != CK_OK ||
            createTexture(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
                scratchUsage, 1, "CreateGPUTexture.ditherResolved",
                DitherResolved) != CK_OK)
            return Error;
        DitherScratchSamples = samples;
    }
    return CK_OK;
}

CKERROR CKSdlGpuRasterizerContext::SnapshotDitherSource(
    SDL_GPUTexture *source, SDL_GPUTextureType sourceType,
    unsigned sourceMip, unsigned sourceLayer, unsigned width, unsigned height,
    SDL_GPUTexture **snapshot)
{
    if (snapshot) *snapshot = nullptr;
    if (!source || !snapshot || !width || !height)
        return CKERR_INVALIDPARAMETER;
    if (sourceType == SDL_GPU_TEXTURETYPE_2D &&
        sourceMip == 0 && sourceLayer == 0) {
        *snapshot = source;
        return CK_OK;
    }
    if (!DitherSource) {
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = DitherSourceFormat;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = DitherScratchWidth;
        info.height = DitherScratchHeight;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        DitherSource = CKSdlGpuOwn(Device,
            SDL_CreateGPUTexture(Device, &info), SDL_ReleaseGPUTexture);
        if (!DitherSource)
            return Fail("CreateGPUTexture.ditherSource");
    }
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(Commands);
    if (!copy)
        return Fail("BeginGPUCopyPass.ditherSource");
    SDL_GPUTextureLocation from = {}, to = {};
    from.texture = source;
    from.mip_level = sourceMip;
    if (sourceType == SDL_GPU_TEXTURETYPE_3D)
        from.z = sourceLayer;
    else
        from.layer = sourceLayer;
    to.texture = DitherSource.get();
    SDL_CopyGPUTextureToTexture(copy, &from, &to, width, height, 1, false);
    SDL_EndGPUCopyPass(copy);
    *snapshot = DitherSource.get();
    return CK_OK;
}

CKERROR CKSdlGpuRasterizerContext::Flush(bool presentWindow)
{
    CKRE_PROFILE_SCOPE("CKRE.SDL.Encode");
    if (Draws.Size() == 0 && (!PassOpen || !Pass.ClearFlags)) return CK_OK;
    if (!EnsureCommands()) return Error;
    if (!Target) {
        // A non-presenting submission cannot execute a swapchain pass. Keep
        // the pending draw packets so the caller can retry with presentation.
        if (!presentWindow) return CKERR_INVALIDOPERATION;
        const CKERROR acquired = AcquireSwapchain();
        if (acquired != CK_OK) return acquired;
        // A minimized window has no swapchain image. Resource work still submits.
        if (!Swapchain) {
            Draws.Clear(); DrawResources.Clear(); Uniforms.Clear(); Bindings.Clear();
            BatchVertices.Clear(); BatchIndices.Clear(); Pass.ClearFlags = 0;
            return CK_OK;
        }
    }
    CKRE_PROFILE_VALUE("CKRE.Batch.Draws", Draws.Size());
    CKRE_PROFILE_VALUE("CKRE.Batch.VertexBytes", BatchVertices.Size());
    CKRE_PROFILE_VALUE("CKRE.Batch.IndexBytes", BatchIndices.Size());
    CKRE_PROFILE_VALUE("CKRE.Batch.UniformSnapshotBytes", Uniforms.Data.Size());
    CKRE_PROFILE_VALUE("CKRE.Batch.BindingGroups", Bindings.Size());
    const auto batch = UploadGeometry(BatchVertices, BatchIndices);
    if (Error != CK_OK) return Error;
    const unsigned width = Target ? std::max(1u, Target->Color->Info.width >> Target->Desc.ColorMip) : SwapWidth;
    const unsigned height = Target ? std::max(1u, Target->Color->Info.height >> Target->Desc.ColorMip) : SwapHeight;
    const bool fullRect = Pass.Rect.left == 0 && Pass.Rect.top == 0 && unsigned(Pass.Rect.right) == width && unsigned(Pass.Rect.bottom) == height;
    const bool hasDepth = Target && Target->Depth;
    const auto samples = CKSdlGpuSampleCount(Target ? Target->Color->Samples : 1);
    const CKDWORD logicalColorFormat = Draws.Size() != 0
        ? Draws[0].ColorTargetFormat : Pass.ColorTargetFormat;
    const bool ditherEnabled = Draws.Size() != 0 && Draws[0].DitherEnable;
    const bool outputResolve = CKSdlGpuNeedsOutputResolve(
        Target, logicalColorFormat, ditherEnabled);
    CKRenderPassDesc routedPass = Pass;
    if (outputResolve && (routedPass.ClearFlags & CKRST_CTXCLEAR_COLOR))
        routedPass.ClearColor = CKSdlGpuQuantizeClearColor(
            routedPass.ClearColor, logicalColorFormat);
    SDL_GPUColorTargetInfo color = {};
    color.texture = Target ? (Target->Color->Multisample ? Target->Color->Multisample.get() : Target->Color->Image.get()) : Swapchain;
    color.mip_level = Target ? Target->Desc.ColorMip : 0;
    color.layer_or_depth_plane = Target ? Target->Desc.ColorLayer : 0;
    color.clear_color = {float((routedPass.ClearColor >> 16) & 255) / 255, float((routedPass.ClearColor >> 8) & 255) / 255,
                         float(routedPass.ClearColor & 255) / 255, float(routedPass.ClearColor >> 24) / 255};
    color.load_op = fullRect && (routedPass.ClearFlags & CKRST_CTXCLEAR_COLOR) ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    color.store_op = SDL_GPU_STOREOP_STORE;
    if (Target && Target->VolumeSlice) {
        if (color.load_op == SDL_GPU_LOADOP_LOAD) {
            if (Target->Color->Defined.IsSet((int)(Target->Desc.ColorMip *
                Target->Color->Info.layer_count_or_depth + Target->Desc.ColorLayer))) {
                if (CopyVolumeSlice(*Target->Color, Target->Desc.ColorMip, Target->Desc.ColorLayer,
                                     Target->VolumeSlice.get(), false) != CK_OK) return Error;
            } else color.load_op = SDL_GPU_LOADOP_DONT_CARE;
        }
        color.texture = Target->VolumeSlice.get(); color.mip_level = color.layer_or_depth_plane = 0;
    }
    SDL_GPUTexture *finalColorTexture = Target
        ? (Target->VolumeSlice ? Target->VolumeSlice.get() : Target->Color->Image.get())
        : Swapchain;
    const unsigned finalColorMip = Target && !Target->VolumeSlice
        ? Target->Desc.ColorMip : 0;
    const unsigned finalColorLayer = Target && !Target->VolumeSlice
        ? Target->Desc.ColorLayer : 0;
    const SDL_GPUTextureType finalColorType = Target && !Target->VolumeSlice
        ? Target->Color->Info.type : SDL_GPU_TEXTURETYPE_2D;
    const SDL_GPUTextureFormat finalColorFormat = Target ? Target->Color->Info.format
        : SDL_GetGPUSwapchainTextureFormat(Device, Window);
    const bool nativeLowBit = CKSdlGpuNativePackedColor(
        finalColorFormat, logicalColorFormat);
    const SDL_GPUTextureFormat scratchFormat =
        SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    const SDL_GPULoadOp originalColorLoad = color.load_op;
    bool seedMultisample = false;
    if (outputResolve) {
        if (EnsureDitherTargets(width, height, Target->Color->Samples,
                                finalColorFormat) != CK_OK)
            return Error;
        if (originalColorLoad == SDL_GPU_LOADOP_LOAD) {
            SDL_GPUTexture *snapshot = nullptr;
            if (SnapshotDitherSource(finalColorTexture, finalColorType,
                    finalColorMip, finalColorLayer, width, height,
                    &snapshot) != CK_OK ||
                ConvertColorTexture(snapshot, DitherScratch.get(),
                    scratchFormat, width, height) != CK_OK)
                return Error;
        }
        if (Target->Color->Samples > 1) {
            color.texture = DitherMultisample.get();
            color.resolve_texture = DitherResolved.get();
            color.store_op = SDL_GPU_STOREOP_RESOLVE_AND_STORE;
            color.load_op = SDL_GPU_LOADOP_DONT_CARE;
            seedMultisample = originalColorLoad == SDL_GPU_LOADOP_LOAD;
        } else {
            color.texture = DitherScratch.get();
            color.load_op = originalColorLoad;
        }
        color.mip_level = color.layer_or_depth_plane = 0;
    } else if (Target && Target->Color->Multisample) {
        color.resolve_texture = Target->Color->Image.get();
        color.resolve_mip_level = color.mip_level; color.resolve_layer = color.layer_or_depth_plane;
        color.store_op = SDL_GPU_STOREOP_RESOLVE_AND_STORE;
    }
    SDL_GPUDepthStencilTargetInfo ds = {};
    const SDL_GPUTextureFormat colorFormat = outputResolve
        ? scratchFormat : finalColorFormat;
    const SDL_GPUTextureFormat depthFormat = hasDepth ? Target->Depth->Info.format : SDL_GPU_TEXTUREFORMAT_INVALID;
    if (hasDepth) {
        ds.texture = Target->Depth->Image.get(); ds.clear_depth = Pass.ClearZ; ds.clear_stencil = Uint8(Pass.ClearStencil);
        ds.load_op = fullRect && (Pass.ClearFlags & CKRST_CTXCLEAR_DEPTH) ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        ds.stencil_load_op = fullRect && (Pass.ClearFlags & CKRST_CTXCLEAR_STENCIL) ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        ds.store_op = ds.stencil_store_op = SDL_GPU_STOREOP_STORE;
    }
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(Commands, &color, 1, hasDepth ? &ds : nullptr);
    if (!pass) return Fail("BeginGPURenderPass");
    SDL_GPUViewport viewport = {float(Pass.Rect.left), float(Pass.Rect.top), float(Pass.Rect.right - Pass.Rect.left),
                               float(Pass.Rect.bottom - Pass.Rect.top), 0, 1};
    if (!Target) viewport = {0, 0, float(width), float(height), 0, 1};
    SDL_Rect passRect = {std::max(0, Pass.Rect.left), std::max(0, Pass.Rect.top),
        std::max(0, std::min(int(width), Pass.Rect.right) - std::max(0, Pass.Rect.left)),
        std::max(0, std::min(int(height), Pass.Rect.bottom) - std::max(0, Pass.Rect.top))};
    if (!Target) passRect = {0, 0, int(width), int(height)};
    SDL_SetGPUViewport(pass, &viewport); SDL_SetGPUScissor(pass, &passRect);
    if (seedMultisample) {
        const SDL_Rect fullTarget = {0, 0, int(width), int(height)};
        if (DrawColorTexture(pass, DitherScratch.get(), colorFormat,
                depthFormat, samples, width, height, fullTarget,
                CKFF_COLOR_TARGET_RGBA8, FALSE, FALSE) != CK_OK) {
            SDL_EndGPURenderPass(pass);
            return Error;
        }
        SDL_SetGPUViewport(pass, &viewport);
        SDL_SetGPUScissor(pass, &passRect);
    }
    if (!fullRect && routedPass.ClearFlags &&
        ClearRect(pass, routedPass, colorFormat, depthFormat, samples) != CK_OK) {
        SDL_EndGPURenderPass(pass); return Error;
    }
    SDL_GPUGraphicsPipeline *boundPipeline = nullptr;
    unsigned boundBindings = UINT32_MAX;
    CKSdlGpuUniformBindings boundUniforms;
    SDL_Rect boundScissor = passRect;
    bool scissorBound = true;
    bool stencilReferenceBound = false;
    Uint8 boundStencilReference = 0;
    CKSdlGpuVertexBindingState boundVertexBuffers[3];
    SDL_GPUBuffer *boundIndexBuffer = nullptr;
    Uint32 boundIndexOffset = 0;
    SDL_GPUIndexElementSize boundIndexSize = SDL_GPU_INDEXELEMENTSIZE_16BIT;
    bool indexBufferBound = false;
    Bindings.MarkReferenced();
    for (auto &draw : Draws) {
        auto pipeline = Pipeline(draw, colorFormat, depthFormat, samples);
        if (!pipeline) { SDL_EndGPURenderPass(pass); return Error; }
        if (boundPipeline != pipeline) {
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            boundPipeline = pipeline;
            boundBindings = UINT32_MAX;
            boundUniforms.Invalidate();
            CKRE_PROFILE_VALUE("CKRE.Batch.PipelineBinds", 1);
        }
        SDL_Rect scissor = passRect;
        if (draw.State.ScissorEnabled) {
            scissor.x = std::max(passRect.x, draw.State.Scissor.left);
            scissor.y = std::max(passRect.y, draw.State.Scissor.top);
            scissor.w = std::max(0, std::min(passRect.x + passRect.w, draw.State.Scissor.right) - scissor.x);
            scissor.h = std::max(0, std::min(passRect.y + passRect.h, draw.State.Scissor.bottom) - scissor.y);
        }
        if (!scissorBound || scissor.x != boundScissor.x || scissor.y != boundScissor.y ||
            scissor.w != boundScissor.w || scissor.h != boundScissor.h) {
            SDL_SetGPUScissor(pass, &scissor);
            boundScissor = scissor;
            scissorBound = true;
        }
        const Uint8 stencilReference = Uint8(draw.State.StencilRef);
        if (!stencilReferenceBound || stencilReference != boundStencilReference) {
            SDL_SetGPUStencilReference(pass, stencilReference);
            boundStencilReference = stencilReference;
            stencilReferenceBound = true;
        }
        if (!scissor.w || !scissor.h) continue;
        if (draw.Program->Interface.VertexInputs.Size() != 0) {
            SDL_GPUBufferBinding vb = {draw.VB ? draw.NativeVB : batch.Buffer.get(),
                draw.VB ? draw.StartVertex * draw.Layout->Stride : draw.VertexOffset};
            CKSdlGpuBindVertexBuffer(pass, boundVertexBuffers, 0, vb);
            if (draw.Layout1) {
                SDL_GPUBufferBinding vb1 = {draw.VB1 ? draw.NativeVB1 : batch.Buffer.get(),
                    draw.VB1 ? draw.Stream1StartVertex * draw.Layout1->Stride : draw.VertexOffset1};
                CKSdlGpuBindVertexBuffer(pass, boundVertexBuffers, 1, vb1);
            }
            SDL_GPUBufferBinding defaults = {draw.Program->DefaultVertices.get(), 0};
            CKSdlGpuBindVertexBuffer(pass, boundVertexBuffers,
                                     draw.Layout1 ? 2u : 1u, defaults);
        }
        if (boundBindings != draw.Bindings) {
            const auto &bindings = Bindings[draw.Bindings];
            if (draw.Program->Vertex->Desc.SamplerCount)
                SDL_BindGPUVertexSamplers(pass, 0, bindings.Vertex, draw.Program->Vertex->Desc.SamplerCount);
            if (draw.Program->Fragment->Desc.SamplerCount)
                SDL_BindGPUFragmentSamplers(pass, 0, bindings.Fragment, draw.Program->Fragment->Desc.SamplerCount);
            boundBindings = draw.Bindings;
        }
        for (int i = 0; i < draw.Program->UniformLayout.Buffers.Size(); ++i) {
            const auto &buffer = draw.Program->UniformLayout.Buffers[i];
            const unsigned offset = draw.UniformOffsets[i];
            if (!boundUniforms.NeedsPush(buffer, offset, Uniforms.Data)) continue;
            const void *data = Uniforms.Data.Begin() + offset;
            if (buffer.Stage == CKRST_SHADER_VERTEX) SDL_PushGPUVertexUniformData(Commands, buffer.Slot, data, buffer.Size);
            else SDL_PushGPUFragmentUniformData(Commands, buffer.Slot, data, buffer.Size);
            CKRE_PROFILE_VALUE("CKRE.Batch.UniformPushBytes", buffer.Size);
            CKRE_PROFILE_VALUE("CKRE.Batch.UniformPushes", 1);
        }
        if (!draw.Marker.IsEmpty()) SDL_InsertGPUDebugLabel(Commands, draw.Marker.Str());
        if (draw.IndexCount) {
            const Uint32 indexSizeBytes = draw.Index32 ? 4u : 2u;
            SDL_GPUBufferBinding ib = {draw.IB ? draw.NativeIB : batch.Buffer.get(),
                draw.IB ? 0u : batch.IndexOffset};
            const Uint32 firstIndex = draw.IB ? draw.StartIndex
                                              : draw.IndexOffset / indexSizeBytes;
            const SDL_GPUIndexElementSize indexSize = draw.Index32
                ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT;
            if (!indexBufferBound || boundIndexBuffer != ib.buffer ||
                boundIndexOffset != ib.offset || boundIndexSize != indexSize) {
                SDL_BindGPUIndexBuffer(pass, &ib, indexSize);
                boundIndexBuffer = ib.buffer;
                boundIndexOffset = ib.offset;
                boundIndexSize = indexSize;
                indexBufferBound = true;
            }
            SDL_DrawGPUIndexedPrimitives(pass, draw.IndexCount, 1, firstIndex, 0, 0);
        } else {
            SDL_DrawGPUPrimitives(pass, draw.VertexCount, 1, 0, 0);
        }
    }
    SDL_EndGPURenderPass(pass);
    if (outputResolve) {
        SDL_GPUColorTargetInfo resolved = {};
        resolved.texture = finalColorTexture;
        resolved.mip_level = finalColorMip;
        resolved.layer_or_depth_plane = finalColorLayer;
        resolved.load_op = SDL_GPU_LOADOP_LOAD;
        resolved.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *resolvePass = SDL_BeginGPURenderPass(
            Commands, &resolved, 1, nullptr);
        if (!resolvePass) return Fail("BeginGPURenderPass.ditherResolve");
        const bool explicitQuantize = ditherEnabled || !nativeLowBit;
        SDL_GPUTexture *resolveSource = Target->Color->Samples > 1
            ? DitherResolved.get() : DitherScratch.get();
        if (DrawColorTexture(resolvePass, resolveSource, finalColorFormat,
                SDL_GPU_TEXTUREFORMAT_INVALID, SDL_GPU_SAMPLECOUNT_1,
                width, height, passRect, logicalColorFormat,
                ditherEnabled ? TRUE : FALSE,
                explicitQuantize ? TRUE : FALSE) != CK_OK) {
            SDL_EndGPURenderPass(resolvePass);
            return Error;
        }
        SDL_EndGPURenderPass(resolvePass);
    }
    if (Target) {
        if (Target->VolumeSlice && CopyVolumeSlice(*Target->Color, Target->Desc.ColorMip,
            Target->Desc.ColorLayer, Target->VolumeSlice.get(), true) != CK_OK) return Error;
        Target->Color->Defined.Set((int)(Target->Desc.ColorMip *
            Target->Color->Info.layer_count_or_depth + Target->Desc.ColorLayer));
        Target->Color->Referenced = true;
        Target->Color->AttachmentInitialized = true;
        if (Target->Color->AutoMips && Target->Desc.ColorMip == 0) {
            const CKERROR error = GenerateGpuMips(*Target->Color, Target->Desc.ColorLayer);
            if (error != CK_OK) return error;
        }
    }
    Pass.ClearFlags = 0; Draws.Clear(); DrawResources.Clear(); Uniforms.Clear(); Bindings.Clear();
    BatchVertices.Clear(); BatchIndices.Clear();
    return CK_OK;
}

CKERROR CKSdlGpuRasterizerContext::AcquireSwapchain()
{
    if (Swapchain) return CK_OK;
    const unsigned previousWidth = SwapWidth, previousHeight = SwapHeight;
    CKRE_PROFILE_SCOPE("CKRE.SDL.Acquire");
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(Commands, Window, &Swapchain, &SwapWidth, &SwapHeight))
        return Fail("WaitAndAcquireGPUSwapchainTexture");
    if (Swapchain && (SwapWidth != previousWidth || SwapHeight != previousHeight))
        SDL_Log("SDL_gpu swapchain: window=%u drawable=%ux%u logical=%ux%u",
                SDL_GetWindowID(Window), SwapWidth, SwapHeight, Width, Height);
    return CK_OK;
}

CKERROR CKSdlGpuRasterizerContext::PresentTexture(CKDWORD handle, CKDWORD width, CKDWORD height,
                                        CKPresentSync sync)
{
    if (!Ready()) return CKERR_INVALIDOPERATION;
    auto source = Textures.Get(handle);
    if (!source || source->Depth || source->Multisample ||
        source->Info.type != SDL_GPU_TEXTURETYPE_2D ||
        !width || !height || width > source->Info.width || height > source->Info.height)
        return CKERR_INVALIDPARAMETER;

    if (!CKSdlGpuValidPresentSync(sync)) return CKERR_INVALIDPARAMETER;

    // Finish the native-target scene pass, then let SDL encode its dedicated
    // blit straight to the acquired swapchain image. This keeps the portable
    // texture backbuffer/readback semantics while avoiding another fullscreen
    // draw packet, geometry upload and native render-pass setup.
    CKERROR error = Flush();
    if (error != CK_OK) return error;
    if (sync != CKRST_PRESENT_UNCHANGED) {
        const auto mode = sync == CKRST_PRESENT_VSYNC
            ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;
        if (mode != PresentMode) {
            if (Swapchain) return CKERR_INVALIDOPERATION;
            if (!SDL_SetGPUSwapchainParameters(Device, Window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode))
                return Fail("SetGPUSwapchainParameters");
            PresentMode = mode;
        }
    }
    if (!EnsureCommands()) return Error;
    error = AcquireSwapchain();
    if (error != CK_OK) return error;

    if (Swapchain) {
        const SDL_GPUTextureFormat swapchainFormat = SDL_GetGPUSwapchainTextureFormat(Device, Window);
        if (PresentCopySupported && CKSdlGpuCanCopyPresent(source->Info.format, swapchainFormat,
                                   width, height, SwapWidth, SwapHeight)) {
            if (!PresentCopyLogged) {
                SDL_Log("SDL_gpu present path: exact texture copy format=%u size=%ux%u",
                        unsigned(swapchainFormat), width, height);
                PresentCopyLogged = true;
            }
            auto *copy = SDL_BeginGPUCopyPass(Commands);
            if (!copy) return Fail("BeginGPUCopyPass.present");
            SDL_GPUTextureLocation sourceLocation = {}, destinationLocation = {};
            sourceLocation.texture = source->Image.get();
            destinationLocation.texture = Swapchain;
            SDL_CopyGPUTextureToTexture(copy, &sourceLocation, &destinationLocation,
                                        width, height, 1, false);
            SDL_EndGPUCopyPass(copy);
        } else {
            SDL_GPUBlitInfo blit = {};
            blit.source = {source->Image.get(), 0, 0, 0, 0, width, height};
            blit.destination = {Swapchain, 0, 0, 0, 0, SwapWidth, SwapHeight};
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
            blit.flip_mode = SDL_FLIP_NONE;
            blit.filter = SDL_GPU_FILTER_LINEAR;
            SDL_BlitGPUTexture(Commands, &blit);
        }
        source->Referenced = true;
    }
    PassOpen = false;
    Target.reset();
    Pass = CKRenderPassDesc();
    ++FrameStats.Passes;
    ++FrameStats.Blits;
    return CK_OK;
}

CKERROR CKSdlGpuRasterizerContext::Blit(CKDWORD dstHandle, CKDWORD dstMip, CKDWORD dstLayer, CKDWORD dx, CKDWORD dy,
    CKDWORD srcHandle, CKDWORD srcMip, CKDWORD srcLayer, const CKRECT *rect)
{
    if (!Ready()) return CKERR_INVALIDOPERATION;
    auto src = Textures.Get(srcHandle), dst = Textures.Get(dstHandle);
    if (!src || !dst || src->Depth || dst->Depth || dst->Multisample || src == dst ||
        srcMip >= src->Info.num_levels || dstMip >= dst->Info.num_levels ||
        srcLayer >= CKSdlGpuTextureLayers(*src, srcMip) || dstLayer >= CKSdlGpuTextureLayers(*dst, dstMip)) return CKERR_INVALIDPARAMETER;
    const unsigned sw = std::max(1u, src->Info.width >> srcMip), sh = std::max(1u, src->Info.height >> srcMip);
    const unsigned dw = std::max(1u, dst->Info.width >> dstMip), dh = std::max(1u, dst->Info.height >> dstMip);
    const unsigned sx = rect ? rect->left : 0, sy = rect ? rect->top : 0;
    const unsigned w = rect ? rect->right - rect->left : sw, h = rect ? rect->bottom - rect->top : sh;
    if (sx >= sw || sy >= sh || dx >= dw || dy >= dh || !w || !h || w > sw - sx || h > sh - sy || w > dw - dx || h > dh - dy)
        return CKERR_INVALIDPARAMETER;
    if (dst->AutoMips && dst->Info.type != SDL_GPU_TEXTURETYPE_3D &&
        !(dst->Info.usage & SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)) return CKERR_NOTIMPLEMENTED;
    CKERROR error = Flush();
    if (error != CK_OK) return error;
    error = PreserveTexture(*dst);
    if (error != CK_OK) return error;
    if (!EnsureCommands()) return Error;
    if (src->Info.format == dst->Info.format) {
        SDL_GPUTextureLocation source = {}, destination = {};
        source.texture = src->Image.get(); source.mip_level = srcMip; source.x = sx; source.y = sy;
        if (src->Info.type == SDL_GPU_TEXTURETYPE_3D) source.z = srcLayer; else source.layer = srcLayer;
        destination.texture = dst->Image.get(); destination.mip_level = dstMip; destination.x = dx; destination.y = dy;
        if (dst->Info.type == SDL_GPU_TEXTURETYPE_3D) destination.z = dstLayer; else destination.layer = dstLayer;
        auto *copy = SDL_BeginGPUCopyPass(Commands);
        if (!copy) return Fail("BeginGPUCopyPass.blit");
        SDL_CopyGPUTextureToTexture(copy, &source, &destination, w, h, 1, false);
        SDL_EndGPUCopyPass(copy);
    } else {
        if (src->Info.type != SDL_GPU_TEXTURETYPE_2D ||
            dst->Info.type != SDL_GPU_TEXTURETYPE_2D ||
            srcMip != 0 || dstMip != 0 || srcLayer != 0 || dstLayer != 0 ||
            sx != 0 || sy != 0 || dx != 0 || dy != 0 ||
            w != sw || h != sh || w != dw || h != dh)
            return CKERR_NOTIMPLEMENTED;
        error = ConvertColorTexture(src->Image.get(), dst->Image.get(),
                                    dst->Info.format, w, h);
        if (error != CK_OK) return error;
    }
    src->Referenced = true;
    dst->Defined.Set((int)(dstMip * dst->Info.layer_count_or_depth + dstLayer));
    ++FrameStats.Blits;
    return dst->AutoMips && dstMip == 0 ? GenerateGpuMips(*dst, dstLayer) : CK_OK;
}

CKERROR CKSdlGpuRasterizerContext::ReadTexture(CKDWORD handle, CKDWORD mip, CKReadbackDesc *desc, CKSdlGpuReadbackTicket *out)
{
    if (out) out->reset();
    if (!Ready()) return CKERR_INVALIDOPERATION;
    auto texture = Textures.Get(handle);
    if (!desc || !texture || texture->Depth || mip >= texture->Info.num_levels ||
        texture->Info.type != SDL_GPU_TEXTURETYPE_2D) return CKERR_INVALIDPARAMETER;
    const auto format = texture->Info.format;
    if (format != SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM && format != SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM)
        return CKERR_NOTIMPLEMENTED;
    desc->Width = std::max(1u, texture->Info.width >> mip); desc->Height = std::max(1u, texture->Info.height >> mip);
    desc->RowPitch = desc->Width * 4; desc->RequiredSize = desc->RowPitch * desc->Height;
    desc->Format = format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ? _32_ARGB8888 : _32_ABGR8888;
    desc->YFlip = FALSE;
    if (!out) return CK_OK;
    const CKERROR error = Flush();
    if (error != CK_OK) return error;
    if (!EnsureCommands()) return Error;
    auto ticket = std::make_shared<CKSdlGpuReadback>();
    ticket->Data.Resize((int)desc->RequiredSize);
    SDL_GPUTransferBufferCreateInfo info = {SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, desc->RequiredSize, 0};
    ticket->Transfer = CKSdlGpuOwn(Device, SDL_CreateGPUTransferBuffer(Device, &info), SDL_ReleaseGPUTransferBuffer);
    if (!ticket->Transfer) return Fail("CreateGPUTransferBuffer.readback");
    SDL_GPUTextureRegion source = {};
    source.texture = texture->Image.get(); source.mip_level = mip; source.w = desc->Width; source.h = desc->Height; source.d = 1;
    SDL_GPUTextureTransferInfo destination = {ticket->Transfer.get(), 0, 0, 0};
    auto *copy = SDL_BeginGPUCopyPass(Commands);
    if (!copy) return Fail("BeginGPUCopyPass.readback");
    SDL_DownloadFromGPUTexture(copy, &source, &destination);
    SDL_EndGPUCopyPass(copy);
    texture->Referenced = true;
    Readbacks.PushBack(ticket); *out = std::move(ticket);
    return CK_OK;
}

CKERROR CKSdlGpuRasterizerContext::Submit(CKPresentSync sync, CKBOOL presentWindow,
                                          CKDWORD *number)
{
    CKRE_PROFILE_SCOPE("CKRE.SDL.Submit");
    if (number) *number = 0;
    if (!Ready()) return CKERR_INVALIDOPERATION;
    if (!CKSdlGpuValidPresentSync(sync)) return CKERR_INVALIDPARAMETER;
    if (LastSubmitId == (CKQWORD)-1)
        return CKERR_INVALIDOPERATION;
    if (!presentWindow && !Target &&
        (Draws.Size() != 0 || (PassOpen && Pass.ClearFlags)))
        return CKERR_INVALIDOPERATION;
    if (sync != CKRST_PRESENT_UNCHANGED) {
        const auto mode = sync == CKRST_PRESENT_VSYNC ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;
        if (mode != PresentMode) {
            if (Swapchain) return CKERR_INVALIDOPERATION;
            if (!SDL_SetGPUSwapchainParameters(Device, Window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode))
                return Fail("SetGPUSwapchainParameters");
            PresentMode = mode;
        }
    }
    const CKERROR error = Flush(presentWindow != FALSE);
    if (error != CK_OK) return error;
    std::shared_ptr<SDL_GPUFence> fence;
    if (Commands) {
        fence = CKRE_PROFILE_CALL("CKRE.SDL.NativeSubmit",
            CKSdlGpuOwn(Device, SDL_SubmitGPUCommandBufferAndAcquireFence(Commands), SDL_ReleaseGPUFence));
        Commands = nullptr; Swapchain = nullptr;
        if (!fence) return Fail("SubmitGPUCommandBufferAndAcquireFence");
        for (auto &ticket : Readbacks) if (!ticket->Fence) ticket->Fence = fence;
    }
    ++LastSubmitId;
    Submissions.PushBack(CKSdlGpuSubmission());
    CKSdlGpuSubmission &submission = Submissions.Back();
    submission.Fence.swap(fence);
    submission.Geometry.Swap(PendingGeometry);
    submission.BufferUploads.Swap(PendingBufferUploads);
    submission.SubmitId = LastSubmitId;
    PassOpen = false; Target.reset(); Pass = CKRenderPassDesc();
    TransientVertexInfo.Clear(); TransientIndexInfo.Clear();
    // Keep common CPU allocations warm, with a shared 16 MiB retention budget.
    // GPU uploads already own their snapshots; trimming cannot affect a fence.
    uint64_t retained = 0;
    TransientVertices.Trim(retained);
    TransientIndices.Trim(retained);
    FrameStats.Frames = ++Submission;
    Stats = FrameStats;
    FrameStats = CKSdlGpuSubmissionStats();
    if (number) *number = Submission;
    Collect();
    // No pass is open, so completed jobs may change what later draws use.
    CollectJobs();
    CKRE_PROFILE_VALUE("CKRE.Queue.PendingSubmissions", Submissions.Size());
    if (Submissions.Size() > 3) {
        CKRE_PROFILE_SCOPE("CKRE.SDL.WaitInflight");
        SDL_GPUFence *oldest = Submissions[0].Fence.get();
        if (!SDL_WaitForGPUFences(Device, true, &oldest, 1)) return Fail("WaitForGPUFences.inflight");
        Collect();
    }
    return CK_OK;
}

CKQWORD CKSdlGpuRasterizerContext::GetLastSubmitId() const
{
    return LastSubmitId;
}

CKQWORD CKSdlGpuRasterizerContext::GetCompletedSubmitId()
{
    if (Ready())
        Collect();
    return CompletedSubmitId;
}
